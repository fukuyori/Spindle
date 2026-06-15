#pragma once

#include <QString>

class EpubBook;
struct Chapter;

// Port of exportCurrentChapterHtml/convertBodyToAozora from main.ts.
// Produces a standalone Aozora-Bunko-style XHTML document for one chapter
// (images inlined as data URIs, publisher CSS reduced to Aozora vocabulary).
namespace aozora {

QString exportChapter(const EpubBook &book, const Chapter &chapter);

} // namespace aozora
