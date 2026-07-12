#pragma once

#include <QLocale>
#include <QObject>
#include <QString>

class TtsEngine;

// Block-by-block read-aloud of the current chapter. The controller owns
// playback order and state, but not the text: block text lives in the
// WebEngine DOM, so on textRequested(gen, index, side) the owner fetches it
// (async runJavaScript) and calls provideText() back with the same triple.
// `gen` guards against stale JS callbacks after a stop/restart.
//
// One side is read per playback: the original text, or the translation
// (Mode::Translation), block by block in document order. An empty provided
// text skips that block and advances.
class TtsController : public QObject {
    Q_OBJECT
public:
    enum class State { Idle, Speaking, Paused };
    enum class Side { Original, Translation };
    enum class Mode { Original, Translation };

    explicit TtsController(TtsEngine *engine, QObject *parent = nullptr);

    State state() const { return m_state; }
    int currentIndex() const { return m_index; }

    void setRate(double rate);                    // -1..1
    void setOriginalLocale(const QLocale &locale) { m_originalLocale = locale; }

    void play(int blockCount, int startIndex, Mode mode);
    void pause();
    void resume();
    void stop();

    // Response to textRequested. `lang` is the text's language code when known
    // ("" = the original side's language, e.g. a fallback to the source text).
    void provideText(int gen, int index, Side side, const QString &text, const QString &lang);

signals:
    void textRequested(int gen, int index, Side side);
    void positionChanged(int index, Side side); // an utterance actually started
    void stateChanged();
    void finished(); // reached the chapter end on its own (stop() stays silent)
    void errorOccurred(const QString &message);

private:
    void requestCurrent();
    void advance();
    void speakNow(const QString &text, const QLocale &locale);
    void setState(State state);

    TtsEngine *m_engine = nullptr;
    State m_state = State::Idle;
    Mode m_mode = Mode::Original;
    Side m_side = Side::Original;
    int m_index = 0;
    int m_count = 0;
    int m_gen = 0;
    double m_rate = 0.0;
    QLocale m_originalLocale;
    // Text that arrived while paused: spoken on resume() instead of engine
    // resume (the engine never saw it).
    bool m_pendingValid = false;
    QString m_pendingText;
    QLocale m_pendingLocale;
};
