#pragma once

#include <QLocale>
#include <QObject>
#include <QString>
#include <QStringList>

// Speech-synthesis engine abstraction. The default implementation wraps
// Qt TextToSpeech (OS voices: WinRT/SAPI, AVSpeechSynthesizer,
// speech-dispatcher); further engines (VOICEVOX, Piper, ...) can be added
// behind this interface without touching the playback logic in TtsController.
//
// One utterance at a time: speak() replaces whatever is playing, and exactly
// one utteranceFinished() is emitted when it completes on its own (never for
// stop()).
class TtsEngine : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

    virtual bool available() const = 0;
    // Select the output language. The engine picks a voice for the locale
    // (honoring a preferred voice name if one was set for that language).
    virtual void setLocale(const QLocale &locale) = 0;
    // Preferred voice for a primary language subtag ("ja", "en", ...);
    // empty name = engine default.
    virtual void setPreferredVoiceName(const QString &lang, const QString &name) = 0;
    // Voice names selectable for `locale` (for the settings dialog).
    virtual QStringList voiceNames(const QLocale &locale) = 0;
    virtual void setRate(double rate) = 0; // -1.0 (slowest) .. 1.0 (fastest)
    virtual void speak(const QString &text) = 0;
    virtual void pause() = 0;
    virtual void resume() = 0;
    virtual void stop() = 0;

signals:
    void utteranceFinished();
    void errorOccurred(const QString &message);
};

// System TTS engine, or nullptr when this build has no speech backend.
TtsEngine *createTtsEngine(QObject *parent = nullptr);
