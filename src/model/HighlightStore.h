#pragma once

#include "model/Highlight.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

// Per-book highlight persistence + JSON (de)serialization.
// Files live under QStandardPaths AppDataLocation/highlights/<bookId>.json and
// use the BookHighlightFile shape (version 1) shared with import/export.
namespace highlight_store {

QString storageDir();
QString filePathFor(const QString &id);

QVector<Highlight> load(const QString &id);
void save(const QString &id, const BookRef &book, const QVector<Highlight> &highlights);

// In-list mutation helpers mirroring highlights.ts.
void upsert(QVector<Highlight> &list, const Highlight &highlight);
void remove(QVector<Highlight> &list, const QString &highlightId);
QVector<Highlight> byChapter(const QVector<Highlight> &list, const QString &chapterPath);

// JSON conversion (also used by JSON import/export).
QJsonObject toJson(const Highlight &h);
Highlight fromJson(const QJsonObject &obj);
QByteArray serializeFile(const BookHighlightFile &file);
BookHighlightFile parseFile(const QByteArray &json, bool *ok = nullptr);

} // namespace highlight_store
