#include "tts/CompositeTtsEngine.h"

#include "tts/PiperEngine.h"
#include "tts/VoicevoxEngine.h"

namespace {
const QLatin1String kVoicevoxPrefix("VOICEVOX: ");
const QLatin1String kPiperPrefix("Piper: ");

QString primaryLang(const QLocale &locale)
{
    return locale.name().section(QLatin1Char('_'), 0, 0);
}
} // namespace

CompositeTtsEngine::CompositeTtsEngine(TtsEngine *system, QObject *parent)
    : TtsEngine(parent)
    , m_system(system)
    , m_voicevox(new VoicevoxEngine(this))
    , m_piper(new PiperEngine(this))
{
    if (m_system)
        m_system->setParent(this);
    hookChild(m_system);
    hookChild(m_voicevox);
    hookChild(m_piper);
    m_active = m_system ? m_system : static_cast<TtsEngine *>(m_voicevox);
}

void CompositeTtsEngine::hookChild(TtsEngine *child)
{
    if (!child)
        return;
    // Only the active child speaks; suppress signals from a child that was
    // switched away from with work still in flight.
    connect(child, &TtsEngine::utteranceFinished, this, [this, child] {
        if (child == m_active)
            emit utteranceFinished();
    });
    connect(child, &TtsEngine::errorOccurred, this, [this, child](const QString &message) {
        if (child == m_active)
            emit errorOccurred(message);
    });
    connect(child, &TtsEngine::synthesized, this,
            [this, child](int sampleRate, const QByteArray &pcm) {
                if (child == m_active)
                    emit synthesized(sampleRate, pcm);
            });
}

bool CompositeTtsEngine::available() const
{
    return m_system || m_voicevox->available() || m_piper->available();
}

TtsEngine *CompositeTtsEngine::engineForVoiceName(const QString &name) const
{
    if (name.startsWith(kVoicevoxPrefix))
        return m_voicevox;
    if (name.startsWith(kPiperPrefix))
        return m_piper;
    return m_system;
}

void CompositeTtsEngine::setPreferredVoiceName(const QString &lang, const QString &name)
{
    if (name.isEmpty())
        m_preferredVoice.remove(lang);
    else
        m_preferredVoice.insert(lang, name);
    // Forward with the routing prefix stripped; the OS engine sees plain names.
    if (name.startsWith(kVoicevoxPrefix))
        m_voicevox->setPreferredVoiceName(lang, name.mid(kVoicevoxPrefix.size()));
    else if (name.startsWith(kPiperPrefix))
        m_piper->setPreferredVoiceName(lang, name.mid(kPiperPrefix.size()));
    else if (m_system)
        m_system->setPreferredVoiceName(lang, name);
}

void CompositeTtsEngine::setLocale(const QLocale &locale)
{
    const QString lang = primaryLang(locale);
    TtsEngine *want = engineForVoiceName(m_preferredVoice.value(lang));
    if (!want) {
        // No OS engine and no explicit choice: pick whichever AI engine can
        // plausibly speak the language (VOICEVOX for Japanese, Piper if it
        // has a model). Errors surface at speak time otherwise.
        if (locale.language() == QLocale::Japanese)
            want = m_voicevox;
        else if (m_piper->available() && !m_piper->voiceNames(locale).isEmpty())
            want = m_piper;
        else
            want = m_voicevox;
    }
    if (want != m_active) {
        m_active->stop();
        m_active = want;
    }
    m_active->setLocale(locale);
}

QStringList CompositeTtsEngine::voiceNames(const QLocale &locale)
{
    QStringList out;
    if (m_system)
        out << m_system->voiceNames(locale);
    const QStringList voicevox = m_voicevox->voiceNames(locale);
    for (const QString &name : voicevox)
        out << kVoicevoxPrefix + name;
    const QStringList piper = m_piper->voiceNames(locale);
    for (const QString &name : piper)
        out << kPiperPrefix + name;
    return out;
}

void CompositeTtsEngine::setRate(double rate)
{
    if (m_system)
        m_system->setRate(rate);
    m_voicevox->setRate(rate);
    m_piper->setRate(rate);
}

void CompositeTtsEngine::speak(const QString &text)
{
    m_active->speak(text);
}

bool CompositeTtsEngine::canSynthesize() const
{
    return m_active->canSynthesize();
}

void CompositeTtsEngine::synthesize(const QString &text)
{
    m_active->synthesize(text);
}

void CompositeTtsEngine::pause()
{
    m_active->pause();
}

void CompositeTtsEngine::resume()
{
    m_active->resume();
}

void CompositeTtsEngine::stop()
{
    // Stop every child, not just the active one — a previous utterance's
    // engine may still be synthesizing after a mid-playback switch.
    if (m_system)
        m_system->stop();
    m_voicevox->stop();
    m_piper->stop();
}
