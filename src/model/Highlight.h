#pragma once

#include <QString>
#include <QVector>
#include <optional>

// Highlight model. A highlight is anchored to a chapter (zip path) and a
// document-order block index within that chapter (the same leaf-block unit used
// for translation). Within the block, `offset`/`length` are character counts
// (UTF-16 units) into the text of the chosen side (original or translation).
// `length` may run past the anchor block into following blocks, but never past
// the chapter or across sides. See resources/reader.js for the shared rules.

enum class HighlightColor { Yellow, Blue, Pink, Orange, Green, Purple };
enum class HighlightSource { Kindle, User };
enum class HighlightSide { Original, Translation };

QString toString(HighlightColor color);
HighlightColor highlightColorFromString(const QString &value); // defaults to Yellow
QString toString(HighlightSource source);
HighlightSource highlightSourceFromString(const QString &value); // defaults to User
QString toString(HighlightSide side);
HighlightSide highlightSideFromString(const QString &value); // defaults to Original

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
    QString chapter;                              // zip-internal chapter path (never crossed)
    int block = 0;                                // document-order block index (original anchor)
    HighlightSide side = HighlightSide::Original; // which text offset/length count into
    QString lang;                                 // target language; only when side==Translation
    int offset = 0;                               // chars from block start to highlight start
    int length = 0;                               // highlight length in chars (may cross blocks)
    QString text;                                 // resolved highlighted string (snapshot)
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
    int version = 2;
    BookRef book;
    QVector<Highlight> highlights;
};

// Stable per-book id derived from title|author (matches highlights.ts bookId).
QString bookId(const QString &title, const QString &author);

QString generateHighlightId();
