#include "core/AozoraExport.h"

#include "epub/EpubBook.h"
#include "epub/PathUtil.h"

#include <QDomDocument>
#include <QRegularExpression>
#include <QTextStream>
#include <QVector>

namespace aozora {

namespace {

const char *AOZORA_CSS = R"CSS(body {
  margin: 2em auto;
  padding: 0 2em;
  max-width: 42em;
  font-family: "Hiragino Mincho ProN", "Yu Mincho", "YuMincho", "MS Mincho", serif;
  line-height: 1.8;
  color: #000;
  background: #fff;
}
.metadata { text-align: center; margin-bottom: 3em; }
.title { font-size: 1.8em; font-weight: normal; margin: 0 0 0.5em; }
.author { font-size: 1.2em; font-weight: normal; margin: 0; color: #333; }
.main_text { text-align: justify; }
.o-midashi { font-size: 1.5em; font-weight: bold; margin: 2em 0 1em; }
.naka-midashi { font-size: 1.25em; font-weight: bold; margin: 1.6em 0 0.8em; }
.ko-midashi { font-size: 1.1em; font-weight: bold; margin: 1.2em 0 0.6em; }
.jisage_1 { padding-left: 1em; }
.jisage_2 { padding-left: 2em; }
.jisage_3 { padding-left: 3em; }
.chitsuki_1 { text-align: right; padding-right: 1em; }
.chitsuki_2 { text-align: right; padding-right: 2em; }
.caption { text-align: center; font-size: 0.9em; color: #555; margin: 0.4em 0 1em; }
ruby rt { font-size: 0.5em; }
ruby rp { display: none; }
em.sesame_dot { font-style: normal; text-emphasis: filled sesame; -webkit-text-emphasis: filled sesame; }
em.dot { font-style: normal; text-emphasis: filled circle; -webkit-text-emphasis: filled circle; }
p { margin: 0 0 1em; text-indent: 1em; }
p:first-child { text-indent: 0; }
p.translation { color: #555; border-left: 2px solid #ccc; padding-left: 0.7em; text-indent: 0; }
img { max-width: 100%; height: auto; display: block; margin: 1em auto; }
hr { border: none; border-top: 1px solid #999; margin: 2em 0; }
.bibliographical_information,
.notation_notes,
.after_text { font-size: 0.9em; color: #444; margin-top: 3em; })CSS";

QString escapeXml(const QString &v)
{
    QString out = v;
    out.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    out.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    out.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    out.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    out.replace(QLatin1Char('\''), QStringLiteral("&apos;"));
    return out;
}

QByteArray mimeFor(const QString &path)
{
    const QString p = path.toLower();
    auto ends = [&](const char *s) { return p.endsWith(QLatin1String(s)); };
    if (ends(".jpg") || ends(".jpeg")) return "image/jpeg";
    if (ends(".png")) return "image/png";
    if (ends(".gif")) return "image/gif";
    if (ends(".svg")) return "image/svg+xml";
    if (ends(".webp")) return "image/webp";
    return "application/octet-stream";
}

bool isAozoraClass(const QString &cls)
{
    static const QRegularExpression re(QStringLiteral(
        "^(o-midashi|naka-midashi|ko-midashi|jisage_\\d+|chitsuki_\\d+|main_text|metadata|title|"
        "author|bibliographical_information|notation_notes|after_text|sesame_dot|dot|caption|"
        "translation)$"));
    return re.match(cls).hasMatch();
}

QString tagOf(const QDomElement &el)
{
    const QString ln = el.localName();
    return (ln.isEmpty() ? el.tagName() : ln).toLower();
}

void collectElements(const QDomElement &root, QVector<QDomElement> &out)
{
    for (QDomNode n = root.firstChild(); !n.isNull(); n = n.nextSibling()) {
        if (!n.isElement())
            continue;
        QDomElement el = n.toElement();
        out.append(el);
        collectElements(el, out);
    }
}

void moveChildren(QDomElement &from, QDomElement &to)
{
    while (!from.firstChild().isNull())
        to.appendChild(from.firstChild()); // appendChild reparents
}

void renameElement(QDomDocument &doc, QDomElement el, const QString &newTag, const QString &newClass)
{
    QDomElement repl = doc.createElement(newTag);
    if (!newClass.isEmpty())
        repl.setAttribute(QStringLiteral("class"), newClass);
    moveChildren(el, repl);
    QDomNode parent = el.parentNode();
    if (!parent.isNull())
        parent.replaceChild(repl, el);
}

void stripPlaceholders(QDomNode node)
{
    static const QRegularExpression placeholder(QStringLiteral("〚/?[A-Za-z]\\d*〛"));
    for (QDomNode n = node.firstChild(); !n.isNull(); n = n.nextSibling()) {
        if (n.isText()) {
            QString v = n.toText().data();
            if (v.contains(placeholder)) {
                v.remove(placeholder);
                n.toText().setData(v);
            }
        } else if (n.isElement()) {
            stripPlaceholders(n);
        }
    }
}

QString innerHtml(const QDomElement &el)
{
    QString out;
    QTextStream stream(&out);
    for (QDomNode n = el.firstChild(); !n.isNull(); n = n.nextSibling())
        n.save(stream, 0);
    return out;
}

} // namespace

QString exportChapter(const EpubBook &book, const Chapter &chapter)
{
    const QString raw = book.readText(chapter.path);
    QDomDocument doc;
    if (!doc.setContent(raw)) {
        if (!doc.setContent(raw, QDomDocument::ParseOption::UseNamespaceProcessing))
            return {};
    }

    QDomNodeList bodies = doc.elementsByTagName(QStringLiteral("body"));
    if (bodies.isEmpty())
        return {};
    QDomElement body = bodies.at(0).toElement();

    const QString chapterDir = path_util::dirname(chapter.path);

    // Snapshot every element once; transforms below reference original tags.
    QVector<QDomElement> all;
    collectElements(body, all);

    // 1. Remove script/style/link.
    for (const QDomElement &el : all) {
        const QString t = tagOf(el);
        if (t == QLatin1String("script") || t == QLatin1String("style") || t == QLatin1String("link")) {
            QDomNode parent = el.parentNode();
            if (!parent.isNull())
                parent.removeChild(el);
        }
    }

    // 2. Strip 〚..〛 placeholder markers from text.
    stripPlaceholders(body);

    // 3. Inline images as data URIs.
    for (QDomElement el : all) {
        const QString t = tagOf(el);
        if (t != QLatin1String("img") && t != QLatin1String("image"))
            continue;
        QString attr = el.hasAttribute(QStringLiteral("src")) ? QStringLiteral("src")
                       : el.hasAttribute(QStringLiteral("href")) ? QStringLiteral("href")
                                                                 : QStringLiteral("xlink:href");
        const QString value = el.attribute(attr);
        if (value.isEmpty() || path_util::isExternalUrl(value))
            continue;
        const QString resolved = path_util::resolve(chapterDir, value);
        const QByteArray bytes = book.readBytes(path_util::stripHash(resolved));
        if (bytes.isEmpty())
            continue;
        const QString dataUri = QStringLiteral("data:%1;base64,%2")
                                    .arg(QString::fromLatin1(mimeFor(resolved)),
                                         QString::fromLatin1(bytes.toBase64()));
        el.setAttribute(attr, dataUri);
    }

    // 4. Headings → Aozora heading vocabulary (single pass on original tags).
    for (QDomElement el : all) {
        const QString t = tagOf(el);
        if (t == QLatin1String("h1")) renameElement(doc, el, QStringLiteral("h2"), QStringLiteral("o-midashi"));
        else if (t == QLatin1String("h2")) renameElement(doc, el, QStringLiteral("h3"), QStringLiteral("naka-midashi"));
        else if (t == QLatin1String("h3")) renameElement(doc, el, QStringLiteral("h4"), QStringLiteral("ko-midashi"));
        else if (t == QLatin1String("h4") || t == QLatin1String("h5") || t == QLatin1String("h6"))
            renameElement(doc, el, QStringLiteral("h5"), QStringLiteral("ko-midashi"));
    }

    // 5. Emphasis markup → Aozora vocabulary.
    for (QDomElement el : all) {
        const QString t = tagOf(el);
        if (t == QLatin1String("strong") || t == QLatin1String("b"))
            renameElement(doc, el, QStringLiteral("em"), QStringLiteral("sesame_dot"));
        else if (t == QLatin1String("i"))
            renameElement(doc, el, QStringLiteral("em"), QString());
    }

    // 6/7. Filter class attributes to Aozora vocabulary; drop inline styles.
    //      Re-collect because renames created fresh elements.
    QVector<QDomElement> current;
    collectElements(body, current);
    for (QDomElement el : current) {
        if (el.hasAttribute(QStringLiteral("style")))
            el.removeAttribute(QStringLiteral("style"));
        if (el.hasAttribute(QStringLiteral("class"))) {
            const QStringList parts = el.attribute(QStringLiteral("class"))
                                          .split(QRegularExpression(QStringLiteral("\\s+")),
                                                 Qt::SkipEmptyParts);
            QStringList kept;
            for (const QString &c : parts)
                if (isAozoraClass(c))
                    kept.append(c);
            if (kept.isEmpty())
                el.removeAttribute(QStringLiteral("class"));
            else
                el.setAttribute(QStringLiteral("class"), kept.join(QLatin1Char(' ')));
        }
    }

    const QString mainText = innerHtml(body);
    const QString title = book.title().isEmpty() ? QStringLiteral("(無題)") : book.title();
    const QString docTitle = title + QStringLiteral(" ― ") + chapter.label;

    QString out;
    QTextStream s(&out);
    s << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    s << "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.1//EN\" "
         "\"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd\">\n";
    s << "<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"ja\">\n<head>\n";
    s << "<meta http-equiv=\"Content-Type\" content=\"application/xhtml+xml; charset=UTF-8\" />\n";
    s << "<title>" << escapeXml(docTitle) << "</title>\n";
    s << "<style type=\"text/css\">\n" << AOZORA_CSS << "\n</style>\n</head>\n<body>\n";
    s << "<div class=\"metadata\">\n";
    s << "<h1 class=\"title\">" << escapeXml(title) << "</h1>\n";
    if (!book.author().isEmpty())
        s << "<h2 class=\"author\">" << escapeXml(book.author()) << "</h2>\n";
    s << "<p class=\"author\">" << escapeXml(chapter.label) << "</p>\n";
    s << "</div>\n<div class=\"main_text\">\n";
    s << mainText;
    s << "\n</div>\n</body>\n</html>";
    return out;
}

} // namespace aozora
