#include "core/BlockIndex.h"

#include <QDomDocument>
#include <QDomElement>
#include <functional>

namespace block_index {
namespace {

bool isBlockTag(const QString &tag)
{
    static const QStringList tags = {
        QStringLiteral("p"),  QStringLiteral("h1"),         QStringLiteral("h2"),
        QStringLiteral("h3"), QStringLiteral("h4"),         QStringLiteral("h5"),
        QStringLiteral("h6"), QStringLiteral("li"),         QStringLiteral("blockquote"),
        QStringLiteral("dd"), QStringLiteral("figcaption"), QStringLiteral("dt")};
    return tags.contains(tag.toLower());
}

// Does this element contain a descendant block element? (Then it is not a leaf.)
bool hasDescendantBlock(const QDomElement &el)
{
    for (QDomNode n = el.firstChild(); !n.isNull(); n = n.nextSibling()) {
        if (!n.isElement())
            continue;
        const QDomElement c = n.toElement();
        if (isBlockTag(c.tagName()))
            return true;
        if (hasDescendantBlock(c))
            return true;
    }
    return false;
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

// Depth-first, document order. Invokes `visit` on each leaf block with its index.
void walkLeafBlocks(const QDomElement &el, int &counter,
                    const std::function<void(const QDomElement &, int)> &visit)
{
    for (QDomNode n = el.firstChild(); !n.isNull(); n = n.nextSibling()) {
        if (!n.isElement())
            continue;
        QDomElement c = n.toElement();
        const QString tag = c.tagName().toLower();
        if (tag == QLatin1String("script") || tag == QLatin1String("style"))
            continue;
        if (isBlockTag(tag) && !hasDescendantBlock(c))
            visit(c, counter++);
        else
            walkLeafBlocks(c, counter, visit);
    }
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

} // namespace

QVector<BlockInfo> enumerateBlocks(const QString &xhtml)
{
    QVector<BlockInfo> out;
    QDomDocument doc;
    if (!parse(doc, xhtml))
        return out;
    const QDomElement body = bodyElement(doc);
    if (body.isNull())
        return out;
    int counter = 0;
    walkLeafBlocks(body, counter, [&](const QDomElement &el, int idx) {
        QString text;
        appendText(el, text);
        text.replace(QChar(0x00a0), QLatin1Char(' '));
        out.append({idx, text});
    });
    return out;
}

QString injectBlockIds(const QString &xhtml)
{
    QDomDocument doc;
    if (!parse(doc, xhtml))
        return {};
    QDomElement body = bodyElement(doc);
    if (body.isNull())
        return {};
    int counter = 0;
    walkLeafBlocks(body, counter, [](const QDomElement &el, int idx) {
        // QDom nodes are implicitly shared, so this mutates the document.
        const_cast<QDomElement &>(el).setAttribute(QStringLiteral("data-spindle-block"), idx);
    });
    return doc.toString(-1);
}

} // namespace block_index
