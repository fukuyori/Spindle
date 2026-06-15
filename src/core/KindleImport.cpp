#include "core/KindleImport.h"

#include <QRegularExpression>
#include <QSet>
#include <vector>

namespace kindle {

namespace {

// --- minimal tolerant HTML parser -----------------------------------------
struct HNode {
    QString tag;       // empty => text node; "#root" for the root
    QString classAttr;
    QString text;      // text-node content
    std::vector<HNode> children;
    bool isText() const { return tag.isEmpty(); }
};

QString decodeEntities(const QString &in)
{
    QString out;
    out.reserve(in.size());
    for (int i = 0; i < in.size();) {
        const QChar c = in.at(i);
        if (c != QLatin1Char('&')) {
            out += c;
            ++i;
            continue;
        }
        const int semi = in.indexOf(QLatin1Char(';'), i + 1);
        if (semi < 0 || semi - i > 12) {
            out += c;
            ++i;
            continue;
        }
        const QString ent = in.mid(i + 1, semi - i - 1);
        QChar decoded;
        bool ok = true;
        if (ent == QLatin1String("amp")) decoded = QLatin1Char('&');
        else if (ent == QLatin1String("lt")) decoded = QLatin1Char('<');
        else if (ent == QLatin1String("gt")) decoded = QLatin1Char('>');
        else if (ent == QLatin1String("quot")) decoded = QLatin1Char('"');
        else if (ent == QLatin1String("apos") || ent == QLatin1String("#39")) decoded = QLatin1Char('\'');
        else if (ent == QLatin1String("nbsp")) decoded = QChar(0x00a0);
        else if (ent.startsWith(QLatin1Char('#'))) {
            bool num = false;
            const int code = ent.startsWith(QLatin1String("#x")) || ent.startsWith(QLatin1String("#X"))
                                 ? ent.mid(2).toInt(&num, 16)
                                 : ent.mid(1).toInt(&num, 10);
            if (num && code > 0)
                decoded = QChar(code);
            else
                ok = false;
        } else {
            ok = false;
        }
        if (ok) {
            out += decoded;
            i = semi + 1;
        } else {
            out += c;
            ++i;
        }
    }
    return out;
}

QString attrValue(const QString &attrs, const QString &name)
{
    const QRegularExpression re(
        QStringLiteral("\\b%1\\s*=\\s*(\"([^\"]*)\"|'([^']*)')").arg(name),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(attrs);
    if (!m.hasMatch())
        return {};
    return m.captured(2).isNull() ? m.captured(3) : m.captured(2);
}

const QSet<QString> &voidElements()
{
    static const QSet<QString> s = {"area", "base", "br", "col", "embed", "hr", "img",
                                    "input", "link", "meta", "param", "source", "track", "wbr"};
    return s;
}

HNode parseHtml(QString s)
{
    // Drop comments, doctype, scripts and styles up front.
    static const QRegularExpression comment(QStringLiteral("<!--.*?-->"),
                                            QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression doctype(QStringLiteral("<!DOCTYPE[^>]*>"),
                                            QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression scriptStyle(
        QStringLiteral("<(script|style)\\b[^>]*>.*?</\\1>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    s.remove(comment);
    s.remove(doctype);
    s.remove(scriptStyle);

    HNode root;
    root.tag = QStringLiteral("#root");
    std::vector<HNode *> stack;
    stack.push_back(&root);

    const int n = s.size();
    int i = 0;
    while (i < n) {
        if (s.at(i) == QLatin1Char('<')) {
            const int gt = s.indexOf(QLatin1Char('>'), i);
            if (gt < 0)
                break;
            QString inner = s.mid(i + 1, gt - i - 1);
            i = gt + 1;
            if (inner.isEmpty())
                continue;

            if (inner.startsWith(QLatin1Char('/'))) {
                const QString name = inner.mid(1).trimmed().toLower();
                for (int k = static_cast<int>(stack.size()) - 1; k >= 1; --k) {
                    if (stack[static_cast<size_t>(k)]->tag == name) {
                        stack.resize(static_cast<size_t>(k));
                        break;
                    }
                }
            } else {
                bool selfClose = inner.endsWith(QLatin1Char('/'));
                if (selfClose)
                    inner.chop(1);
                inner = inner.trimmed();
                int sp = -1;
                for (int k = 0; k < inner.size(); ++k) {
                    if (inner.at(k).isSpace()) { sp = k; break; }
                }
                const QString name = (sp < 0 ? inner : inner.left(sp)).toLower();
                const QString attrs = sp < 0 ? QString() : inner.mid(sp + 1);
                HNode node;
                node.tag = name;
                node.classAttr = attrValue(attrs, QStringLiteral("class"));
                stack.back()->children.push_back(std::move(node));
                if (!selfClose && !voidElements().contains(name))
                    stack.push_back(&stack.back()->children.back());
            }
        } else {
            const int lt = s.indexOf(QLatin1Char('<'), i);
            const QString chunk = (lt < 0 ? s.mid(i) : s.mid(i, lt - i));
            i = (lt < 0 ? n : lt);
            if (!chunk.isEmpty()) {
                HNode t;
                t.text = decodeEntities(chunk);
                stack.back()->children.push_back(std::move(t));
            }
        }
    }
    return root;
}

void collectText(const HNode &node, QString &out)
{
    if (node.isText()) {
        out += node.text;
        return;
    }
    for (const HNode &c : node.children)
        collectText(c, out);
}

// textContent collapsed to single spaces and trimmed (matches kindle-import text()).
QString nodeText(const HNode &node)
{
    QString raw;
    collectText(node, raw);
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    return raw.replace(ws, QStringLiteral(" ")).trimmed();
}

const HNode *findByClassContains(const HNode &node, const QString &needle)
{
    for (const HNode &c : node.children) {
        if (c.isText())
            continue;
        if (c.classAttr.contains(needle))
            return &c;
        if (const HNode *found = findByClassContains(c, needle))
            return found;
    }
    return nullptr;
}

const HNode *findFirstTag(const HNode &node, const QString &tag)
{
    for (const HNode &c : node.children) {
        if (c.isText())
            continue;
        if (c.tag == tag)
            return &c;
        if (const HNode *found = findFirstTag(c, tag))
            return found;
    }
    return nullptr;
}

// --- Kindle-specific parsing ----------------------------------------------
HighlightColor mapColor(const QString &raw)
{
    const QString c = raw.toLower();
    if (c == QLatin1String("blue") || c == QLatin1String("aqua")) return HighlightColor::Blue;
    if (c == QLatin1String("pink") || c == QLatin1String("red")) return HighlightColor::Pink;
    if (c == QLatin1String("orange")) return HighlightColor::Orange;
    if (c == QLatin1String("green")) return HighlightColor::Green;
    if (c == QLatin1String("purple")) return HighlightColor::Purple;
    return HighlightColor::Yellow;
}

std::optional<int> firstIntCapture(const QString &raw, const QString &pattern)
{
    const QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(raw);
    if (!m.hasMatch())
        return std::nullopt;
    bool ok = false;
    const int v = m.captured(1).toInt(&ok);
    return ok ? std::optional<int>(v) : std::nullopt;
}

struct PendingHeading {
    HighlightColor color = HighlightColor::Yellow;
    std::optional<int> page;
    std::optional<int> location;
    EntryKind kind = EntryKind::Highlight;
};

PendingHeading parseNoteHeading(const HNode &el, QString &currentChapter,
                                std::optional<int> &currentChapterNumber)
{
    const QString raw = nodeText(el);
    PendingHeading p;

    const HNode *colorSpan = findByClassContains(el, QStringLiteral("annotation_"));
    if (!colorSpan)
        colorSpan = findByClassContains(el, QStringLiteral("highlight_"));
    if (colorSpan) {
        const QRegularExpression re(QStringLiteral("(?:annotation|highlight)_(\\w+)"));
        const QRegularExpressionMatch m = re.match(colorSpan->classAttr);
        if (m.hasMatch())
            p.color = mapColor(m.captured(1));
    }

    const QString lower = raw.toLower();
    if (raw.contains(QStringLiteral("メモ")) || lower.contains(QStringLiteral("note")))
        p.kind = EntryKind::Note;
    else if (raw.contains(QStringLiteral("ブックマーク")) || lower.contains(QStringLiteral("bookmark")))
        p.kind = EntryKind::Bookmark;
    else
        p.kind = EntryKind::Highlight;

    p.page = firstIntCapture(raw, QStringLiteral("(?:ページ|Page)\\s*[:：]?\\s*([0-9]+)"));
    p.location = firstIntCapture(
        raw, QStringLiteral("(?:位置|Location|Loc\\.?)\\s*(?:No\\.?|[#:：·])?\\s*([0-9]+)"));

    const int dash = raw.indexOf(QStringLiteral(" - "));
    QString chapterPart;
    if (dash >= 0) {
        const QString afterDash = raw.mid(dash + 3);
        chapterPart = afterDash.section(QLatin1Char('>'), 0, 0).trimmed();
    }
    std::optional<int> chapterNumber =
        firstIntCapture(chapterPart, QStringLiteral("Chapter\\s*(\\d+)"));
    if (!chapterNumber)
        chapterNumber = firstIntCapture(chapterPart, QStringLiteral("第\\s*(\\d+)\\s*章"));

    if (!chapterPart.isEmpty())
        currentChapter = chapterPart;
    if (chapterNumber)
        currentChapterNumber = chapterNumber;

    return p;
}

} // namespace

KindleNotebook parseKindleNotebook(const QString &html)
{
    KindleNotebook nb;
    const HNode root = parseHtml(html);

    if (const HNode *t = findByClassContains(root, QStringLiteral("bookTitle")))
        nb.title = nodeText(*t);
    if (const HNode *a = findByClassContains(root, QStringLiteral("authors")))
        nb.author = nodeText(*a);

    const HNode *container = findByClassContains(root, QStringLiteral("bodyContainer"));
    if (!container)
        container = findFirstTag(root, QStringLiteral("body"));
    if (!container)
        container = &root;

    QString currentPart;
    QString currentChapter;
    std::optional<int> currentChapterNumber;
    bool hasPending = false;
    PendingHeading pending;

    for (const HNode &el : container->children) {
        if (el.isText())
            continue;
        const QString cls = el.classAttr;

        if (cls.contains(QStringLiteral("sectionHeading"))) {
            currentPart = nodeText(el);
            continue;
        }
        if (cls.contains(QStringLiteral("noteHeading"))) {
            pending = parseNoteHeading(el, currentChapter, currentChapterNumber);
            hasPending = true;
            continue;
        }
        if (cls.contains(QStringLiteral("noteText")) && hasPending) {
            const QString body = nodeText(el);
            if (body.isEmpty()) {
                hasPending = false;
                continue;
            }
            KindleEntry entry;
            entry.partTitle = currentPart;
            entry.chapterTitle = currentChapter;
            entry.chapterNumber = currentChapterNumber;
            entry.page = pending.page;
            entry.location = pending.location;
            entry.color = pending.color;
            entry.kind = pending.kind;
            entry.text = body;
            nb.entries.append(entry);
            hasPending = false;
        }
    }

    return nb;
}

} // namespace kindle
