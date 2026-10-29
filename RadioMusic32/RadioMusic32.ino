/*
 RadioMusic32

 Pot 1 (A9)   — sample index 0–15 (both voices)
 Pot 2 (A7)   — filter cutoff for selected voice
 CV 1 (pin 9) — trigger voice 1 (interrupt)
 CV 2 (A6)    — trigger voice 2
 CV 3 (A8)    — V/oct pitch for voice 2

 Short press  — toggle selected voice (LED 1 or 2)
 Hold + pot   — bank select A–P (LED binary), release confirms
 Hold 1.5s    — enter/exit setup

 Setup pages (4):
   0  LED 1  Voice 1: volume (pot1) + decay (pot2)
   1  LED 2  Voice 2: volume (pot1) + decay (pot2)
   2  LED 3  V/oct:   semitones (pot1) + fine tune (pot2)
   3  LED 4  Lo-fi:   sample rate (pot1) + on/off (pot2)
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

#define POT_SAMPLE   A9
#define POT_FILTER   A7
#define CV_TRIG1      9
#define CV_TRIG2     A6
#define CV_VOCT      A8
#define RESET_BUTTON  8
#define SD_CS         10

#define ADC_BITS      13
#define ADC_MAX       (1 << ADC_BITS)

#define SHORT_MS      10
#define LONG_MS     1500
#define CTRL_MS       20
#define AUDITION_MS  150
#define TRIG_HI     2300
#define TRIG_LO     1700

#define FILTER_RESONANCE  1.15f   // fixed slight bump, no user control
#define DEFAULT_LOFI_RATE 32000

#define EEPROM_BANK 0

// ─────────────────────────────────────────────────────────────────────────────
AudioEngine  audioEngine;
FileScanner  fileScanner;
LedControl   ledControl;
Bounce       btn;

enum Mode { NORMAL, BANK_SEL, SETUP };
Mode mode = NORMAL;

elapsedMillis btnTimer = 0;
bool btnHeld = false, longDone = false, bankChanged = false;

volatile bool trig1Pending = false;
bool trig2High = false;

uint8_t bank          = 0;
uint8_t sampleIndex   = 0;
uint8_t selectedVoice = 0;   // pot2 filter targets this voice

float voiceVol[VOICE_COUNT]   = {0.85f, 0.85f};
uint16_t voiceDecay[VOICE_COUNT] = {350, 350};
bool voiceOneShot[VOICE_COUNT]   = {false, false};

int  v2Semitones = 0;
float v2Fine     = 0.0f;

uint16_t lofiRate    = DEFAULT_LOFI_RATE;
bool     lofiEnabled = true;

uint8_t       setupPage = 0;
elapsedMillis auditTimer, ctrlTimer;
int prevPot1 = -1, prevPot2 = -1;

// ─────────────────────────────────────────────────────────────────────────────

float mapf(float x, float lo, float hi, float a, float b) {
    return a + (constrain(x,lo,hi)-lo)/(hi-lo)*(b-a);
}
int ravg(uint8_t pin, uint8_t n) {
    int32_t s=0; for(uint8_t i=0;i<n;i++) s+=analogRead(pin); return s/n;
}

void trig1ISR() { trig1Pending = true; }

void updateV2Speed() {
    float semi = v2Semitones + v2Fine/100.0f;
    audioEngine.setVoicePlaybackSpeed(1, powf(2.0f, semi/12.0f));
}

AudioFileInfo* getFile(uint8_t v) {
    uint8_t n = fileScanner.numFiles[bank][v];
    if (!n) return NULL;
    return &fileScanner.fileInfos[bank][v][sampleIndex < n ? sampleIndex : n-1];
}

void triggerVoice(uint8_t v) {
    AudioFileInfo* fi = getFile(v);
    if (fi) audioEngine.triggerVoice(v, fi);
}

void applyVoice(uint8_t v) {
    audioEngine.setVoiceVolume(v, voiceVol[v]);
    audioEngine.setVoiceDecayMs(v, voiceDecay[v], voiceOneShot[v]);
}

// ─── pot 2 → filter cutoff for selected voice (live) ─────────────────────────
void updateFilterPot() {
    int raw = ravg(POT_FILTER, 8);
    static int prev = -1;
    if (prev >= 0 && abs(raw - prev) < 30) return;
    prev = raw;
    float cutoff = mapf(raw, 0, ADC_MAX-1, 80.0f, 12000.0f);
    audioEngine.setVoiceFilter(selectedVoice, cutoff, FILTER_RESONANCE);
}

// ─── CV 3 → V/oct for voice 2 (live) ─────────────────────────────────────────
void updateVoct() {
    int raw = analogRead(CV_VOCT);
    static int prev = -1;
    if (prev >= 0 && abs(raw - prev) < 16) return;
    prev = raw;
    float semi = mapf(raw, 0, ADC_MAX-1, -24.0f, 24.0f) + v2Semitones + v2Fine/100.0f;
    audioEngine.setVoicePlaybackSpeed(1, powf(2.0f, semi/12.0f));
}

// ─── LEDs ────────────────────────────────────────────────────────────────────
void updateLEDs() {
    if (mode == SETUP)    { ledControl.single(setupPage); ledControl.showReset(true);  return; }
    if (mode == BANK_SEL) { ledControl.multi(bank & 0x0F); ledControl.showReset(true); return; }
    ledControl.single(selectedVoice);
    ledControl.showReset(false);
}

// ─── button ──────────────────────────────────────────────────────────────────
void handleButton() {
    btn.update();
    if (btn.rose()) {
        btnTimer = 0; btnHeld = true; longDone = false; bankChanged = false;
        if (mode == NORMAL) mode = BANK_SEL;
    }
    if (btnHeld && !longDone && btnTimer >= LONG_MS) {
        longDone = true;
        if (mode == BANK_SEL) {
            if (bankChanged) EEPROM.write(EEPROM_BANK, bank);
            mode = SETUP; setupPage = 0; prevPot1 = prevPot2 = -1;
        } else if (mode == SETUP) {
            mode = NORMAL;
        }
    }
    if (btn.fell()) {
        if (!longDone && btnTimer >= SHORT_MS) {
            if      (mode == BANK_SEL) { if (bankChanged) EEPROM.write(EEPROM_BANK, bank); mode = NORMAL; }
            else if (mode == SETUP)    { setupPage = (setupPage+1)%4; prevPot1 = prevPot2 = -1; }
            else                       { selectedVoice = (selectedVoice+1) % VOICE_COUNT; }
        }
        btnHeld = false; btnTimer = 0; longDone = false;
    }
}

// ─── triggers ────────────────────────────────────────────────────────────────
void updateTriggers() {
    if (trig1Pending) { trig1Pending = false; triggerVoice(0); }
    int cv2 = analogRead(CV_TRIG2);
    if (!trig2High && cv2 >= TRIG_HI) { trig2High = true;  triggerVoice(1); }
    else if (trig2High && cv2 <= TRIG_LO) trig2High = false;
}

// ─── controls ────────────────────────────────────────────────────────────────
void updateControls() {
    if (ctrlTimer < CTRL_MS) return;
    ctrlTimer = 0;

    int p1 = ravg(POT_SAMPLE, 12);
    int p2 = ravg(POT_FILTER, 12);
    bool c1 = (prevPot1 < 0 || abs(p1-prevPot1) > 40);
    bool c2 = (prevPot2 < 0 || abs(p2-prevPot2) > 40);

    if (mode == BANK_SEL) {
        uint8_t nb = (uint8_t)mapf(p1, 0, ADC_MAX-1, 0, BANKS-1);
        if (nb != bank) { bank = nb; bankChanged = true; }
    } else if (mode == NORMAL) {
        sampleIndex = (uint8_t)mapf(p1, 0, ADC_MAX-1, 0, MAX_FILES_PER_SLOT-1);
    } else if (mode == SETUP) {
        switch (setupPage) {
            case 0:
                voiceVol[0]   = mapf(p1, 0, ADC_MAX-1, 0.0f, 1.0f);
                voiceDecay[0] = (uint16_t)mapf(p2, 0, ADC_MAX-1, 20.0f, 3000.0f);
                voiceOneShot[0] = p2 > ADC_MAX - 256;
                applyVoice(0); break;
            case 1:
                voiceVol[1]   = mapf(p1, 0, ADC_MAX-1, 0.0f, 1.0f);
                voiceDecay[1] = (uint16_t)mapf(p2, 0, ADC_MAX-1, 20.0f, 3000.0f);
                voiceOneShot[1] = p2 > ADC_MAX - 256;
                applyVoice(1); break;
            case 2:
                v2Semitones = (int)mapf(p1, 0, ADC_MAX-1, -24.0f, 24.0f);
                v2Fine      = mapf(p2, 0, ADC_MAX-1, -50.0f, 50.0f);
                updateV2Speed(); break;
            case 3:
                lofiRate    = (uint16_t)mapf(p1, 0, ADC_MAX-1, 8000.0f, 32000.0f);
                lofiEnabled = p2 > (int)(ADC_MAX * 0.2f);
                audioEngine.setLofiEnabled(lofiEnabled);
                audioEngine.setLofiSampleRate(lofiRate); break;
        }
        if ((c1||c2) && auditTimer >= AUDITION_MS && setupPage < 2) {
            auditTimer = 0; triggerVoice(setupPage);
        }
    }
    prevPot1 = p1; prevPot2 = p2;
}

// ─────────────────────────────────────────────────────────────────────────────
void setup() {
    analogReadRes(ADC_BITS);
    ledControl.init();
    ledControl.single(0);

    SPI.setMOSI(7);
    SPI.setSCK(14);
    SPI.setMISO(12);
    if (!SD.begin(SD_CS)) reBoot(0);

    fileScanner.scan();

    uint8_t saved = EEPROM.read(EEPROM_BANK);
    if (saved < BANKS) bank = saved;

    bool found = false;
    for (uint8_t v=0; v<VOICE_COUNT && !found; v++) if (fileScanner.numFiles[bank][v]) found=true;
    if (!found) {
        for (uint8_t b=0; b<BANKS && !found; b++)
            for (uint8_t v=0; v<VOICE_COUNT && !found; v++)
                if (fileScanner.numFiles[b][v]) { bank=b; found=true; }
        if (!found) reBoot(0);
    }

    audioEngine.init(DEFAULT_LOFI_RATE);
    for (uint8_t v=0; v<VOICE_COUNT; v++) applyVoice(v);
    // Set both voice filters open by default
    audioEngine.setVoiceFilter(0, 12000.0f, FILTER_RESONANCE);
    audioEngine.setVoiceFilter(1, 12000.0f, FILTER_RESONANCE);

    pinMode(RESET_BUTTON, INPUT);
    btn.attach(RESET_BUTTON); btn.interval(5);
    pinMode(CV_TRIG1, INPUT);
    attachInterrupt(CV_TRIG1, trig1ISR, RISING);
}

void loop() {
    handleButton();
    updateFilterPot();   // pot 2 → cutoff for selected voice
    updateVoct();        // CV 3 → pitch for voice 2
    updateControls();    // pot 1 + setup pages
    updateTriggers();    // CV 1 + CV 2
    updateLEDs();
    if (audioEngine.update() || audioEngine.error) reBoot(0);
}
