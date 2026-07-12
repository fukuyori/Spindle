#pragma once

#include "tts/TtsEngine.h"

#include <QAudioFormat>
#include <QHash>
#include <QVector>

class QTextToSpeech;

// Qt TextToSpeech (OS native voices) implementation of TtsEngine.
//
// One QTextToSpeech instance per OS speech engine: a single engine often
// doesn't carry every language (e.g. Windows machines where OneCore/WinRT has
// only Japanese voices while English lives in legacy SAPI), so setLocale()
// searches a catalog built across all engines and switches the active
// instance to whichever engine actually has a voice for the language.
class QtTtsEngine : public TtsEngine {
    Q_OBJECT
public:
    explicit QtTtsEngine(QObject *parent = nullptr);

    bool available() const override;
    void setLocale(const QLocale &locale) override;
    void setPreferredVoiceName(const QString &lang, const QString &name) override;
    QStringList voiceNames(const QLocale &locale) override;
    void setRate(double rate) override;
    void speak(const QString &text) override;
    void pause() override;
    void resume() override;
    void stop() override;
    bool canSynthesize() const override; // engine capability of the active instance
    void synthesize(const QString &text) override;

private:
    struct VoiceEntry {
        QTextToSpeech *tts;
        QLocale locale;
        QString name;
    };

    void hookInstance(QTextToSpeech *tts);
    // All voices of a primary language ("ja", "en", ...) across every engine,
    // in engine-priority order. Built once per language and cached.
    const QVector<VoiceEntry> &catalogFor(const QString &lang);
    static QString primaryLang(const QLocale &locale);

    QVector<QTextToSpeech *> m_instances; // one per usable OS engine
    QTextToSpeech *m_active = nullptr;
    QHash<QString, QVector<VoiceEntry>> m_catalog;
    QHash<QString, QString> m_preferredVoice; // primary lang subtag -> voice name
    double m_rate = 0.0;
    // True while an utterance we started is in flight; a Speaking -> Ready
    // transition then means "finished on its own" (stop() clears the flag so
    // the same transition after a stop stays silent).
    bool m_expectFinish = false;
    // Offline synthesis in flight: QTextToSpeech::synthesized delivers PCM in
    // chunks which are accumulated here until the state returns to Ready,
    // then converted to 16-bit mono in one go.
    bool m_synthesizing = false;
    QByteArray m_synthPcm;
    QAudioFormat m_synthFormat;
};
