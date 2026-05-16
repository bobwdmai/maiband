# BandForge

BandForge is an original Ubuntu-native music creation workstation inspired by familiar loop-and-track DAW workflows. It is not a GarageBand copy and does not use Apple branding, Apple assets, or Apple sound content.

This repo currently implements the first working foundation:

- C++20 core project model with tracks, clips, mixer state, instruments, effects, automation, tempo, and time signatures.
- JSON project bundle format with `project.json`, `Audio/`, `MIDI/`, and `Renders/`.
- Timeline editing helpers for moving, trimming, duplicating, splitting, quantizing clips, and snapshot undo/redo.
- MIDI utilities, tempo mapping, plugin/instrument registries, simple preview synthesis, audio-loop playback, library metadata search, and 16-bit WAV export.
- JUCE desktop app shell with transport, arrange timeline, track list, loop/instrument browser, piano roll preview, smart controls, and offline WAV export.
- Original starter library with 96 generated loops: 48 WAV loops plus 48 editable MIDI patterns across drums, 808, bass, keys, pads, strings, leads, arps, plucks, guitar synth, sampler, and FX fills.

## Ubuntu Dependencies

Install a compiler, CMake, JUCE dependencies, and a modern audio stack:

```bash
sudo apt update
sudo apt install build-essential cmake git pkg-config \
  libasound2-dev libjack-jackd2-dev libfreetype6-dev \
  libx11-dev libxext-dev libxinerama-dev libxrandr-dev \
  libxcursor-dev libxcomposite-dev libglu1-mesa-dev
```

JUCE can be provided in one of three ways:

```bash
# Option A: let CMake fetch JUCE
cmake -S . -B build -DBANDFORGE_FETCH_JUCE=ON

# Option B: clone JUCE into the repo
git clone https://github.com/juce-framework/JUCE.git third_party/JUCE
cmake -S . -B build

# Option C: install JUCE with CMake package support, then run normally
cmake -S . -B build
```

Build and run:

```bash
cmake --build build --parallel
./build/BandForge_artefacts/BandForge
```

Core-only build:

```bash
cmake -S . -B build-core -DBANDFORGE_BUILD_APP=OFF
cmake --build build-core --parallel
ctest --test-dir build-core --output-on-failure
```

Core-only CLI:

```bash
./build-core/bandforge_cli new Demo.bandforge
./build-core/bandforge_cli export Demo.bandforge Demo.wav
```

## Project Bundle Format

A saved project is a directory:

```text
My Song.bandforge/
  project.json
  Audio/
  MIDI/
  Renders/
```

`project.json` stores tempo, meter, tracks, clips, automation, instrument state, mixer state, and effect-slot state. Media files are referenced by relative paths inside the bundle.

## Current App Behavior

The JUCE UI boots into a starter song with keys, drum, and vocal tracks. It can save/open `.bandforge` bundles, select tracks/clips, move/trim regions on the timeline with snapping, toggle mute/solo/record-arm, add specialized MIDI tracks from the `+ MIDI` chooser, insert audio loops, MIDI loops, instruments, drum presets, and presets from a tabbed library panel with search plus instrument/genre/key/BPM/tag filters, and export a WAV render of the preview engine.

Available MIDI track kinds are Keys, Synth Lead, Bass, Pad, Strings, Guitar Synth, Arp, Pluck, Sampler, Drum Kit, Drum Rack, Beat Sequencer, and 808. Legacy `midi` and `drums` project files still load.

The next implementation pass should replace the preview renderer with JUCE-backed audio/MIDI recording, plugin hosting, real loop drag/drop, and higher-quality time stretching.

## Sound Library Policy

`assets/library/manifest.json` contains starter metadata, preset definitions, and embedded MIDI loop patterns. `assets/library/loops/` contains original generated WAV loops. Rebuild the pack with:

```bash
python3 tools/generate_loop_pack.py
```

Third-party loop/sample files must be original or open licensed before distribution, with license and attribution tracked in the manifest and summarized in `assets/library/LICENSES.md`.
