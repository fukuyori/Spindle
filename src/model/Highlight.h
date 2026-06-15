#pragma once

#include <QString>
#include <QVector>
#include <optional>

// Mirrors the Highlight model from the original Spindle (highlights.ts).
// Positions are character offsets (UTF-16 units) into a chapter's extracted
// body text, identified by the chapter's zip-internal path.

enum class HighlightColor { Yellow, Blue, Pink, Orange, Green, Purple };
enum class HighlightSource { Kindle, User };

QString toString(HighlightColor color);
HighlightColor highlightColorFromString(const QString &value); // defaults to Yellow
QString toString(HighlightSource source);
HighlightSource highlightSourceFromString(const QString &value); // defaults to User

struct KindleMeta {
    std::optional<int> location;
    std::optional<int> page;
    QString chapterTitle;
    QString partTitle;
    bool isEmpty() const
    {
        return !location && !page && chapterTitle.isEmpty() && partTitle.isEmpty();
    }
};

struct Highlight {
    QString id;
    QString chapter; // zip-internal chapter path
    QString text;
    int start = 0;
    int end = 0;
    HighlightColor color = HighlightColor::Yellow;
    QString note;
    HighlightSource source = HighlightSource::User;
    std::optional<KindleMeta> kindle;
    QString createdAt; // ISO-8601
    QString updatedAt; // ISO-8601
};

struct BookRef {
    QString id;
    QString title;
    QString author;
};

struct BookHighlightFile {
    int version = 1;
    BookRef book;
    QVector<Highlight> highlights;
};

// Stable per-book id derived from title|author (matches highlights.ts bookId).
QString bookId(const QString &title, const QString &author);

QString generateHighlightId();
