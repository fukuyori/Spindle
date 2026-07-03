#pragma once

#include <QCache>
#include <QHash>
#include <QWebEngineUrlSchemeHandler>

class EpubBook;

// Serves open EPUBs over a custom "epub://<book-id>/<zip-path>" URL so several
// books (one per window) can be open at once. A single shared handler is
// installed on the default profile; each window registers its book under a
// unique id (the URL host), and requests are routed by that id.
class EpubSchemeHandler : public QWebEngineUrlSchemeHandler {
    Q_OBJECT
public:
    explicit EpubSchemeHandler(QObject *parent = nullptr);

    // Process-wide shared handler.
    static EpubSchemeHandler *instance();

    void registerBook(const QString &id, EpubBook *book);
    void unregisterBook(const QString &id);

    // Reader theme CSS embedded into served chapter documents for this book, so
    // the first painted frame is already styled. Changing it drops that book's
    // cached (already-styled) documents.
    void setThemeCss(const QString &id, const QString &css);

    static const char *schemeName() { return "epub"; }
    static QString urlFor(const QString &id, const QString &zipPath); // -> epub://<id>/<path>
    static QString zipPathFor(const QUrl &url);

    // Registers the scheme with QtWebEngine. Must be called before QApplication.
    static void registerScheme();

    void requestStarted(QWebEngineUrlRequestJob *job) override;

private:
    void dropCachedEntries(const QString &id);

    QHash<QString, EpubBook *> m_books;
    QHash<QString, QString> m_themeCss; // book id -> embedded theme CSS
    // Chapter documents with data-spindle-block ids injected, keyed by
    // "<id>/<zipPath>" with cost = bytes. Avoids re-parsing + re-serializing the
    // same chapter on every visit (前へ/次へ round trips become instant).
    QCache<QString, QByteArray> m_injected{32 * 1024 * 1024};
};
