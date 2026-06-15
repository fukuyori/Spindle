#include "model/Highlight.h"

#include <QUuid>
#include <cstdint>

QString toString(HighlightColor color)
{
    switch (color) {
    case HighlightColor::Yellow: return QStringLiteral("yellow");
    case HighlightColor::Blue:   return QStringLiteral("blue");
    case HighlightColor::Pink:   return QStringLiteral("pink");
    case HighlightColor::Orange: return QStringLiteral("orange");
    case HighlightColor::Green:  return QStringLiteral("green");
    case HighlightColor::Purple: return QStringLiteral("purple");
    }
    return QStringLiteral("yellow");
}

HighlightColor highlightColorFromString(const QString &value)
{
    if (value == QLatin1String("blue"))   return HighlightColor::Blue;
    if (value == QLatin1String("pink"))   return HighlightColor::Pink;
    if (value == QLatin1String("orange")) return HighlightColor::Orange;
    if (value == QLatin1String("green"))  return HighlightColor::Green;
    if (value == QLatin1String("purple")) return HighlightColor::Purple;
    return HighlightColor::Yellow;
}

QString toString(HighlightSource source)
{
    return source == HighlightSource::Kindle ? QStringLiteral("kindle")
                                             : QStringLiteral("user");
}

HighlightSource highlightSourceFromString(const QString &value)
{
    return value == QLatin1String("kindle") ? HighlightSource::Kindle
                                            : HighlightSource::User;
}

QString bookId(const QString &title, const QString &author)
{
    const QString input = title.trimmed() + QLatin1Char('|') + author.trimmed();
    std::int32_t hash = 0;
    for (const QChar &ch : input) {
        const std::int64_t next = static_cast<std::int64_t>(hash) * 31 + ch.unicode();
        hash = static_cast<std::int32_t>(static_cast<std::uint32_t>(next & 0xffffffffu));
    }
    const std::uint32_t unsignedHash = static_cast<std::uint32_t>(hash);
    return QStringLiteral("b") + QString::number(static_cast<qulonglong>(unsignedHash), 36);
}

QString generateHighlightId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
