#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;

// Talks to a local Ollama instance: POST {endpoint}/api/chat with a literary
// translation system prompt (mirrors the Rust ollama_translate command).
// One request in flight at a time; the caller drives sequencing.
class OllamaClient : public QObject {
    Q_OBJECT
public:
    explicit OllamaClient(QObject *parent = nullptr);

    // `requestId` is echoed back in finished() so the caller can match the reply
    // to the exact request even when several runs overlap.
    void translate(const QString &endpoint, const QString &model, const QString &targetLang,
                   const QString &text, const QString &glossary = QString(), int requestId = 0);
    void summarize(const QString &endpoint, const QString &model, const QString &targetLang,
                   const QString &text, const QString &detailInstruction = QString(),
                   int requestId = 0);

signals:
    void finished(int requestId, bool ok, const QString &result);

private:
    QNetworkAccessManager *m_nam;
};
