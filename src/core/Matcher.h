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
    int start = 0;
    int end = 0;
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
