# Radio Music (Custom 3-Voice Firmware Build)

This repository is based on the original [Tom Whitwell Radio Music project](https://github.com/TomWhitwell/RadioMusic), with a custom firmware profile focused on:

- 3 trigger inputs -> 3 sample voices
- 12-bit lo-fi character
- resonant low-pass filtering
- simplified SD file handling (`1/`, `2/`, `3/`)
- web-based SD library conversion workflow

---

## Original project resources

- Full wiki: <https://github.com/TomWhitwell/RadioMusic/wiki>
- Hardware files: [RadioMusicHardware/](./RadioMusicHardware)
- Original firmware source: [RadioMusic/](./RadioMusic)

Original Radio Music is open hardware and CC-BY-SA licensed. See [License](#license).

---

## Current custom firmware behavior

Implemented in [RadioMusic/](./RadioMusic):

- Trigger mapping:
  - Voice 1: Reset CV input
  - Voice 2: Channel CV input
  - Voice 3: Time CV input
- Playback:
  - 3 independent sample voices mixed to mono output
  - per-voice volume + decay
  - one-shot style long decay behavior
- Tone:
  - custom lo-fi stage (12-bit quantization + sample-rate style decimation)
  - resonant low-pass filter on master bus
- Control UI:
  - short press / long press mode system
  - 4 setup pages (LED-indicated)
  - auto-audition while editing parameters (rate-limited)

---

## SD card file handling (custom profile)

Use exactly three root folders:

- `1` (voice 1 sample set)
- `2` (voice 2 sample set)
- `3` (voice 3 sample set)

Constraints:

- max 16 files per folder
- recommended format: mono 16-bit 32k WAV
- normal mode sample index is selected by pot and applied globally (slot 0..15)

If a folder has fewer than 16 files, out-of-range slots clamp to the last available file in that folder.

---

## Build firmware

### Toolchain

- Arduino CLI (or Arduino IDE with Teensy support)
- Teensy core/package installed
- target board: Teensy 3.1 / 3.2

### CLI build command

From repository root:

```bash
arduino-cli compile --fqbn teensy:avr:teensy31 ./RadioMusic
```

Flash using your preferred Teensy workflow (Arduino IDE or Teensy loader).

---

## Web Library Manager (GitHub Pages app)

Path: [docs/library-manager/index.html](./docs/library-manager/index.html)

Features:

- assign files to folders `1`, `2`, `3`
- drag-and-drop files between folders
- per-file browser preview
- convert to mono 16-bit 32k WAV in-browser
- optional silence trimming and normalization
- auto-name output files `01.wav` .. `16.wav`
- export SD-ready ZIP with folder layout
- export report with duration + peak/warning metrics

To publish on GitHub Pages, serve the `docs/` directory as Pages source.

---

## Repository layout (important paths)

- firmware sketch: [RadioMusic/RadioMusic.ino](./RadioMusic/RadioMusic.ino)
- audio engine: [RadioMusic/AudioEngine.h](./RadioMusic/AudioEngine.h), [RadioMusic/AudioEngine.cpp](./RadioMusic/AudioEngine.cpp)
- lo-fi effect: [RadioMusic/AudioEffectLoFi.h](./RadioMusic/AudioEffectLoFi.h), [RadioMusic/AudioEffectLoFi.cpp](./RadioMusic/AudioEffectLoFi.cpp)
- settings parser: [RadioMusic/Settings.h](./RadioMusic/Settings.h), [RadioMusic/Settings.cpp](./RadioMusic/Settings.cpp)
- custom notes: [RadioMusic/Radio_Music_Next.txt](./RadioMusic/Radio_Music_Next.txt)

---

## License

Radio Music is open hardware/software by Tom Whitwell under CC-BY-SA:

- <https://creativecommons.org/licenses/by-sa/3.0/>

This repository includes work derived from the original project and must preserve attribution/share-alike terms.
