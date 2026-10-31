# RadioMusic32

Custom Eurorack firmware for the [Music Thing Modular Radio Music](https://github.com/TomWhitwell/RadioMusic) hardware, based on a Teensy 3.2.

2-voice sample player with Akai S900/S950-style 12-bit playback, band-limited (alias-free) pitch shifting, and chromatic V/oct playback on voice 2.

---

## Hardware

Original Radio Music PCB — no hardware modifications required.

| Pin | Signal |
|-----|--------|
| A9 | Pot 1 — sample index |
| A7 | Pot 2 — setup pages only |
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
| Pot 1 | Sample index 0–15 (both voices, LED binary) |
| CV 1 | Trigger voice 1 |
| CV 2 | Trigger voice 2 |
| CV 3 | V/oct pitch for voice 2 (±24 semitones) |
| Hold + Pot 1 | Bank select A–P (LED binary) |
| Release < 1.5s | Confirm bank, back to normal |
| Hold ≥ 1.5s | Confirm bank + enter setup |

### Setup mode
| Short press | Next page |
|---|---|
| Hold ≥ 1.5s | Exit to normal |

| Page | LED | Pot 1 | Pot 2 |
|------|-----|-------|-------|
| 0 | 1 | Voice 1 volume | Voice 1 decay (fully clockwise = whole sample) |
| 1 | 2 | Voice 2 volume | Voice 2 decay (fully clockwise = whole sample) |
| 2 | 3 | V/oct semitones (−24…+24) | Fine tune (−50…+50 cents) |
| 3 | 4 | S950 emulated sample rate (8k–44.1k Hz) | S950 mode on/off |

Defaults at power-up: whole-sample playback, S950 mode on at 32 kHz.
Total pitch is limited to +2 octaves.

Auto-audition fires on pages 0 and 1 when a pot moves.

---

## Signal chain

```
Voice 1: SD → S950 record filter → 12-bit → sinc resampler → S950 playback filter → envelope ─┐
Voice 2: SD → S950 record filter → 12-bit → sinc resampler → S950 playback filter → envelope ─┴→ mix → DAC
```

Both filters are 6-pole Butterworth low-passes, modelled on the MF6 switched-capacitor
filter in the S900/S950. All audio processing is fixed-point (Teensy 3.2 has no FPU).

- **Record filter** runs at the file's sample rate, before resampling. With S950 mode on it
  limits bandwidth to the emulated sample rate, just as the S950's input filter did. Because it
  runs per source sample, its cutoff follows pitch like the S950's clocked filter. When pitching
  up it also drops to the output Nyquist, so nothing folds back as alias.
- **12-bit** quantisation (S950 mode only) is applied to the stored sample, not the mix.
- **Sinc resampler** (16-tap windowed sinc) converts to the 44.1 kHz output rate with
  interpolation artefacts around −60 dB or better.
- **Playback filter** tracks the playback clock when pitching down, removing images the
  way the S950's reconstruction filter does.

Lower emulated sample rates give a darker, band-limited S950 tone, not aliasing grit.

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
- Format: **mono 16-bit 44.1k WAV** (recommended: matches the output rate, so voice 1 needs no rate conversion). 32k WAV is also accepted.

Use the [Sample Manager](https://radio32.github.io/RadioMusic32/) to lay out, convert and package files.

---

## Build & flash

```bash
# Compile
arduino-cli compile --fqbn teensy:avr:teensy31 ./RadioMusic32

# Flash (Teensy connected)
arduino-cli upload --fqbn teensy:avr:teensy31 --port usb:124000 ./RadioMusic32
```

---

## Sample Manager

**https://radio32.github.io/RadioMusic32/**

- 16 bank tabs (A–P) with sample counts, 2 voice lists each
- Reorder samples by dragging; drop on the other voice or on a bank tab to move them there (⌥ to copy)
- Cut/copy/paste between banks, **Move to…** dialog, undo/redo
- Open an existing card folder to rearrange it
- Preview, duration and waveform for each sample
- Convert to mono 16-bit 44.1k WAV in-browser (band-limited resampling), with trim and normalise options
- SD-ready ZIP download with an export report

---

## License

Based on [Radio Music](https://github.com/TomWhitwell/RadioMusic) by Tom Whitwell,
licensed CC-BY-SA 3.0. This derivative work preserves attribution and share-alike terms.

https://creativecommons.org/licenses/by-sa/3.0/
