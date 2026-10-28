#ifndef AudioEffectSCF_h
#define AudioEffectSCF_h

#include <AudioStream.h>

class AudioEffectSCF : public AudioStream {
public:
	AudioEffectSCF() : AudioStream(1, inputQueueArray) {}

	void setEnabled(bool state);
	void setClockRateHz(float hz);
	void setControlStride(uint8_t samples);
	virtual void update(void);

private:
	struct Biquad {
		float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
		float a1 = 0.0f, a2 = 0.0f;
		float z1 = 0.0f, z2 = 0.0f;
	};

	audio_block_t* inputQueueArray[1];
	volatile bool enabled = true;
	volatile float targetClockHz = 44100.0f;
	volatile uint8_t controlStride = 16;

	// 3 x 2-pole Butterworth stages -> 6-pole switched-cap emulation
	// Q values from report: 0.5176, 0.7071, 1.9319
	Biquad scfStage[3];
	// 1 x fixed post-smoothing stage (Sallen-Key style)
	Biquad smoothStage;

	uint8_t controlCounter = 0;

	void updateDynamicCoefficients(float cutoffHz);
	void updateSmoothingCoefficients();
	void configureLowPass(Biquad& biquad, float cutoffHz, float q);
	float processBiquad(Biquad& biquad, float input);
};

#endif
