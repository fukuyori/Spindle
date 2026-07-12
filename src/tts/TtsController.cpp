#include "tts/TtsController.h"

#include "tts/TtsEngine.h"

TtsController::TtsController(TtsEngine *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
{
    if (m_engine) {
        connect(m_engine, &TtsEngine::utteranceFinished, this, [this] {
            if (m_state != State::Idle)
                advance();
        });
        connect(m_engine, &TtsEngine::errorOccurred, this, [this](const QString &message) {
            if (m_state == State::Idle)
                return;
            stop();
            emit errorOccurred(message);
        });
    }
}

void TtsController::setRate(double rate)
{
    m_rate = qBound(-1.0, rate, 1.0);
    if (m_engine)
        m_engine->setRate(m_rate);
}

void TtsController::play(int blockCount, int startIndex, Mode mode)
{
    if (!m_engine || blockCount <= 0)
        return;
    m_engine->stop();
    m_count = blockCount;
    m_index = qBound(0, startIndex, blockCount - 1);
    m_mode = mode;
    m_side = mode == Mode::Translation ? Side::Translation : Side::Original; // fixed per playback
    m_pendingValid = false;
    m_engine->setRate(m_rate);
    setState(State::Speaking);
    requestCurrent();
}

void TtsController::pause()
{
    if (m_state != State::Speaking)
        return;
    setState(State::Paused);
    m_engine->pause(); // no-op when between utterances (awaiting text)
}

void TtsController::resume()
{
    if (m_state != State::Paused)
        return;
    setState(State::Speaking);
    if (m_pendingValid) {
        // The text arrived while paused; the engine never saw it.
        m_pendingValid = false;
        speakNow(m_pendingText, m_pendingLocale);
    } else {
        m_engine->resume();
    }
}

void TtsController::stop()
{
    ++m_gen; // invalidate any in-flight text callback
    m_pendingValid = false;
    if (m_state == State::Idle)
        return;
    m_engine->stop();
    setState(State::Idle);
}

void TtsController::provideText(int gen, int index, Side side, const QString &text,
                                const QString &lang)
{
    if (m_state == State::Idle || gen != m_gen || index != m_index || side != m_side)
        return; // stale or superseded
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        advance(); // nothing to say for this side of the block
        return;
    }
    const QLocale locale = lang.isEmpty() ? m_originalLocale : QLocale(lang);
    if (m_state == State::Paused) {
        m_pendingValid = true;
        m_pendingText = trimmed;
        m_pendingLocale = locale;
        return;
    }
    speakNow(trimmed, locale);
}

void TtsController::requestCurrent()
{
    emit textRequested(++m_gen, m_index, m_side);
}

void TtsController::advance()
{
    if (m_state == State::Idle)
        return;
    ++m_index;
    if (m_index >= m_count) {
        setState(State::Idle);
        emit finished();
        return;
    }
    requestCurrent();
}

void TtsController::speakNow(const QString &text, const QLocale &locale)
{
    m_engine->setLocale(locale);
    emit positionChanged(m_index, m_side);
    m_engine->speak(text);
}

void TtsController::setState(State state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}
