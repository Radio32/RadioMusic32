#ifndef AudioEngine_h
#define AudioEngine_h

#include <Audio.h>
#include "SDPlayPCM.h"
#include "AudioFileInfo.h"

#define MAX_ERRORS   10
#define VOICE_COUNT   2

class AudioEngine {
public:
    // ── sources (S950 record/playback filtering lives inside the players) ────
    SDPlayPCM            playRaw1;
    SDPlayPCM            playRaw2;
    AudioEffectEnvelope  env1;
    AudioEffectEnvelope  env2;

    // ── master bus: mix → DAC ────────────────────────────────────────────────
    AudioMixer4              voiceMixer;
    AudioAnalyzePeak         peak1;
    AudioOutputAnalog        dac1;

    AudioConnection pc1, pc2, pc3, pc4, pc5, pc6;

    AudioEngine() :
        pc1(playRaw1,    env1),
        pc2(playRaw2,    env2),
        pc3(env1,    0,  voiceMixer, 0),
        pc4(env2,    0,  voiceMixer, 1),
        pc5(voiceMixer, 0, dac1,  0),
        pc6(voiceMixer, 0, peak1, 0)
    {
        playRaw1.playerID = 1;
        playRaw2.playerID = 2;
    }

    void  init();
    bool  update();
    bool  triggerVoice(uint8_t v, AudioFileInfo* fi);
    void  setVoiceVolume(uint8_t v, float gain);
    void  setVoiceDecayMs(uint8_t v, uint16_t ms, bool oneShot);
    void  setVoicePlaybackSpeed(uint8_t v, float speed);
    void  setEmulation(bool enabled, uint16_t sampleRate);
    float getPeak();

    bool error = false;

private:
    AudioEffectEnvelope*     envelopes[VOICE_COUNT] = {&env1, &env2};
    SDPlayPCM*               players[VOICE_COUNT]   = {&playRaw1, &playRaw2};
    float                    voiceGain[VOICE_COUNT]  = {0.8f, 0.8f};
};

#endif
