#include "core/Search.h"

#include <QRegularExpression>

namespace {
constexpr int kSnippetContext = 30;

QString collapseWhitespace(const QString &input)
{
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    QString out = input;
    return out.replace(ws, QStringLiteral(" "));
}
} // namespace

QVector<SearchHit> searchChapters(const QVector<ChapterText> &chapters,
                                  const QString &rawQuery, int limit)
{
    QVector<SearchHit> hits;
    const QString query = rawQuery.trimmed();
    if (query.isEmpty())
        return hits;

    static const QRegularExpression ws(QStringLiteral("\\s+"));
    QString queryNormalized = query;
    queryNormalized.replace(ws, QStringLiteral(" "));
    queryNormalized = queryNormalized.toLower();

    for (const ChapterText &chapter : chapters) {
        const QString haystack = chapter.normalizedBody.toLower();
        int from = 0;
        while (true) {
            const int index = haystack.indexOf(queryNormalized, from);
            if (index < 0)
                break;

            const int startOrig = chapter.normalizedToOriginal.value(index);
            const int endIdx = qMin(index + queryNormalized.size() - 1,
                                    chapter.normalizedToOriginal.size() - 1);
            const int endOrig = chapter.normalizedToOriginal.value(endIdx) + 1;

            const int snippetStart = qMax(0, startOrig - kSnippetContext);
            const int snippetEnd = qMin(chapter.body.size(), endOrig + kSnippetContext);

            SearchHit hit;
            hit.chapterPath = chapter.path;
            hit.chapterLabel = chapter.label;
            hit.start = startOrig;
            hit.end = endOrig;
            hit.snippetBefore = collapseWhitespace(chapter.body.mid(snippetStart, startOrig - snippetStart));
            hit.snippetMatch = collapseWhitespace(chapter.body.mid(startOrig, endOrig - startOrig));
            hit.snippetAfter = collapseWhitespace(chapter.body.mid(endOrig, snippetEnd - endOrig));
            hits.append(hit);

            if (hits.size() >= limit)
                return hits;
            from = index + qMax(1, queryNormalized.size());
        }
    }

    return hits;
}
