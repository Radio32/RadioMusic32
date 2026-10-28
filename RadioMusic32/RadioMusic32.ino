/*
 RadioMusic32
 2-voice sample player for Eurorack
 Voice 1 : pure sample player   — triggered by Reset CV (interrupt)
 Voice 2 : chromatic V/oct      — triggered by Channel CV, pitched by Time CV
 Banks A-P, 16 files per voice per bank
*/

#include <Bounce2.h>
#include <EEPROM.h>
#include <SPI.h>
#include <SD.h>

#include "AudioSystemHelpers.h"
#include "AudioEngine.h"
#include "FileScanner.h"
#include "LedControl.h"
#include "RadioMusic32.h"

// ─── pins ────────────────────────────────────────────────────────────────────
#define CHAN_POT_PIN  A9
#define CHAN_CV_PIN   A6
#define TIME_POT_PIN  A7
#define TIME_CV_PIN   A8
#define RESET_BUTTON  8
#define RESET_CV      9

// ─── ADC ─────────────────────────────────────────────────────────────────────
#define ADC_BITS      13
#define ADC_MAX_VALUE (1 << ADC_BITS)

// ─── timing ──────────────────────────────────────────────────────────────────
#define SHORT_PRESS_MS   10
#define LONG_PRESS_MS  1500   // same threshold for bank-select → setup transition
#define CTRL_UPDATE_MS   20
#define AUDITION_MS     150

// ─── trigger thresholds ──────────────────────────────────────────────────────
#define TRIG_HIGH  2300
#define TRIG_LOW   1700

// ─── EEPROM ───────────────────────────────────────────────────────────────────
#define EEPROM_BANK_ADDR 0

// ─── defaults ────────────────────────────────────────────────────────────────
#define DEFAULT_CUTOFF     7000.0f
#define DEFAULT_RESONANCE     1.2f
#define DEFAULT_LOFI_RATE   32000
#define VOCT_SEMITONES         48   // ±2 octave total span (adjustable on page 3)

// ─── objects ─────────────────────────────────────────────────────────────────
AudioEngine  audioEngine;
FileScanner  fileScanner;
LedControl   ledControl;
Bounce       resetBtn;

// ─── mode ────────────────────────────────────────────────────────────────────
enum Mode { MODE_NORMAL, MODE_BANK_SELECT, MODE_SETUP };
Mode currentMode = MODE_NORMAL;

// ─── button state ─────────────────────────────────────────────────────────────
elapsedMillis buttonTimer    = 0;
bool          buttonHeld     = false;
bool          longPressHandled = false;

// ─── triggers ────────────────────────────────────────────────────────────────
volatile bool resetTrigPending = false;
bool          chanCvHigh       = false;

// ─── bank / sample ───────────────────────────────────────────────────────────
uint8_t selectedBank      = 0;   // 0–15 = A–P
bool    bankSelectChanged = false;
uint8_t globalSampleIndex = 0;   // 0–15 within the current bank

// ─── voice params ─────────────────────────────────────────────────────────────
float    voiceVolume[VOICE_COUNT]  = {0.85f, 0.85f};
uint16_t voiceDecayMs[VOICE_COUNT] = {350, 350};
bool     voiceOneShot[VOICE_COUNT] = {false, false};

// ─── master / V/oct ──────────────────────────────────────────────────────────
float   masterCutoff    = DEFAULT_CUTOFF;
float   masterResonance = DEFAULT_RESONANCE;
float   currentVoct     = 1.0f;
int     prevVoctAdc     = -1;
int     voctSemitones   = VOCT_SEMITONES;  // tunable on setup page 3
float   voctRootOffset  = 0.0f;            // semitone trim

// ─── edit / audition ─────────────────────────────────────────────────────────
uint8_t       setupPage    = 0;
elapsedMillis auditTimer   = 0;
elapsedMillis ctrlTimer    = 0;
int           prevChanPot  = -1;
int           prevTimePot  = -1;

// ─────────────────────────────────────────────────────────────────────────────

float mapf(float x, float lo, float hi, float outLo, float outHi) {
    if (x < lo) x = lo;
    if (x > hi) x = hi;
    return outLo + (x - lo) / (hi - lo) * (outHi - outLo);
}

int readAvg(uint8_t pin, uint8_t n) {
    int32_t s = 0;
    for (uint8_t i = 0; i < n; i++) s += analogRead(pin);
    return (int)(s / n);
}

// Called by interrupt on rising edge of Reset CV → triggers voice 1
void resetCvISR() { resetTrigPending = true; }

// ─── V/oct ────────────────────────────────────────────────────────────────────
float calculateVoct(int adcValue) {
    float semi = ((float)adcValue - (float)(ADC_MAX_VALUE / 2))
                 * (float)voctSemitones / (float)ADC_MAX_VALUE
                 + voctRootOffset;
    return powf(2.0f, semi / 12.0f);
}

// ─── file lookup ─────────────────────────────────────────────────────────────
AudioFileInfo* getVoiceFile(uint8_t voice) {
    uint8_t count = fileScanner.numFiles[selectedBank][voice];
    if (count == 0) return NULL;
    uint8_t idx = globalSampleIndex < count ? globalSampleIndex : count - 1;
    return &fileScanner.fileInfos[selectedBank][voice][idx];
}

// ─── voice trigger ────────────────────────────────────────────────────────────
void triggerVoice(uint8_t voice) {
    AudioFileInfo* fi = getVoiceFile(voice);
    if (!fi) return;
    if (voice == 1) audioEngine.setVoicePlaybackSpeed(1, currentVoct);
    audioEngine.triggerVoice(voice, fi);
}

void applyVoiceParams(uint8_t v) {
    audioEngine.setVoiceVolume(v, voiceVolume[v]);
    audioEngine.setVoiceDecayMs(v, voiceDecayMs[v], voiceOneShot[v]);
}

// ─── LEDs ────────────────────────────────────────────────────────────────────
void updateLEDs() {
    if (currentMode == MODE_SETUP) {
        ledControl.single(setupPage % 4);
        ledControl.showReset(true);
    } else if (currentMode == MODE_BANK_SELECT) {
        ledControl.multi(selectedBank & 0x0F);
        ledControl.showReset(true);
    } else {
        ledControl.multi(globalSampleIndex & 0x0F);
        ledControl.showReset(false);
    }
}

// ─── button ──────────────────────────────────────────────────────────────────
void handleButton() {
    resetBtn.update();

    if (resetBtn.rose()) {
        buttonTimer     = 0;
        buttonHeld      = true;
        longPressHandled = false;
        bankSelectChanged = false;
        if (currentMode == MODE_NORMAL) {
            currentMode = MODE_BANK_SELECT;
        }
    }

    // Long hold: transition to setup (from bank-select) or exit setup
    if (buttonHeld && !longPressHandled && buttonTimer >= LONG_PRESS_MS) {
        longPressHandled = true;
        if (currentMode == MODE_BANK_SELECT || currentMode == MODE_NORMAL) {
            currentMode = MODE_SETUP;
            setupPage   = 0;
        } else if (currentMode == MODE_SETUP) {
            currentMode = MODE_NORMAL;
        }
    }

    if (resetBtn.fell()) {
        buttonHeld = false;
        if (!longPressHandled && buttonTimer >= SHORT_PRESS_MS) {
            if (currentMode == MODE_BANK_SELECT) {
                // Short press: confirm bank, return to normal
                if (bankSelectChanged) {
                    EEPROM.write(EEPROM_BANK_ADDR, selectedBank);
                }
                currentMode = MODE_NORMAL;
            } else if (currentMode == MODE_SETUP) {
                // Short press: cycle page
                setupPage = (setupPage + 1) % 4;
                prevChanPot = -1;  // force re-read so new page applies immediately
                prevTimePot = -1;
            }
        }
        buttonTimer      = 0;
        longPressHandled = false;
    }
}

// ─── triggers ────────────────────────────────────────────────────────────────
void updateTriggers() {
    // Voice 1: interrupt-driven Reset CV
    if (resetTrigPending) {
        resetTrigPending = false;
        triggerVoice(0);
    }

    // Voice 2: edge-detect on Channel CV
    int chanCv = analogRead(CHAN_CV_PIN);
    if (!chanCvHigh && chanCv >= TRIG_HIGH) {
        chanCvHigh = true;
        triggerVoice(1);
    } else if (chanCvHigh && chanCv <= TRIG_LOW) {
        chanCvHigh = false;
    }
}

// ─── V/oct continuous update ─────────────────────────────────────────────────
void updateVoct() {
    int raw = analogRead(TIME_CV_PIN);
    if (prevVoctAdc < 0 || abs(raw - prevVoctAdc) > 16) {
        prevVoctAdc = raw;
        currentVoct = calculateVoct(raw);
        audioEngine.setVoicePlaybackSpeed(1, currentVoct);
    }
}

// ─── controls (pots, bank-select) ────────────────────────────────────────────
void updateControls() {
    if (ctrlTimer < CTRL_UPDATE_MS) return;
    ctrlTimer = 0;

    int chanPot = readAvg(CHAN_POT_PIN, 12);
    int timePot = readAvg(TIME_POT_PIN, 12);

    bool chanChanged = (prevChanPot < 0 || abs(chanPot - prevChanPot) > 40);
    bool timeChanged = (prevTimePot < 0 || abs(timePot - prevTimePot) > 40);
    bool anyChanged  = chanChanged || timeChanged;

    // Bank select: pot changes bank while button is held
    if (currentMode == MODE_BANK_SELECT) {
        uint8_t nb = (uint8_t)mapf(chanPot, 0, ADC_MAX_VALUE - 1, 0, BANKS - 1);
        if (nb != selectedBank) {
            selectedBank      = nb;
            bankSelectChanged = true;
        }
        prevChanPot = chanPot;
        prevTimePot = timePot;
        return;
    }

    if (currentMode == MODE_NORMAL) {
        globalSampleIndex = (uint8_t)mapf(chanPot, 0, ADC_MAX_VALUE - 1, 0, MAX_FILES_PER_SLOT - 1);
        prevChanPot = chanPot;
        prevTimePot = timePot;
        return;
    }

    // Setup pages ─────────────────────────────────────────────────────────────
    if (currentMode == MODE_SETUP) {
        if (setupPage == 0) {
            // Voice 1: volume + decay
            voiceVolume[0]  = mapf(chanPot, 0, ADC_MAX_VALUE - 1, 0.0f, 1.0f);
            voiceDecayMs[0] = (uint16_t)mapf(timePot, 0, ADC_MAX_VALUE - 1, 20.0f, 3000.0f);
            voiceOneShot[0] = timePot > (ADC_MAX_VALUE - 256);
            applyVoiceParams(0);
        } else if (setupPage == 1) {
            // Voice 2: volume + decay
            voiceVolume[1]  = mapf(chanPot, 0, ADC_MAX_VALUE - 1, 0.0f, 1.0f);
            voiceDecayMs[1] = (uint16_t)mapf(timePot, 0, ADC_MAX_VALUE - 1, 20.0f, 3000.0f);
            voiceOneShot[1] = timePot > (ADC_MAX_VALUE - 256);
            applyVoiceParams(1);
        } else if (setupPage == 2) {
            // Master filter
            masterCutoff    = mapf(chanPot, 0, ADC_MAX_VALUE - 1, 300.0f, 10000.0f);
            masterResonance = mapf(timePot, 0, ADC_MAX_VALUE - 1, 0.7f, 4.3f);
            audioEngine.setMasterFilter(masterCutoff, masterResonance);
        } else if (setupPage == 3) {
            // V/oct calibration: semitone span + root trim
            voctSemitones  = (int)mapf(chanPot, 0, ADC_MAX_VALUE - 1, 24.0f, 96.0f);
            voctRootOffset = mapf(timePot, 0, ADC_MAX_VALUE - 1, -12.0f, 12.0f);
            // Recompute with new calibration
            currentVoct = calculateVoct(prevVoctAdc >= 0 ? prevVoctAdc : ADC_MAX_VALUE / 2);
            audioEngine.setVoicePlaybackSpeed(1, currentVoct);
        }

        // Auto-audition while editing (throttled)
        if (anyChanged && auditTimer >= AUDITION_MS) {
            auditTimer = 0;
            if (setupPage < 2) triggerVoice((uint8_t)setupPage);  // pages 0/1 → voice 0/1
        }
    }

    prevChanPot = chanPot;
    prevTimePot = timePot;
}

// ─── SD ──────────────────────────────────────────────────────────────────────
bool openSD() {
    SPI.setMOSI(7);
    SPI.setSCK(14);
    return SD.begin(SS);
}

// ─── setup ───────────────────────────────────────────────────────────────────
void setup() {
    analogReadRes(ADC_BITS);
    ledControl.init();
    ledControl.single(0);

    if (!openSD()) reBoot(0);

    fileScanner.scan();

    // Restore saved bank
    uint8_t saved = EEPROM.read(EEPROM_BANK_ADDR);
    if (saved < BANKS) selectedBank = saved;

    // Require at least one voice in the selected bank
    bool anyFiles = false;
    for (uint8_t v = 0; v < VOICE_COUNT; v++) {
        if (fileScanner.numFiles[selectedBank][v] > 0) anyFiles = true;
    }
    if (!anyFiles) {
        // Find first bank that has files
        for (uint8_t b = 0; b < BANKS; b++) {
            for (uint8_t v = 0; v < VOICE_COUNT; v++) {
                if (fileScanner.numFiles[b][v] > 0) { selectedBank = b; goto foundBank; }
            }
        }
        reBoot(0);   // nothing on SD at all
    }
    foundBank:

    audioEngine.init(DEFAULT_CUTOFF, DEFAULT_RESONANCE, DEFAULT_LOFI_RATE);
    for (uint8_t v = 0; v < VOICE_COUNT; v++) applyVoiceParams(v);

    pinMode(RESET_BUTTON, INPUT);
    resetBtn.attach(RESET_BUTTON);
    resetBtn.interval(5);

    pinMode(RESET_CV, INPUT);
    attachInterrupt(RESET_CV, resetCvISR, RISING);
}

// ─── loop ────────────────────────────────────────────────────────────────────
void loop() {
    handleButton();
    updateVoct();
    updateControls();
    updateTriggers();
    updateLEDs();

    if (audioEngine.update() || audioEngine.error) reBoot(0);
}
