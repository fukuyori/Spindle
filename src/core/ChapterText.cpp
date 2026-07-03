#include "core/ChapterText.h"

#include "epub/EpubBook.h"

#include <QDomDocument>
#include <QRegularExpression>

namespace {

// Whitespace set matching matcher.ts: space, tab, LF, CR, ideographic space, nbsp.
bool isWhitespace(ushort u)
{
    return u == 0x20 || u == 0x09 || u == 0x0a || u == 0x0d || u == 0x3000 || u == 0xa0;
}

void appendTextContent(const QDomNode &node, QString &out)
{
    for (QDomNode n = node.firstChild(); !n.isNull(); n = n.nextSibling()) {
        if (n.isElement()) {
            const QString tag = n.toElement().tagName().toLower();
            if (tag == QLatin1String("script") || tag == QLatin1String("style"))
                continue;
            appendTextContent(n, out);
        } else if (n.isText() || n.isCDATASection()) {
            out += n.nodeValue();
        }
    }
}

} // namespace

QString extractChapterBody(const QString &xhtml)
{
    QDomDocument doc;
    if (!doc.setContent(xhtml, QDomDocument::ParseOption::UseNamespaceProcessing)) {
        // Lenient retry without namespace processing for slightly malformed XHTML.
        if (!doc.setContent(xhtml))
            return {};
    }

    // Locate <body>; fall back to the document element.
    QDomElement bodyEl;
    QDomNodeList bodies = doc.elementsByTagName(QStringLiteral("body"));
    if (bodies.isEmpty())
        bodies = doc.elementsByTagName(QStringLiteral("html"));
    if (!bodies.isEmpty())
        bodyEl = bodies.at(0).toElement();
    const QDomNode root = bodyEl.isNull() ? QDomNode(doc.documentElement()) : QDomNode(bodyEl);

    QString text;
    appendTextContent(root, text);

    // Keep offsets aligned with the page's document.body.textContent (what the
    // injected reader JS uses for highlight rendering): do NOT remove placeholder
    // markers (that would shift offsets). The nbsp→space swap is length-preserving
    // and only helps text matching, so positions stay identical.
    text.replace(QChar(0x00a0), QLatin1Char(' '));
    return text;
}

ChapterText buildChapterText(const ChapterRef &ref, const QString &body)
{
    ChapterText ct;
    ct.path = ref.path;
    ct.label = ref.label;
    ct.body = body;

    // normalize: collapse whitespace runs to a single space, drop leading space.
    QString normalized;
    QVector<int> normMap;
    normalized.reserve(body.size());
    normMap.reserve(body.size());
    bool lastWasSpace = true;
    for (int i = 0; i < body.size(); ++i) {
        const ushort u = body.at(i).unicode();
        if (isWhitespace(u)) {
            if (!lastWasSpace) {
                normalized += QLatin1Char(' ');
                normMap.append(i);
                lastWasSpace = true;
            }
        } else {
            normalized += body.at(i);
            normMap.append(i);
            lastWasSpace = false;
        }
    }
    ct.normalizedBody = normalized;
    ct.normalizedLower = normalized.toLower();
    ct.normalizedToOriginal = normMap;

    // compact: strip all whitespace.
    QString compact;
    QVector<int> compactMap;
    compact.reserve(body.size());
    compactMap.reserve(body.size());
    for (int i = 0; i < body.size(); ++i) {
        if (!isWhitespace(body.at(i).unicode())) {
            compact += body.at(i);
            compactMap.append(i);
        }
    }
    ct.compactBody = compact;
    ct.compactToOriginal = compactMap;

    return ct;
}

QVector<ChapterText> buildChapterTextsFromSources(const QVector<ChapterSource> &sources)
{
    QVector<ChapterText> out;
    out.reserve(sources.size());
    for (const ChapterSource &src : sources) {
        // One parse per chapter: body text and leaf blocks come from the same
        // QDomDocument (scanChapter) instead of two independent parses.
        const block_index::ChapterScan scan = block_index::scanChapter(src.xhtml);
        ChapterText ct = buildChapterText(ChapterRef{src.path, src.label}, scan.bodyText);
        ct.blocks = scan.blocks;
        out.append(ct);
    }
    return out;
}

QVector<ChapterText> buildChapterTexts(const EpubBook &book,
                                       const QVector<ChapterRef> &chapters)
{
    QVector<ChapterSource> sources;
    sources.reserve(chapters.size());
    for (const ChapterRef &ref : chapters) {
        if (!book.contains(ref.path))
            continue;
        sources.append({ref.path, ref.label, book.readText(ref.path)});
    }
    return buildChapterTextsFromSources(sources);
}
