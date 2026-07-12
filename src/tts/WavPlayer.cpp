#include "tts/WavPlayer.h"

#include "tts/WavUtil.h"

#include <QAudioFormat>
#include <QAudioSink>

WavPlayer::WavPlayer(QObject *parent)
    : QObject(parent)
{
}

void WavPlayer::play(const QByteArray &wav)
{
    if (m_suspended) {
        m_pendingWav = wav;
        return;
    }
    reallyPlay(wav);
}

void WavPlayer::reallyPlay(const QByteArray &wav)
{
    teardownSink();
    const wav_util::Pcm data = wav_util::parseWav(wav);
    if (!data.ok) {
        emit errorOccurred(QStringLiteral("音声データを再生できません (16-bit PCM WAV 以外)"));
        return;
    }
    QAudioFormat format;
    format.setSampleRate(data.sampleRate);
    format.setChannelCount(data.channels);
    format.setSampleFormat(QAudioFormat::Int16);
    m_pcm = data.data;
    m_buffer.setData(m_pcm);
    m_buffer.open(QIODevice::ReadOnly);
    m_sink = new QAudioSink(format, this);
    connect(m_sink, &QAudioSink::stateChanged, this, [this](QAudio::State state) {
        // Idle = the sink drained the buffer: the utterance finished on its own.
        if (state == QAudio::IdleState && m_expectFinish) {
            m_expectFinish = false;
            teardownSink();
            emit finished();
        }
    });
    m_expectFinish = true;
    m_sink->start(&m_buffer);
    if (m_sink->error() != QAudio::NoError) {
        m_expectFinish = false;
        teardownSink();
        emit errorOccurred(QStringLiteral("音声出力を開始できません"));
    }
}

void WavPlayer::pause()
{
    m_suspended = true;
    if (m_sink)
        m_sink->suspend();
}

void WavPlayer::resume()
{
    m_suspended = false;
    if (!m_pendingWav.isEmpty()) {
        const QByteArray wav = m_pendingWav;
        m_pendingWav.clear();
        reallyPlay(wav);
    } else if (m_sink) {
        m_sink->resume();
    }
}

void WavPlayer::stop()
{
    m_suspended = false;
    m_pendingWav.clear();
    m_expectFinish = false;
    teardownSink();
}

void WavPlayer::teardownSink()
{
    if (m_sink) {
        m_sink->stop();
        m_sink->deleteLater();
        m_sink = nullptr;
    }
    if (m_buffer.isOpen())
        m_buffer.close();
}
