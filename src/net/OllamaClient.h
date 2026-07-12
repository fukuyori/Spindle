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
    // to the exact request even when several runs overlap. When `targetCode`
    // (ISO 639-1) is given, a reply whose script clearly doesn't match the
    // target language is retried once with a stronger instruction, then
    // reported as a failure — small translation models sometimes answer in the
    // source language, and caching that would pin the mistake.
    void translate(const QString &endpoint, const QString &model, const QString &targetLang,
                   const QString &text, const QString &glossary = QString(), int requestId = 0,
                   const QString &targetCode = QString());
    void summarize(const QString &endpoint, const QString &model, const QString &targetLang,
                   const QString &text, const QString &detailInstruction = QString(),
                   const QString &glossary = QString(), int requestId = 0);

signals:
    void finished(int requestId, bool ok, const QString &result);

private:
    void translateAttempt(const QString &endpoint, const QString &model,
                          const QString &targetLang, const QString &text,
                          const QString &glossary, int requestId, const QString &targetCode,
                          int attempt);

    QNetworkAccessManager *m_nam;
};
