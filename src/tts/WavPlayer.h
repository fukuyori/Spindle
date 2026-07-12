#pragma once

#include <QByteArray>
#include <QBuffer>
#include <QObject>

class QAudioSink;

// Plays a WAV byte buffer (16-bit PCM) through the default audio output.
// One utterance at a time — play() replaces whatever is playing. finished()
// is emitted when playback drains on its own (never for stop()).
//
// pause() works even between play() calls: audio handed to play() while
// paused is held back and starts on resume(). This matters for the local AI
// engines, where the user can pause while a paragraph is still being
// synthesized — the result must not start sounding under a paused UI.
class WavPlayer : public QObject {
    Q_OBJECT
public:
    explicit WavPlayer(QObject *parent = nullptr);

    void play(const QByteArray &wav);
    void pause();
    void resume();
    void stop();

signals:
    void finished();
    void errorOccurred(const QString &message);

private:
    void reallyPlay(const QByteArray &wav);
    void teardownSink();

    QAudioSink *m_sink = nullptr;
    QBuffer m_buffer;
    QByteArray m_pcm;
    QByteArray m_pendingWav; // arrived while paused
    bool m_suspended = false;
    bool m_expectFinish = false;
};
