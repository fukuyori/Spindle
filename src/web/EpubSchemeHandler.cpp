#include "web/EpubSchemeHandler.h"

#include "core/BlockIndex.h"
#include "epub/EpubBook.h"

#include <QBuffer>
#include <QRegularExpression>
#include <QStringConverter>
#include <QUrl>
#include <QWebEngineUrlRequestJob>
#include <QWebEngineUrlScheme>

namespace {

QByteArray mimeFor(const QString &path)
{
    const QString p = path.toLower();
    auto ends = [&](const char *s) { return p.endsWith(QLatin1String(s)); };
    if (ends(".xhtml") || ends(".xht"))   return "application/xhtml+xml";
    if (ends(".html") || ends(".htm"))    return "text/html";
    if (ends(".css"))                     return "text/css";
    if (ends(".js"))                      return "text/javascript";
    if (ends(".xml") || ends(".opf") || ends(".ncx")) return "application/xml";
    if (ends(".jpg") || ends(".jpeg"))    return "image/jpeg";
    if (ends(".png"))                     return "image/png";
    if (ends(".gif"))                     return "image/gif";
    if (ends(".svg"))                     return "image/svg+xml";
    if (ends(".webp"))                    return "image/webp";
    if (ends(".otf"))                     return "font/otf";
    if (ends(".ttf"))                     return "font/ttf";
    if (ends(".woff"))                    return "font/woff";
    if (ends(".woff2"))                   return "font/woff2";
    if (ends(".json"))                    return "application/json";
    return "application/octet-stream";
}

// Best-effort script scrub for chapter documents that QDom couldn't parse (the
// sanitizing injectBlockIds pass never ran on them). Regex on decoded UTF-8:
// coarse, but ApplicationWorld isolation keeps the bridge out of page reach
// regardless — this is defense in depth. Returns a null QByteArray when the
// bytes aren't valid UTF-8 (then the caller serves the original untouched).
QByteArray scrubActiveContent(const QByteArray &bytes)
{
    QStringDecoder decoder(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
    QString text = decoder.decode(bytes);
    if (decoder.hasError())
        return {};
    static const QRegularExpression scriptBlock(
        QStringLiteral("<script\\b[^>]*>.*?</script[^>]*>"),
        QRegularExpression::CaseInsensitiveOption
            | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression scriptTag(QStringLiteral("<script\\b[^>]*/?>"),
                                              QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression onAttr(
        QStringLiteral("\\son[a-zA-Z]+\\s*=\\s*(\"[^\"]*\"|'[^']*'|[^\\s>]+)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression jsUrl(
        QStringLiteral("(href|src)\\s*=\\s*([\"'])\\s*javascript:[^\"']*\\2"),
        QRegularExpression::CaseInsensitiveOption);
    text.remove(scriptBlock);
    text.remove(scriptTag); // unclosed <script> after the block pass
    text.remove(onAttr);
    text.replace(jsUrl, QStringLiteral("\\1=\\2#\\2"));
    return text.toUtf8();
}

} // namespace

EpubSchemeHandler::EpubSchemeHandler(QObject *parent)
    : QWebEngineUrlSchemeHandler(parent)
{
}

EpubSchemeHandler *EpubSchemeHandler::instance()
{
    static EpubSchemeHandler *handler = new EpubSchemeHandler();
    return handler;
}

void EpubSchemeHandler::registerBook(const QString &id, EpubBook *book)
{
    m_books.insert(id, book);
    dropCachedEntries(id); // a re-registered id serves a different book
}

void EpubSchemeHandler::unregisterBook(const QString &id)
{
    m_books.remove(id);
    m_themeCss.remove(id);
    dropCachedEntries(id);
}

void EpubSchemeHandler::setThemeCss(const QString &id, const QString &css)
{
    if (m_themeCss.value(id) == css)
        return; // unchanged — keep the cached styled documents
    m_themeCss.insert(id, css);
    dropCachedEntries(id);
}

void EpubSchemeHandler::dropCachedEntries(const QString &id)
{
    const QString prefix = id + QLatin1Char('/');
    const QList<QString> keys = m_injected.keys();
    for (const QString &key : keys) {
        if (key.startsWith(prefix))
            m_injected.remove(key);
    }
}

QString EpubSchemeHandler::urlFor(const QString &id, const QString &zipPath)
{
    QString clean = zipPath;
    while (clean.startsWith(QLatin1Char('/')))
        clean.remove(0, 1);
    return QStringLiteral("epub://") + id + QLatin1Char('/')
           + QString::fromUtf8(QUrl::toPercentEncoding(clean, "/"));
}

QString EpubSchemeHandler::zipPathFor(const QUrl &url)
{
    QString path = url.path(QUrl::FullyDecoded);
    while (path.startsWith(QLatin1Char('/')))
        path.remove(0, 1);
    return path;
}

void EpubSchemeHandler::registerScheme()
{
    QWebEngineUrlScheme scheme(schemeName());
    scheme.setSyntax(QWebEngineUrlScheme::Syntax::Host);
    scheme.setDefaultPort(QWebEngineUrlScheme::PortUnspecified);
    scheme.setFlags(QWebEngineUrlScheme::SecureScheme
                    | QWebEngineUrlScheme::LocalScheme
                    | QWebEngineUrlScheme::LocalAccessAllowed
                    | QWebEngineUrlScheme::ContentSecurityPolicyIgnored
                    | QWebEngineUrlScheme::CorsEnabled);
    QWebEngineUrlScheme::registerScheme(scheme);
}

void EpubSchemeHandler::requestStarted(QWebEngineUrlRequestJob *job)
{
    const QString id = job->requestUrl().host();
    EpubBook *book = m_books.value(id, nullptr);
    if (!book) {
        job->fail(QWebEngineUrlRequestJob::RequestFailed);
        return;
    }
    const QString zipPath = zipPathFor(job->requestUrl());
    if (!book->contains(zipPath)) {
        job->fail(QWebEngineUrlRequestJob::UrlNotFound);
        return;
    }

    QByteArray mime = mimeFor(zipPath);
    QByteArray data;
    // For chapter documents, inject document-order block ids so highlight
    // anchoring shares one coordinate system with C++ (see BlockIndex). If the
    // markup won't parse, fall back to serving the original bytes untouched.
    // Injection is a full DOM parse + serialize, so successful results are
    // cached per (book id, path).
    if (mime == "application/xhtml+xml" || mime == "text/html") {
        const QString cacheKey = id + QLatin1Char('/') + zipPath;
        if (const QByteArray *cached = m_injected.object(cacheKey)) {
            data = *cached;
            mime += "; charset=utf-8";
        } else {
            const QString themeCss = m_themeCss.value(id);
            const QString injected =
                block_index::injectBlockIds(book->readText(zipPath), themeCss);
            if (!injected.isEmpty()) {
                data = injected.toUtf8();
            } else {
                // Unparseable markup: the sanitizing DOM pass never ran, so
                // scrub scripts from the original before serving it.
                data = scrubActiveContent(book->readBytes(zipPath));
                // Best-effort theme embedding so even this path paints styled.
                if (!data.isNull() && !themeCss.isEmpty()) {
                    QString text = QString::fromUtf8(data);
                    const int headEnd =
                        text.indexOf(QLatin1String("</head>"), 0, Qt::CaseInsensitive);
                    if (headEnd >= 0) {
                        text.insert(headEnd,
                                    QStringLiteral("<style id=\"__spindle_theme\">%1</style>")
                                        .arg(themeCss));
                        data = text.toUtf8();
                    }
                }
            }
            if (!data.isNull()) {
                mime += "; charset=utf-8";
                m_injected.insert(cacheKey, new QByteArray(data),
                                  qMax(1, static_cast<int>(data.size())));
            }
        }
    }
    if (data.isNull())
        data = book->readBytes(zipPath);

    QBuffer *buffer = new QBuffer(job);
    buffer->setData(data);
    buffer->open(QIODevice::ReadOnly);
    job->reply(mime, buffer);
}
