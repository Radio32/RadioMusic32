#ifndef AudioEffectLoFi_h
#define AudioEffectLoFi_h

#include <AudioStream.h>

class AudioEffectLoFi : public AudioStream {
public:
	AudioEffectLoFi() : AudioStream(1, inputQueueArray) {}

	void setEnabled(bool state);
	void setBitDepth(uint8_t bits);
	void setDecimation(uint8_t factor);
	virtual void update(void);

private:
	audio_block_t* inputQueueArray[1];
	volatile bool enabled = true;
	volatile uint8_t bitDepth = 12;
	volatile uint8_t decimation = 2;

	uint8_t holdCounter = 0;
	int16_t heldSample  = 0;

	// Fixed reconstruction LP filter — single-pole IIR, fc ≈ 15 kHz @ 44.1 kHz
	// Cleans quantisation/decimation artefacts, matches S950 bandwidth.
	// α = exp(−2π × 15000 / 44100) ≈ 0.1181
	static constexpr float LP_ALPHA = 0.1181f;
	static constexpr float LP_BETA  = 1.0f - LP_ALPHA;
	float lpState = 0.0f;
};

#endif
