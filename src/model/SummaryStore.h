#pragma once

#include <QString>
#include <QVector>

struct ChapterSummary {
    QString chapter;
    QString chapterTitle;
    QString targetLang;
    QString detail;
    QString model;
    QString summaryMarkdown;
    QString createdAt;
    QString updatedAt;
};

namespace summary_store {

QString filePathFor(const QString &epubPath);
QVector<ChapterSummary> load(const QString &epubPath);
void save(const QString &epubPath, const QVector<ChapterSummary> &summaries);
void upsert(QVector<ChapterSummary> &summaries, const ChapterSummary &summary);
ChapterSummary find(const QVector<ChapterSummary> &summaries, const QString &chapter,
                    const QString &targetLang, const QString &detail);

} // namespace summary_store
