#include "net/OllamaClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QUrl>

namespace {

// stream:false means Ollama sends nothing until generation completes, so the
// whole generation must fit in this window. 5 minutes tolerates slow local
// models while still surfacing a hung server instead of waiting forever.
constexpr int kTransferTimeoutMs = 300'000;

QString shortResponseForMessage(const QByteArray &bytes)
{
    QString text = QString::fromUtf8(bytes).trimmed();
    text.replace(QLatin1Char('\n'), QLatin1Char(' '));
    text.replace(QLatin1Char('\r'), QLatin1Char(' '));
    static constexpr qsizetype kMax = 400;
    if (text.size() > kMax)
        text = text.left(kMax) + QStringLiteral("...");
    return text;
}

// Ollama pairs error HTTP statuses with a {"error":"..."} body whose text is
// far more actionable than Qt's status-line message (e.g. `model "x" not
// found` instead of "server replied: Not Found") — surface it when present.
QString ollamaErrorDetail(const QByteArray &bytes, QNetworkReply *reply)
{
    const QString detail =
        QJsonDocument::fromJson(bytes).object().value(QStringLiteral("error")).toString();
    return detail.isEmpty() ? reply->errorString() : detail;
}

QString extractOllamaContent(const QByteArray &bytes, const QString &emptyMessage, bool *ok)
{
    *ok = false;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return QStringLiteral("Ollama の応答を JSON として解析できませんでした: %1")
            .arg(parseError.errorString());
    }

    const QJsonObject root = doc.object();
    const QString apiError = root.value(QStringLiteral("error")).toString().trimmed();
    if (!apiError.isEmpty())
        return apiError;

    const QJsonObject message = root.value(QStringLiteral("message")).toObject();
    const QString content = message.value(QStringLiteral("content")).toString().trimmed();
    if (!content.isEmpty()) {
        *ok = true;
        return content;
    }

    const QString thinking = message.value(QStringLiteral("thinking")).toString().trimmed();
    if (!thinking.isEmpty()) {
        return emptyMessage
            + QStringLiteral("（thinking のみで本文がありません。モデル設定を確認してください）");
    }

    const QString response = root.value(QStringLiteral("response")).toString().trimmed();
    if (!response.isEmpty()) {
        *ok = true;
        return response;
    }

    QStringList details;
    const QString doneReason = root.value(QStringLiteral("done_reason")).toString().trimmed();
    if (!doneReason.isEmpty())
        details.append(QStringLiteral("done_reason=%1").arg(doneReason));
    if (root.contains(QStringLiteral("prompt_eval_count")))
        details.append(QStringLiteral("prompt_eval_count=%1")
                           .arg(root.value(QStringLiteral("prompt_eval_count")).toInt()));
    if (root.contains(QStringLiteral("eval_count"))) {
        details.append(QStringLiteral("eval_count=%1")
                           .arg(root.value(QStringLiteral("eval_count")).toInt()));
    } else if (root.value(QStringLiteral("done")).toBool(false)) {
        details.append(QStringLiteral("eval_countなし"));
    }

    const QString suffix =
        details.isEmpty() ? QString() : QStringLiteral("（%1）").arg(details.join(QStringLiteral(", ")));
    const QString raw = shortResponseForMessage(bytes);
    return raw.isEmpty() ? emptyMessage + suffix
                         : emptyMessage + suffix + QStringLiteral("\n応答: %1").arg(raw);
}

} // namespace

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
    body[QStringLiteral("think")] = false;
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
    request.setTransferTimeout(kTransferTimeoutMs);

    QNetworkReply *reply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, url, requestId]() {
        reply->deleteLater();
        const QByteArray bytes = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit finished(requestId, false,
                          QStringLiteral("Ollama への接続に失敗しました (%1): %2")
                              .arg(url.toString(), ollamaErrorDetail(bytes, reply)));
            return;
        }
        bool ok = false;
        const QString result =
            extractOllamaContent(bytes, QStringLiteral("Ollama が空の翻訳を返しました"), &ok);
        if (!ok) {
            emit finished(requestId, false, result);
            return;
        }
        emit finished(requestId, true, result);
    });
}

void OllamaClient::summarize(const QString &endpoint, const QString &model,
                             const QString &targetLang, const QString &text,
                             const QString &detailInstruction, const QString &glossary,
                             int requestId)
{
    QString base = endpoint.trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    const QUrl url(base + QStringLiteral("/api/chat"));

    const QString target = targetLang.trimmed().isEmpty()
                               ? QStringLiteral("the requested target language")
                               : targetLang.trimmed();
    QString system =
        QStringLiteral(
            "You are a careful reading assistant. Summarize the user's text. "
            "Write every part of the summary only in %1, regardless of the source text's "
            "language. If the source text is in another language, translate the summary "
            "into %1 instead of answering in the source language. Output only the summary. "
            "Keep important names, relationships, events, and claims. Do not add facts "
            "that are not in the text. Use concise bullet points when helpful.")
            .arg(target);
    if (!detailInstruction.trimmed().isEmpty())
        system += QStringLiteral(" ") + detailInstruction.trimmed();
    if (!glossary.isEmpty())
        system += glossary;

    QJsonObject body;
    body[QStringLiteral("model")] = model;
    body[QStringLiteral("stream")] = false;
    body[QStringLiteral("think")] = false;
    QJsonObject options;
    options[QStringLiteral("temperature")] = 0.2;
    body[QStringLiteral("options")] = options;
    QJsonArray messages;
    messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                {QStringLiteral("content"), system}});
    const QString user =
        QStringLiteral("Summarize the following text in %1 only.\n\n%2").arg(target, text);
    messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("user")},
                                {QStringLiteral("content"), user}});
    body[QStringLiteral("messages")] = messages;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(kTransferTimeoutMs);

    QNetworkReply *reply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, requestId]() {
        reply->deleteLater();
        const QByteArray bytes = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit finished(requestId, false,
                          QStringLiteral("Ollama への接続に失敗しました (%1): %2")
                              .arg(url.toString(), ollamaErrorDetail(bytes, reply)));
            return;
        }
        bool ok = false;
        const QString result =
            extractOllamaContent(bytes, QStringLiteral("Ollama が空の要約を返しました"), &ok);
        if (!ok) {
            emit finished(requestId, false, result);
            return;
        }
        emit finished(requestId, true, result);
    });
}
