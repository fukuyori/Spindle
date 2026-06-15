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

bool findBestMatch(const kindle::KindleEntry &entry, const QVector<const ChapterText *> &candidates,
                   MatchResult &result)
{
    const QString &target = entry.text;
    const QString normalizedTarget = normalizeForMatch(target);
    const QString compactTarget = compactForMatch(target);

    for (const ChapterText *chapter : candidates) {
        const int exact = static_cast<int>(chapter->body.indexOf(target));
        if (exact >= 0) {
            result = {entry, chapter->path, exact, static_cast<int>(exact + target.size()), target,
                      QStringLiteral("exact")};
            return true;
        }

        if (!normalizedTarget.isEmpty()) {
            const int idx = chapter->normalizedBody.indexOf(normalizedTarget);
            if (idx >= 0) {
                const int start = chapter->normalizedToOriginal.value(idx);
                const int endIdx = qMin(idx + normalizedTarget.size() - 1,
                                        chapter->normalizedToOriginal.size() - 1);
                const int end = chapter->normalizedToOriginal.value(endIdx) + 1;
                result = {entry, chapter->path, start, end, chapter->body.mid(start, end - start),
                          QStringLiteral("normalized")};
                return true;
            }
        }

        if (!compactTarget.isEmpty()) {
            const int idx = chapter->compactBody.indexOf(compactTarget);
            if (idx >= 0) {
                const int start = chapter->compactToOriginal.value(idx);
                const int endIdx = qMin(idx + compactTarget.size() - 1,
                                        chapter->compactToOriginal.size() - 1);
                const int end = chapter->compactToOriginal.value(endIdx) + 1;
                result = {entry, chapter->path, start, end, chapter->body.mid(start, end - start),
                          QStringLiteral("normalized")};
                return true;
            }
        }
    }

    if (compactTarget.size() >= 10) {
        const double ratios[] = {0.6, 0.4, 0.3};
        for (double ratio : ratios) {
            const int prefixLen = qMin(compactTarget.size(),
                                       qMax(15, qMin(40, static_cast<int>(compactTarget.size() * ratio))));
            const QString prefix = compactTarget.left(prefixLen);
            for (const ChapterText *chapter : candidates) {
                const int idx = chapter->compactBody.indexOf(prefix);
                if (idx >= 0) {
                    const int start = chapter->compactToOriginal.value(idx);
                    const int endIdx = qMin(idx + compactTarget.size() - 1,
                                            chapter->compactToOriginal.size() - 1);
                    const int end = chapter->compactToOriginal.value(endIdx) + 1;
                    result = {entry, chapter->path, start, end, chapter->body.mid(start, end - start),
                              QStringLiteral("prefix")};
                    return true;
                }
            }
        }
    }

    return false;
}

} // namespace

MatchOutcome matchEntries(const QVector<kindle::KindleEntry> &entries,
                          const QVector<ChapterText> &chapters)
{
    MatchOutcome out;

    QHash<int, const ChapterText *> byChapterNumber;
    for (int i = 0; i < chapters.size(); ++i) {
        byChapterNumber.insert(i + 1, &chapters[i]);
        if (auto num = labelChapterNumber(chapters[i].label))
            byChapterNumber.insert(*num, &chapters[i]);
    }

    QVector<const ChapterText *> all;
    all.reserve(chapters.size());
    for (const ChapterText &c : chapters)
        all.append(&c);

    for (const kindle::KindleEntry &entry : entries) {
        if (entry.text.isEmpty())
            continue;

        QVector<const ChapterText *> candidates;
        if (entry.chapterNumber) {
            auto it = byChapterNumber.constFind(*entry.chapterNumber);
            if (it != byChapterNumber.constEnd())
                candidates.append(it.value());
        }
        if (candidates.isEmpty())
            candidates = all;

        MatchResult result;
        bool matched = findBestMatch(entry, candidates, result);
        if (!matched && candidates.size() < all.size())
            matched = findBestMatch(entry, all, result);

        if (matched)
            out.matches.append(result);
        else
            out.failures.append(entry);
    }

    return out;
}

} // namespace matcher
