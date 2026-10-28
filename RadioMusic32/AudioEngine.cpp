#include "AudioEngine.h"
#include "Arduino.h"

void AudioEngine::init(uint16_t lofiSampleRate) {
    AudioMemory(56);

    for (uint8_t i = 0; i < VOICE_COUNT; i++) {
        players[i]->loopPlayback(false);
        envelopes[i]->attack(1.0f);
        envelopes[i]->hold(0.0f);
        envelopes[i]->decay(350.0f);
        envelopes[i]->sustain(0.0f);
        envelopes[i]->release(0.0f);
        voiceMixer.gain(i, voiceGain[i]);
        scf[i]->setEnabled(true);
        scf[i]->setControlStride(16);
        scf[i]->setClockRateHz(44100.0f);
        // Default filter: open, low resonance
        filters[i]->frequency(8000.0f);
        filters[i]->resonance(0.9f);
    }

    setLofiEnabled(true);
    setLofiSampleRate(lofiSampleRate);
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
    currentFI[v] = fi;
    scf[v]->setClockRateHz((float)fi->getSampleRate() * players[v]->playbackSpeed);
    envelopes[v]->noteOff();
    bool ok = players[v]->playFrom(fi);
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
    speed = constrain(speed, 0.03f, 8.0f);
    players[v]->playbackSpeed = speed;
    if (currentFI[v] != NULL) {
        scf[v]->setClockRateHz((float)currentFI[v]->getSampleRate() * speed);
    } else {
        scf[v]->setClockRateHz(44100.0f * speed);
    }
}

void AudioEngine::setVoiceFilter(uint8_t v, float cutoff, float resonance) {
    if (v >= VOICE_COUNT) return;
    cutoff    = constrain(cutoff,    80.0f,  16000.0f);
    resonance = constrain(resonance, 0.7f,   4.5f);
    filters[v]->frequency(cutoff);
    filters[v]->resonance(resonance);
}

void AudioEngine::setLofiEnabled(bool enabled) {
    lofi.setEnabled(enabled);
    lofi.setBitDepth(enabled ? 12 : 16);
}

void AudioEngine::setLofiSampleRate(uint16_t rate) {
    rate = constrain(rate, 4000, 44100);
    uint8_t d = (uint8_t)((44100 + rate / 2) / rate);
    d = constrain(d, (uint8_t)1, (uint8_t)16);
    lofi.setDecimation(d);
}

float AudioEngine::getPeak() {
    return peak1.available() ? peak1.read() : 0.0f;
}
