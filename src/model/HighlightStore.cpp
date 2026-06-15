#include "model/HighlightStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>

namespace highlight_store {

QString storageDir()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return base + QStringLiteral("/highlights");
}

QString filePathFor(const QString &id)
{
    return storageDir() + QStringLiteral("/") + id + QStringLiteral(".json");
}

QJsonObject toJson(const Highlight &h)
{
    QJsonObject o;
    o[QStringLiteral("id")] = h.id;
    o[QStringLiteral("chapter")] = h.chapter;
    o[QStringLiteral("text")] = h.text;
    o[QStringLiteral("start")] = h.start;
    o[QStringLiteral("end")] = h.end;
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
    h.text = obj.value(QStringLiteral("text")).toString();
    h.start = obj.value(QStringLiteral("start")).toInt();
    h.end = obj.value(QStringLiteral("end")).toInt();
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
        *ok = (file.version == 1 && root.contains(QStringLiteral("highlights")));
    return file;
}

QVector<Highlight> load(const QString &id)
{
    QFile f(filePathFor(id));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    bool ok = false;
    BookHighlightFile file = parseFile(f.readAll(), &ok);
    return file.highlights;
}

void save(const QString &id, const BookRef &book, const QVector<Highlight> &highlights)
{
    QDir().mkpath(storageDir());
    BookHighlightFile file;
    file.version = 1;
    file.book = book;
    file.highlights = highlights;
    QFile f(filePathFor(id));
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        f.write(serializeFile(file));
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
    std::sort(out.begin(), out.end(),
              [](const Highlight &a, const Highlight &b) { return a.start < b.start; });
    return out;
}

} // namespace highlight_store
