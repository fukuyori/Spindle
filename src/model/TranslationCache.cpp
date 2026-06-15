#include "model/TranslationCache.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

QString TranslationCache::normalizeKey(const QString &text)
{
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    return QString(text).replace(ws, QStringLiteral(" ")).trimmed();
}

QString TranslationCache::filePath() const
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                        + QStringLiteral("/translations");
    return dir + QStringLiteral("/") + m_bookId + QStringLiteral(".") + m_lang
           + QStringLiteral(".json");
}

void TranslationCache::load(const QString &bookId, const QString &lang)
{
    flush(); // persist any pending changes for the previous (book, lang)
    m_bookId = bookId;
    m_lang = lang;
    m_map.clear();
    m_dirty = false;
    if (bookId.isEmpty() || lang.isEmpty())
        return;

    QFile f(filePath());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject entries = obj.value(QStringLiteral("entries")).toObject();
    for (auto it = entries.begin(); it != entries.end(); ++it)
        m_map.insert(it.key(), it.value().toString());
}

bool TranslationCache::contains(const QString &sourceText) const
{
    return m_map.contains(normalizeKey(sourceText));
}

QString TranslationCache::lookup(const QString &sourceText) const
{
    return m_map.value(normalizeKey(sourceText));
}

void TranslationCache::put(const QString &sourceText, const QString &translation)
{
    const QString key = normalizeKey(sourceText);
    if (key.isEmpty() || translation.isEmpty())
        return;
    if (m_map.value(key) == translation)
        return;
    m_map.insert(key, translation);
    m_dirty = true;
}

void TranslationCache::flush() const
{
    if (!m_dirty || m_bookId.isEmpty() || m_lang.isEmpty())
        return;
    QDir().mkpath(QFileInfo(filePath()).absolutePath());

    QJsonObject entries;
    for (auto it = m_map.begin(); it != m_map.end(); ++it)
        entries.insert(it.key(), it.value());
    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("book_id")] = m_bookId;
    root[QStringLiteral("lang")] = m_lang;
    root[QStringLiteral("entries")] = entries;

    QFile f(filePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        m_dirty = false;
    }
}
