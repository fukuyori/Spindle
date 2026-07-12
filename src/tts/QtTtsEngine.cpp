#include "tts/QtTtsEngine.h"

#include "tts/WavUtil.h"

#include <QTextToSpeech>
#include <QVoice>

namespace {

// Normalize a QTextToSpeech synthesis buffer to 16-bit samples.
QByteArray toInt16(const QAudioFormat &format, const QByteArray &raw)
{
    switch (format.sampleFormat()) {
    case QAudioFormat::Int16:
        return raw;
    case QAudioFormat::Float: {
        const auto *src = reinterpret_cast<const float *>(raw.constData());
        const qsizetype n = raw.size() / 4;
        QByteArray out(n * 2, Qt::Uninitialized);
        auto *dst = reinterpret_cast<qint16 *>(out.data());
        for (qsizetype i = 0; i < n; ++i)
            dst[i] = qint16(qBound(-1.0f, src[i], 1.0f) * 32767.0f);
        return out;
    }
    case QAudioFormat::Int32: {
        const auto *src = reinterpret_cast<const qint32 *>(raw.constData());
        const qsizetype n = raw.size() / 4;
        QByteArray out(n * 2, Qt::Uninitialized);
        auto *dst = reinterpret_cast<qint16 *>(out.data());
        for (qsizetype i = 0; i < n; ++i)
            dst[i] = qint16(src[i] >> 16);
        return out;
    }
    case QAudioFormat::UInt8: {
        const auto *src = reinterpret_cast<const quint8 *>(raw.constData());
        const qsizetype n = raw.size();
        QByteArray out(n * 2, Qt::Uninitialized);
        auto *dst = reinterpret_cast<qint16 *>(out.data());
        for (qsizetype i = 0; i < n; ++i)
            dst[i] = qint16((int(src[i]) - 128) << 8);
        return out;
    }
    default:
        return QByteArray();
    }
}

} // namespace

QtTtsEngine::QtTtsEngine(QObject *parent)
    : TtsEngine(parent)
{
    const QStringList engines = QTextToSpeech::availableEngines();
    for (const QString &name : engines) {
        if (name == QLatin1String("mock"))
            continue; // reports Ready but produces no sound
        auto *tts = new QTextToSpeech(name, this);
        if (tts->state() == QTextToSpeech::Error) {
            delete tts;
            continue;
        }
        hookInstance(tts);
        m_instances.append(tts);
    }
    m_active = m_instances.value(0, nullptr);
}

void QtTtsEngine::hookInstance(QTextToSpeech *tts)
{
    connect(tts, &QTextToSpeech::stateChanged, this, [this, tts](QTextToSpeech::State state) {
        if (tts != m_active || state != QTextToSpeech::Ready)
            return;
        if (m_expectFinish) {
            m_expectFinish = false;
            emit utteranceFinished();
        }
        if (m_synthesizing) {
            m_synthesizing = false;
            const QByteArray int16 = toInt16(m_synthFormat, m_synthPcm);
            m_synthPcm.clear();
            if (int16.isEmpty()) {
                emit errorOccurred(QStringLiteral("OS 音声の合成データを変換できません"));
                return;
            }
            emit synthesized(m_synthFormat.sampleRate(),
                             wav_util::toMono16(int16, m_synthFormat.channelCount()));
        }
    });
    connect(tts, &QTextToSpeech::errorOccurred, this,
            [this, tts](QTextToSpeech::ErrorReason, const QString &message) {
                if (tts != m_active)
                    return;
                m_expectFinish = false;
                m_synthesizing = false;
                emit errorOccurred(message);
            });
}

bool QtTtsEngine::available() const
{
    return !m_instances.isEmpty();
}

QString QtTtsEngine::primaryLang(const QLocale &locale)
{
    return locale.name().section(QLatin1Char('_'), 0, 0);
}

const QVector<QtTtsEngine::VoiceEntry> &QtTtsEngine::catalogFor(const QString &lang)
{
    const auto it = m_catalog.constFind(lang);
    if (it != m_catalog.constEnd())
        return *it;
    QVector<VoiceEntry> entries;
    for (QTextToSpeech *tts : std::as_const(m_instances)) {
        // availableVoices() only lists the current locale's voices, so walk the
        // engine's locales of this language. Only runs once per language (and
        // never mid-utterance: setLocale/voiceNames callers are between
        // utterances or in the settings dialog).
        const QLocale saved = tts->locale();
        const QList<QLocale> locales = tts->availableLocales();
        for (const QLocale &loc : locales) {
            if (primaryLang(loc) != lang)
                continue;
            tts->setLocale(loc);
            const QList<QVoice> voices = tts->availableVoices();
            for (const QVoice &v : voices)
                entries.append({tts, loc, v.name()});
        }
        tts->setLocale(saved);
    }
    return *m_catalog.insert(lang, entries);
}

void QtTtsEngine::setLocale(const QLocale &locale)
{
    if (m_instances.isEmpty())
        return;
    const QString lang = primaryLang(locale);
    const QVector<VoiceEntry> &entries = catalogFor(lang);
    if (entries.isEmpty())
        return; // no voice for this language anywhere: keep the current voice
                // (mispronounced beats silent)
    const QString preferred = m_preferredVoice.value(lang);
    const VoiceEntry *chosen = nullptr;
    int best = -1;
    for (const VoiceEntry &e : entries) {
        int score = 0;
        if (!preferred.isEmpty() && e.name == preferred)
            score += 8; // the user's picked voice wins wherever it lives
        if (e.locale == locale)
            score += 4; // exact locale over same-language
        if (e.tts == m_active)
            score += 2; // don't switch engines without a reason
        if (score > best) {
            best = score;
            chosen = &e;
        }
    }
    if (chosen->tts != m_active) {
        m_active->stop();
        m_active = chosen->tts;
        m_active->setRate(m_rate);
    }
    m_active->setLocale(chosen->locale);
    if (m_active->voice().name() != chosen->name) {
        const QList<QVoice> voices = m_active->availableVoices();
        for (const QVoice &v : voices) {
            if (v.name() == chosen->name) {
                m_active->setVoice(v);
                break;
            }
        }
    }
}

void QtTtsEngine::setPreferredVoiceName(const QString &lang, const QString &name)
{
    if (name.isEmpty())
        m_preferredVoice.remove(lang);
    else
        m_preferredVoice.insert(lang, name);
}

QStringList QtTtsEngine::voiceNames(const QLocale &locale)
{
    QStringList out;
    const QVector<VoiceEntry> &entries = catalogFor(primaryLang(locale));
    for (const VoiceEntry &e : entries)
        if (!out.contains(e.name))
            out << e.name;
    return out;
}

void QtTtsEngine::setRate(double rate)
{
    m_rate = rate;
    if (m_active)
        m_active->setRate(rate);
}

void QtTtsEngine::speak(const QString &text)
{
    if (!m_active)
        return;
    m_expectFinish = true;
    m_active->say(text);
}

void QtTtsEngine::pause()
{
    if (m_active)
        m_active->pause();
}

void QtTtsEngine::resume()
{
    if (m_active)
        m_active->resume();
}

void QtTtsEngine::stop()
{
    m_expectFinish = false;
    m_synthesizing = false;
    m_synthPcm.clear();
    if (m_active)
        m_active->stop();
}

bool QtTtsEngine::canSynthesize() const
{
    return m_active
           && m_active->engineCapabilities().testFlag(
               QTextToSpeech::Capability::Synthesize);
}

void QtTtsEngine::synthesize(const QString &text)
{
    if (!canSynthesize()) {
        emit errorOccurred(
            QStringLiteral("この OS 音声はファイル書き出しに対応していません（%1）")
                .arg(m_active ? m_active->engine() : QString()));
        return;
    }
    m_synthesizing = true;
    m_synthPcm.clear();
    // Chunked delivery; completion is the state returning to Ready (handled
    // in hookInstance's stateChanged handler).
    QTextToSpeech *tts = m_active;
    m_active->synthesize(text, this,
                         [this, tts](const QAudioFormat &format, const QByteArray &data) {
                             if (tts != m_active || !m_synthesizing)
                                 return;
                             m_synthFormat = format;
                             m_synthPcm += data;
                         });
}
