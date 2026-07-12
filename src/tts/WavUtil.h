#pragma once

#include <QByteArray>

// Small PCM/WAV helpers shared by the TTS engines: playback parses engine
// output (WavPlayer), and the audio-file export needs to normalize utterances
// from different engines (24 kHz VOICEVOX, 22.05 kHz Piper, whatever the OS
// voice emits) into one stream. 16-bit PCM throughout.
namespace wav_util {

struct Pcm {
    int sampleRate = 0;
    int channels = 0;
    QByteArray data; // 16-bit interleaved samples
    bool ok = false;
};

// Parse a RIFF/WAVE buffer (16-bit PCM only — what VOICEVOX and Piper emit).
Pcm parseWav(const QByteArray &wav);

// Downmix interleaved 16-bit PCM to mono by averaging channels.
QByteArray toMono16(const QByteArray &interleaved, int channels);

// Linear-interpolation resample of 16-bit mono PCM.
QByteArray resampleMono16(const QByteArray &mono, int fromRate, int toRate);

// A complete WAV file (16-bit PCM mono) around the samples.
QByteArray buildWav(int sampleRate, const QByteArray &mono);

} // namespace wav_util
