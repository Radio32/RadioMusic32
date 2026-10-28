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
	int16_t heldSample = 0;
};

#endif
