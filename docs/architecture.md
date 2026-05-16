# BandForge Architecture

## Layers

BandForge is split into a dependency-light C++ core and a JUCE desktop shell.

- `src/core`: project model, serialization, timeline editing, MIDI transforms, transport math, mixer helpers, library metadata, preview synthesis, and WAV export.
- `src/app`: JUCE UI and real-time audio callback integration.
- `assets/library`: starter library manifest and future open-license asset locations.
- `packaging`: Linux desktop packaging metadata.

## Core Concepts

- `Project`: top-level song document with tempo map, time signatures, tracks, and generated IDs.
- `Track`: audio, MIDI, drums, or master lane with mixer, instrument, clips, and automation.
- `Clip`: timeline region containing either audio metadata or editable MIDI data.
- `MixerChannel`: volume, pan, sends, mute/solo/record-arm/input-monitoring, and effect slots.
- `InstrumentSlot` / `EffectSlot`: serializable host state used by internal engines now and plugin hosting later.
- `SoundLibrary`: searchable metadata for loops and presets, including licensing fields.

## Audio Direction

The current `AudioEngine` is intentionally small: it renders deterministic preview audio from MIDI clips so the UI, transport, and export paths are functional before full recording and sample playback arrive.

The production audio engine should add:

- JUCE `AudioDeviceManager` routing with PipeWire/JACK-friendly configuration.
- Audio-file decoding, resampling, buffering, and clip playback.
- Record-arm input capture to project-bundle `Audio/`.
- Internal instruments and effects as reusable processors.
- Plugin hosting for VST3 first, LV2 after the app engine is stable.

## Save/Load

Project bundles should keep all media paths relative whenever possible. Missing files should remain visible in the UI and produce silent clips rather than failing the entire project load.

## UI Direction

The interface should stay familiar to DAW users:

- top transport/control bar.
- left track list.
- central arrange timeline.
- bottom editor panel for piano roll/audio editor/smart controls.
- side library/inspector.

Current “real DAW” behavior targets:

- track selection plus mute/solo/record-arm toggles.
- clip selection and timeline editing (move/trim with snap-to-grid).
- searchable library with insert actions that create regions at the playhead.
- editor tabs that reflect the current selection and update saved mixer/effect state.

Visual styling must remain original and avoid copying proprietary Apple assets or exact visual treatments.
