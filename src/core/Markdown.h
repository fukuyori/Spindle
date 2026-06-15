#pragma once

#include "model/Highlight.h"

#include <QString>
#include <QVector>

// Port of markdown.ts — the Spindle highlight Markdown format (with embedded
// <!--spindle ...--> metadata blocks) for export/import round-tripping.
namespace markdown {

struct ChapterLabel {
    QString path;
    QString label;
};

QString exportMarkdown(const BookRef &book, const QVector<Highlight> &highlights,
                       const QVector<ChapterLabel> &chapterLabels, const QString &exportedAtIso);

BookHighlightFile parseMarkdown(const QString &text);

} // namespace markdown
