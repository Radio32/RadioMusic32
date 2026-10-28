/*
 RadioMusic32 custom firmware
 - 3 trigger inputs -> 3 sample voices
 - SD folders 1, 2, 3 map to voice 1, 2, 3
 - Max 16 files per folder, 16-bit mono 32k WAV target
 - 12-bit lo-fi stage + resonant low-pass filter
*/

#include <Bounce2.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>

#include "AudioSystemHelpers.h"
#include "AudioEngine.h"
#include "FileScanner.h"
#include "LedControl.h"
#include "RadioMusic.h"
#include "Settings.h"

#define CHAN_POT_PIN A9
#define CHAN_CV_PIN A6
#define TIME_POT_PIN A7
#define TIME_CV_PIN A8
#define RESET_BUTTON 8
#define RESET_CV 9
#define SHORT_PRESS_DURATION 10
#define LONG_PRESS_DURATION 600
#define ADC_BITS 13
#define ADC_MAX_VALUE (1 << ADC_BITS)

#define MAX_FILES_PER_FOLDER 16
#define FOLDER_COUNT 3
#define CONTROL_UPDATE_MS 25
#define AUDITION_INTERVAL_MS 130

#define CHANNEL_TRIG_HIGH 2300
#define CHANNEL_TRIG_LOW 1700
#define TIME_TRIG_HIGH 2300
#define TIME_TRIG_LOW 1700

Settings settings("SETTINGS.TXT");
LedControl ledControl;
FileScanner fileScanner;
AudioEngine audioEngine;

Bounce resetButtonBounce;
elapsedMillis buttonTimer = 0;
bool buttonHeld = false;
bool longPressHandled = false;

volatile bool resetTrigPending = false;
bool channelTrigHigh = false;
bool timeTrigHigh = false;

elapsedMillis controlPoll = 0;
elapsedMillis auditionTimer = 0;

bool setupMode = false;
uint8_t setupPage = 0;
uint8_t globalSampleIndex = 0;

AudioFileInfo* voiceFiles[FOLDER_COUNT][MAX_FILES_PER_FOLDER];
uint8_t voiceFileCount[FOLDER_COUNT] = {0, 0, 0};
uint16_t totalAcceptedFiles = 0;

float voiceVolume[VOICE_COUNT] = {0.85, 0.85, 0.85};
uint16_t voiceDecayMs[VOICE_COUNT] = {350, 350, 350};
bool voiceOneShot[VOICE_COUNT] = {false, false, false};

float masterFilterCutoff = 7000.0;
float masterFilterResonance = 1.2;

int prevChannelPot = -1;
int prevTimePot = -1;

float mapf(float x, float inMin, float inMax, float outMin, float outMax) {
	if (x < inMin) x = inMin;
	if (x > inMax) x = inMax;
	float ratio = (x - inMin) / (inMax - inMin);
	return outMin + (ratio * (outMax - outMin));
}

int readAverage(uint8_t pin, uint8_t samples) {
	int32_t sum = 0;
	for (uint8_t i = 0; i < samples; i++) {
		sum += analogRead(pin);
	}
	return (int)(sum / samples);
}

void resetcvVoice1() {
	resetTrigPending = true;
}

bool openSDCard() {
	SPI.setMOSI(7);
	SPI.setSCK(14);
	return SD.begin(SS);
}

bool hasPrefix(const String& name, const String& prefix) {
	return name.startsWith(prefix);
}

bool isAcceptedFormat(AudioFileInfo& fileInfo) {
	return fileInfo.getChannels() == 1 &&
		   fileInfo.getBytesPerSample() == 2 &&
		   fileInfo.getSampleRate() == 32000;
}

void assignVoiceFiles() {
	for (int i = 0; i < FOLDER_COUNT; i++) {
		voiceFileCount[i] = 0;
		for (int j = 0; j < MAX_FILES_PER_FOLDER; j++) {
			voiceFiles[i][j] = NULL;
		}
	}
	totalAcceptedFiles = 0;

	for (int bank = 0; bank <= fileScanner.lastBankIndex; bank++) {
		int count = fileScanner.numFilesInBank[bank];
		for (int i = 0; i < count; i++) {
			AudioFileInfo* info = &fileScanner.fileInfos[bank][i];
			if (!isAcceptedFormat(*info)) continue;

			int voice = -1;
			if (hasPrefix(info->name, "1/")) voice = 0;
			else if (hasPrefix(info->name, "2/")) voice = 1;
			else if (hasPrefix(info->name, "3/")) voice = 2;

			if (voice < 0) continue;
			if (voiceFileCount[voice] >= MAX_FILES_PER_FOLDER) continue;

			voiceFiles[voice][voiceFileCount[voice]] = info;
			voiceFileCount[voice]++;
			totalAcceptedFiles++;
		}
	}
}

void triggerVoice(uint8_t voice) {
	if (voice >= VOICE_COUNT) return;
	if (voiceFileCount[voice] == 0) return;

	uint8_t index = globalSampleIndex;
	if (index >= voiceFileCount[voice]) {
		index = voiceFileCount[voice] - 1;
	}
	AudioFileInfo* fileInfo = voiceFiles[voice][index];
	if (fileInfo == NULL) return;
	audioEngine.triggerVoice(voice, fileInfo);
}

void applyVoiceParameters(uint8_t voice) {
	audioEngine.setVoiceVolume(voice, voiceVolume[voice]);
	audioEngine.setVoiceDecayMs(voice, voiceDecayMs[voice], voiceOneShot[voice]);
}

void triggerVoiceForPage(uint8_t page) {
	if (page < 3) {
		triggerVoice(page);
	}
}

void updateLEDs() {
	if (setupMode) {
		ledControl.single(setupPage % 4);
		ledControl.showReset(true);
		return;
	}

	ledControl.showReset(false);
	ledControl.multi(globalSampleIndex & 0x0F);
}

void handleButton() {
	resetButtonBounce.update();

	if (resetButtonBounce.rose()) {
		buttonTimer = 0;
		buttonHeld = true;
		longPressHandled = false;
	}

	if (buttonHeld && !longPressHandled && buttonTimer >= LONG_PRESS_DURATION) {
		setupMode = !setupMode;
		longPressHandled = true;
		if (setupMode) {
			setupPage = 0;
		}
	}

	if (resetButtonBounce.fell()) {
		if (!longPressHandled && buttonTimer >= SHORT_PRESS_DURATION) {
			if (setupMode) {
				setupPage = (setupPage + 1) % 4;
			} else {
				triggerVoice(0);
				triggerVoice(1);
				triggerVoice(2);
			}
		}
		buttonHeld = false;
		buttonTimer = 0;
		longPressHandled = false;
	}
}

void updateTriggers() {
	if (resetTrigPending) {
		resetTrigPending = false;
		triggerVoice(0);
	}

	int channelCv = analogRead(CHAN_CV_PIN);
	if (!channelTrigHigh && channelCv >= CHANNEL_TRIG_HIGH) {
		channelTrigHigh = true;
		triggerVoice(1);
	} else if (channelTrigHigh && channelCv <= CHANNEL_TRIG_LOW) {
		channelTrigHigh = false;
	}

	int timeCv = analogRead(TIME_CV_PIN);
	if (!timeTrigHigh && timeCv >= TIME_TRIG_HIGH) {
		timeTrigHigh = true;
		triggerVoice(2);
	} else if (timeTrigHigh && timeCv <= TIME_TRIG_LOW) {
		timeTrigHigh = false;
	}
}

void maybeAudition(bool changed) {
	if (!changed) return;
	if (auditionTimer < AUDITION_INTERVAL_MS) return;
	auditionTimer = 0;
	triggerVoiceForPage(setupPage);
}

void updateControls() {
	if (controlPoll < CONTROL_UPDATE_MS) return;
	controlPoll = 0;

	int channelPot = readAverage(CHAN_POT_PIN, 12);
	int timePot = readAverage(TIME_POT_PIN, 12);

	if (!setupMode) {
		uint8_t index = (uint8_t)mapf(channelPot, 0, ADC_MAX_VALUE - 1, 0, MAX_FILES_PER_FOLDER - 1);
		globalSampleIndex = index;
		prevChannelPot = channelPot;
		prevTimePot = timePot;
		return;
	}

	bool channelChanged = prevChannelPot < 0 || abs(channelPot - prevChannelPot) > 40;
	bool timeChanged = prevTimePot < 0 || abs(timePot - prevTimePot) > 40;
	bool changed = channelChanged || timeChanged;

	if (setupPage == 0) {
		voiceVolume[0] = mapf(channelPot, 0, ADC_MAX_VALUE - 1, 0.0, 1.0);
		voiceDecayMs[0] = (uint16_t)mapf(timePot, 0, ADC_MAX_VALUE - 1, 20.0, 3000.0);
		voiceOneShot[0] = false;
		applyVoiceParameters(0);
	} else if (setupPage == 1) {
		voiceVolume[1] = mapf(channelPot, 0, ADC_MAX_VALUE - 1, 0.0, 1.0);
		voiceDecayMs[1] = (uint16_t)mapf(timePot, 0, ADC_MAX_VALUE - 1, 20.0, 3000.0);
		voiceOneShot[1] = false;
		applyVoiceParameters(1);
	} else if (setupPage == 2) {
		voiceVolume[2] = mapf(channelPot, 0, ADC_MAX_VALUE - 1, 0.0, 1.0);
		voiceDecayMs[2] = (uint16_t)mapf(timePot, 0, ADC_MAX_VALUE - 1, 20.0, 3000.0);
		voiceOneShot[2] = false;
		applyVoiceParameters(2);
	} else if (setupPage == 3) {
		masterFilterCutoff = mapf(channelPot, 0, ADC_MAX_VALUE - 1, 300.0, 10000.0);
		masterFilterResonance = mapf(timePot, 0, ADC_MAX_VALUE - 1, 0.9, 4.0);
		audioEngine.setMasterFilter(masterFilterCutoff, masterFilterResonance);
	}

	maybeAudition(changed);

	prevChannelPot = channelPot;
	prevTimePot = timePot;
}

void setup() {
	analogReadRes(ADC_BITS);
	ledControl.init();
	ledControl.single(0);

	if (!openSDCard()) {
		reBoot(0);
	}

	settings.init(true);
	settings.lofi12bit = true;
	settings.lofiSampleRate = 32000;
	settings.lofiLowPassCutoff = 7000.0;
	settings.lofiLowPassResonance = 1.2;

	File root = SD.open("/");
	fileScanner.scan(&root, settings);
	assignVoiceFiles();

	if (totalAcceptedFiles == 0 || voiceFileCount[0] == 0 || voiceFileCount[1] == 0 || voiceFileCount[2] == 0) {
		reBoot(0);
	}

	audioEngine.init(settings);
	audioEngine.setLofiEnabled(true);
	audioEngine.setLofiSampleRate(32000);
	audioEngine.setMasterFilter(masterFilterCutoff, masterFilterResonance);

	for (uint8_t v = 0; v < VOICE_COUNT; v++) {
		applyVoiceParameters(v);
	}

	pinMode(RESET_BUTTON, INPUT);
	resetButtonBounce.attach(RESET_BUTTON);
	resetButtonBounce.interval(5);

	pinMode(RESET_CV, INPUT);
	attachInterrupt(RESET_CV, resetcvVoice1, RISING);
}

void loop() {
	handleButton();
	updateControls();
	updateTriggers();
	updateLEDs();

	if (audioEngine.update() || audioEngine.error) {
		reBoot(0);
	}
}
