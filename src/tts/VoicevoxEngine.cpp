#include "tts/VoicevoxEngine.h"

#include "tts/WavUtil.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QSettings>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <cmath>

VoicevoxEngine::VoicevoxEngine(QObject *parent)
    : TtsEngine(parent)
{
    connect(&m_player, &WavPlayer::finished, this, &TtsEngine::utteranceFinished);
    connect(&m_player, &WavPlayer::errorOccurred, this, &TtsEngine::errorOccurred);
}

QString VoicevoxEngine::endpoint()
{
    QString ep = QSettings()
                     .value(QStringLiteral("tts/voicevoxEndpoint"),
                            QStringLiteral("http://localhost:50021"))
                     .toString()
                     .trimmed();
    while (ep.endsWith(QLatin1Char('/')))
        ep.chop(1);
    return ep;
}

void VoicevoxEngine::setPreferredVoiceName(const QString &lang, const QString &name)
{
    if (lang == QLatin1String("ja"))
        m_voice = name;
}

int VoicevoxEngine::styleIdForVoice() const
{
    for (const Speaker &s : m_speakers)
        if (s.name == m_voice)
            return s.styleId;
    return m_speakers.isEmpty() ? -1 : m_speakers.first().styleId;
}

void VoicevoxEngine::fetchSpeakers(const QString &ep, std::function<void(bool, QString)> done)
{
    if (ep == m_speakersEndpoint && !m_speakers.isEmpty()) {
        done(true, QString());
        return;
    }
    QNetworkRequest req(QUrl(ep + QStringLiteral("/speakers")));
    req.setTransferTimeout(1200); // local server: answers instantly or not at all
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, ep, reply, done] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            done(false, QStringLiteral("VOICEVOX に接続できません (%1): %2")
                            .arg(ep, reply->errorString()));
            return;
        }
        m_speakers.clear();
        const QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).array();
        for (const QJsonValue &v : arr) {
            const QJsonObject speaker = v.toObject();
            const QString name = speaker.value(QStringLiteral("name")).toString();
            const QJsonArray styles = speaker.value(QStringLiteral("styles")).toArray();
            for (const QJsonValue &sv : styles) {
                const QJsonObject style = sv.toObject();
                m_speakers.append(
                    {QStringLiteral("%1（%2）").arg(
                         name, style.value(QStringLiteral("name")).toString()),
                     style.value(QStringLiteral("id")).toInt()});
            }
        }
        m_speakersEndpoint = ep;
        done(!m_speakers.isEmpty(),
             m_speakers.isEmpty() ? QStringLiteral("VOICEVOX に話者がありません") : QString());
    });
}

QStringList VoicevoxEngine::voiceNames(const QLocale &locale)
{
    QStringList out;
    if (locale.language() != QLocale::Japanese)
        return out;
    const QString ep = endpoint();
    if (ep != m_speakersEndpoint || m_speakers.isEmpty()) {
        // Synchronous fetch for the settings dialog, bounded by the request's
        // own 1.2 s transfer timeout.
        QEventLoop loop;
        bool finished = false;
        fetchSpeakers(ep, [&](bool, const QString &) {
            finished = true;
            loop.quit();
        });
        if (!finished)
            loop.exec();
    }
    for (const Speaker &s : m_speakers)
        out << s.name;
    return out;
}

void VoicevoxEngine::requestWav(const QString &text, std::function<void(QByteArray)> done)
{
    const int gen = ++m_gen;
    const QString ep = endpoint();
    fetchSpeakers(ep, [this, gen, text, ep, done](bool ok, const QString &error) {
        if (gen != m_gen)
            return;
        if (!ok) {
            emit errorOccurred(error);
            return;
        }
        const int style = styleIdForVoice();
        QUrl queryUrl(ep + QStringLiteral("/audio_query"));
        QUrlQuery q;
        q.addQueryItem(QStringLiteral("text"),
                       QString::fromUtf8(QUrl::toPercentEncoding(text)));
        q.addQueryItem(QStringLiteral("speaker"), QString::number(style));
        queryUrl.setQuery(q);
        QNetworkRequest req(queryUrl);
        req.setTransferTimeout(30000);
        QNetworkReply *reply = m_nam.post(req, QByteArray());
        connect(reply, &QNetworkReply::finished, this, [this, gen, reply, ep, style, done] {
            reply->deleteLater();
            if (gen != m_gen)
                return;
            if (reply->error() != QNetworkReply::NoError) {
                emit errorOccurred(QStringLiteral("VOICEVOX クエリ生成に失敗: ")
                                   + reply->errorString());
                return;
            }
            QJsonObject query = QJsonDocument::fromJson(reply->readAll()).object();
            // rate -1..1 -> speed 0.5..2 (log scale, 0 = normal)
            query.insert(QStringLiteral("speedScale"), std::pow(2.0, m_rate));
            QUrl synthUrl(ep + QStringLiteral("/synthesis"));
            QUrlQuery sq;
            sq.addQueryItem(QStringLiteral("speaker"), QString::number(style));
            synthUrl.setQuery(sq);
            QNetworkRequest sreq(synthUrl);
            sreq.setHeader(QNetworkRequest::ContentTypeHeader,
                           QStringLiteral("application/json"));
            sreq.setTransferTimeout(120000); // CPU synthesis of long paragraphs
            QNetworkReply *synth =
                m_nam.post(sreq, QJsonDocument(query).toJson(QJsonDocument::Compact));
            connect(synth, &QNetworkReply::finished, this, [this, gen, synth, done] {
                synth->deleteLater();
                if (gen != m_gen)
                    return;
                if (synth->error() != QNetworkReply::NoError) {
                    emit errorOccurred(QStringLiteral("VOICEVOX 合成に失敗: ")
                                       + synth->errorString());
                    return;
                }
                done(synth->readAll());
            });
        });
    });
}

void VoicevoxEngine::speak(const QString &text)
{
    m_player.stop();
    requestWav(text, [this](const QByteArray &wav) { m_player.play(wav); });
}

void VoicevoxEngine::synthesize(const QString &text)
{
    requestWav(text, [this](const QByteArray &wav) {
        const wav_util::Pcm pcm = wav_util::parseWav(wav);
        if (!pcm.ok) {
            emit errorOccurred(QStringLiteral("VOICEVOX の音声データを解析できません"));
            return;
        }
        emit synthesized(pcm.sampleRate, wav_util::toMono16(pcm.data, pcm.channels));
    });
}

void VoicevoxEngine::stop()
{
    ++m_gen; // orphan in-flight replies (they self-delete)
    m_player.stop();
}
