#pragma once

#include <QWebEngineUrlSchemeHandler>

class EpubBook;

// Serves the open EPUB's entries over a custom "epub://book/<zip-path>" URL so
// the web engine can load chapters together with their own CSS, fonts and
// images at full fidelity (relative URLs resolve naturally).
class EpubSchemeHandler : public QWebEngineUrlSchemeHandler {
    Q_OBJECT
public:
    explicit EpubSchemeHandler(QObject *parent = nullptr);

    void setBook(EpubBook *book) { m_book = book; }

    static const char *schemeName() { return "epub"; }
    static QString urlFor(const QString &zipPath); // -> "epub://book/<zipPath>"
    static QString zipPathFor(const QUrl &url);

    // Registers the scheme with QtWebEngine. Must be called before QApplication.
    static void registerScheme();

    void requestStarted(QWebEngineUrlRequestJob *job) override;

private:
    EpubBook *m_book = nullptr;
};
