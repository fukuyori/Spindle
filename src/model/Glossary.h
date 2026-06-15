#pragma once

#include <QString>
#include <QVector>

// Per-book translation glossary, stored as a sidecar next to the EPUB:
//   <book>.glossary.json   (e.g. book.epub -> book.glossary.json)
// Format (one source->target language pair per file):
//   {
//     "source_lang": "la",
//     "target_lang": "ja",
//     "entries": [ { "src": "Caesar", "dst": "カエサル", "note": "name" } ]
//   }
// The glossary is applied only when its target_lang matches the language being
// translated into. Entries become an instruction block appended to the
// translation system prompt (soft enforcement by the model).
class Glossary {
public:
    struct Entry {
        QString source;
        QString target;
        QString note;
    };

    // Load the entries for (epubPath, lang). Resets state; safe to call again on
    // language change. No-op if either is empty or the file is missing/invalid.
    void load(const QString &epubPath, const QString &lang);

    bool isEmpty() const { return m_entries.isEmpty(); }
    int size() const { return m_entries.size(); }
    const QVector<Entry> &entries() const { return m_entries; }

    // Instruction block to append to the system prompt ("" when empty).
    QString promptBlock() const;

private:
    QVector<Entry> m_entries;
};
