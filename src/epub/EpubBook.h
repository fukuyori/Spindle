#pragma once

#include "epub/ZipArchive.h"

#include <QHash>
#include <QString>
#include <QVector>
#include <memory>

class QDomElement;

struct ManifestItem {
    QString href;
    QString mediaType;
    QString path; // resolved zip-internal path
    QString properties;
};

enum class PageSpread {
    Auto,
    Left,
    Right,
    Center,
};

struct Chapter {
    QString id;
    QString href;
    QString path; // resolved zip-internal path
    QString label;
    bool fixedLayout = false;
    PageSpread pageSpread = PageSpread::Auto;
};

struct TocItem {
    QString label;
    QString href; // resolved zip-internal path, may carry a #fragment
    QVector<TocItem> subitems;
};

// Loads and models an EPUB 2/3 file. Mirrors the parsing logic of the original
// TypeScript Spindle so chapter paths and offsets stay compatible.
class EpubBook {
public:
    bool open(const QString &filePath);

    bool isValid() const { return m_valid; }
    QString errorString() const { return m_error; }

    QString title() const { return m_title; }
    QString author() const { return m_author; }
    QString language() const { return m_language; } // dc:language ("" if absent)
    QString opfDir() const { return m_opfDir; }
    bool verticalRtl() const { return m_verticalRtl; }
    bool fixedLayout() const { return m_fixedLayout; }

    const QVector<Chapter> &chapters() const { return m_chapters; }
    const QVector<TocItem> &toc() const { return m_toc; }

    int chapterIndexForPath(const QString &path) const;

    // Raw entry access (chapters, images, css). path is zip-internal.
    QByteArray readBytes(const QString &path) const { return m_zip.readBytes(path); }
    QString readText(const QString &path) const { return m_zip.readText(path); }
    bool contains(const QString &path) const { return m_zip.contains(path); }

private:
    QVector<TocItem> parseNavToc(const QString &navPath);
    QVector<TocItem> parseNavList(const QDomElement &list, const QString &basePath);
    QVector<TocItem> parseNcxToc(const QString &ncxPath);
    QVector<TocItem> parseNcxPoints(const QVector<QDomElement> &points, const QString &basePath);
    static QString findTocLabel(const QVector<TocItem> &items, const QString &path);

    ZipArchive m_zip;
    bool m_valid = false;
    QString m_error;
    QString m_title;
    QString m_author;
    QString m_language;
    QString m_opfDir;
    bool m_verticalRtl = false;
    bool m_fixedLayout = false;
    QVector<Chapter> m_chapters;
    QVector<TocItem> m_toc;
    QHash<QString, int> m_chapterIndexByPath; // zip path -> spine index
};
