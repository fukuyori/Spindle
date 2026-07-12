#include "tts/WavUtil.h"

#include <QtGlobal>

namespace wav_util {

Pcm parseWav(const QByteArray &wav)
{
    Pcm out;
    if (wav.size() < 44 || !wav.startsWith("RIFF") || wav.mid(8, 4) != "WAVE")
        return out;
    auto u16 = [&wav](qsizetype o) {
        return quint16(quint8(wav[o])) | quint16(quint8(wav[o + 1])) << 8;
    };
    auto u32 = [&wav](qsizetype o) {
        return quint32(quint8(wav[o])) | quint32(quint8(wav[o + 1])) << 8
               | quint32(quint8(wav[o + 2])) << 16 | quint32(quint8(wav[o + 3])) << 24;
    };
    bool haveFmt = false;
    qsizetype pos = 12;
    while (pos + 8 <= wav.size()) {
        const QByteArray id = wav.mid(pos, 4);
        const quint32 size = u32(pos + 4);
        const qsizetype body = pos + 8;
        if (id == "fmt " && body + 16 <= wav.size()) {
            const quint16 audioFormat = u16(body);
            const quint16 channels = u16(body + 2);
            const quint32 rate = u32(body + 4);
            const quint16 bits = u16(body + 14);
            if ((audioFormat != 1 && audioFormat != 0xFFFE) || bits != 16 || channels == 0)
                return out;
            out.sampleRate = int(rate);
            out.channels = channels;
            haveFmt = true;
        } else if (id == "data") {
            out.data = wav.mid(body, qMin<qsizetype>(size, wav.size() - body));
        }
        pos = body + size + (size & 1); // chunks are word-aligned
    }
    out.ok = haveFmt && !out.data.isEmpty();
    return out;
}

QByteArray toMono16(const QByteArray &interleaved, int channels)
{
    if (channels <= 1)
        return interleaved;
    const auto *src = reinterpret_cast<const qint16 *>(interleaved.constData());
    const qsizetype frames = interleaved.size() / 2 / channels;
    QByteArray out(frames * 2, Qt::Uninitialized);
    auto *dst = reinterpret_cast<qint16 *>(out.data());
    for (qsizetype i = 0; i < frames; ++i) {
        int acc = 0;
        for (int c = 0; c < channels; ++c)
            acc += src[i * channels + c];
        dst[i] = qint16(acc / channels);
    }
    return out;
}

QByteArray resampleMono16(const QByteArray &mono, int fromRate, int toRate)
{
    if (fromRate == toRate || fromRate <= 0 || toRate <= 0)
        return mono;
    const auto *src = reinterpret_cast<const qint16 *>(mono.constData());
    const qsizetype n = mono.size() / 2;
    if (n < 2)
        return mono;
    const qsizetype outN = qMax<qsizetype>(1, n * toRate / fromRate);
    QByteArray out(outN * 2, Qt::Uninitialized);
    auto *dst = reinterpret_cast<qint16 *>(out.data());
    for (qsizetype i = 0; i < outN; ++i) {
        const double pos = double(i) * fromRate / toRate;
        const qsizetype base = qMin<qsizetype>(qsizetype(pos), n - 2);
        const double frac = pos - double(base);
        dst[i] = qint16(src[base] * (1.0 - frac) + src[base + 1] * frac);
    }
    return out;
}

QByteArray buildWav(int sampleRate, const QByteArray &mono)
{
    QByteArray out;
    out.reserve(44 + mono.size());
    auto p16 = [&out](quint16 v) {
        out.append(char(v & 0xff));
        out.append(char(v >> 8));
    };
    auto p32 = [&out](quint32 v) {
        out.append(char(v & 0xff));
        out.append(char((v >> 8) & 0xff));
        out.append(char((v >> 16) & 0xff));
        out.append(char((v >> 24) & 0xff));
    };
    out.append("RIFF");
    p32(quint32(36 + mono.size()));
    out.append("WAVE");
    out.append("fmt ");
    p32(16);
    p16(1); // PCM
    p16(1); // mono
    p32(quint32(sampleRate));
    p32(quint32(sampleRate * 2)); // byte rate
    p16(2);                       // block align
    p16(16);                      // bits per sample
    out.append("data");
    p32(quint32(mono.size()));
    out.append(mono);
    return out;
}

} // namespace wav_util
