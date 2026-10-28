#include "AudioEffectSCF.h"

#include <math.h>
#include "Arduino.h"

void AudioEffectSCF::setEnabled(bool state) {
	enabled = state;
}

void AudioEffectSCF::setClockRateHz(float hz) {
	if (hz < 1000.0f) hz = 1000.0f;
	if (hz > 96000.0f) hz = 96000.0f;
	targetClockHz = hz;
}

void AudioEffectSCF::setControlStride(uint8_t samples) {
	if (samples < 1) samples = 1;
	if (samples > 64) samples = 64;
	controlStride = samples;
}

void AudioEffectSCF::configureLowPass(Biquad& biquad, float cutoffHz, float q) {
	const float sampleRate = AUDIO_SAMPLE_RATE_EXACT;
	float maxCutoff = sampleRate * 0.49f;
	if (cutoffHz < 20.0f) cutoffHz = 20.0f;
	if (cutoffHz > maxCutoff) cutoffHz = maxCutoff;

	const float omega = 2.0f * PI * cutoffHz / sampleRate;
	const float cs = cosf(omega);
	const float sn = sinf(omega);
	const float alpha = sn / (2.0f * q);

	float b0 = (1.0f - cs) * 0.5f;
	float b1 = 1.0f - cs;
	float b2 = (1.0f - cs) * 0.5f;
	float a0 = 1.0f + alpha;
	float a1 = -2.0f * cs;
	float a2 = 1.0f - alpha;

	biquad.b0 = b0 / a0;
	biquad.b1 = b1 / a0;
	biquad.b2 = b2 / a0;
	biquad.a1 = a1 / a0;
	biquad.a2 = a2 / a0;
}

void AudioEffectSCF::updateDynamicCoefficients(float cutoffHz) {
	configureLowPass(scfStage[0], cutoffHz, 0.5176f);
	configureLowPass(scfStage[1], cutoffHz, 0.7071f);
	configureLowPass(scfStage[2], cutoffHz, 1.9319f);
}

void AudioEffectSCF::updateSmoothingCoefficients() {
	// Fixed post stage as per report (~19.5k target, clipped by Nyquist when needed)
	configureLowPass(smoothStage, 19500.0f, 0.7071f);
}

float AudioEffectSCF::processBiquad(Biquad& biquad, float input) {
	float y = biquad.b0 * input + biquad.z1;
	biquad.z1 = biquad.b1 * input - biquad.a1 * y + biquad.z2;
	biquad.z2 = biquad.b2 * input - biquad.a2 * y;
	return y;
}

void AudioEffectSCF::update(void) {
	audio_block_t* in = receiveReadOnly(0);
	if (!in) return;

	audio_block_t* out = allocate();
	if (!out) {
		release(in);
		return;
	}

	if (!enabled) {
		memcpy(out->data, in->data, sizeof(out->data));
		transmit(out);
		release(out);
		release(in);
		return;
	}

	updateSmoothingCoefficients();

	uint8_t stride = controlStride;
	uint8_t counter = controlCounter;

	for (uint16_t i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
		if (counter == 0) {
			// Dynamic tracking to playback Nyquist: fc = playbackClock * 0.5
			float cutoff = targetClockHz * 0.5f;
			updateDynamicCoefficients(cutoff);
		}
		counter++;
		if (counter >= stride) counter = 0;

		float x = (float)in->data[i];
		x = processBiquad(scfStage[0], x);
		x = processBiquad(scfStage[1], x);
		x = processBiquad(scfStage[2], x);
		x = processBiquad(smoothStage, x);

		int32_t s = (int32_t)x;
		if (s > 32767) s = 32767;
		if (s < -32768) s = -32768;
		out->data[i] = (int16_t)s;
	}

	controlCounter = counter;
	transmit(out);
	release(out);
	release(in);
}
