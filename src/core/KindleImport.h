#pragma once

#include "model/Highlight.h"

#include <QString>
#include <QVector>
#include <optional>

// Port of kindle-import.ts — parses an exported Kindle "Notebook" HTML file.
namespace kindle {

enum class EntryKind { Highlight, Note, Bookmark };

struct KindleEntry {
    QString partTitle;
    QString chapterTitle;
    std::optional<int> chapterNumber;
    std::optional<int> page;
    std::optional<int> location;
    HighlightColor color = HighlightColor::Yellow;
    QString text;
    EntryKind kind = EntryKind::Highlight;
    QString note;
};

struct KindleNotebook {
    QString title;
    QString author;
    QVector<KindleEntry> entries;
};

KindleNotebook parseKindleNotebook(const QString &html);

} // namespace kindle
