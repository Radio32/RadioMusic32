/*
 RadioMusic32
 2-voice sample player for Eurorack
 Voice 1 : pure sample player   — triggered by Reset CV (interrupt)
 Voice 2 : chromatic V/oct      — triggered by Channel CV, pitched by Time CV
 Banks A–P, 16 files per voice per bank
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
#define SHORT_PRESS_MS    10
#define LONG_PRESS_MS   1500   // long hold → setup mode; also bank-select limit
#define CTRL_UPDATE_MS    20
#define AUDITION_MS      150
#define BLINK_MS         200   // LED blink period for pages 4–5

// ─── trigger thresholds ──────────────────────────────────────────────────────
#define TRIG_HIGH  2300
#define TRIG_LOW   1700

// ─── EEPROM ──────────────────────────────────────────────────────────────────
#define EEPROM_BANK_ADDR 0

// ─── defaults ────────────────────────────────────────────────────────────────
#define DEFAULT_LOFI_RATE   32000
#define VOCT_SEMITONES         48   // ±2 octave span (tunable on page 5)

// ─── setup pages ─────────────────────────────────────────────────────────────
//  0  Voice 1 volume + decay
//  1  Voice 2 volume + decay
//  2  Voice 1 filter cutoff + resonance
//  3  Voice 2 filter cutoff + resonance
//  4  Lo-fi sample rate + enable/disable       ← LEDs blink
//  5  V/oct span + root trim                  ← LEDs blink
#define SETUP_PAGES  6

// ─── objects ─────────────────────────────────────────────────────────────────
AudioEngine  audioEngine;
FileScanner  fileScanner;
LedControl   ledControl;
Bounce       resetBtn;

// ─── mode ────────────────────────────────────────────────────────────────────
enum Mode { MODE_NORMAL, MODE_BANK_SELECT, MODE_SETUP };
Mode currentMode = MODE_NORMAL;

// ─── button ──────────────────────────────────────────────────────────────────
elapsedMillis buttonTimer     = 0;
bool          buttonHeld      = false;
bool          longPressHandled = false;

// ─── triggers ────────────────────────────────────────────────────────────────
volatile bool resetTrigPending = false;
bool          chanCvHigh       = false;

// ─── bank / sample ───────────────────────────────────────────────────────────
uint8_t selectedBank      = 0;
bool    bankSelectChanged = false;
uint8_t globalSampleIndex = 0;   // 0–15, stepped by short press

// ─── voice params ────────────────────────────────────────────────────────────
float    voiceVolume[VOICE_COUNT]       = {0.85f, 0.85f};
uint16_t voiceDecayMs[VOICE_COUNT]      = {350, 350};
bool     voiceOneShot[VOICE_COUNT]      = {false, false};
float    voiceFilterCutoff[VOICE_COUNT] = {8000.0f, 8000.0f};
float    voiceFilterRes[VOICE_COUNT]    = {0.9f, 0.9f};

// ─── lo-fi / V/oct ───────────────────────────────────────────────────────────
uint16_t lofiSampleRate  = DEFAULT_LOFI_RATE;
bool     lofiEnabled     = true;
int      voctSemitones   = VOCT_SEMITONES;
float    voctRootOffset  = 0.0f;
float    currentVoct     = 1.0f;
int      prevVoctAdc     = -1;

// ─── setup UI ────────────────────────────────────────────────────────────────
uint8_t       setupPage   = 0;
elapsedMillis auditTimer  = 0;
elapsedMillis ctrlTimer   = 0;
elapsedMillis blinkTimer  = 0;
bool          blinkState  = false;
int           prevChanPot = -1;
int           prevTimePot = -1;

// ─────────────────────────────────────────────────────────────────────────────

float mapf(float x, float lo, float hi, float outLo, float outHi) {
    x = constrain(x, lo, hi);
    return outLo + (x - lo) / (hi - lo) * (outHi - outLo);
}

int readAvg(uint8_t pin, uint8_t n) {
    int32_t s = 0;
    for (uint8_t i = 0; i < n; i++) s += analogRead(pin);
    return (int)(s / n);
}

void resetCvISR() { resetTrigPending = true; }

// ─── V/oct ───────────────────────────────────────────────────────────────────
float calculateVoct(int adc) {
    float semi = ((float)adc - (float)(ADC_MAX_VALUE / 2))
                 * (float)voctSemitones / (float)ADC_MAX_VALUE
                 + voctRootOffset;
    return powf(2.0f, semi / 12.0f);
}

// ─── file lookup ─────────────────────────────────────────────────────────────
AudioFileInfo* getFile(uint8_t voice) {
    uint8_t count = fileScanner.numFiles[selectedBank][voice];
    if (!count) return NULL;
    uint8_t idx = (globalSampleIndex < count) ? globalSampleIndex : count - 1;
    return &fileScanner.fileInfos[selectedBank][voice][idx];
}

// ─── voice trigger ───────────────────────────────────────────────────────────
void triggerVoice(uint8_t v) {
    AudioFileInfo* fi = getFile(v);
    if (!fi) return;
    if (v == 1) audioEngine.setVoicePlaybackSpeed(1, currentVoct);
    audioEngine.triggerVoice(v, fi);
}

void applyVoiceParams(uint8_t v) {
    audioEngine.setVoiceVolume(v, voiceVolume[v]);
    audioEngine.setVoiceDecayMs(v, voiceDecayMs[v], voiceOneShot[v]);
    audioEngine.setVoiceFilter(v, voiceFilterCutoff[v], voiceFilterRes[v]);
}

// ─── LEDs ────────────────────────────────────────────────────────────────────
void updateLEDs() {
    if (currentMode == MODE_SETUP) {
        // Pages 0–3: solid LED at page position
        // Pages 4–5: LED blinks at position (page - 4)
        bool extended = (setupPage >= 4);

        if (blinkTimer >= BLINK_MS) {
            blinkTimer = 0;
            blinkState = !blinkState;
        }

        if (!extended) {
            ledControl.single(setupPage);
            ledControl.showReset(true);
        } else {
            if (blinkState) {
                ledControl.single(setupPage - 4);
            } else {
                ledControl.multi(0);          // all off
            }
            ledControl.showReset(true);
        }
        return;
    }

    if (currentMode == MODE_BANK_SELECT) {
        ledControl.multi(selectedBank & 0x0F);
        ledControl.showReset(true);
        return;
    }

    // Normal: top LEDs = sample index in binary, reset LED off
    ledControl.multi(globalSampleIndex & 0x0F);
    ledControl.showReset(false);
}

// ─── button ──────────────────────────────────────────────────────────────────
void handleButton() {
    resetBtn.update();

    if (resetBtn.rose()) {
        buttonTimer      = 0;
        buttonHeld       = true;
        longPressHandled = false;
        bankSelectChanged = false;
        // Entering bank-select starts immediately on press
        if (currentMode == MODE_NORMAL) currentMode = MODE_BANK_SELECT;
    }

    // Long hold → enter setup (from bank-select) or exit setup
    if (buttonHeld && !longPressHandled && buttonTimer >= LONG_PRESS_MS) {
        longPressHandled = true;
        if (currentMode == MODE_BANK_SELECT) {
            currentMode = MODE_SETUP;
            setupPage   = 0;
            prevChanPot = -1;
            prevTimePot = -1;
        } else if (currentMode == MODE_SETUP) {
            currentMode = MODE_NORMAL;
        }
    }

    if (resetBtn.fell()) {
        buttonHeld = false;
        if (!longPressHandled && buttonTimer >= SHORT_PRESS_MS) {
            if (currentMode == MODE_BANK_SELECT) {
                // Confirm bank and return to normal
                if (bankSelectChanged) EEPROM.write(EEPROM_BANK_ADDR, selectedBank);
                currentMode = MODE_NORMAL;

            } else if (currentMode == MODE_SETUP) {
                // Cycle setup page
                setupPage = (setupPage + 1) % SETUP_PAGES;
                prevChanPot = -1;
                prevTimePot = -1;

            } else {
                // Normal mode: step sample index forward
                uint8_t maxIdx = 0;
                for (uint8_t v = 0; v < VOICE_COUNT; v++) {
                    if (fileScanner.numFiles[selectedBank][v] > maxIdx)
                        maxIdx = fileScanner.numFiles[selectedBank][v];
                }
                if (maxIdx > 0) {
                    globalSampleIndex = (globalSampleIndex + 1) % maxIdx;
                }
            }
        }
        buttonTimer      = 0;
        longPressHandled = false;
    }
}

// ─── triggers ────────────────────────────────────────────────────────────────
void updateTriggers() {
    if (resetTrigPending) {
        resetTrigPending = false;
        triggerVoice(0);
    }

    int chanCv = analogRead(CHAN_CV_PIN);
    if (!chanCvHigh && chanCv >= TRIG_HIGH) {
        chanCvHigh = true;
        triggerVoice(1);
    } else if (chanCvHigh && chanCv <= TRIG_LOW) {
        chanCvHigh = false;
    }
}

// ─── V/oct continuous read ───────────────────────────────────────────────────
void updateVoct() {
    int raw = analogRead(TIME_CV_PIN);
    if (prevVoctAdc < 0 || abs(raw - prevVoctAdc) > 16) {
        prevVoctAdc = raw;
        currentVoct = calculateVoct(raw);
        audioEngine.setVoicePlaybackSpeed(1, currentVoct);
    }
}

// ─── controls ────────────────────────────────────────────────────────────────
void updateControls() {
    if (ctrlTimer < CTRL_UPDATE_MS) return;
    ctrlTimer = 0;

    int chanPot = readAvg(CHAN_POT_PIN, 12);
    int timePot = readAvg(TIME_POT_PIN, 12);

    bool chanChanged = (prevChanPot < 0 || abs(chanPot - prevChanPot) > 40);
    bool timeChanged = (prevTimePot < 0 || abs(timePot - prevTimePot) > 40);

    // Bank select: pot scrolls through banks while button held
    if (currentMode == MODE_BANK_SELECT) {
        uint8_t nb = (uint8_t)mapf(chanPot, 0, ADC_MAX_VALUE - 1, 0, BANKS - 1);
        if (nb != selectedBank) { selectedBank = nb; bankSelectChanged = true; }
        prevChanPot = chanPot;
        prevTimePot = timePot;
        return;
    }

    // Normal: pot updates sample index live (complementary to short press)
    if (currentMode == MODE_NORMAL) {
        prevChanPot = chanPot;
        prevTimePot = timePot;
        return;
    }

    // Setup pages ─────────────────────────────────────────────────────────────
    if (currentMode == MODE_SETUP) {
        switch (setupPage) {
            case 0:  // Voice 1: volume + decay
                voiceVolume[0]  = mapf(chanPot, 0, ADC_MAX_VALUE-1, 0.0f, 1.0f);
                voiceDecayMs[0] = (uint16_t)mapf(timePot, 0, ADC_MAX_VALUE-1, 20.0f, 3000.0f);
                voiceOneShot[0] = timePot > (ADC_MAX_VALUE - 256);
                applyVoiceParams(0);
                break;

            case 1:  // Voice 2: volume + decay
                voiceVolume[1]  = mapf(chanPot, 0, ADC_MAX_VALUE-1, 0.0f, 1.0f);
                voiceDecayMs[1] = (uint16_t)mapf(timePot, 0, ADC_MAX_VALUE-1, 20.0f, 3000.0f);
                voiceOneShot[1] = timePot > (ADC_MAX_VALUE - 256);
                applyVoiceParams(1);
                break;

            case 2:  // Voice 1: filter cutoff + resonance
                voiceFilterCutoff[0] = mapf(chanPot, 0, ADC_MAX_VALUE-1, 200.0f, 12000.0f);
                voiceFilterRes[0]    = mapf(timePot, 0, ADC_MAX_VALUE-1, 0.7f, 4.3f);
                audioEngine.setVoiceFilter(0, voiceFilterCutoff[0], voiceFilterRes[0]);
                break;

            case 3:  // Voice 2: filter cutoff + resonance
                voiceFilterCutoff[1] = mapf(chanPot, 0, ADC_MAX_VALUE-1, 200.0f, 12000.0f);
                voiceFilterRes[1]    = mapf(timePot, 0, ADC_MAX_VALUE-1, 0.7f, 4.3f);
                audioEngine.setVoiceFilter(1, voiceFilterCutoff[1], voiceFilterRes[1]);
                break;

            case 4:  // Lo-fi: sample rate + enable
                lofiSampleRate = (uint16_t)mapf(chanPot, 0, ADC_MAX_VALUE-1, 8000.0f, 32000.0f);
                lofiEnabled    = timePot > 96;
                audioEngine.setLofiEnabled(lofiEnabled);
                audioEngine.setLofiSampleRate(lofiSampleRate);
                break;

            case 5:  // V/oct: span + root trim
                voctSemitones  = (int)mapf(chanPot, 0, ADC_MAX_VALUE-1, 24.0f, 96.0f);
                voctRootOffset = mapf(timePot, 0, ADC_MAX_VALUE-1, -12.0f, 12.0f);
                currentVoct    = calculateVoct(prevVoctAdc >= 0 ? prevVoctAdc : ADC_MAX_VALUE / 2);
                audioEngine.setVoicePlaybackSpeed(1, currentVoct);
                break;
        }

        // Auto-audition on pages 0–3 while turning a pot
        if ((chanChanged || timeChanged) && auditTimer >= AUDITION_MS && setupPage < 4) {
            auditTimer = 0;
            triggerVoice(setupPage < 2 ? setupPage : setupPage - 2);
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

    uint8_t saved = EEPROM.read(EEPROM_BANK_ADDR);
    if (saved < BANKS) selectedBank = saved;

    // Ensure selected bank has at least something; otherwise find first with files
    bool found = false;
    for (uint8_t v = 0; v < VOICE_COUNT && !found; v++)
        if (fileScanner.numFiles[selectedBank][v] > 0) found = true;
    if (!found) {
        for (uint8_t b = 0; b < BANKS && !found; b++)
            for (uint8_t v = 0; v < VOICE_COUNT && !found; v++)
                if (fileScanner.numFiles[b][v] > 0) { selectedBank = b; found = true; }
        if (!found) reBoot(0);
    }

    audioEngine.init(DEFAULT_LOFI_RATE);
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
