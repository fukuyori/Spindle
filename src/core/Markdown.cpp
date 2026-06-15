#include "core/Markdown.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>

namespace markdown {

namespace {

QString colorBadge(HighlightColor c)
{
    switch (c) {
    case HighlightColor::Yellow: return QStringLiteral("🟡");
    case HighlightColor::Blue:   return QStringLiteral("🔵");
    case HighlightColor::Pink:   return QStringLiteral("🌸");
    case HighlightColor::Orange: return QStringLiteral("🟠");
    case HighlightColor::Green:  return QStringLiteral("🟢");
    case HighlightColor::Purple: return QStringLiteral("🟣");
    }
    return {};
}

QString capitalize(const QString &v)
{
    if (v.isEmpty())
        return v;
    return v.left(1).toUpper() + v.mid(1);
}

QString escapeYaml(const QString &value)
{
    if (value.isEmpty())
        return QStringLiteral("\"\"");
    static const QRegularExpression special(QStringLiteral("[\":#\\n]"));
    if (special.match(value).hasMatch()) {
        QString v = value;
        v.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        v.replace(QLatin1Char('"'), QStringLiteral("\\\""));
        return QLatin1Char('"') + v + QLatin1Char('"');
    }
    return value;
}

QHash<QString, QString> parseSimpleYaml(const QString &block)
{
    QHash<QString, QString> result;
    for (const QString &rawLine : block.split(QLatin1Char('\n'))) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int colon = line.indexOf(QLatin1Char(':'));
        if (colon < 0)
            continue;
        const QString key = line.left(colon).trimmed();
        QString value = line.mid(colon + 1).trimmed();
        if (value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')) && value.size() >= 2) {
            value = value.mid(1, value.size() - 2);
            value.replace(QStringLiteral("\\\""), QStringLiteral("\""));
            value.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
        }
        result.insert(key, value);
    }
    return result;
}

QHash<QString, QString> extractFrontmatter(const QString &text)
{
    static const QRegularExpression re(QStringLiteral("^---\\n([\\s\\S]*?)\\n---\\n"));
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch())
        return {};
    return parseSimpleYaml(m.captured(1));
}

void extractQuoteAndNote(const QString &text, QString &quote, QString &note)
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    int i = 0;
    while (i < lines.size() && !lines[i].startsWith(QLatin1Char('>')))
        ++i;

    QStringList quoteLines;
    while (i < lines.size() && lines[i].startsWith(QLatin1Char('>'))) {
        QString l = lines[i];
        static const QRegularExpression marker(QStringLiteral("^>\\s?"));
        l.remove(marker);
        quoteLines.append(l);
        ++i;
    }
    quote = quoteLines.join(QLatin1Char('\n')).trimmed();

    while (i < lines.size() && lines[i].trimmed().isEmpty())
        ++i;

    note.clear();
    if (i < lines.size() && lines[i].startsWith(QStringLiteral("**Note**:"))) {
        QStringList noteLines;
        QString first = lines[i];
        static const QRegularExpression notePrefix(QStringLiteral("^\\*\\*Note\\*\\*:\\s?"));
        first.remove(notePrefix);
        noteLines.append(first);
        ++i;
        while (i < lines.size()) {
            const QString &l = lines[i];
            if (l.startsWith(QStringLiteral("<!--spindle")) || l.startsWith(QStringLiteral("---"))
                || l.startsWith(QStringLiteral("##")) || l.startsWith(QStringLiteral("# ")))
                break;
            noteLines.append(l);
            ++i;
        }
        note = noteLines.join(QLatin1Char('\n'));
        note.replace(QStringLiteral("  \n"), QStringLiteral("\n"));
        note = note.trimmed();
    }
}

} // namespace

QString exportMarkdown(const BookRef &book, const QVector<Highlight> &highlights,
                       const QVector<ChapterLabel> &chapterLabels, const QString &exportedAtIso)
{
    QStringList lines;
    lines << QStringLiteral("---");
    lines << QStringLiteral("spindle: 1");
    lines << QStringLiteral("book_id: ") + book.id;
    lines << QStringLiteral("book_title: ") + escapeYaml(book.title);
    lines << QStringLiteral("book_author: ") + escapeYaml(book.author);
    lines << QStringLiteral("exported_at: ") + exportedAtIso;
    lines << QStringLiteral("count: ") + QString::number(highlights.size());
    lines << QStringLiteral("---");
    lines << QString();
    lines << QStringLiteral("# ") + (book.title.isEmpty() ? QStringLiteral("(無題)") : book.title);
    if (!book.author.isEmpty()) {
        lines << QString();
        lines << QLatin1Char('*') + book.author + QLatin1Char('*');
    }
    lines << QString();
    lines << QStringLiteral("合計 %1 件のハイライト").arg(highlights.size());
    lines << QString();

    QHash<QString, QString> labelByPath;
    QHash<QString, int> orderByPath;
    for (int i = 0; i < chapterLabels.size(); ++i) {
        labelByPath.insert(chapterLabels[i].path, chapterLabels[i].label);
        orderByPath.insert(chapterLabels[i].path, i);
    }

    QVector<Highlight> sorted = highlights;
    std::sort(sorted.begin(), sorted.end(), [&](const Highlight &a, const Highlight &b) {
        if (a.chapter != b.chapter) {
            const int ai = orderByPath.value(a.chapter, 9999);
            const int bi = orderByPath.value(b.chapter, 9999);
            return ai < bi;
        }
        return a.start < b.start;
    });

    QString currentChapter;
    bool first = true;
    for (const Highlight &h : sorted) {
        if (first || h.chapter != currentChapter) {
            currentChapter = h.chapter;
            first = false;
            const QString label = labelByPath.value(h.chapter, h.chapter);
            lines << QStringLiteral("---");
            lines << QString();
            lines << QStringLiteral("## ") + label;
            lines << QString();
        }

        lines << QStringLiteral("<!--spindle");
        lines << QStringLiteral("id: ") + h.id;
        lines << QStringLiteral("chapter: ") + h.chapter;
        lines << QStringLiteral("start: ") + QString::number(h.start);
        lines << QStringLiteral("end: ") + QString::number(h.end);
        lines << QStringLiteral("color: ") + toString(h.color);
        lines << QStringLiteral("source: ") + toString(h.source);
        if (h.kindle) {
            if (h.kindle->location)
                lines << QStringLiteral("kindle_location: ") + QString::number(*h.kindle->location);
            if (h.kindle->page)
                lines << QStringLiteral("kindle_page: ") + QString::number(*h.kindle->page);
            if (!h.kindle->chapterTitle.isEmpty())
                lines << QStringLiteral("kindle_chapter: ") + escapeYaml(h.kindle->chapterTitle);
            if (!h.kindle->partTitle.isEmpty())
                lines << QStringLiteral("kindle_part: ") + escapeYaml(h.kindle->partTitle);
        }
        lines << QStringLiteral("created_at: ") + h.createdAt;
        lines << QStringLiteral("updated_at: ") + h.updatedAt;
        lines << QStringLiteral("-->");
        lines << QString();
        lines << QStringLiteral("%1 **%2**").arg(colorBadge(h.color), capitalize(toString(h.color)));
        lines << QString();
        const QStringList quoteLines = h.text.split(QRegularExpression(QStringLiteral("\\r?\\n")));
        QStringList blockquoted;
        for (const QString &l : quoteLines)
            blockquoted << QStringLiteral("> ") + l;
        lines << blockquoted.join(QLatin1Char('\n'));
        lines << QString();
        if (!h.note.isEmpty()) {
            QString note = h.note;
            note.replace(QRegularExpression(QStringLiteral("\\r?\\n")), QStringLiteral("  \n"));
            lines << QStringLiteral("**Note**: ") + note;
            lines << QString();
        }
    }

    lines << QStringLiteral("---");
    lines << QString();
    return lines.join(QLatin1Char('\n'));
}

BookHighlightFile parseMarkdown(const QString &text)
{
    BookHighlightFile file;
    file.version = 1;
    QString normalized = text;
    normalized.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));

    const QHash<QString, QString> fm = extractFrontmatter(normalized);
    file.book.id = fm.value(QStringLiteral("book_id"));
    file.book.title = fm.value(QStringLiteral("book_title"));
    file.book.author = fm.value(QStringLiteral("book_author"));

    static const QRegularExpression commentRe(QStringLiteral("<!--spindle\\s+([\\s\\S]*?)-->"));
    QRegularExpressionMatchIterator it = commentRe.globalMatch(normalized);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QHash<QString, QString> meta = parseSimpleYaml(m.captured(1));
        const int afterIdx = m.capturedEnd(0);
        const QString rest = normalized.mid(afterIdx);

        QString quote, note;
        extractQuoteAndNote(rest, quote, note);

        if (meta.value(QStringLiteral("id")).isEmpty() || meta.value(QStringLiteral("chapter")).isEmpty())
            continue;

        Highlight h;
        h.id = meta.value(QStringLiteral("id"));
        h.chapter = meta.value(QStringLiteral("chapter"));
        h.text = quote;
        h.start = meta.value(QStringLiteral("start"), QStringLiteral("0")).toInt();
        h.end = meta.value(QStringLiteral("end"), QStringLiteral("0")).toInt();
        h.color = highlightColorFromString(meta.value(QStringLiteral("color")));
        h.source = highlightSourceFromString(meta.value(QStringLiteral("source")));
        if (!note.isEmpty())
            h.note = note;
        h.createdAt = meta.value(QStringLiteral("created_at"));
        h.updatedAt = meta.value(QStringLiteral("updated_at"));

        if (meta.contains(QStringLiteral("kindle_location")) || meta.contains(QStringLiteral("kindle_page"))
            || meta.contains(QStringLiteral("kindle_chapter")) || meta.contains(QStringLiteral("kindle_part"))) {
            KindleMeta k;
            if (meta.contains(QStringLiteral("kindle_location")))
                k.location = meta.value(QStringLiteral("kindle_location")).toInt();
            if (meta.contains(QStringLiteral("kindle_page")))
                k.page = meta.value(QStringLiteral("kindle_page")).toInt();
            k.chapterTitle = meta.value(QStringLiteral("kindle_chapter"));
            k.partTitle = meta.value(QStringLiteral("kindle_part"));
            if (!k.isEmpty())
                h.kindle = k;
        }

        file.highlights.append(h);
    }

    return file;
}

} // namespace markdown
