#include "tts/TtsEngine.h"

#ifdef SPINDLE_HAS_QT_TTS
#include "tts/QtTtsEngine.h"
#endif

TtsEngine *createTtsEngine(QObject *parent)
{
#ifdef SPINDLE_HAS_QT_TTS
    auto *engine = new QtTtsEngine(parent);
    if (engine->available())
        return engine;
    delete engine;
#else
    Q_UNUSED(parent);
#endif
    return nullptr;
}
