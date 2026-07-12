#pragma once

#include "tts/TtsEngine.h"

#include <QHash>

class VoicevoxEngine;
class PiperEngine;

// Bundles the OS engine with the local AI engines (VOICEVOX, Piper) behind
// the single TtsEngine interface the rest of the app knows. Voices from the
// AI engines appear in voiceNames() with a "VOICEVOX: " / "Piper: " prefix
// (OS voices stay unprefixed, so existing saved settings keep working), and
// setLocale() routes each utterance to whichever engine owns the preferred
// voice for that language — so e.g. Japanese can go to VOICEVOX while
// English falls back to an OS voice within the same playback.
class CompositeTtsEngine : public TtsEngine {
    Q_OBJECT
public:
    // `system` is the OS engine (may be nullptr when the build/host has
    // none); it is reparented to this object.
    explicit CompositeTtsEngine(TtsEngine *system, QObject *parent = nullptr);

    bool available() const override;
    void setLocale(const QLocale &locale) override;
    void setPreferredVoiceName(const QString &lang, const QString &name) override;
    QStringList voiceNames(const QLocale &locale) override;
    void setRate(double rate) override;
    void speak(const QString &text) override;
    void pause() override;
    void resume() override;
    void stop() override;
    bool canSynthesize() const override;
    void synthesize(const QString &text) override;

private:
    void hookChild(TtsEngine *child);
    TtsEngine *engineForVoiceName(const QString &name) const;

    TtsEngine *m_system = nullptr; // may be null
    VoicevoxEngine *m_voicevox = nullptr;
    PiperEngine *m_piper = nullptr;
    TtsEngine *m_active = nullptr;
    QHash<QString, QString> m_preferredVoice; // primary lang -> prefixed name
};
