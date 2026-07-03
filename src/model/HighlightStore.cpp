#include "model/HighlightStore.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace highlight_store {

QString filePathFor(const QString &epubPath)
{
    if (epubPath.isEmpty())
        return {};
    // Sidecar next to the EPUB: <book>.highlights.json
    QString base = epubPath;
    if (base.endsWith(QLatin1String(".epub"), Qt::CaseInsensitive))
        base.chop(5);
    return base + QStringLiteral(".highlights.json");
}

QJsonObject toJson(const Highlight &h)
{
    QJsonObject o;
    o[QStringLiteral("id")] = h.id;
    o[QStringLiteral("chapter")] = h.chapter;
    o[QStringLiteral("block")] = h.block;
    o[QStringLiteral("side")] = toString(h.side);
    if (h.side == HighlightSide::Translation && !h.lang.isEmpty())
        o[QStringLiteral("lang")] = h.lang;
    o[QStringLiteral("offset")] = h.offset;
    o[QStringLiteral("length")] = h.length;
    o[QStringLiteral("text")] = h.text;
    o[QStringLiteral("color")] = toString(h.color);
    if (!h.note.isEmpty())
        o[QStringLiteral("note")] = h.note;
    o[QStringLiteral("source")] = toString(h.source);
    if (h.kindle && !h.kindle->isEmpty()) {
        QJsonObject k;
        if (h.kindle->location)
            k[QStringLiteral("location")] = *h.kindle->location;
        if (h.kindle->page)
            k[QStringLiteral("page")] = *h.kindle->page;
        if (!h.kindle->chapterTitle.isEmpty())
            k[QStringLiteral("chapterTitle")] = h.kindle->chapterTitle;
        if (!h.kindle->partTitle.isEmpty())
            k[QStringLiteral("partTitle")] = h.kindle->partTitle;
        o[QStringLiteral("kindle")] = k;
    }
    o[QStringLiteral("createdAt")] = h.createdAt;
    o[QStringLiteral("updatedAt")] = h.updatedAt;
    return o;
}

Highlight fromJson(const QJsonObject &obj)
{
    Highlight h;
    h.id = obj.value(QStringLiteral("id")).toString();
    h.chapter = obj.value(QStringLiteral("chapter")).toString();
    h.block = obj.value(QStringLiteral("block")).toInt();
    h.side = highlightSideFromString(obj.value(QStringLiteral("side")).toString());
    h.lang = obj.value(QStringLiteral("lang")).toString();
    h.offset = obj.value(QStringLiteral("offset")).toInt();
    h.length = obj.value(QStringLiteral("length")).toInt();
    h.text = obj.value(QStringLiteral("text")).toString();
    h.color = highlightColorFromString(obj.value(QStringLiteral("color")).toString());
    h.note = obj.value(QStringLiteral("note")).toString();
    h.source = highlightSourceFromString(obj.value(QStringLiteral("source")).toString());
    if (obj.contains(QStringLiteral("kindle"))) {
        const QJsonObject k = obj.value(QStringLiteral("kindle")).toObject();
        KindleMeta meta;
        if (k.contains(QStringLiteral("location")))
            meta.location = k.value(QStringLiteral("location")).toInt();
        if (k.contains(QStringLiteral("page")) && !k.value(QStringLiteral("page")).isNull())
            meta.page = k.value(QStringLiteral("page")).toInt();
        meta.chapterTitle = k.value(QStringLiteral("chapterTitle")).toString();
        meta.partTitle = k.value(QStringLiteral("partTitle")).toString();
        if (!meta.isEmpty())
            h.kindle = meta;
    }
    h.createdAt = obj.value(QStringLiteral("createdAt")).toString();
    h.updatedAt = obj.value(QStringLiteral("updatedAt")).toString();
    return h;
}

QByteArray serializeFile(const BookHighlightFile &file)
{
    QJsonObject root;
    root[QStringLiteral("version")] = file.version;
    QJsonObject book;
    book[QStringLiteral("id")] = file.book.id;
    book[QStringLiteral("title")] = file.book.title;
    book[QStringLiteral("author")] = file.book.author;
    root[QStringLiteral("book")] = book;
    QJsonArray arr;
    for (const Highlight &h : file.highlights)
        arr.append(toJson(h));
    root[QStringLiteral("highlights")] = arr;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

BookHighlightFile parseFile(const QByteArray &json, bool *ok)
{
    BookHighlightFile file;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        if (ok)
            *ok = false;
        return file;
    }
    const QJsonObject root = doc.object();
    file.version = root.value(QStringLiteral("version")).toInt(1);
    const QJsonObject book = root.value(QStringLiteral("book")).toObject();
    file.book.id = book.value(QStringLiteral("id")).toString();
    file.book.title = book.value(QStringLiteral("title")).toString();
    file.book.author = book.value(QStringLiteral("author")).toString();
    const QJsonArray arr = root.value(QStringLiteral("highlights")).toArray();
    for (const QJsonValue &v : arr) {
        if (v.isObject())
            file.highlights.append(fromJson(v.toObject()));
    }
    if (ok)
        *ok = root.contains(QStringLiteral("highlights"));
    return file;
}

QVector<Highlight> load(const QString &epubPath)
{
    const QString path = filePathFor(epubPath);
    if (path.isEmpty())
        return {};
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    bool ok = false;
    BookHighlightFile file = parseFile(f.readAll(), &ok);
    return file.highlights;
}

bool save(const QString &epubPath, const BookRef &book, const QVector<Highlight> &highlights)
{
    const QString path = filePathFor(epubPath);
    if (path.isEmpty())
        return false;
    BookHighlightFile file;
    file.version = 2;
    file.book = book;
    file.highlights = highlights;
    // Atomic replace: a crash mid-write must not destroy the user's highlights.
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(serializeFile(file));
    return f.commit();
}

void upsert(QVector<Highlight> &list, const Highlight &highlight)
{
    for (Highlight &h : list) {
        if (h.id == highlight.id) {
            h = highlight;
            return;
        }
    }
    list.append(highlight);
}

void remove(QVector<Highlight> &list, const QString &highlightId)
{
    for (int i = 0; i < list.size(); ++i) {
        if (list[i].id == highlightId) {
            list.removeAt(i);
            return;
        }
    }
}

QVector<Highlight> byChapter(const QVector<Highlight> &list, const QString &chapterPath)
{
    QVector<Highlight> out;
    for (const Highlight &h : list) {
        if (h.chapter == chapterPath)
            out.append(h);
    }
    std::sort(out.begin(), out.end(), [](const Highlight &a, const Highlight &b) {
        if (a.block != b.block)
            return a.block < b.block;
        return a.offset < b.offset;
    });
    return out;
}

} // namespace highlight_store
