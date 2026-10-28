#include "AudioEngine.h"
#include "Arduino.h"

void AudioEngine::init(float lofiCutoff, float lofiResonance, uint16_t lofiSampleRate) {
    AudioMemory(40);

    for (uint8_t i = 0; i < VOICE_COUNT; i++) {
        players[i]->loopPlayback(false);
        envelopes[i]->attack(1.0f);
        envelopes[i]->hold(0.0f);
        envelopes[i]->decay(350.0f);
        envelopes[i]->sustain(0.0f);
        envelopes[i]->release(0.0f);
        voiceMixer.gain(i, voiceVolume[i]);
    }

    setLofiEnabled(true);
    setLofiSampleRate(lofiSampleRate);
    setMasterFilter(lofiCutoff, lofiResonance);
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
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 1.0f) gain = 1.0f;
    voiceVolume[voiceIndex] = gain;
    voiceMixer.gain(voiceIndex, gain);
}

void AudioEngine::setVoiceDecayMs(uint8_t voiceIndex, uint16_t decayMs, bool oneShot) {
    if (voiceIndex >= VOICE_COUNT) return;
    float d = oneShot ? 60000.0f : (float)decayMs;
    if (d < 5.0f)      d = 5.0f;
    if (d > 60000.0f)  d = 60000.0f;
    envelopes[voiceIndex]->decay(d);
}

void AudioEngine::setVoicePlaybackSpeed(uint8_t voiceIndex, float speed) {
    if (voiceIndex >= VOICE_COUNT) return;
    if (speed < 0.03f) speed = 0.03f;
    if (speed > 8.0f)  speed = 8.0f;
    players[voiceIndex]->playbackSpeed = speed;
}

void AudioEngine::setMasterFilter(float cutoff, float resonance) {
    if (cutoff    < 80.0f)    cutoff    = 80.0f;
    if (cutoff    > 16000.0f) cutoff    = 16000.0f;
    if (resonance < 0.7f)     resonance = 0.7f;
    if (resonance > 4.5f)     resonance = 4.5f;
    lowPass.frequency(cutoff);
    lowPass.resonance(resonance);
}

void AudioEngine::setLofiEnabled(bool enabled) {
    lofi.setEnabled(enabled);
    lofi.setBitDepth(enabled ? 12 : 16);
}

void AudioEngine::setLofiSampleRate(uint16_t sampleRate) {
    if (sampleRate < 4000)  sampleRate = 4000;
    if (sampleRate > 44100) sampleRate = 44100;
    uint8_t decimation = (uint8_t)((44100 + sampleRate / 2) / sampleRate);
    if (decimation < 1)  decimation = 1;
    if (decimation > 16) decimation = 16;
    lofi.setDecimation(decimation);
}

float AudioEngine::getPeak() {
    if (peak1.available()) return peak1.read();
    return 0.0f;
}
