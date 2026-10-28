#ifndef AudioEngine_h
#define AudioEngine_h

#include <Audio.h>

#include "AudioEffectLoFi.h"
#include "SDPlayPCM.h"
#include "Settings.h"

#define MAX_ERRORS 10
#define VOICE_COUNT 3

class AudioEngine {
public:
	SDPlayPCM playRaw1;
	SDPlayPCM playRaw2;
	SDPlayPCM playRaw3;

	AudioEffectEnvelope env1;
	AudioEffectEnvelope env2;
	AudioEffectEnvelope env3;

	AudioMixer4 voiceMixer;
	AudioEffectLoFi lofi;
	AudioFilterStateVariable lowPass;
	AudioAnalyzePeak peak1;
	AudioOutputAnalog dac1;

	AudioConnection patchCord1;
	AudioConnection patchCord2;
	AudioConnection patchCord3;
	AudioConnection patchCord4;
	AudioConnection patchCord5;
	AudioConnection patchCord6;
	AudioConnection patchCord7;
	AudioConnection patchCord8;
	AudioConnection patchCord9;

	AudioEngine() :
		patchCord1(playRaw1, env1),
		patchCord2(playRaw2, env2),
		patchCord3(playRaw3, env3),
		patchCord4(env1, 0, voiceMixer, 0),
		patchCord5(env2, 0, voiceMixer, 1),
		patchCord6(env3, 0, voiceMixer, 2),
		patchCord7(voiceMixer, 0, lofi, 0),
		patchCord8(lofi, 0, lowPass, 0),
		patchCord9(lowPass, 0, dac1, 0),
		peakPatch(lowPass, 0, peak1, 0) {
		playRaw1.playerID = 1;
		playRaw2.playerID = 2;
		playRaw3.playerID = 3;
	}

	void init(Settings& settings);
	bool update();
	bool triggerVoice(uint8_t voiceIndex, AudioFileInfo* fileInfo);
	void setVoiceVolume(uint8_t voiceIndex, float gain);
	void setVoiceDecayMs(uint8_t voiceIndex, uint16_t decayMs, bool oneShot);
	void setMasterFilter(float cutoff, float resonance);
	void setLofiEnabled(bool enabled);
	void setLofiSampleRate(uint16_t sampleRate);
	float getPeak();

	bool error = false;

private:
	AudioConnection peakPatch;
	Settings* settings = NULL;
	AudioFileInfo* currentFileInfo[VOICE_COUNT] = {NULL, NULL, NULL};
	AudioEffectEnvelope* envelopes[VOICE_COUNT] = {&env1, &env2, &env3};
	SDPlayPCM* players[VOICE_COUNT] = {&playRaw1, &playRaw2, &playRaw3};
	float voiceVolume[VOICE_COUNT] = {0.8, 0.8, 0.8};
};

#endif
