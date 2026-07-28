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

// A glossary reply contains at most 20 short entries. Without an explicit
// generation limit, a model that fails to close the JSON can keep producing
// tokens until the request-level timeout expires (and block the next queued
// request on single-slot Ollama servers).
constexpr int kGlossaryMaxPredictTokens = 2048;

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
    QString content = message.value(QStringLiteral("content")).toString().trimmed();
    // Reasoning models (e.g. Qwen3) can inline their chain of thought in the
    // content even with think:false, ending it with "</think>" — keep only
    // what follows the marker.
    const qsizetype think = content.lastIndexOf(QLatin1String("</think>"));
    if (think >= 0)
        content = content.mid(think + qsizetype(qstrlen("</think>"))).trimmed();
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

// True when `text` is clearly NOT written in the language `code` — judged by
// script, and only for scripts we can tell apart reliably (kana for ja, Han
// without kana for zh, hangul for ko, Latin for en/fr/de/es). Short results
// are never flagged: a translated proper noun can legitimately be all-Han
// even in Japanese. Used to catch a model answering in the source language.
bool clearlyWrongLanguage(const QString &text, const QString &code)
{
    int han = 0, kana = 0, hangul = 0, latin = 0, letters = 0;
    for (const QChar &qc : text) {
        const ushort u = qc.unicode();
        if (qc.isLetter())
            ++letters;
        if ((u >= 0x4E00 && u <= 0x9FFF) || (u >= 0x3400 && u <= 0x4DBF)
            || (u >= 0xF900 && u <= 0xFAFF))
            ++han;
        else if (u >= 0x3040 && u <= 0x30FF)
            ++kana;
        else if ((u >= 0xAC00 && u <= 0xD7A3) || (u >= 0x1100 && u <= 0x11FF))
            ++hangul;
        else if ((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z')
                 || (u >= 0x00C0 && u <= 0x024F))
            ++latin;
    }
    if (letters < 12)
        return false; // too short to judge
    if (code == QLatin1String("ja"))
        return kana == 0; // Japanese prose always carries kana
    if (code == QLatin1String("zh"))
        return kana > 0 || (han == 0 && latin > han) || hangul > letters / 2;
    if (code == QLatin1String("ko"))
        return hangul == 0;
    if (code == QLatin1String("en") || code == QLatin1String("fr")
        || code == QLatin1String("de") || code == QLatin1String("es"))
        return latin < letters / 2; // mostly non-Latin -> not translated
    return false; // unknown target: no opinion
}

} // namespace

OllamaClient::OllamaClient(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this))
{
}

void OllamaClient::translate(const QString &endpoint, const QString &model,
                             const QString &targetLang, const QString &text,
                             const QString &glossary, int requestId,
                             const QString &targetCode)
{
    translateAttempt(endpoint, model, targetLang, text, glossary, requestId, targetCode, 1);
}

void OllamaClient::translateAttempt(const QString &endpoint, const QString &model,
                                    const QString &targetLang, const QString &text,
                                    const QString &glossary, int requestId,
                                    const QString &targetCode, int attempt)
{
    QString base = endpoint.trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    const QUrl url(base + QStringLiteral("/api/chat"));

    QString system =
        QStringLiteral(
            "You are a professional literary translator. Translate the user's text into %1. "
            "Output only the translation itself — no explanations, notes, labels, or surrounding "
            "quotation marks. Preserve the original meaning, tone, and punctuation. Always "
            "translate: languages that share a script are still different languages (e.g. "
            "Japanese is not Chinese), so return the text unchanged only if it is already "
            "entirely written in %1.")
            .arg(targetLang);
    if (!glossary.isEmpty())
        system += glossary;
    // Translation-tuned models (e.g. TranslateGemma) ignore system-turn
    // instructions and expect the task stated right before the text, so the
    // user turn repeats it. Instruction-following models just see it twice.
    // The wording is load-bearing: "to Japanese. Provide only the
    // translation…" made gemma answer in the source language, while this
    // phrasing tested clean on both model families — don't reword casually.
    // The glossary rides in the user turn too, inside the task statement.
    // Placement is load-bearing (tested on TranslateGemma): put before the
    // task line, the model answers "no text to translate"; put after the
    // text, it can translate the glossary itself. In the task line both a
    // 20-entry list and a one-term text translated correctly.
    QString user =
        glossary.isEmpty()
            ? QStringLiteral("Translate the following text into %1. "
                             "Output only the %1 translation:\n\n%2")
                  .arg(targetLang, text)
            : QStringLiteral("Translate the following text into %1. "
                             "Output only the %1 translation. %2\n\n%3")
                  .arg(targetLang, glossary.trimmed(), text);
    if (attempt > 1) {
        // Retry after a wrong-language reply: nudge the sampling with an
        // explicit reminder (an identical request at low temperature would
        // mostly reproduce the same wrong answer).
        user += QStringLiteral("\n\nIMPORTANT: The translation must be written in %1.")
                    .arg(targetLang);
    }

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
                                {QStringLiteral("content"), user}});
    body[QStringLiteral("messages")] = messages;

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(kTransferTimeoutMs);

    QNetworkReply *reply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, url, requestId, endpoint, model, targetLang, text, glossary,
             targetCode, attempt]() {
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
        if (!targetCode.isEmpty() && clearlyWrongLanguage(result, targetCode)) {
            if (attempt < 2) {
                translateAttempt(endpoint, model, targetLang, text, glossary, requestId,
                                 targetCode, attempt + 1);
                return;
            }
            emit finished(requestId, false,
                          QStringLiteral("モデルが翻訳先言語 (%1) で応答しませんでした。"
                                         "別のモデルをお試しください")
                              .arg(targetLang));
            return;
        }
        emit finished(requestId, true, result);
    });
}

void OllamaClient::extractGlossary(const QString &endpoint, const QString &model,
                                   const QString &targetLang, const QString &text,
                                   int requestId)
{
    QString base = endpoint.trimmed();
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    const QUrl url(base + QStringLiteral("/api/chat"));

    const QString target = targetLang.trimmed().isEmpty()
                               ? QStringLiteral("the requested target language")
                               : targetLang.trimmed();
    const QString system =
        QStringLiteral(
            "You are a terminology extractor preparing a translation glossary. From the "
            "user's text, list the proper nouns and recurring special terms (person names, "
            "place names, organizations, titles of works, jargon) whose rendering must stay "
            "consistent throughout a book. Skip ordinary vocabulary. Reply with JSON only, "
            "in exactly this shape: "
            "{\"entries\":[{\"src\":\"term exactly as written in the text\","
            "\"dst\":\"its translation into %1\",\"note\":\"short category such as "
            "person / place / organization / term\"}]}. "
            "Copy \"src\" verbatim from the text. Write \"dst\" only in %1. "
            "List at most 20 of the most important terms; reply {\"entries\":[]} "
            "when there are none.")
            .arg(target);
    const QString user =
        QStringLiteral("Extract glossary terms from the following text. Reply with JSON "
                       "only:\n\n%1")
            .arg(text);

    QJsonObject body;
    body[QStringLiteral("model")] = model;
    body[QStringLiteral("stream")] = false;
    body[QStringLiteral("think")] = false;
    const QJsonObject entrySchema{
        {QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"),
         QJsonObject{{QStringLiteral("src"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                     {QStringLiteral("dst"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}},
                     {QStringLiteral("note"),
                      QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}}}}},
        {QStringLiteral("required"),
         QJsonArray{QStringLiteral("src"), QStringLiteral("dst"), QStringLiteral("note")}},
        {QStringLiteral("additionalProperties"), false}};
    body[QStringLiteral("format")] =
        QJsonObject{{QStringLiteral("type"), QStringLiteral("object")},
                    {QStringLiteral("properties"),
                     QJsonObject{{QStringLiteral("entries"),
                                  QJsonObject{{QStringLiteral("type"),
                                               QStringLiteral("array")},
                                              {QStringLiteral("items"), entrySchema},
                                              {QStringLiteral("maxItems"), 20}}}}},
                    {QStringLiteral("required"), QJsonArray{QStringLiteral("entries")}},
                    {QStringLiteral("additionalProperties"), false}};
    QJsonObject options;
    options[QStringLiteral("temperature")] = 0.0;
    options[QStringLiteral("num_predict")] = kGlossaryMaxPredictTokens;
    body[QStringLiteral("options")] = options;
    QJsonArray messages;
    messages.append(QJsonObject{{QStringLiteral("role"), QStringLiteral("system")},
                                {QStringLiteral("content"), system}});
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
        const QJsonObject root = QJsonDocument::fromJson(bytes).object();
        if (root.value(QStringLiteral("done_reason")).toString()
            == QLatin1String("length")) {
            emit finished(
                requestId, false,
                QStringLiteral("Ollama の用語リストが出力上限に達しました。"
                               "要約モデルを変更して再実行してください"));
            return;
        }
        bool ok = false;
        const QString result = extractOllamaContent(
            bytes, QStringLiteral("Ollama が空の用語リストを返しました"), &ok);
        emit finished(requestId, ok, result);
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
    // Like translate(): the glossary rides in the user turn inside the task
    // statement — never before it, which derails translation-tuned models.
    const QString user =
        glossary.isEmpty()
            ? QStringLiteral("Summarize the following text in %1 only.\n\n%2").arg(target, text)
            : QStringLiteral("Summarize the following text in %1 only. %2\n\n%3")
                  .arg(target, glossary.trimmed(), text);
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
