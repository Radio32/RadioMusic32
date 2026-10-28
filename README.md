# RadioMusic32

Custom Eurorack firmware for the [Music Thing Modular Radio Music](https://github.com/TomWhitwell/RadioMusic) hardware, based on a Teensy 3.2.

2-voice sample player with S950-style lo-fi character, per-voice resonant filter, and chromatic V/oct playback on voice 2.

---

## Hardware

Original Radio Music PCB — no hardware modifications required.

| Pin | Signal |
|-----|--------|
| A9 | Pot 1 — sample index |
| A7 | Pot 2 — filter cutoff (selected voice) |
| 9 | CV 1 — trigger voice 1 (interrupt) |
| A6 | CV 2 — trigger voice 2 |
| A8 | CV 3 — V/oct pitch voice 2 |
| 8 | Button |
| 3–6 | LEDs |
| 11 | Reset LED |
| A14 | DAC out |

---

## Controls

### Normal mode
| Control | Function |
|---------|----------|
| Pot 1 | Sample index 0–15 (both voices) |
| Pot 2 | Filter cutoff for selected voice |
| CV 1 | Trigger voice 1 |
| CV 2 | Trigger voice 2 |
| CV 3 | V/oct pitch for voice 2 |
| Short press | Toggle selected voice (LED 1 or 2 lit) |
| Hold + Pot 1 | Bank select A–P (LED binary) |
| Release < 1.5s | Confirm bank, back to normal |
| Hold ≥ 1.5s | Confirm bank + enter setup |

### Setup mode
| Short press | Next page |
|---|---|
| Hold ≥ 1.5s | Exit to normal |

| Page | LED | Pot 1 | Pot 2 |
|------|-----|-------|-------|
| 0 | 1 | Voice 1 volume | Voice 1 decay |
| 1 | 2 | Voice 2 volume | Voice 2 decay |
| 2 | 3 | V/oct semitones (−24…+24) | Fine tune (−50…+50 cents) |
| 3 | 4 | Lo-fi rate (8k–32k Hz) | Lo-fi on/off |

Auto-audition fires on pages 0 and 1 when a pot moves.

---

## Signal chain

```
Voice 1: SD → envelope → filter (pot 2, fixed 1.15 resonance) ─┐
Voice 2: SD → envelope → filter (pot 2, fixed 1.15 resonance) ─┴→ mix → lo-fi → DAC
```

The lo-fi stage reduces bit depth to 12 bits and decimates the sample rate,
recreating the gritty character of the Akai S950. At 32k it is clean; lower
values add texture and aliasing.

---

## SD card layout

```
A/
  1/  01.wav … 16.wav   ← voice 1 samples for bank A
  2/  01.wav … 16.wav   ← voice 2 samples for bank A
B/
  1/  …
  2/  …
…
P/
  1/  …
  2/  …
```

- 16 banks: **A–P**
- 2 voices per bank: **1** (sampler) and **2** (chromatic)
- Max 16 files per voice per bank
- Format: **mono 16-bit 32k WAV** (recommended)

Use the [web library manager](https://radio32.github.io/RadioMusic32/) to convert and package files.

---

## Build & flash

```bash
# Compile
arduino-cli compile --fqbn teensy:avr:teensy31 ./RadioMusic32

# Flash (Teensy connected)
arduino-cli upload --fqbn teensy:avr:teensy31 --port usb:124000 ./RadioMusic32
```

---

## Web Library Manager

**https://radio32.github.io/RadioMusic32/**

- 16 bank tabs (A–P), 2 voice columns each
- Drag-and-drop files between banks/voices
- Per-file preview
- Convert to mono 16-bit 32k WAV in-browser
- Trim silence + normalize options
- SD-ready ZIP download
- Export report with duration, peak, and warnings

---

## License

Based on [Radio Music](https://github.com/TomWhitwell/RadioMusic) by Tom Whitwell,
licensed CC-BY-SA 3.0. This derivative work preserves attribution and share-alike terms.

https://creativecommons.org/licenses/by-sa/3.0/
