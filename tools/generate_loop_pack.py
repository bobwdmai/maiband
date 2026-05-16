#!/usr/bin/env python3
import json
import math
import struct
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
LIBRARY = ROOT / "assets" / "library"
LOOPS = LIBRARY / "loops"
SAMPLE_RATE = 22050
LICENSE = "Original BandForge content; generated programmatically for this repository"
ATTRIBUTION = "BandForge"

CATEGORIES = [
    ("Drums", "drum-kit", "Rock", "C", 120, 4, "drum-kit"),
    ("808", "808", "Hip Hop", "C", 140, 4, "808"),
    ("Bass", "bass", "Pop", "C", 110, 4, "bass"),
    ("Keys", "keys", "Pop", "C", 100, 8, "keys"),
    ("Pads", "pad", "Ambient", "A minor", 90, 8, "pad"),
    ("Strings", "strings", "Cinematic", "G minor", 84, 8, "strings"),
    ("Leads", "synth-lead", "Electronic", "E minor", 128, 4, "synth-lead"),
    ("Arps", "arp", "Electronic", "D minor", 126, 4, "arp"),
    ("Plucks", "pluck", "Dance", "F minor", 124, 4, "pluck"),
    ("Guitar Synth", "guitar-synth", "Indie", "G", 112, 4, "guitar-synth"),
    ("Sampler", "sampler", "Lo Fi", "C minor", 92, 4, "sampler"),
    ("FX Fills", "sampler", "Electronic", "C", 128, 2, "sampler"),
]

PRESETS = [
    ("preset-warm-keys", "Warm Keys", "poly-synth", "Keyboards", ["soft", "starter"]),
    ("preset-bright-lead", "Bright Lead", "lead-synth", "Synth Lead", ["lead", "synth", "melody"]),
    ("preset-round-bass", "Round Bass", "bass-synth", "Bass", ["bass", "low", "warm"]),
    ("preset-wide-pad", "Wide Pad", "pad-synth", "Pads", ["pad", "ambient", "wide"]),
    ("preset-studio-strings", "Studio Strings", "strings", "Strings", ["strings", "orchestral", "ensemble"]),
    ("preset-synth-guitar", "Clean Synth Guitar", "guitar-synth", "Guitar Synth", ["guitar", "pluck", "clean"]),
    ("preset-pulse-arp", "Pulse Arp", "arp-synth", "Arp", ["arp", "sequence", "pulse"]),
    ("preset-glass-pluck", "Glass Pluck", "pluck-synth", "Pluck", ["pluck", "bell", "short"]),
    ("preset-quick-sampler", "Quick Sampler", "sampler", "Sampler", ["sampler", "sample", "starter"]),
    ("preset-open-kit", "Open Kit", "drum-machine", "Drums", ["starter", "tight"]),
    ("preset-punch-rack", "Punch Rack", "drum-rack", "Drum Rack", ["drums", "rack", "punch"]),
    ("preset-step-machine", "Step Machine", "beat-sequencer", "Beat Sequencer", ["drums", "steps", "sequencer"]),
    ("preset-deep-808", "Deep 808", "808", "808", ["808", "sub", "drums"]),
    ("preset-clean-vocal", "Clean Vocal Chain", "audio-effect-chain", "Voice", ["eq", "compressor", "reverb"]),
]

def slug(value):
    result = []
    for ch in value.lower():
        if ch.isalnum():
            result.append(ch)
        elif result and result[-1] != "-":
            result.append("-")
    return "".join(result).strip("-")

def midi_frequency(note):
    return 440.0 * (2.0 ** ((note - 69) / 12.0))

def envelope(local, duration, fast=False):
    attack = 0.005 if fast else 0.03
    release = 0.06 if fast else 0.2
    return max(0.0, min(local / attack, (duration - local) / release, 1.0))

def tone(t, note, amp, shape):
    frequency = midi_frequency(note)
    if shape == "bass":
        return amp * (math.sin(2 * math.pi * frequency * 0.5 * t) + 0.3 * math.sin(2 * math.pi * frequency * t))
    if shape == "pad":
        return amp * (math.sin(2 * math.pi * frequency * t) + 0.25 * math.sin(2 * math.pi * frequency * 1.5 * t))
    if shape == "pluck":
        return amp * (math.sin(2 * math.pi * frequency * t) + 0.18 * math.sin(2 * math.pi * frequency * 3 * t))
    if shape == "string":
        return amp * (math.sin(2 * math.pi * frequency * t) + 0.14 * math.sin(2 * math.pi * frequency * 2.01 * t))
    return amp * (math.sin(2 * math.pi * frequency * t) + 0.22 * math.sin(2 * math.pi * frequency * 2 * t))

def pattern_notes(track_kind, variation):
    if track_kind in {"drum-kit", "drum-rack", "beat-sequencer", "808"}:
        kick = 35 if track_kind == "808" else 36
        hat = 46 if track_kind in {"808", "beat-sequencer"} else 42
        notes = []
        for bar in range(2):
            base = bar * 4.0
            notes += [(kick, 112, 10, base, 0.25), (38, 96, 10, base + 1.0, 0.25), (kick, 106, 10, base + 2.0, 0.25), (38, 102, 10, base + 3.0, 0.25)]
            for step in range(8):
                if variation == 4 and step in {3, 7}:
                    notes.append((hat, 88, 10, base + step * 0.5 + 0.25, 0.1))
                notes.append((hat, 72 + (step % 2) * 8, 10, base + step * 0.5, 0.12))
        return notes
    if track_kind == "bass":
        return [(36, 106, 1, 0.0, 0.75), (36, 90, 1, 1.5, 0.5), (33, 100, 1, 2.0, 0.75), (31, 96, 1, 3.0, 0.75)]
    if track_kind == "synth-lead":
        return [(72, 98, 1, 0.0, 0.5), (74, 92, 1, 0.5, 0.5), (76, 96, 1, 1.0, 0.75), (79, 102, 1, 2.0, 1.0)]
    if track_kind == "arp":
        chord = [60, 64, 67, 72]
        return [(chord[(step + variation) % 4], 82, 1, step * 0.25, 0.18) for step in range(16)]
    if track_kind in {"pluck", "guitar-synth", "sampler"}:
        return [(60, 96, 1, 0.0, 0.25), (64, 86, 1, 0.5, 0.25), (67, 92, 1, 1.0, 0.25), (72, 98, 1, 1.5, 0.5)]
    root = 60 if track_kind != "strings" else 55
    return [(root, 92, 1, 0.0, 2.0), (root + 4, 88, 1, 0.0, 2.0), (root + 7, 88, 1, 0.0, 2.0), (root - 3, 90, 1, 2.0, 2.0), (root, 86, 1, 2.0, 2.0), (root + 4, 84, 1, 2.0, 2.0)]

def synth_sample(track_kind, note, t, local, duration, velocity):
    amp = (velocity / 127.0) * 0.18 * envelope(local, duration, track_kind in {"drum-kit", "808", "drum-rack", "beat-sequencer", "pluck", "guitar-synth"})
    if track_kind in {"drum-kit", "drum-rack", "beat-sequencer", "808"}:
        if note <= 36:
            return amp * math.sin(2 * math.pi * (56 if track_kind == "808" else 92) * t) * math.exp(-local * (8 if track_kind == "808" else 18))
        if note == 38:
            return amp * math.sin(2 * math.pi * 190 * t) * math.exp(-local * 24)
        return amp * math.sin(2 * math.pi * 6200 * t) * math.exp(-local * 50)
    shape = "pad" if track_kind in {"pad", "keys"} else "string" if track_kind == "strings" else "bass" if track_kind == "bass" else "pluck" if track_kind in {"pluck", "guitar-synth", "sampler"} else "lead"
    return tone(t, note - 12 if track_kind == "bass" else note, amp, shape)

def write_loop(path, bpm, beats, track_kind, variation):
    seconds = beats * 60.0 / bpm
    frames = int(seconds * SAMPLE_RATE)
    notes = pattern_notes(track_kind, variation)
    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(SAMPLE_RATE)
        for frame in range(frames):
            t = frame / SAMPLE_RATE
            beat = t * bpm / 60.0
            value = 0.0
            for pitch, velocity, _channel, start, duration in notes:
                if start <= beat < start + duration:
                    local_beat = beat - start
                    value += synth_sample(track_kind, pitch, t, local_beat * 60.0 / bpm, duration * 60.0 / bpm, velocity)
            value = max(-0.95, min(0.95, value))
            output.writeframesraw(struct.pack("<h", int(value * 32767)))

def midi_json(notes):
    return {
        "notes": [
            {
                "pitch": pitch,
                "velocity": velocity,
                "channel": channel,
                "startBeat": start,
                "durationBeats": duration,
            }
            for pitch, velocity, channel, start, duration in notes
        ]
    }

def main():
    LOOPS.mkdir(parents=True, exist_ok=True)
    loops = []
    moods = ["Clean", "Warm", "Wide", "Drive"]
    for instrument, track_kind, genre, key, bpm, beats, tag in CATEGORIES:
        for variation, mood in enumerate(moods, start=1):
            stem = f"{slug(instrument)}-{slug(mood)}-{variation}"
            wav_name = f"{stem}.wav"
            write_loop(LOOPS / wav_name, bpm, beats, track_kind, variation)
            base_tags = [tag, genre.lower().replace(" ", "-"), mood.lower()]
            loops.append({
                "id": f"loop-audio-{stem}",
                "name": f"{mood} {instrument}",
                "kind": "audio",
                "path": f"loops/{wav_name}",
                "targetTrackKind": "audio",
                "instrument": instrument,
                "genre": genre,
                "key": key,
                "bpm": bpm,
                "beats": beats,
                "tags": base_tags + ["audio", "original"],
                "license": LICENSE,
                "attribution": ATTRIBUTION
            })
            loops.append({
                "id": f"loop-midi-{stem}",
                "name": f"{mood} {instrument} Pattern",
                "kind": "midi",
                "path": "",
                "targetTrackKind": track_kind,
                "instrument": instrument,
                "genre": genre,
                "key": key,
                "bpm": bpm,
                "beats": beats,
                "tags": base_tags + ["midi", "editable"],
                "license": LICENSE,
                "attribution": ATTRIBUTION,
                "midi": midi_json(pattern_notes(track_kind, variation))
            })
    manifest = {
        "schemaVersion": "1.0",
        "loops": loops,
        "presets": [
            {
                "id": preset_id,
                "name": name,
                "instrumentType": instrument_type,
                "category": category,
                "tags": tags,
            }
            for preset_id, name, instrument_type, category, tags in PRESETS
        ],
    }
    (LIBRARY / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

if __name__ == "__main__":
    main()
