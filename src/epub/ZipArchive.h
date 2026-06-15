#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// Thin RAII wrapper over miniz for reading EPUB (zip) contents from a file.
// All lookups are by zip-internal path using '/' separators.
class ZipArchive {
public:
    ZipArchive() = default;
    ~ZipArchive();

    ZipArchive(const ZipArchive &) = delete;
    ZipArchive &operator=(const ZipArchive &) = delete;

    // Opens the archive at filePath. Returns false on failure.
    bool open(const QString &filePath);
    void close();
    bool isOpen() const { return m_open; }

    bool contains(const QString &path) const;

    // Raw bytes of an entry; empty QByteArray if missing.
    QByteArray readBytes(const QString &path) const;

    // UTF-8 decoded text of an entry.
    QString readText(const QString &path) const;

    // All entry paths (files only).
    QStringList entries() const;

private:
    struct Impl;
    Impl *m_d = nullptr;
    bool m_open = false;
};
