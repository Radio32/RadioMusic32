#ifndef AudioEngine_h
#define AudioEngine_h

#include <Audio.h>
#include "AudioEffectLoFi.h"
#include "SDPlayPCM.h"
#include "AudioFileInfo.h"

#define MAX_ERRORS   10
#define VOICE_COUNT   2

class AudioEngine {
public:
    // ── sources ──────────────────────────────────────────────────────────────
    SDPlayPCM            playRaw1;
    SDPlayPCM            playRaw2;
    AudioEffectEnvelope  env1;
    AudioEffectEnvelope  env2;

    // ── per-voice resonant filters ───────────────────────────────────────────
    AudioFilterStateVariable filter1;   // voice 1
    AudioFilterStateVariable filter2;   // voice 2

    // ── master bus: mix → lo-fi S950 stage → DAC ─────────────────────────────
    AudioMixer4              voiceMixer;
    AudioEffectLoFi          lofi;
    AudioAnalyzePeak         peak1;
    AudioOutputAnalog        dac1;

    AudioConnection pc1, pc2, pc3, pc4, pc5, pc6, pc7, pc8, pc9, pc10;

    AudioEngine() :
        pc1(playRaw1,    env1),
        pc2(playRaw2,    env2),
        pc3(env1,    0,  filter1, 0),
        pc4(env2,    0,  filter2, 0),
        pc5(filter1, 0,  voiceMixer, 0),   // LP output of filter1
        pc6(filter2, 0,  voiceMixer, 1),   // LP output of filter2
        pc7(voiceMixer, 0, lofi,  0),
        pc8(lofi,    0,  dac1,  0),
        pc9(lofi,    0,  peak1, 0),
        pc10(playRaw1, playRaw1)            // placeholder — replaced below
    {
        playRaw1.playerID = 1;
        playRaw2.playerID = 2;
    }

    void  init(uint16_t lofiSampleRate);
    bool  update();
    bool  triggerVoice(uint8_t v, AudioFileInfo* fi);
    void  setVoiceVolume(uint8_t v, float gain);
    void  setVoiceDecayMs(uint8_t v, uint16_t ms, bool oneShot);
    void  setVoicePlaybackSpeed(uint8_t v, float speed);
    void  setVoiceFilter(uint8_t v, float cutoff, float resonance);
    void  setLofiEnabled(bool enabled);
    void  setLofiSampleRate(uint16_t rate);
    float getPeak();

    bool error = false;

private:
    AudioEffectEnvelope*     envelopes[VOICE_COUNT] = {&env1, &env2};
    SDPlayPCM*               players[VOICE_COUNT]   = {&playRaw1, &playRaw2};
    AudioFilterStateVariable* filters[VOICE_COUNT]  = {&filter1, &filter2};
    AudioFileInfo*           currentFI[VOICE_COUNT] = {NULL, NULL};
    float                    voiceGain[VOICE_COUNT]  = {0.8f, 0.8f};
};

#endif
