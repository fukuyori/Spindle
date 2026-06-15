#pragma once

#include <QString>
#include <QStringList>

class EpubBook;
class TranslationCache;

// Builds a derived EPUB whose leaf paragraphs carry their cached translations,
// either alongside the original (bilingual) or replacing it (translation-only).
namespace translated_epub {

enum class Mode { Bilingual, Translation };

// Normalized source texts of all leaf blocks that are NOT yet in the cache
// (deduplicated). The caller can translate these to complete the book.
QStringList collectMissing(const EpubBook &book, const TranslationCache &cache);

// Write the translated EPUB. `srcPath` is the original .epub; all entries are
// copied through, with chapter XHTML rewritten using the cache. Returns false
// and sets *err on failure.
bool write(const EpubBook &book, const QString &srcPath, const QString &outPath, Mode mode,
           const TranslationCache &cache, QString *err);

} // namespace translated_epub
