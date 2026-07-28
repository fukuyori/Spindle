#pragma once

#include <QByteArray>
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
    // Extract translation-glossary candidates (proper nouns, recurring terms)
    // from `text`, translated into `targetLang`. Ollama is asked for JSON
    // output ("format":"json"); the result is the raw JSON string
    // {"entries":[{"src","dst","note"}]} — the caller parses and validates.
    void extractGlossary(const QString &endpoint, const QString &model,
                         const QString &targetLang, const QString &text, int requestId = 0);
    // OCR one page image with an Ollama vision model (/api/generate + images).
    // A reply showing repetition collapse — a vision-model failure mode where
    // one phrase repeats endlessly — is retried once with `retryModel` (when
    // given and different), then reported as a failure.
    void ocrImage(const QString &endpoint, const QString &model, const QString &retryModel,
                  const QByteArray &imageData, int requestId = 0);

signals:
    void finished(int requestId, bool ok, const QString &result);

private:
    void translateAttempt(const QString &endpoint, const QString &model,
                          const QString &targetLang, const QString &text,
                          const QString &glossary, int requestId, const QString &targetCode,
                          int attempt);
    void ocrAttempt(const QString &endpoint, const QString &model, const QString &retryModel,
                    const QByteArray &imageData, int requestId, int attempt);

    QNetworkAccessManager *m_nam;
};
