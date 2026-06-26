#include "model/SummaryStore.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QJsonObject toJson(const ChapterSummary &summary)
{
    QJsonObject obj;
    obj[QStringLiteral("scope")] = QStringLiteral("chapter");
    obj[QStringLiteral("chapter")] = summary.chapter;
    obj[QStringLiteral("chapterTitle")] = summary.chapterTitle;
    obj[QStringLiteral("targetLang")] = summary.targetLang;
    obj[QStringLiteral("detail")] = summary.detail;
    obj[QStringLiteral("model")] = summary.model;
    obj[QStringLiteral("summaryMarkdown")] = summary.summaryMarkdown;
    obj[QStringLiteral("createdAt")] = summary.createdAt;
    obj[QStringLiteral("updatedAt")] = summary.updatedAt;
    return obj;
}

ChapterSummary fromJson(const QJsonObject &obj)
{
    ChapterSummary summary;
    if (obj.value(QStringLiteral("scope")).toString() != QLatin1String("chapter"))
        return summary;
    summary.chapter = obj.value(QStringLiteral("chapter")).toString();
    summary.chapterTitle = obj.value(QStringLiteral("chapterTitle")).toString();
    summary.targetLang = obj.value(QStringLiteral("targetLang")).toString();
    summary.detail = obj.value(QStringLiteral("detail")).toString();
    summary.model = obj.value(QStringLiteral("model")).toString();
    summary.summaryMarkdown = obj.value(QStringLiteral("summaryMarkdown")).toString();
    summary.createdAt = obj.value(QStringLiteral("createdAt")).toString();
    summary.updatedAt = obj.value(QStringLiteral("updatedAt")).toString();
    return summary;
}

bool sameKey(const ChapterSummary &a, const ChapterSummary &b)
{
    return a.chapter == b.chapter && a.targetLang == b.targetLang && a.detail == b.detail;
}

} // namespace

namespace summary_store {

QString filePathFor(const QString &epubPath)
{
    if (epubPath.isEmpty())
        return {};
    QString base = epubPath;
    if (base.endsWith(QLatin1String(".epub"), Qt::CaseInsensitive))
        base.chop(5);
    return base + QStringLiteral(".summaries.json");
}

QVector<ChapterSummary> load(const QString &epubPath)
{
    const QString path = filePathFor(epubPath);
    if (path.isEmpty())
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    const QJsonArray arr = root.value(QStringLiteral("summaries")).toArray();
    QVector<ChapterSummary> summaries;
    summaries.reserve(arr.size());
    for (const QJsonValue &value : arr) {
        if (!value.isObject())
            continue;
        ChapterSummary summary = fromJson(value.toObject());
        if (!summary.chapter.isEmpty() && !summary.summaryMarkdown.isEmpty())
            summaries.append(summary);
    }
    return summaries;
}

void save(const QString &epubPath, const QVector<ChapterSummary> &summaries)
{
    const QString path = filePathFor(epubPath);
    if (path.isEmpty())
        return;

    QJsonArray arr;
    for (const ChapterSummary &summary : summaries)
        arr.append(toJson(summary));

    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("summaries")] = arr;

    QFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

void upsert(QVector<ChapterSummary> &summaries, const ChapterSummary &summary)
{
    for (ChapterSummary &existing : summaries) {
        if (sameKey(existing, summary)) {
            const QString createdAt = existing.createdAt;
            existing = summary;
            existing.createdAt = createdAt.isEmpty() ? summary.createdAt : createdAt;
            return;
        }
    }
    summaries.append(summary);
}

ChapterSummary find(const QVector<ChapterSummary> &summaries, const QString &chapter,
                    const QString &targetLang, const QString &detail)
{
    for (const ChapterSummary &summary : summaries) {
        if (summary.chapter == chapter && summary.targetLang == targetLang
            && summary.detail == detail) {
            return summary;
        }
    }
    return {};
}

} // namespace summary_store
