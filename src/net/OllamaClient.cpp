#include "net/OllamaClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

OllamaClient::OllamaClient(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this))
{
}

void OllamaClient::translate(const QString &endpoint, const QString &model,
                             const QString &targetLang, const QString &text,
                             const QString &glossary, int requestId)
{
    QString base = endpoint.trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    const QUrl url(base + QStringLiteral("/api/chat"));

    QString system =
        QStringLiteral(
            "You are a professional literary translator. Translate the user's text into %1. "
            "Output only the translation itself — no explanations, notes, labels, or surrounding "
            "quotation marks. Preserve the original meaning, tone, and punctuation. If the text is "
            "already in %1, return it unchanged.")
            .arg(targetLang);
    if (!glossary.isEmpty())
        system += glossary;

    QJsonObject body;
    body[QStringLiteral("model")] = model;
    body[QStringLiteral("stream")] = false;
    QJsonObject options;
    options[QStringLiteral("temperature")] = 0.2;
    body[QStringLiteral("options")] = options;
    QJsonArray messages;
    messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                {QStringLiteral("content"), system}});
    messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                {QStringLiteral("content"), text}});
    body[QStringLiteral("messages")] = messages;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply *reply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, requestId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit finished(requestId, false,
                          QStringLiteral("Ollama への接続に失敗しました (%1): %2")
                              .arg(url.toString(), reply->errorString()));
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        const QString content = doc.object()
                                    .value(QStringLiteral("message"))
                                    .toObject()
                                    .value(QStringLiteral("content"))
                                    .toString()
                                    .trimmed();
        if (content.isEmpty()) {
            emit finished(requestId, false, QStringLiteral("Ollama が空の翻訳を返しました"));
            return;
        }
        emit finished(requestId, true, content);
    });
}

void OllamaClient::summarize(const QString &endpoint, const QString &model,
                             const QString &targetLang, const QString &text,
                             const QString &detailInstruction, int requestId)
{
    QString base = endpoint.trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    const QUrl url(base + QStringLiteral("/api/chat"));

    QString system =
        QStringLiteral(
            "You are a careful reading assistant. Summarize the user's text. "
            "The summary must be written only in %1, regardless of the source text's "
            "language. Output only the summary. Keep important names, relationships, "
            "events, and claims. Do not add facts that are not in the text. Use concise "
            "bullet points when helpful.")
            .arg(targetLang);
    if (!detailInstruction.trimmed().isEmpty())
        system += QStringLiteral(" ") + detailInstruction.trimmed();

    QJsonObject body;
    body[QStringLiteral("model")] = model;
    body[QStringLiteral("stream")] = false;
    QJsonObject options;
    options[QStringLiteral("temperature")] = 0.2;
    body[QStringLiteral("options")] = options;
    QJsonArray messages;
    messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                {QStringLiteral("content"), system}});
    messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                {QStringLiteral("content"), text}});
    body[QStringLiteral("messages")] = messages;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));

    QNetworkReply *reply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, requestId]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit finished(requestId, false,
                          QStringLiteral("Ollama への接続に失敗しました (%1): %2")
                              .arg(url.toString(), reply->errorString()));
            return;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        const QString content = doc.object()
                                    .value(QStringLiteral("message"))
                                    .toObject()
                                    .value(QStringLiteral("content"))
                                    .toString()
                                    .trimmed();
        if (content.isEmpty()) {
            emit finished(requestId, false, QStringLiteral("Ollama が空の要約を返しました"));
            return;
        }
        emit finished(requestId, true, content);
    });
}
