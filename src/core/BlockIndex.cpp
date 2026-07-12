#include "core/BlockIndex.h"

#include <QDomDocument>
#include <QDomElement>
#include <QSet>

namespace block_index {
namespace {

// Expects an already-lowercased tag name.
bool isBlockTag(const QString &tag)
{
    static const QSet<QString> tags = {
        QStringLiteral("p"),  QStringLiteral("h1"),         QStringLiteral("h2"),
        QStringLiteral("h3"), QStringLiteral("h4"),         QStringLiteral("h5"),
        QStringLiteral("h6"), QStringLiteral("li"),         QStringLiteral("blockquote"),
        QStringLiteral("dd"), QStringLiteral("figcaption"), QStringLiteral("dt")};
    return tags.contains(tag);
}

// Concatenated text content, skipping script/style, with nbsp normalized to a
// regular space (matches ChapterText / the page's textContent length per char).
void appendText(const QDomNode &node, QString &out)
{
    for (QDomNode n = node.firstChild(); !n.isNull(); n = n.nextSibling()) {
        if (n.isElement()) {
            const QString tag = n.toElement().tagName().toLower();
            if (tag == QLatin1String("script") || tag == QLatin1String("style"))
                continue;
            appendText(n, out);
        } else if (n.isText() || n.isCDATASection()) {
            out += n.nodeValue();
        }
    }
}

// Single pass over the subtree of `el`, appending leaf blocks (block-tag
// elements with no block-tag descendant) to `leaves` in document order.
// Returns whether the subtree contains any block-tag element. A block child
// whose subtree added no leaves has no block descendants, so it is itself a
// leaf — this keeps the walk O(n) (no per-element descendant rescans).
bool collectLeaves(const QDomElement &el, QVector<QDomElement> &leaves)
{
    bool hasBlock = false;
    for (QDomNode n = el.firstChild(); !n.isNull(); n = n.nextSibling()) {
        if (!n.isElement())
            continue;
        QDomElement c = n.toElement();
        const QString tag = c.tagName().toLower();
        if (tag == QLatin1String("script") || tag == QLatin1String("style"))
            continue;
        const bool childHas = collectLeaves(c, leaves);
        const bool blk = isBlockTag(tag);
        if (blk && !childHas)
            leaves.append(c);
        hasBlock = hasBlock || blk || childHas;
    }
    return hasBlock;
}

// Containers whose direct inline content is prose when it holds bare text.
// Block tags themselves are excluded: a blockquote/p with bare text is already
// a leaf block, and re-wrapping would shift existing highlight anchors.
bool isWrapContainer(const QString &tag)
{
    static const QSet<QString> tags = {
        QStringLiteral("body"),    QStringLiteral("div"),  QStringLiteral("section"),
        QStringLiteral("article"), QStringLiteral("main"), QStringLiteral("aside"),
        QStringLiteral("center"),  QStringLiteral("td"),   QStringLiteral("th")};
    return tags.contains(tag);
}

// Phrasing-level elements that belong inside a wrapped paragraph. Anything
// else (tables, lists, figures, unknown tags…) ends the current run.
bool isInlineTag(const QString &tag)
{
    static const QSet<QString> tags = {
        QStringLiteral("span"), QStringLiteral("a"),    QStringLiteral("em"),
        QStringLiteral("strong"), QStringLiteral("i"),  QStringLiteral("b"),
        QStringLiteral("u"),    QStringLiteral("s"),    QStringLiteral("small"),
        QStringLiteral("big"),  QStringLiteral("sub"),  QStringLiteral("sup"),
        QStringLiteral("ruby"), QStringLiteral("rt"),   QStringLiteral("rp"),
        QStringLiteral("rb"),   QStringLiteral("code"), QStringLiteral("q"),
        QStringLiteral("cite"), QStringLiteral("abbr"), QStringLiteral("time"),
        QStringLiteral("mark"), QStringLiteral("kbd"),  QStringLiteral("samp"),
        QStringLiteral("var"),  QStringLiteral("dfn"),  QStringLiteral("ins"),
        QStringLiteral("del"),  QStringLiteral("tt"),   QStringLiteral("font"),
        QStringLiteral("wbr"),  QStringLiteral("bdi"),  QStringLiteral("bdo"),
        QStringLiteral("img"),  QStringLiteral("image"), QStringLiteral("nobr")};
    return tags.contains(tag);
}

QString lowerTag(const QDomElement &el)
{
    const QString ln = el.localName();
    return (ln.isEmpty() ? el.tagName() : ln).toLower();
}

bool isWhitespaceText(const QDomNode &n)
{
    return (n.isText() || n.isCDATASection()) && n.nodeValue().trimmed().isEmpty();
}

// Move a run of inline nodes into a synthetic <p> if it carries any real text
// (image-only or whitespace runs are left in place). margin/text-indent are
// zeroed so the wrapped text renders like the bare inline flow it was.
void wrapRun(QDomDocument &doc, QDomElement &parent, QVector<QDomNode> &run)
{
    if (run.isEmpty())
        return;
    QString text;
    for (const QDomNode &n : run) {
        if (n.isText() || n.isCDATASection())
            text += n.nodeValue();
        else if (n.isElement())
            appendText(n, text);
    }
    if (text.trimmed().isEmpty()) {
        run.clear();
        return;
    }
    QDomElement p = doc.createElement(QStringLiteral("p"));
    p.setAttribute(QStringLiteral("style"), QStringLiteral("margin:0;text-indent:0;"));
    parent.insertBefore(p, run.first());
    for (const QDomNode &n : run)
        p.appendChild(n); // appendChild reparents
    run.clear();
}

void normalizeContainer(QDomDocument &doc, QDomElement el)
{
    // Snapshot the children first: wrapping reparents nodes as we go.
    QVector<QDomNode> children;
    for (QDomNode n = el.firstChild(); !n.isNull(); n = n.nextSibling())
        children.append(n);

    // Recurse into child elements before rearranging this level.
    for (const QDomNode &n : children) {
        if (!n.isElement())
            continue;
        const QString tag = lowerTag(n.toElement());
        if (tag != QLatin1String("script") && tag != QLatin1String("style"))
            normalizeContainer(doc, n.toElement());
    }

    if (!isWrapContainer(lowerTag(el)))
        return;

    QVector<QDomNode> run;   // current inline run, in document order
    QVector<QDomNode> brSeq; // pending <br> sequence (brs + whitespace between)
    int brCount = 0;

    auto settleBrSeq = [&]() {
        if (brCount >= 2) {
            // Paragraph separator. Wrap what came before; drop one <br> so the
            // block boundary replaces the line break it used to render, then
            // leave the rest in place to keep the original blank-line spacing.
            wrapRun(doc, el, run);
            for (const QDomNode &b : brSeq) {
                if (b.isElement()) {
                    el.removeChild(b);
                    break;
                }
            }
        } else {
            run += brSeq; // a lone <br> is a line break within the paragraph
        }
        brSeq.clear();
        brCount = 0;
    };

    for (const QDomNode &n : children) {
        if (n.isElement() && lowerTag(n.toElement()) == QLatin1String("br")) {
            brSeq.append(n);
            ++brCount;
            continue;
        }
        if (!brSeq.isEmpty() && isWhitespaceText(n)) {
            brSeq.append(n);
            continue;
        }
        settleBrSeq();
        if (n.isText() || n.isCDATASection()) {
            run.append(n);
            continue;
        }
        if (n.isElement() && isInlineTag(lowerTag(n.toElement()))) {
            run.append(n);
            continue;
        }
        // Block tag, container, comment, or unknown element: ends the run.
        wrapRun(doc, el, run);
    }
    settleBrSeq();
    wrapRun(doc, el, run);
}

QDomElement bodyElement(const QDomDocument &doc)
{
    QDomNodeList bodies = doc.elementsByTagName(QStringLiteral("body"));
    if (bodies.isEmpty())
        bodies = doc.elementsByTagName(QStringLiteral("html"));
    if (!bodies.isEmpty())
        return bodies.at(0).toElement();
    return doc.documentElement();
}

bool parse(QDomDocument &doc, const QString &xhtml)
{
    if (doc.setContent(xhtml, QDomDocument::ParseOption::UseNamespaceProcessing))
        return true;
    return static_cast<bool>(doc.setContent(xhtml)); // lenient retry
}

// Strip active content from a served chapter document. EPUB scripting is not
// supported (matching most readers): <script> elements, on* event-handler
// attributes and javascript: URLs are removed so book markup can't run code in
// the reading view. Runs on the same parsed DOM used for block-id injection.
void sanitizeElement(QDomElement el)
{
    // Attributes: drop event handlers and javascript: URLs.
    const QDomNamedNodeMap attrs = el.attributes();
    QStringList remove;
    for (int i = 0; i < attrs.count(); ++i) {
        const QDomAttr a = attrs.item(i).toAttr();
        if (a.isNull())
            continue;
        const QString name = a.name().toLower();
        if (name.startsWith(QLatin1String("on"))
            || ((name == QLatin1String("href") || name == QLatin1String("src")
                 || name == QLatin1String("xlink:href"))
                && a.value().trimmed().startsWith(QLatin1String("javascript:"),
                                                  Qt::CaseInsensitive))) {
            remove.append(a.name());
        }
    }
    for (const QString &name : remove)
        el.removeAttribute(name);

    // Children: remove <script> outright (HTML or namespaced SVG), recurse rest.
    QVector<QDomElement> children;
    for (QDomNode n = el.firstChild(); !n.isNull(); n = n.nextSibling()) {
        if (n.isElement())
            children.append(n.toElement());
    }
    for (QDomElement c : children) {
        const QString local = c.localName().isEmpty() ? c.tagName() : c.localName();
        if (local.compare(QLatin1String("script"), Qt::CaseInsensitive) == 0)
            el.removeChild(c);
        else
            sanitizeElement(c);
    }
}

QVector<BlockInfo> blocksOf(const QVector<QDomElement> &leaves)
{
    QVector<BlockInfo> out;
    out.reserve(leaves.size());
    for (int i = 0; i < leaves.size(); ++i) {
        QString text;
        appendText(leaves.at(i), text);
        text.replace(QChar(0x00a0), QLatin1Char(' '));
        out.append({i, text});
    }
    return out;
}

} // namespace

void normalizeBareText(QDomElement body)
{
    if (body.isNull())
        return;
    QDomDocument doc = body.ownerDocument();
    normalizeContainer(doc, body);
}

QVector<BlockInfo> enumerateBlocks(const QString &xhtml)
{
    QDomDocument doc;
    if (!parse(doc, xhtml))
        return {};
    QDomElement body = bodyElement(doc);
    if (body.isNull())
        return {};
    normalizeBareText(body);
    QVector<QDomElement> leaves;
    collectLeaves(body, leaves);
    return blocksOf(leaves);
}

ChapterScan scanChapter(const QString &xhtml)
{
    ChapterScan out;
    QDomDocument doc;
    if (!parse(doc, xhtml))
        return out;
    QDomElement body = bodyElement(doc);
    if (body.isNull())
        return out;
    normalizeBareText(body);
    out.ok = true;
    appendText(body, out.bodyText);
    out.bodyText.replace(QChar(0x00a0), QLatin1Char(' '));
    QVector<QDomElement> leaves;
    collectLeaves(body, leaves);
    out.blocks = blocksOf(leaves);
    return out;
}

QString injectBlockIds(const QString &xhtml, const QString &themeCss)
{
    QDomDocument doc;
    if (!parse(doc, xhtml))
        return {};
    QDomElement body = bodyElement(doc);
    if (body.isNull())
        return {};
    // Sanitize the whole document (the <head> can carry scripts too).
    sanitizeElement(doc.documentElement());
    normalizeBareText(body);
    QVector<QDomElement> leaves;
    collectLeaves(body, leaves);
    for (int i = 0; i < leaves.size(); ++i) {
        // QDom nodes are implicitly shared, so this mutates the document.
        leaves[i].setAttribute(QStringLiteral("data-spindle-block"), i);
    }

    // Embed the reader theme so the first painted frame is already styled
    // (no flash of the book's own styles). Placed last in <head> so it wins
    // source-order ties against the book's stylesheets.
    if (!themeCss.isEmpty()) {
        QDomElement style = doc.createElement(QStringLiteral("style"));
        style.setAttribute(QStringLiteral("id"), QStringLiteral("__spindle_theme"));
        style.appendChild(doc.createTextNode(themeCss));
        QDomElement head;
        for (QDomNode n = doc.documentElement().firstChild(); !n.isNull();
             n = n.nextSibling()) {
            if (!n.isElement())
                continue;
            const QDomElement el = n.toElement();
            const QString local = el.localName().isEmpty() ? el.tagName() : el.localName();
            if (local.compare(QLatin1String("head"), Qt::CaseInsensitive) == 0) {
                head = el;
                break;
            }
        }
        if (!head.isNull())
            head.appendChild(style);
        else if (!body.isNull())
            body.insertBefore(style, body.firstChild());
        else
            doc.documentElement().appendChild(style);
    }

    return doc.toString(-1);
}

} // namespace block_index
