#include "AudioEngine.h"
#include "Arduino.h"

void AudioEngine::init() {
    AudioMemory(24);

    for (uint8_t i = 0; i < VOICE_COUNT; i++) {
        players[i]->loopPlayback(false);
        envelopes[i]->attack(1.0f);
        envelopes[i]->hold(0.0f);
        envelopes[i]->decay(60000.0f);
        envelopes[i]->sustain(0.0f);
        envelopes[i]->release(0.0f);
        voiceMixer.gain(i, voiceGain[i]);
    }
    error = false;
}

bool AudioEngine::update() {
    for (uint8_t i = 0; i < VOICE_COUNT; i++) {
        if (players[i]->errors > MAX_ERRORS) { error = true; return true; }
    }
    return false;
}

bool AudioEngine::triggerVoice(uint8_t v, AudioFileInfo* fi) {
    if (v >= VOICE_COUNT || !fi) return false;
    fi->startPlayFrom = 0;
    envelopes[v]->noteOff();
    bool ok = players[v]->playFrom(fi);
    // File sample rate may have changed
    players[v]->updateFilters();
    envelopes[v]->noteOn();
    return ok;
}

void AudioEngine::setVoiceVolume(uint8_t v, float gain) {
    if (v >= VOICE_COUNT) return;
    gain = constrain(gain, 0.0f, 1.0f);
    voiceGain[v] = gain;
    voiceMixer.gain(v, gain);
}

void AudioEngine::setVoiceDecayMs(uint8_t v, uint16_t ms, bool oneShot) {
    if (v >= VOICE_COUNT) return;
    float d = oneShot ? 60000.0f : (float)ms;
    d = constrain(d, 5.0f, 60000.0f);
    envelopes[v]->decay(d);
}

void AudioEngine::setVoicePlaybackSpeed(uint8_t v, float speed) {
    if (v >= VOICE_COUNT) return;
    // +2 octaves max keeps SD reads and the input-side filter within budget
    speed = constrain(speed, 0.03f, 4.0f);
    players[v]->playbackSpeed = speed;
    players[v]->updateFilters();
}

void AudioEngine::setEmulation(bool enabled, uint16_t sampleRate) {
    for (uint8_t i = 0; i < VOICE_COUNT; i++) {
        players[i]->setEmulation(enabled ? (float)sampleRate : 0.0f, enabled);
    }
}

float AudioEngine::getPeak() {
    return peak1.available() ? peak1.read() : 0.0f;
}
