#pragma once

#include "core/ChapterText.h"

#include <QString>
#include <QVector>

// Port of search.ts.
struct SearchHit {
    QString chapterPath;
    QString chapterLabel;
    int start = 0;
    int end = 0;
    QString snippetBefore;
    QString snippetMatch;
    QString snippetAfter;
};

QVector<SearchHit> searchChapters(const QVector<ChapterText> &chapters,
                                  const QString &rawQuery, int limit = 500);
