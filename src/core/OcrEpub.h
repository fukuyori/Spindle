#pragma once

#include <QString>
#include <QVector>

// Builds a reflowable text EPUB from OCR results — one chapter per source
// page, in spine order — so the extracted text can be read (and searched,
// translated, summarized, spoken) like any text book.
namespace ocr_epub {

struct Page {
    QString title; // chapter heading, e.g. "Page 12 — 目次ラベル"
    QString text;  // OCR text (or the failure note for failed pages)
    bool failed = false;
};

// Write a self-contained EPUB 3 to `outPath`. `language` is the dc:language
// code ("" falls back to "und"); `verticalRtl` lays the text out vertically
// (writing-mode: vertical-rl) with a right-to-left spine, matching Japanese
// books whose source was vertical. Returns false and sets *err on failure.
bool write(const QString &outPath, const QString &title, const QString &language,
           bool verticalRtl, const QVector<Page> &pages, QString *err);

} // namespace ocr_epub
