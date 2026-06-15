#include "epub/PathUtil.h"

#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

namespace path_util {

namespace {
QString safeDecode(const QString &value)
{
    const QByteArray decoded = QByteArray::fromPercentEncoding(value.toUtf8());
    const QString out = QString::fromUtf8(decoded);
    return out.isEmpty() && !value.isEmpty() ? value : out;
}
} // namespace

QString normalize(const QString &path)
{
    QStringList parts;
    for (const QString &part : path.split('/')) {
        if (part.isEmpty() || part == ".")
            continue;
        if (part == "..") {
            if (!parts.isEmpty())
                parts.removeLast();
        } else {
            parts.append(part);
        }
    }
    return parts.join('/');
}

QString dirname(const QString &path)
{
    const QString normalized = normalize(path);
    const int index = normalized.lastIndexOf('/');
    return index >= 0 ? normalized.left(index) : QString();
}

QString resolve(const QString &basePath, const QString &href)
{
    if (href.isEmpty())
        return normalize(basePath);
    if (isExternalUrl(href))
        return href;

    const int hashIdx = href.indexOf('#');
    const QString pathPart = hashIdx >= 0 ? href.left(hashIdx) : href;
    const QString hash = hashIdx >= 0 ? href.mid(hashIdx + 1) : QString();
    const QString decodedPath = safeDecode(pathPart);
    const QString combined = basePath.isEmpty() ? decodedPath : basePath + "/" + decodedPath;
    const QString normalized = normalize(combined);
    return hash.isEmpty() ? normalized : normalized + "#" + hash;
}

QString stripHash(const QString &path)
{
    const int idx = path.indexOf('#');
    return idx >= 0 ? path.left(idx) : path;
}

QString hashOf(const QString &path)
{
    const int idx = path.indexOf('#');
    return idx >= 0 ? safeDecode(path.mid(idx + 1)) : QString();
}

bool isExternalUrl(const QString &url)
{
    static const QRegularExpression re(
        QStringLiteral("^(https?:|data:|blob:|mailto:)"),
        QRegularExpression::CaseInsensitiveOption);
    return re.match(url).hasMatch();
}

} // namespace path_util
