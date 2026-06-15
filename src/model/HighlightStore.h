#pragma once

#include "model/Highlight.h"

#include <QJsonObject>
#include <QString>
#include <QVector>

// Per-book highlight persistence + JSON (de)serialization.
// Highlights are stored as a sidecar file next to the EPUB:
//   <epubPath>.spindle.highlights.json
// using the BookHighlightFile shape (version 2) shared with import/export.
namespace highlight_store {

// Sidecar path next to the EPUB (empty if epubPath is empty).
QString filePathFor(const QString &epubPath);

QVector<Highlight> load(const QString &epubPath);
void save(const QString &epubPath, const BookRef &book, const QVector<Highlight> &highlights);

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
