#pragma once

#include "tts/TtsEngine.h"
#include "tts/WavPlayer.h"

#include <QNetworkAccessManager>
#include <QVector>

#include <functional>

// VOICEVOX (https://voicevox.hiroshiba.jp) — local Japanese neural TTS with a
// REST API (default http://localhost:50021). Japanese only: voiceNames()
// returns nothing for other languages, so the composite engine never routes
// them here. The endpoint is read from QSettings (tts/voicevoxEndpoint) on
// each use; the speaker list is fetched lazily and cached per endpoint.
class VoicevoxEngine : public TtsEngine {
    Q_OBJECT
public:
    explicit VoicevoxEngine(QObject *parent = nullptr);

    bool available() const override { return true; } // errors surface at speak time
    void setLocale(const QLocale &locale) override { Q_UNUSED(locale); } // ja only
    void setPreferredVoiceName(const QString &lang, const QString &name) override;
    QStringList voiceNames(const QLocale &locale) override;
    void setRate(double rate) override { m_rate = rate; }
    void speak(const QString &text) override;
    void pause() override { m_player.pause(); }
    void resume() override { m_player.resume(); }
    void stop() override;
    bool canSynthesize() const override { return true; }
    void synthesize(const QString &text) override;

private:
    struct Speaker {
        QString name; // "四国めたん（あまあま）"
        int styleId;
    };

    static QString endpoint(); // from QSettings, trailing '/' stripped
    // Invokes `done` once the speaker list for `ep` is cached (immediately on
    // a cache hit). `done` may run synchronously.
    void fetchSpeakers(const QString &ep, std::function<void(bool, QString)> done);
    int styleIdForVoice() const; // preferred voice, else the first speaker
    // Shared audio_query + synthesis chain behind speak()/synthesize(); calls
    // `done(wav)` on success, emits errorOccurred and drops otherwise. A
    // stop()/newer request in the meantime silently orphans the chain.
    void requestWav(const QString &text, std::function<void(QByteArray)> done);

    QNetworkAccessManager m_nam;
    WavPlayer m_player;
    QVector<Speaker> m_speakers;
    QString m_speakersEndpoint; // endpoint m_speakers was fetched from
    QString m_voice;            // preferred voice name for ja
    double m_rate = 0.0;
    int m_gen = 0; // invalidates in-flight replies after stop()/new speak()
};
