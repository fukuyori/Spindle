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

    void translate(const QString &endpoint, const QString &model, const QString &targetLang,
                   const QString &text, const QString &glossary = QString());

signals:
    void finished(bool ok, const QString &result);

private:
    QNetworkAccessManager *m_nam;
};
