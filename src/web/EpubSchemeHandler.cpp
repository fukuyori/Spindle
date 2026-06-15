#include "web/EpubSchemeHandler.h"

#include "epub/EpubBook.h"

#include <QBuffer>
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
}

void EpubSchemeHandler::unregisterBook(const QString &id)
{
    m_books.remove(id);
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

    QBuffer *buffer = new QBuffer(job);
    buffer->setData(book->readBytes(zipPath));
    buffer->open(QIODevice::ReadOnly);
    job->reply(mimeFor(zipPath), buffer);
}
