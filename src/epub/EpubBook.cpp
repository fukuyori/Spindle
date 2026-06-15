#include "epub/EpubBook.h"

#include "epub/PathUtil.h"

#include <QDomDocument>
#include <QDomElement>
#include <QHash>
#include <QRegularExpression>

namespace {

QString collapseWs(const QString &s)
{
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    return s.simplified().replace(ws, QStringLiteral(" "));
}

const QRegularExpression &placeholderPattern()
{
    static const QRegularExpression re(QStringLiteral("〚/?[A-Za-z]\\d*〛"));
    return re;
}

QString cleanPlaceholderText(const QString &value)
{
    QString out = value;
    return out.remove(placeholderPattern());
}

QDomDocument parseXml(const QByteArray &data)
{
    QDomDocument doc;
    // Namespace processing on so localName()/namespaceURI() are populated.
    doc.setContent(data, QDomDocument::ParseOption::UseNamespaceProcessing);
    return doc;
}

// Recursively find the first element with a matching local name.
QDomElement firstByLocalName(const QDomElement &root, const QString &localName)
{
    if (root.localName() == localName || root.tagName() == localName)
        return root;
    for (QDomNode n = root.firstChild(); !n.isNull(); n = n.nextSibling()) {
        if (!n.isElement())
            continue;
        QDomElement found = firstByLocalName(n.toElement(), localName);
        if (!found.isNull())
            return found;
    }
    return {};
}

QString textByLocalName(const QDomDocument &doc, const QString &localName)
{
    QDomElement el = firstByLocalName(doc.documentElement(), localName);
    return el.isNull() ? QString() : collapseWs(el.text());
}

// Collect descendant elements matching a local name.
void collectByLocalName(const QDomElement &root, const QString &localName, QVector<QDomElement> &out)
{
    for (QDomNode n = root.firstChild(); !n.isNull(); n = n.nextSibling()) {
        if (!n.isElement())
            continue;
        QDomElement el = n.toElement();
        if (el.localName() == localName || el.tagName() == localName)
            out.append(el);
        collectByLocalName(el, localName, out);
    }
}

QVector<QDomElement> elementsByLocalName(const QDomElement &root, const QString &localName)
{
    QVector<QDomElement> out;
    collectByLocalName(root, localName, out);
    return out;
}

// Direct element children with a given (case-insensitive) tag local name.
QVector<QDomElement> childElements(const QDomElement &parent, const QString &localName)
{
    QVector<QDomElement> out;
    for (QDomNode n = parent.firstChild(); !n.isNull(); n = n.nextSibling()) {
        if (!n.isElement())
            continue;
        QDomElement el = n.toElement();
        const QString name = el.localName().isEmpty() ? el.tagName() : el.localName();
        if (name.compare(localName, Qt::CaseInsensitive) == 0)
            out.append(el);
    }
    return out;
}

QDomElement firstChildElement(const QDomElement &parent, const QString &localName)
{
    const auto kids = childElements(parent, localName);
    return kids.isEmpty() ? QDomElement() : kids.first();
}

} // namespace

bool EpubBook::open(const QString &filePath)
{
    m_valid = false;
    m_error.clear();

    if (!m_zip.open(filePath)) {
        m_error = QStringLiteral("EPUB ファイルを開けませんでした");
        return false;
    }

    QDomDocument container = parseXml(m_zip.readBytes(QStringLiteral("META-INF/container.xml")));
    QDomElement rootfileEl = firstByLocalName(container.documentElement(), QStringLiteral("rootfile"));
    const QString rootfile = rootfileEl.attribute(QStringLiteral("full-path"));
    if (rootfile.isEmpty()) {
        m_error = QStringLiteral("EPUB rootfile が見つかりません");
        return false;
    }

    QDomDocument opf = parseXml(m_zip.readBytes(rootfile));
    m_opfDir = path_util::dirname(rootfile);
    m_title = textByLocalName(opf, QStringLiteral("title"));
    m_author = textByLocalName(opf, QStringLiteral("creator"));

    // --- manifest ---
    QHash<QString, ManifestItem> manifest;
    QVector<ManifestItem> manifestOrder;
    QDomElement manifestEl = firstByLocalName(opf.documentElement(), QStringLiteral("manifest"));
    for (const QDomElement &item : elementsByLocalName(manifestEl, QStringLiteral("item"))) {
        const QString id = item.attribute(QStringLiteral("id"));
        const QString href = item.attribute(QStringLiteral("href"));
        if (id.isEmpty() || href.isEmpty())
            continue;
        ManifestItem mi;
        mi.href = href;
        mi.mediaType = item.attribute(QStringLiteral("media-type"));
        mi.path = path_util::resolve(m_opfDir, href);
        mi.properties = item.attribute(QStringLiteral("properties"));
        manifest.insert(id, mi);
        manifestOrder.append(mi);
    }

    // --- spine ---
    QDomElement spineEl = firstByLocalName(opf.documentElement(), QStringLiteral("spine"));
    const QString ppd = spineEl.attribute(QStringLiteral("page-progression-direction"),
                                          QStringLiteral("ltr"));
    m_verticalRtl = (ppd == QLatin1String("rtl"));

    QStringList spine;
    for (const QDomElement &itemref : elementsByLocalName(spineEl, QStringLiteral("itemref"))) {
        const QString idref = itemref.attribute(QStringLiteral("idref"));
        if (!idref.isEmpty())
            spine.append(idref);
    }

    // --- table of contents (nav preferred, ncx fallback) ---
    ManifestItem navItem;
    bool hasNav = false;
    for (const ManifestItem &item : manifestOrder) {
        if (item.properties.split(' ').contains(QStringLiteral("nav"))
            || item.mediaType.contains(QStringLiteral("nav"))
            || item.href.endsWith(QStringLiteral("nav.xhtml"))
            || item.href.endsWith(QStringLiteral("nav.html"))) {
            navItem = item;
            hasNav = true;
            break;
        }
    }

    ManifestItem ncxItem;
    bool hasNcx = false;
    const QString ncxId = spineEl.attribute(QStringLiteral("toc"));
    if (manifest.contains(ncxId)) {
        ncxItem = manifest.value(ncxId);
        hasNcx = true;
    } else {
        for (const ManifestItem &item : manifestOrder) {
            if (item.href.endsWith(QStringLiteral(".ncx"))) {
                ncxItem = item;
                hasNcx = true;
                break;
            }
        }
    }

    if (hasNav)
        m_toc = parseNavToc(navItem.path);
    if (m_toc.isEmpty() && hasNcx)
        m_toc = parseNcxToc(ncxItem.path);

    // --- chapters (spine order) ---
    int index = 0;
    for (const QString &id : spine) {
        auto it = manifest.constFind(id);
        if (it == manifest.constEnd())
            continue;
        Chapter ch;
        ch.id = id;
        ch.href = it->href;
        ch.path = it->path;
        const QString label = findTocLabel(m_toc, ch.path);
        ch.label = label.isEmpty() ? QStringLiteral("Chapter %1").arg(index + 1) : label;
        m_chapters.append(ch);
        ++index;
    }

    if (m_chapters.isEmpty()) {
        m_error = QStringLiteral("EPUB の spine が空です");
        return false;
    }

    if (m_toc.isEmpty()) {
        for (const Chapter &ch : m_chapters)
            m_toc.append(TocItem{ch.label, ch.path, {}});
    }

    m_valid = true;
    return true;
}

QVector<TocItem> EpubBook::parseNavToc(const QString &navPath)
{
    QDomDocument doc = parseXml(m_zip.readBytes(navPath));
    QDomElement docEl = doc.documentElement();

    // Find a <nav> (prefer epub:type/type ~= toc), then its first <ol>.
    QVector<QDomElement> navs = elementsByLocalName(docEl, QStringLiteral("nav"));
    QDomElement chosen;
    for (const QDomElement &nav : navs) {
        const QString type = nav.attribute(QStringLiteral("epub:type"),
                                           nav.attribute(QStringLiteral("type")));
        if (type.split(' ').contains(QStringLiteral("toc"))) {
            chosen = nav;
            break;
        }
    }
    if (chosen.isNull() && !navs.isEmpty())
        chosen = navs.first();
    if (chosen.isNull())
        return {};

    QDomElement ol = firstChildElement(chosen, QStringLiteral("ol"));
    if (ol.isNull()) {
        const auto ols = elementsByLocalName(chosen, QStringLiteral("ol"));
        if (!ols.isEmpty())
            ol = ols.first();
    }
    if (ol.isNull())
        return {};
    return parseNavList(ol, path_util::dirname(navPath));
}

QVector<TocItem> EpubBook::parseNavList(const QDomElement &list, const QString &basePath)
{
    QVector<TocItem> items;
    for (const QDomElement &li : childElements(list, QStringLiteral("li"))) {
        QDomElement anchor = firstChildElement(li, QStringLiteral("a"));
        if (anchor.isNull())
            anchor = firstChildElement(li, QStringLiteral("span"));
        QDomElement nested = firstChildElement(li, QStringLiteral("ol"));

        const QString href = anchor.isNull() ? QString() : anchor.attribute(QStringLiteral("href"));
        TocItem item;
        const QString label = anchor.isNull() ? QString() : collapseWs(anchor.text()).trimmed();
        item.label = cleanPlaceholderText(label).isEmpty() ? QStringLiteral("Untitled")
                                                           : cleanPlaceholderText(label);
        item.href = href.isEmpty() ? QString() : path_util::resolve(basePath, href);
        if (!nested.isNull())
            item.subitems = parseNavList(nested, basePath);
        items.append(item);
    }
    return items;
}

QVector<TocItem> EpubBook::parseNcxToc(const QString &ncxPath)
{
    QDomDocument doc = parseXml(m_zip.readBytes(ncxPath));
    QDomElement navMap = firstByLocalName(doc.documentElement(), QStringLiteral("navMap"));
    if (navMap.isNull())
        return {};
    return parseNcxPoints(childElements(navMap, QStringLiteral("navPoint")),
                          path_util::dirname(ncxPath));
}

QVector<TocItem> EpubBook::parseNcxPoints(const QVector<QDomElement> &points, const QString &basePath)
{
    QVector<TocItem> items;
    for (const QDomElement &point : points) {
        TocItem item;
        QString label;
        QDomElement navLabel = firstChildElement(point, QStringLiteral("navLabel"));
        if (!navLabel.isNull()) {
            QDomElement textEl = firstChildElement(navLabel, QStringLiteral("text"));
            if (!textEl.isNull())
                label = collapseWs(textEl.text());
        }
        item.label = cleanPlaceholderText(label).isEmpty() ? QStringLiteral("Untitled")
                                                          : cleanPlaceholderText(label);
        QDomElement content = firstChildElement(point, QStringLiteral("content"));
        const QString src = content.isNull() ? QString() : content.attribute(QStringLiteral("src"));
        item.href = path_util::resolve(basePath, src);
        item.subitems = parseNcxPoints(childElements(point, QStringLiteral("navPoint")), basePath);
        items.append(item);
    }
    return items;
}

QString EpubBook::findTocLabel(const QVector<TocItem> &items, const QString &path)
{
    for (const TocItem &item : items) {
        if (path_util::stripHash(item.href) == path)
            return item.label;
        const QString nested = findTocLabel(item.subitems, path);
        if (!nested.isEmpty())
            return nested;
    }
    return {};
}

int EpubBook::chapterIndexForPath(const QString &path) const
{
    const QString clean = path_util::stripHash(path);
    for (int i = 0; i < m_chapters.size(); ++i) {
        if (m_chapters[i].path == clean)
            return i;
    }
    return -1;
}
