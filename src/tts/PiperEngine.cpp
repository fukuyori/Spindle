#include "tts/PiperEngine.h"

#include "tts/WavUtil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QUuid>

#include <cmath>

PiperEngine::PiperEngine(QObject *parent)
    : TtsEngine(parent)
{
    connect(&m_player, &WavPlayer::finished, this, &TtsEngine::utteranceFinished);
    connect(&m_player, &WavPlayer::errorOccurred, this, &TtsEngine::errorOccurred);
}

QString PiperEngine::exePath()
{
    return QSettings().value(QStringLiteral("tts/piperExe")).toString().trimmed();
}

QString PiperEngine::voicesDir()
{
    return QSettings().value(QStringLiteral("tts/piperVoicesDir")).toString().trimmed();
}

bool PiperEngine::available() const
{
    const QString exe = exePath();
    return !exe.isEmpty() && QFileInfo::exists(exe);
}

void PiperEngine::setLocale(const QLocale &locale)
{
    m_lang = locale.name().section(QLatin1Char('_'), 0, 0);
}

void PiperEngine::setPreferredVoiceName(const QString &lang, const QString &name)
{
    if (name.isEmpty())
        m_preferredVoice.remove(lang);
    else
        m_preferredVoice.insert(lang, name);
}

QStringList PiperEngine::modelsFor(const QString &lang)
{
    QStringList out;
    const QString dir = voicesDir();
    if (dir.isEmpty())
        return out;
    const QFileInfoList files =
        QDir(dir).entryInfoList({QStringLiteral("*.onnx")}, QDir::Files, QDir::Name);
    for (const QFileInfo &info : files) {
        const QString base = info.completeBaseName(); // "ja_JP-something-medium"
        if (base.section(QLatin1Char('_'), 0, 0).compare(lang, Qt::CaseInsensitive) == 0)
            out << base;
    }
    return out;
}

QStringList PiperEngine::voiceNames(const QLocale &locale)
{
    if (!available())
        return {};
    return modelsFor(locale.name().section(QLatin1Char('_'), 0, 0));
}

QString PiperEngine::resolveModelPath(const QString &lang) const
{
    const QDir dir(voicesDir());
    const QString preferred = m_preferredVoice.value(lang);
    if (!preferred.isEmpty()) {
        const QString path = dir.filePath(preferred + QStringLiteral(".onnx"));
        if (QFileInfo::exists(path))
            return path;
    }
    const QStringList models = modelsFor(lang);
    return models.isEmpty() ? QString() : dir.filePath(models.first() + QStringLiteral(".onnx"));
}

void PiperEngine::speak(const QString &text)
{
    m_player.stop();
    requestWav(text, [this](const QByteArray &wav) { m_player.play(wav); });
}

void PiperEngine::synthesize(const QString &text)
{
    requestWav(text, [this](const QByteArray &wav) {
        const wav_util::Pcm pcm = wav_util::parseWav(wav);
        if (!pcm.ok) {
            emit errorOccurred(QStringLiteral("Piper の音声データを解析できません"));
            return;
        }
        emit synthesized(pcm.sampleRate, wav_util::toMono16(pcm.data, pcm.channels));
    });
}

void PiperEngine::requestWav(const QString &text, std::function<void(QByteArray)> done)
{
    const int gen = ++m_gen;
    killProcess();

    const QString exe = exePath();
    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
        emit errorOccurred(QStringLiteral("Piper 実行ファイルが見つかりません（音声設定で指定してください）"));
        return;
    }
    const QString model = resolveModelPath(m_lang);
    if (model.isEmpty()) {
        emit errorOccurred(
            QStringLiteral("言語 \"%1\" の Piper 音声モデル (.onnx) が見つかりません").arg(m_lang));
        return;
    }

    const QString outFile = QDir::temp().filePath(
        QStringLiteral("spindle-piper-%1.wav")
            .arg(QUuid::createUuid().toString(QUuid::Id128)));
    m_proc = new QProcess(this);
    // Piper's espeak-ng data ships next to the executable; run from there so
    // its relative lookup works.
    m_proc->setWorkingDirectory(QFileInfo(exe).absolutePath());
    QStringList args{QStringLiteral("-m"), model, QStringLiteral("-f"), outFile};
    // rate -1..1 -> length_scale 2..0.5 (piper scales duration, not speed)
    args << QStringLiteral("--length_scale") << QString::number(std::pow(2.0, -m_rate));

    QProcess *proc = m_proc;
    connect(proc, &QProcess::errorOccurred, this, [this, gen, proc](QProcess::ProcessError) {
        if (gen != m_gen)
            return;
        proc->deleteLater();
        if (m_proc == proc)
            m_proc = nullptr;
        emit errorOccurred(QStringLiteral("Piper を起動できません: ") + proc->program());
    });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, gen, proc, outFile, done](int exitCode, QProcess::ExitStatus status) {
                proc->deleteLater();
                if (m_proc == proc)
                    m_proc = nullptr;
                QFile file(outFile);
                if (gen != m_gen) {
                    file.remove();
                    return;
                }
                if (status != QProcess::NormalExit || exitCode != 0) {
                    file.remove();
                    const QString err =
                        QString::fromLocal8Bit(proc->readAllStandardError()).trimmed();
                    emit errorOccurred(QStringLiteral("Piper が失敗しました")
                                       + (err.isEmpty() ? QString()
                                                        : QStringLiteral(": ") + err.right(200)));
                    return;
                }
                QByteArray wav;
                if (file.open(QIODevice::ReadOnly)) {
                    wav = file.readAll();
                    file.close();
                }
                file.remove();
                if (wav.isEmpty()) {
                    emit errorOccurred(QStringLiteral("Piper が音声を出力しませんでした"));
                    return;
                }
                done(wav);
            });
    proc->start(exe, args);
    proc->write(text.toUtf8());
    proc->write("\n");
    proc->closeWriteChannel();
}

void PiperEngine::stop()
{
    ++m_gen; // orphan the running process's result
    killProcess();
    m_player.stop();
}

void PiperEngine::killProcess()
{
    if (!m_proc)
        return;
    QProcess *proc = m_proc;
    m_proc = nullptr;
    if (proc->state() != QProcess::NotRunning)
        proc->kill(); // its finished handler cleans up (gen mismatch path)
}
