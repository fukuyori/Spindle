#include "core/Matcher.h"

#include <QHash>
#include <QRegularExpression>

namespace matcher {

namespace {

bool isWhitespace(ushort u)
{
    return u == 0x20 || u == 0x09 || u == 0x0a || u == 0x0d || u == 0x3000 || u == 0xa0;
}

// Collapse whitespace runs to single spaces, then trim (matches normalizeForMatch).
QString normalizeForMatch(const QString &input)
{
    QString out;
    out.reserve(input.size());
    bool lastWasSpace = false;
    for (const QChar &ch : input) {
        if (isWhitespace(ch.unicode())) {
            if (!lastWasSpace) {
                out += QLatin1Char(' ');
                lastWasSpace = true;
            }
        } else {
            out += ch;
            lastWasSpace = false;
        }
    }
    return out.trimmed();
}

QString compactForMatch(const QString &input)
{
    QString out;
    out.reserve(input.size());
    for (const QChar &ch : input)
        if (!isWhitespace(ch.unicode()))
            out += ch;
    return out;
}

std::optional<int> labelChapterNumber(const QString &label)
{
    QRegularExpression re(QStringLiteral("Chapter\\s*(\\d+)"), QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch m = re.match(label);
    if (!m.hasMatch()) {
        re = QRegularExpression(QStringLiteral("第\\s*(\\d+)\\s*章"));
        m = re.match(label);
    }
    if (!m.hasMatch())
        return std::nullopt;
    bool ok = false;
    const int v = m.captured(1).toInt(&ok);
    return ok ? std::optional<int>(v) : std::nullopt;
}

// The chapter's leaf-block texts concatenated (no separators), with maps from
// every character back to its (block index, offset within block). Highlight
// offset/length count into this same block-only coordinate (inter-block text
// is excluded), matching the JS renderer.
struct JoinedBlocks {
    QString joined;
    QVector<int> blockOf; // joined index -> block index
    QVector<int> offOf;   // joined index -> offset within that block
    // normalized / compact projections of `joined` with maps back to joined index
    QString normalized;
    QVector<int> normToJoined;
    QString compact;
    QVector<int> compactToJoined;
};

JoinedBlocks buildJoined(const ChapterText &chapter)
{
    JoinedBlocks j;
    for (const block_index::BlockInfo &b : chapter.blocks) {
        for (int i = 0; i < b.text.size(); ++i) {
            j.joined += b.text.at(i);
            j.blockOf.append(b.index);
            j.offOf.append(i);
        }
    }
    bool lastWasSpace = false;
    for (int i = 0; i < j.joined.size(); ++i) {
        const ushort u = j.joined.at(i).unicode();
        if (isWhitespace(u)) {
            if (!lastWasSpace) {
                j.normalized += QLatin1Char(' ');
                j.normToJoined.append(i);
                lastWasSpace = true;
            }
        } else {
            j.normalized += j.joined.at(i);
            j.normToJoined.append(i);
            lastWasSpace = false;
        }
        if (!isWhitespace(u)) {
            j.compact += j.joined.at(i);
            j.compactToJoined.append(i);
        }
    }
    return j;
}

// Translate a [js, je) range in joined coordinates into a MatchResult.
MatchResult makeResult(const kindle::KindleEntry &entry, const ChapterText &chapter,
                       const JoinedBlocks &j, int js, int je, const QString &confidence)
{
    js = qBound(0, js, j.joined.size());
    je = qBound(js, je, j.joined.size());
    MatchResult r;
    r.entry = entry;
    r.chapterPath = chapter.path;
    r.block = js < j.blockOf.size() ? j.blockOf.at(js) : 0;
    r.offset = js < j.offOf.size() ? j.offOf.at(js) : 0;
    r.length = je - js;
    r.matchedText = j.joined.mid(js, je - js);
    r.confidence = confidence;
    return r;
}

bool findInChapter(const kindle::KindleEntry &entry, const ChapterText &chapter,
                   const JoinedBlocks &j, MatchResult &result)
{
    const QString &target = entry.text;
    const int exact = static_cast<int>(j.joined.indexOf(target));
    if (exact >= 0) {
        result = makeResult(entry, chapter, j, exact, exact + target.size(),
                            QStringLiteral("exact"));
        return true;
    }

    const QString normTarget = normalizeForMatch(target);
    if (!normTarget.isEmpty()) {
        const int idx = j.normalized.indexOf(normTarget);
        if (idx >= 0) {
            const int js = j.normToJoined.value(idx);
            const int lastNorm = qMin(idx + normTarget.size() - 1, j.normToJoined.size() - 1);
            const int je = j.normToJoined.value(lastNorm) + 1;
            result = makeResult(entry, chapter, j, js, je, QStringLiteral("normalized"));
            return true;
        }
    }

    const QString compactTarget = compactForMatch(target);
    if (!compactTarget.isEmpty()) {
        const int idx = j.compact.indexOf(compactTarget);
        if (idx >= 0) {
            const int js = j.compactToJoined.value(idx);
            const int lastC = qMin(idx + compactTarget.size() - 1, j.compactToJoined.size() - 1);
            const int je = j.compactToJoined.value(lastC) + 1;
            result = makeResult(entry, chapter, j, js, je, QStringLiteral("normalized"));
            return true;
        }
    }

    return false;
}

} // namespace

MatchOutcome matchEntries(const QVector<kindle::KindleEntry> &entries,
                          const QVector<ChapterText> &chapters)
{
    MatchOutcome out;

    // Precompute joined-block projections once per chapter.
    QVector<JoinedBlocks> joined;
    joined.reserve(chapters.size());
    for (const ChapterText &c : chapters)
        joined.append(buildJoined(c));

    QHash<int, int> byChapterNumber; // chapter number -> chapters index
    for (int i = 0; i < chapters.size(); ++i) {
        byChapterNumber.insert(i + 1, i);
        if (auto num = labelChapterNumber(chapters[i].label))
            byChapterNumber.insert(*num, i);
    }

    for (const kindle::KindleEntry &entry : entries) {
        if (entry.text.isEmpty())
            continue;

        bool matched = false;
        MatchResult result;

        // Prefer the chapter hinted by the entry's chapter number, if any.
        if (entry.chapterNumber) {
            auto it = byChapterNumber.constFind(*entry.chapterNumber);
            if (it != byChapterNumber.constEnd()) {
                const int ci = it.value();
                matched = findInChapter(entry, chapters[ci], joined[ci], result);
            }
        }
        // Otherwise scan all chapters in order.
        if (!matched) {
            for (int ci = 0; ci < chapters.size(); ++ci) {
                if (findInChapter(entry, chapters[ci], joined[ci], result)) {
                    matched = true;
                    break;
                }
            }
        }

        if (matched)
            out.matches.append(result);
        else
            out.failures.append(entry);
    }

    return out;
}

} // namespace matcher
