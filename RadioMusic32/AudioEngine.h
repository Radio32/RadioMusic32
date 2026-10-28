#ifndef AudioEngine_h
#define AudioEngine_h

#include <Audio.h>
#include "AudioEffectLoFi.h"
#include "SDPlayPCM.h"
#include "AudioFileInfo.h"

#define MAX_ERRORS  10
#define VOICE_COUNT  2

class AudioEngine {
public:
    SDPlayPCM playRaw1;
    SDPlayPCM playRaw2;

    AudioEffectEnvelope env1;
    AudioEffectEnvelope env2;

    AudioMixer4             voiceMixer;
    AudioEffectLoFi         lofi;
    AudioFilterStateVariable lowPass;
    AudioAnalyzePeak        peak1;
    AudioOutputAnalog       dac1;

    AudioConnection patchCord1;
    AudioConnection patchCord2;
    AudioConnection patchCord3;
    AudioConnection patchCord4;
    AudioConnection patchCord5;
    AudioConnection patchCord6;
    AudioConnection patchCord7;
    AudioConnection peakPatch;

    AudioEngine() :
        patchCord1(playRaw1, env1),
        patchCord2(playRaw2, env2),
        patchCord3(env1, 0, voiceMixer, 0),
        patchCord4(env2, 0, voiceMixer, 1),
        patchCord5(voiceMixer, 0, lofi, 0),
        patchCord6(lofi, 0, lowPass, 0),
        patchCord7(lowPass, 0, dac1, 0),
        peakPatch(lowPass, 0, peak1, 0)
    {
        playRaw1.playerID = 1;
        playRaw2.playerID = 2;
    }

    void  init(float lofiCutoff, float lofiResonance, uint16_t lofiSampleRate);
    bool  update();
    bool  triggerVoice(uint8_t voiceIndex, AudioFileInfo* fileInfo);
    void  setVoiceVolume(uint8_t voiceIndex, float gain);
    void  setVoiceDecayMs(uint8_t voiceIndex, uint16_t decayMs, bool oneShot);
    void  setVoicePlaybackSpeed(uint8_t voiceIndex, float speed);
    void  setMasterFilter(float cutoff, float resonance);
    void  setLofiEnabled(bool enabled);
    void  setLofiSampleRate(uint16_t sampleRate);
    float getPeak();

    bool error = false;

private:
    AudioFileInfo*    currentFileInfo[VOICE_COUNT] = {NULL, NULL};
    AudioEffectEnvelope* envelopes[VOICE_COUNT]    = {&env1, &env2};
    SDPlayPCM*           players[VOICE_COUNT]      = {&playRaw1, &playRaw2};
    float voiceVolume[VOICE_COUNT]                 = {0.8f, 0.8f};
};

#endif
