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

QVector<BlockInfo> enumerateBlocks(const QString &xhtml)
{
    QDomDocument doc;
    if (!parse(doc, xhtml))
        return {};
    const QDomElement body = bodyElement(doc);
    if (body.isNull())
        return {};
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
    const QDomElement body = bodyElement(doc);
    if (body.isNull())
        return out;
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
