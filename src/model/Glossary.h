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

    // Which task the prompt block is for — the wording differs (translation
    // asks for exact target renderings; summary asks to write terms as given).
    enum class Purpose { Translation, Summary };

    // Load the entries for (epubPath, lang). Resets state; safe to call again on
    // language change. No-op if either is empty or the file is missing/invalid.
    void load(const QString &epubPath, const QString &lang);

    // Sidecar path for `epubPath` (book.epub -> book.glossary.json).
    static QString filePathFor(const QString &epubPath);

    // Read every entry of the sidecar regardless of language, reporting the
    // file's language pair when requested. Empty on a missing/invalid file.
    static QVector<Entry> readFile(const QString &epubPath, QString *sourceLang = nullptr,
                                   QString *targetLang = nullptr);

    // Write the sidecar (overwrites). Returns false with `error` set on failure.
    static bool writeFile(const QString &epubPath, const QString &sourceLang,
                          const QString &targetLang, const QVector<Entry> &entries,
                          QString *error = nullptr);

    // Whole-word-ish, case-insensitive containment test — the same rule that
    // decides which entries are sent along with a translation request.
    static bool appearsInText(const QString &source, const QString &text);

    bool isEmpty() const { return m_entries.isEmpty(); }
    int size() const { return m_entries.size(); }
    const QVector<Entry> &entries() const { return m_entries; }

    // One-line instruction block for the entries appearing in `text`, sized to
    // the text ("" when nothing matches). Sent in both the system and user
    // turns — translation-tuned models (TranslateGemma) only read the latter.
    QString promptBlockForText(const QString &text,
                               Purpose purpose = Purpose::Translation) const;

    // The target for a text that IS exactly one glossary term (e.g. a chapter
    // heading that is just a person's name), "" otherwise. Callers use it to
    // skip the model round-trip, which could only mangle the mandated form.
    QString exactTarget(const QString &text) const;

private:
    QVector<Entry> m_entries;
};
