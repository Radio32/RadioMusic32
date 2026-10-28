#include "AudioEngine.h"

#include "Arduino.h"

void AudioEngine::init(Settings& config) {
	AudioMemory(48);
	settings = &config;

	for (uint8_t i = 0; i < VOICE_COUNT; i++) {
		players[i]->loopPlayback(false);
		envelopes[i]->attack(1.0);
		envelopes[i]->hold(0.0);
		envelopes[i]->decay(350.0);
		envelopes[i]->sustain(0.0);
		envelopes[i]->release(0.0);
		setVoiceVolume(i, voiceVolume[i]);
	}

	setLofiEnabled(settings->lofi12bit);
	setLofiSampleRate(settings->lofiSampleRate);
	setMasterFilter(settings->lofiLowPassCutoff, settings->lofiLowPassResonance);
	error = false;
}

bool AudioEngine::update() {
	for (uint8_t i = 0; i < VOICE_COUNT; i++) {
		if (players[i]->errors > MAX_ERRORS) {
			error = true;
			return true;
		}
	}
	return false;
}

bool AudioEngine::triggerVoice(uint8_t voiceIndex, AudioFileInfo* fileInfo) {
	if (voiceIndex >= VOICE_COUNT || fileInfo == NULL) return false;

	fileInfo->startPlayFrom = 0;
	currentFileInfo[voiceIndex] = fileInfo;
	envelopes[voiceIndex]->noteOff();
	bool started = players[voiceIndex]->playFrom(fileInfo);
	envelopes[voiceIndex]->noteOn();
	return started;
}

void AudioEngine::setVoiceVolume(uint8_t voiceIndex, float gain) {
	if (voiceIndex >= VOICE_COUNT) return;
	if (gain < 0.0) gain = 0.0;
	if (gain > 1.0) gain = 1.0;
	voiceVolume[voiceIndex] = gain;
	voiceMixer.gain(voiceIndex, gain);
}

void AudioEngine::setVoiceDecayMs(uint8_t voiceIndex, uint16_t decayMs, bool oneShot) {
	if (voiceIndex >= VOICE_COUNT) return;

	float targetDecay = decayMs;
	if (oneShot) {
		targetDecay = 60000.0;
	}
	if (targetDecay < 5.0) targetDecay = 5.0;
	if (targetDecay > 60000.0) targetDecay = 60000.0;
	envelopes[voiceIndex]->decay(targetDecay);
}

void AudioEngine::setMasterFilter(float cutoff, float resonance) {
	if (cutoff < 80.0) cutoff = 80.0;
	if (cutoff > 16000.0) cutoff = 16000.0;
	if (resonance < 0.7) resonance = 0.7;
	if (resonance > 4.5) resonance = 4.5;
	lowPass.frequency(cutoff);
	lowPass.resonance(resonance);
}

void AudioEngine::setLofiEnabled(bool enabled) {
	lofi.setEnabled(enabled);
	lofi.setBitDepth(enabled ? 12 : 16);
}

void AudioEngine::setLofiSampleRate(uint16_t sampleRate) {
	if (sampleRate < 4000) sampleRate = 4000;
	if (sampleRate > 44100) sampleRate = 44100;
	uint8_t decimation = (uint8_t)((44100 + (sampleRate / 2)) / sampleRate);
	if (decimation < 1) decimation = 1;
	if (decimation > 16) decimation = 16;
	lofi.setDecimation(decimation);
}

float AudioEngine::getPeak() {
	if (peak1.available()) {
		return peak1.read();
	}
	return 0;
}
