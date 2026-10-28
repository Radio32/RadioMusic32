#include "AudioEffectLoFi.h"

#include "Arduino.h"

void AudioEffectLoFi::setEnabled(bool state) {
	enabled = state;
}

void AudioEffectLoFi::setBitDepth(uint8_t bits) {
	if (bits < 4) bits = 4;
	if (bits > 16) bits = 16;
	bitDepth = bits;
}

void AudioEffectLoFi::setDecimation(uint8_t factor) {
	if (factor < 1) factor = 1;
	if (factor > 16) factor = 16;
	decimation = factor;
}

void AudioEffectLoFi::update(void) {
	audio_block_t* in = receiveReadOnly(0);
	if (!in) return;

	audio_block_t* out = allocate();
	if (!out) {
		release(in);
		return;
	}

	const bool localEnabled = enabled;
	const uint8_t localDecimation = decimation;
	const uint8_t localBitDepth = bitDepth;

	if (!localEnabled || (localBitDepth >= 16 && localDecimation <= 1)) {
		memcpy(out->data, in->data, sizeof(out->data));
		transmit(out);
		release(out);
		release(in);
		return;
	}

	int32_t quantStep = 1;
	if (localBitDepth < 16) {
		quantStep = 1 << (16 - localBitDepth);
	}
	const int32_t halfStep = quantStep >> 1;

	uint8_t counter = holdCounter;
	int16_t sampleHold = heldSample;

	for (uint16_t i = 0; i < AUDIO_BLOCK_SAMPLES; i++) {
		int16_t sample = in->data[i];

		if (localDecimation > 1) {
			if (counter == 0) {
				sampleHold = sample;
			}
			sample = sampleHold;
			counter++;
			if (counter >= localDecimation) counter = 0;
		}

		if (localBitDepth < 16) {
			int32_t s = sample;
			if (s >= 0) {
				s = ((s + halfStep) / quantStep) * quantStep;
			} else {
				s = ((s - halfStep) / quantStep) * quantStep;
			}
			if (s > 32767) s = 32767;
			if (s < -32768) s = -32768;
			sample = (int16_t) s;
		}

		out->data[i] = sample;
	}

	holdCounter = counter;
	heldSample = sampleHold;

	transmit(out);
	release(out);
	release(in);
}
