#pragma once

#include <QHash>
#include <QString>

// Per-book, per-target-language cache of paragraph translations. Keyed by the
// whitespace-normalized source text so it survives reloads and matches blocks
// regardless of incidental whitespace. Persisted as a JSON sidecar file next to
// the EPUB: <book>.<lang>.json (e.g. book.epub -> book.ja.json).
class TranslationCache {
public:
    // Collapse whitespace runs to single spaces and trim (the cache key form).
    static QString normalizeKey(const QString &text);

    // Switch to (epubPath, lang) and load that map from disk (resets state).
    // The cache is stored as a sidecar file next to the EPUB:
    //   <epubPath>.spindle.<lang>.json
    void load(const QString &epubPath, const QString &lang);

    bool contains(const QString &sourceText) const;
    QString lookup(const QString &sourceText) const; // "" on miss
    void put(const QString &sourceText, const QString &translation);

    void flush() const; // write to disk if changed

    int size() const { return m_map.size(); }
    QString lang() const { return m_lang; }
    bool isReady() const { return !m_epubPath.isEmpty(); }

private:
    QString filePath() const;

    QString m_epubPath;
    QString m_lang;
    QHash<QString, QString> m_map;
    mutable bool m_dirty = false;
};
