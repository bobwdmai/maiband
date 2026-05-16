#pragma once

#include <algorithm>
#include <cstdint>
#include <string>

namespace bandforge {

using TrackId = std::uint64_t;
using ClipId = std::uint64_t;

enum class TrackKind {
    Audio,
    Midi,
    Drums,
    Keys,
    SynthLead,
    Bass,
    Pad,
    Strings,
    GuitarSynth,
    Arp,
    Pluck,
    Sampler,
    DrumKit,
    DrumRack,
    BeatSequencer,
    EightOhEight,
    Master
};

enum class ClipKind {
    Audio,
    Midi
};

enum class TransportState {
    Stopped,
    Playing,
    Recording
};

struct BeatRange {
    double startBeat = 0.0;
    double lengthBeats = 0.0;

    [[nodiscard]] double endBeat() const noexcept
    {
        return startBeat + lengthBeats;
    }

    [[nodiscard]] bool contains(double beat) const noexcept
    {
        return beat >= startBeat && beat < endBeat();
    }

    [[nodiscard]] bool overlaps(const BeatRange& other) const noexcept
    {
        return startBeat < other.endBeat() && other.startBeat < endBeat();
    }
};

inline int clampMidiValue(int value) noexcept
{
    return std::clamp(value, 0, 127);
}

inline std::string toString(TrackKind kind)
{
    switch (kind) {
    case TrackKind::Audio:
        return "audio";
    case TrackKind::Midi:
        return "midi";
    case TrackKind::Drums:
        return "drums";
    case TrackKind::Keys:
        return "keys";
    case TrackKind::SynthLead:
        return "synth-lead";
    case TrackKind::Bass:
        return "bass";
    case TrackKind::Pad:
        return "pad";
    case TrackKind::Strings:
        return "strings";
    case TrackKind::GuitarSynth:
        return "guitar-synth";
    case TrackKind::Arp:
        return "arp";
    case TrackKind::Pluck:
        return "pluck";
    case TrackKind::Sampler:
        return "sampler";
    case TrackKind::DrumKit:
        return "drum-kit";
    case TrackKind::DrumRack:
        return "drum-rack";
    case TrackKind::BeatSequencer:
        return "beat-sequencer";
    case TrackKind::EightOhEight:
        return "808";
    case TrackKind::Master:
        return "master";
    }

    return "audio";
}

inline TrackKind trackKindFromString(const std::string& value)
{
    if (value == "midi") {
        return TrackKind::Midi;
    }
    if (value == "drums") {
        return TrackKind::Drums;
    }
    if (value == "keys") {
        return TrackKind::Keys;
    }
    if (value == "synth-lead") {
        return TrackKind::SynthLead;
    }
    if (value == "bass") {
        return TrackKind::Bass;
    }
    if (value == "pad") {
        return TrackKind::Pad;
    }
    if (value == "strings") {
        return TrackKind::Strings;
    }
    if (value == "guitar-synth") {
        return TrackKind::GuitarSynth;
    }
    if (value == "arp") {
        return TrackKind::Arp;
    }
    if (value == "pluck") {
        return TrackKind::Pluck;
    }
    if (value == "sampler") {
        return TrackKind::Sampler;
    }
    if (value == "drum-kit") {
        return TrackKind::DrumKit;
    }
    if (value == "drum-rack") {
        return TrackKind::DrumRack;
    }
    if (value == "beat-sequencer") {
        return TrackKind::BeatSequencer;
    }
    if (value == "808") {
        return TrackKind::EightOhEight;
    }
    if (value == "master") {
        return TrackKind::Master;
    }
    return TrackKind::Audio;
}

inline bool isDrumTrackKind(TrackKind kind) noexcept
{
    return kind == TrackKind::Drums
        || kind == TrackKind::DrumKit
        || kind == TrackKind::DrumRack
        || kind == TrackKind::BeatSequencer
        || kind == TrackKind::EightOhEight;
}

inline bool isMidiTrackKind(TrackKind kind) noexcept
{
    return kind == TrackKind::Midi
        || kind == TrackKind::Drums
        || kind == TrackKind::Keys
        || kind == TrackKind::SynthLead
        || kind == TrackKind::Bass
        || kind == TrackKind::Pad
        || kind == TrackKind::Strings
        || kind == TrackKind::GuitarSynth
        || kind == TrackKind::Arp
        || kind == TrackKind::Pluck
        || kind == TrackKind::Sampler
        || isDrumTrackKind(kind);
}

inline std::string displayName(TrackKind kind)
{
    switch (kind) {
    case TrackKind::Audio:
        return "Audio";
    case TrackKind::Midi:
        return "Software Instrument";
    case TrackKind::Drums:
        return "Drums";
    case TrackKind::Keys:
        return "Keys";
    case TrackKind::SynthLead:
        return "Synth Lead";
    case TrackKind::Bass:
        return "Bass";
    case TrackKind::Pad:
        return "Pad";
    case TrackKind::Strings:
        return "Strings";
    case TrackKind::GuitarSynth:
        return "Guitar Synth";
    case TrackKind::Arp:
        return "Arp";
    case TrackKind::Pluck:
        return "Pluck";
    case TrackKind::Sampler:
        return "Sampler";
    case TrackKind::DrumKit:
        return "Drum Kit";
    case TrackKind::DrumRack:
        return "Drum Rack";
    case TrackKind::BeatSequencer:
        return "Beat Sequencer";
    case TrackKind::EightOhEight:
        return "808";
    case TrackKind::Master:
        return "Master";
    }

    return "Track";
}

inline std::string toString(ClipKind kind)
{
    return kind == ClipKind::Midi ? "midi" : "audio";
}

inline ClipKind clipKindFromString(const std::string& value)
{
    return value == "midi" ? ClipKind::Midi : ClipKind::Audio;
}

} // namespace bandforge
