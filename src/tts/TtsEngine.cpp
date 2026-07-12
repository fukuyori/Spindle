#include "tts/TtsEngine.h"

#ifdef SPINDLE_HAS_QT_TTS
#include "tts/QtTtsEngine.h"
#endif
#ifdef SPINDLE_HAS_LOCAL_TTS
#include "tts/CompositeTtsEngine.h"
#endif

TtsEngine *createTtsEngine(QObject *parent)
{
    TtsEngine *system = nullptr;
#ifdef SPINDLE_HAS_QT_TTS
    auto *qt = new QtTtsEngine(nullptr);
    if (qt->available())
        system = qt;
    else
        delete qt;
#endif
#ifdef SPINDLE_HAS_LOCAL_TTS
    // OS voices plus the local AI engines (VOICEVOX / Piper) behind one facade.
    return new CompositeTtsEngine(system, parent);
#else
    if (system)
        system->setParent(parent);
    return system;
#endif
}
