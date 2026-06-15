#pragma once

#include "core/ChapterText.h"
#include "core/KindleImport.h"

#include <QString>
#include <QVector>

// Port of matcher.ts — maps Kindle Notebook entries onto chapter text offsets.
namespace matcher {

struct MatchResult {
    kindle::KindleEntry entry;
    QString chapterPath;
    int block = 0;  // document-order block index (anchor)
    int offset = 0; // chars from block start to match start
    int length = 0; // match length in chars (may cross blocks within the chapter)
    QString matchedText;
    QString confidence; // "exact" | "normalized" | "prefix"
};

struct MatchOutcome {
    QVector<MatchResult> matches;
    QVector<kindle::KindleEntry> failures;
};

MatchOutcome matchEntries(const QVector<kindle::KindleEntry> &entries,
                          const QVector<ChapterText> &chapters);

} // namespace matcher
