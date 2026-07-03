#pragma once

#include "core/BlockIndex.h"

#include <QString>
#include <QVector>

class EpubBook;

// Plain-text projection of a chapter plus index maps back to the original body,
// mirroring matcher.ts ChapterText. Offsets are UTF-16 code-unit indices.
struct ChapterText {
    QString path;
    QString label;
    QString body;                       // textContent of <body> (placeholders removed, nbsp->space)
    QString normalizedBody;             // runs of whitespace collapsed to single spaces
    QString normalizedLower;            // normalizedBody lowercased (search haystack)
    QVector<int> normalizedToOriginal;  // normalizedBody index -> body index
    QString compactBody;                // all whitespace removed
    QVector<int> compactToOriginal;     // compactBody index -> body index
    QVector<block_index::BlockInfo> blocks; // leaf blocks (== data-spindle-block order)
};

struct ChapterRef {
    QString path;
    QString label;
};

// Extract the body text content of a single chapter XHTML document, matching
// the browser's textContent (scripts removed, 〚..〛 placeholders stripped,
// non-breaking spaces turned into regular spaces).
QString extractChapterBody(const QString &xhtml);

ChapterText buildChapterText(const ChapterRef &ref, const QString &body);

QVector<ChapterText> buildChapterTexts(const EpubBook &book,
                                       const QVector<ChapterRef> &chapters);

// Self-contained chapter input (path/label + raw XHTML) so the parse-heavy
// build can run on a worker thread without touching EpubBook / the zip.
struct ChapterSource {
    QString path;
    QString label;
    QString xhtml;
};

QVector<ChapterText> buildChapterTextsFromSources(const QVector<ChapterSource> &sources);
