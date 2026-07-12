#pragma once

#include "tts/TtsEngine.h"
#include "tts/WavPlayer.h"

#include <QHash>

#include <functional>

class QProcess;

// Piper (https://github.com/rhasspy/piper) — local multi-language neural TTS
// run as a subprocess: text on stdin, WAV to a temp file. Voices are .onnx
// model files in a user-chosen folder, named "<lang>_<REGION>-<name>-<quality>"
// (e.g. en_US-lessac-medium), which is how voiceNames() maps them to
// languages. Executable / voices-folder paths are read from QSettings
// (tts/piperExe, tts/piperVoicesDir) on each use.
class PiperEngine : public TtsEngine {
    Q_OBJECT
public:
    explicit PiperEngine(QObject *parent = nullptr);

    bool available() const override;
    void setLocale(const QLocale &locale) override;
    void setPreferredVoiceName(const QString &lang, const QString &name) override;
    QStringList voiceNames(const QLocale &locale) override;
    void setRate(double rate) override { m_rate = rate; }
    void speak(const QString &text) override;
    void pause() override { m_player.pause(); }
    void resume() override { m_player.resume(); }
    void stop() override;
    bool canSynthesize() const override { return available(); }
    void synthesize(const QString &text) override;

private:
    static QString exePath();    // from QSettings
    static QString voicesDir();  // from QSettings
    static QStringList modelsFor(const QString &lang); // model base names
    QString resolveModelPath(const QString &lang) const;
    // Shared piper-subprocess run behind speak()/synthesize(); calls
    // `done(wav)` on success, emits errorOccurred and drops otherwise.
    void requestWav(const QString &text, std::function<void(QByteArray)> done);
    void killProcess();

    WavPlayer m_player;
    QProcess *m_proc = nullptr;
    QHash<QString, QString> m_preferredVoice; // primary lang -> model base name
    QString m_lang;                           // current utterance language
    double m_rate = 0.0;
    int m_gen = 0; // invalidates the running process's result after stop()
};
