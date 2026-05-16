#include "core/Model.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace bandforge {
namespace {

std::vector<MidiNote> chordPattern(int root)
{
    return {
        { root, 92, 1, 0.0, 2.0 },
        { root + 4, 88, 1, 0.0, 2.0 },
        { root + 7, 88, 1, 0.0, 2.0 },
        { root - 3, 90, 1, 2.0, 2.0 },
        { root, 86, 1, 2.0, 2.0 },
        { root + 4, 84, 1, 2.0, 2.0 },
        { root - 5, 88, 1, 4.0, 2.0 },
        { root - 1, 84, 1, 4.0, 2.0 },
        { root + 2, 84, 1, 4.0, 2.0 },
        { root - 7, 90, 1, 6.0, 2.0 },
        { root - 3, 86, 1, 6.0, 2.0 },
        { root, 84, 1, 6.0, 2.0 },
    };
}

std::vector<MidiNote> drumPattern(bool electronic)
{
    std::vector<MidiNote> notes;
    for (int bar = 0; bar < 2; ++bar) {
        const double base = static_cast<double>(bar) * 4.0;
        notes.push_back({ electronic ? 35 : 36, 110, 10, base + 0.0, 0.25 });
        notes.push_back({ 38, 96, 10, base + 1.0, 0.25 });
        notes.push_back({ electronic ? 35 : 36, 106, 10, base + 2.0, 0.25 });
        notes.push_back({ 38, 100, 10, base + 3.0, 0.25 });
        for (int step = 0; step < 8; ++step) {
            notes.push_back({ electronic ? 46 : 42, 72, 10, base + (static_cast<double>(step) * 0.5), 0.125 });
        }
    }
    return notes;
}

double readNumber(const JsonValue& object, const std::string& key, double fallback)
{
    const auto* value = object.find(key);
    return value != nullptr ? value->numberValue(fallback) : fallback;
}

int readInt(const JsonValue& object, const std::string& key, int fallback)
{
    const auto* value = object.find(key);
    return value != nullptr ? value->intValue(fallback) : fallback;
}

bool readBool(const JsonValue& object, const std::string& key, bool fallback)
{
    const auto* value = object.find(key);
    return value != nullptr ? value->boolValue(fallback) : fallback;
}

std::string readString(const JsonValue& object, const std::string& key, std::string fallback = {})
{
    const auto* value = object.find(key);
    return value != nullptr ? value->stringValue(std::move(fallback)) : std::move(fallback);
}

JsonValue numberMapToJson(const std::map<std::string, double>& values)
{
    JsonValue::Object object;
    for (const auto& [key, value] : values) {
        object[key] = value;
    }
    return object;
}

std::map<std::string, double> numberMapFromJson(const JsonValue* json)
{
    std::map<std::string, double> result;
    if (json == nullptr || !json->isObject()) {
        return result;
    }

    for (const auto& [key, value] : json->object()) {
        result[key] = value.numberValue();
    }
    return result;
}

JsonValue tempoMarkerToJson(const TempoMarker& marker)
{
    return JsonValue::Object {
        { "beat", marker.beat },
        { "bpm", marker.bpm },
    };
}

TempoMarker tempoMarkerFromJson(const JsonValue& json)
{
    return {
        readNumber(json, "beat", 0.0),
        readNumber(json, "bpm", 120.0),
    };
}

JsonValue timeSignatureToJson(const TimeSignatureMarker& marker)
{
    return JsonValue::Object {
        { "beat", marker.beat },
        { "numerator", marker.numerator },
        { "denominator", marker.denominator },
    };
}

TimeSignatureMarker timeSignatureFromJson(const JsonValue& json)
{
    return {
        readNumber(json, "beat", 0.0),
        readInt(json, "numerator", 4),
        readInt(json, "denominator", 4),
    };
}

JsonValue automationToJson(const AutomationLane& lane)
{
    JsonValue::Array points;
    for (const auto& point : lane.points) {
        points.push_back(JsonValue::Object {
            { "beat", point.beat },
            { "value", point.value },
        });
    }

    return JsonValue::Object {
        { "parameterId", lane.parameterId },
        { "points", points },
    };
}

AutomationLane automationFromJson(const JsonValue& json)
{
    AutomationLane lane;
    lane.parameterId = readString(json, "parameterId");
    if (const auto* points = json.find("points"); points != nullptr && points->isArray()) {
        for (const auto& point : points->array()) {
            lane.points.push_back({
                readNumber(point, "beat", 0.0),
                readNumber(point, "value", 0.0),
            });
        }
    }

    std::sort(lane.points.begin(), lane.points.end(), [](const auto& left, const auto& right) {
        return left.beat < right.beat;
    });
    return lane;
}

JsonValue midiNoteToJson(const MidiNote& note)
{
    return JsonValue::Object {
        { "pitch", note.pitch },
        { "velocity", note.velocity },
        { "channel", note.channel },
        { "startBeat", note.startBeat },
        { "durationBeats", note.durationBeats },
    };
}

MidiNote midiNoteFromJson(const JsonValue& json)
{
    return {
        clampMidiValue(readInt(json, "pitch", 60)),
        clampMidiValue(readInt(json, "velocity", 96)),
        std::clamp(readInt(json, "channel", 1), 1, 16),
        readNumber(json, "startBeat", 0.0),
        std::max(0.0, readNumber(json, "durationBeats", 1.0)),
    };
}

JsonValue midiControlToJson(const MidiControlEvent& event)
{
    return JsonValue::Object {
        { "controller", event.controller },
        { "value", event.value },
        { "channel", event.channel },
        { "beat", event.beat },
    };
}

MidiControlEvent midiControlFromJson(const JsonValue& json)
{
    return {
        clampMidiValue(readInt(json, "controller", 64)),
        clampMidiValue(readInt(json, "value", 0)),
        std::clamp(readInt(json, "channel", 1), 1, 16),
        readNumber(json, "beat", 0.0),
    };
}

JsonValue audioClipDataToJson(const AudioClipData& data)
{
    return JsonValue::Object {
        { "mediaPath", data.mediaPath },
        { "mediaStartSeconds", data.mediaStartSeconds },
        { "gainDb", data.gainDb },
        { "stretchToProjectTempo", data.stretchToProjectTempo },
    };
}

AudioClipData audioClipDataFromJson(const JsonValue* json)
{
    AudioClipData data;
    if (json == nullptr || !json->isObject()) {
        return data;
    }

    data.mediaPath = readString(*json, "mediaPath");
    data.mediaStartSeconds = readNumber(*json, "mediaStartSeconds", 0.0);
    data.gainDb = readNumber(*json, "gainDb", 0.0);
    data.stretchToProjectTempo = readBool(*json, "stretchToProjectTempo", true);
    return data;
}

JsonValue midiClipDataToJson(const MidiClipData& data)
{
    JsonValue::Array notes;
    for (const auto& note : data.notes) {
        notes.push_back(midiNoteToJson(note));
    }

    JsonValue::Array controls;
    for (const auto& event : data.controls) {
        controls.push_back(midiControlToJson(event));
    }

    return JsonValue::Object {
        { "notes", notes },
        { "controls", controls },
    };
}

MidiClipData midiClipDataFromJson(const JsonValue* json)
{
    MidiClipData data;
    if (json == nullptr || !json->isObject()) {
        return data;
    }

    if (const auto* notes = json->find("notes"); notes != nullptr && notes->isArray()) {
        for (const auto& note : notes->array()) {
            data.notes.push_back(midiNoteFromJson(note));
        }
    }

    if (const auto* controls = json->find("controls"); controls != nullptr && controls->isArray()) {
        for (const auto& event : controls->array()) {
            data.controls.push_back(midiControlFromJson(event));
        }
    }

    return data;
}

JsonValue clipToJson(const Clip& clip)
{
    return JsonValue::Object {
        { "id", static_cast<double>(clip.id) },
        { "kind", toString(clip.kind) },
        { "name", clip.name },
        { "color", clip.color },
        { "startBeat", clip.startBeat },
        { "lengthBeats", clip.lengthBeats },
        { "muted", clip.muted },
        { "audio", audioClipDataToJson(clip.audio) },
        { "midi", midiClipDataToJson(clip.midi) },
    };
}

Clip clipFromJson(const JsonValue& json)
{
    Clip clip;
    clip.id = static_cast<ClipId>(std::max(0, readInt(json, "id", 0)));
    clip.kind = clipKindFromString(readString(json, "kind", "audio"));
    clip.name = readString(json, "name", "Clip");
    clip.color = readString(json, "color", "#48A868");
    clip.startBeat = readNumber(json, "startBeat", 0.0);
    clip.lengthBeats = std::max(0.0, readNumber(json, "lengthBeats", 4.0));
    clip.muted = readBool(json, "muted", false);
    clip.audio = audioClipDataFromJson(json.find("audio"));
    clip.midi = midiClipDataFromJson(json.find("midi"));
    return clip;
}

JsonValue effectToJson(const EffectSlot& slot)
{
    return JsonValue::Object {
        { "id", slot.id },
        { "type", slot.type },
        { "name", slot.name },
        { "bypassed", slot.bypassed },
        { "parameters", numberMapToJson(slot.parameters) },
    };
}

EffectSlot effectFromJson(const JsonValue& json)
{
    return {
        readString(json, "id"),
        readString(json, "type"),
        readString(json, "name"),
        readBool(json, "bypassed", false),
        numberMapFromJson(json.find("parameters")),
    };
}

JsonValue instrumentToJson(const InstrumentSlot& slot)
{
    return JsonValue::Object {
        { "id", slot.id },
        { "type", slot.type },
        { "presetName", slot.presetName },
        { "parameters", numberMapToJson(slot.parameters) },
    };
}

InstrumentSlot instrumentFromJson(const JsonValue* json)
{
    InstrumentSlot slot;
    if (json == nullptr || !json->isObject()) {
        return slot;
    }

    slot.id = readString(*json, "id");
    slot.type = readString(*json, "type");
    slot.presetName = readString(*json, "presetName");
    slot.parameters = numberMapFromJson(json->find("parameters"));
    return slot;
}

JsonValue mixerToJson(const MixerChannel& mixer)
{
    JsonValue::Array effects;
    for (const auto& slot : mixer.effects) {
        effects.push_back(effectToJson(slot));
    }

    return JsonValue::Object {
        { "volumeDb", mixer.volumeDb },
        { "pan", mixer.pan },
        { "sendLevel", mixer.sendLevel },
        { "muted", mixer.muted },
        { "solo", mixer.solo },
        { "recordArmed", mixer.recordArmed },
        { "inputMonitoring", mixer.inputMonitoring },
        { "effects", effects },
    };
}

MixerChannel mixerFromJson(const JsonValue* json)
{
    MixerChannel mixer;
    if (json == nullptr || !json->isObject()) {
        return mixer;
    }

    mixer.volumeDb = readNumber(*json, "volumeDb", 0.0);
    mixer.pan = std::clamp(readNumber(*json, "pan", 0.0), -1.0, 1.0);
    mixer.sendLevel = std::clamp(readNumber(*json, "sendLevel", 0.0), 0.0, 1.0);
    mixer.muted = readBool(*json, "muted", false);
    mixer.solo = readBool(*json, "solo", false);
    mixer.recordArmed = readBool(*json, "recordArmed", false);
    mixer.inputMonitoring = readBool(*json, "inputMonitoring", false);

    if (const auto* effects = json->find("effects"); effects != nullptr && effects->isArray()) {
        for (const auto& effect : effects->array()) {
            mixer.effects.push_back(effectFromJson(effect));
        }
    }

    return mixer;
}

JsonValue trackToJson(const Track& track)
{
    JsonValue::Array clips;
    for (const auto& clip : track.clips) {
        clips.push_back(clipToJson(clip));
    }

    JsonValue::Array automation;
    for (const auto& lane : track.automation) {
        automation.push_back(automationToJson(lane));
    }

    return JsonValue::Object {
        { "id", static_cast<double>(track.id) },
        { "kind", toString(track.kind) },
        { "name", track.name },
        { "color", track.color },
        { "mixer", mixerToJson(track.mixer) },
        { "instrument", instrumentToJson(track.instrument) },
        { "clips", clips },
        { "automation", automation },
    };
}

Track trackFromJson(const JsonValue& json)
{
    Track track;
    track.id = static_cast<TrackId>(std::max(0, readInt(json, "id", 0)));
    track.kind = trackKindFromString(readString(json, "kind", "audio"));
    track.name = readString(json, "name", "Track");
    track.color = readString(json, "color", "#4D8CFF");
    track.mixer = mixerFromJson(json.find("mixer"));
    track.instrument = instrumentFromJson(json.find("instrument"));

    if (const auto* clips = json.find("clips"); clips != nullptr && clips->isArray()) {
        for (const auto& clip : clips->array()) {
            track.clips.push_back(clipFromJson(clip));
        }
    }

    if (const auto* automation = json.find("automation"); automation != nullptr && automation->isArray()) {
        for (const auto& lane : automation->array()) {
            track.automation.push_back(automationFromJson(lane));
        }
    }

    return track;
}

std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Could not open " + path.string());
    }
    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

void writeTextFile(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("Could not write " + path.string());
    }
    output << content;
}

} // namespace

double AutomationLane::valueAt(double beat, double fallback) const
{
    if (points.empty()) {
        return fallback;
    }
    if (beat <= points.front().beat) {
        return points.front().value;
    }
    if (beat >= points.back().beat) {
        return points.back().value;
    }

    for (std::size_t i = 1; i < points.size(); ++i) {
        const auto& right = points[i];
        if (beat <= right.beat) {
            const auto& left = points[i - 1];
            const double span = right.beat - left.beat;
            if (span <= 0.0) {
                return right.value;
            }
            const double t = (beat - left.beat) / span;
            return left.value + ((right.value - left.value) * t);
        }
    }

    return points.back().value;
}

BeatRange Clip::range() const noexcept
{
    return { startBeat, lengthBeats };
}

Track* Project::findTrack(TrackId id)
{
    const auto found = std::find_if(tracks.begin(), tracks.end(), [id](const Track& track) {
        return track.id == id;
    });
    return found == tracks.end() ? nullptr : &*found;
}

const Track* Project::findTrack(TrackId id) const
{
    const auto found = std::find_if(tracks.begin(), tracks.end(), [id](const Track& track) {
        return track.id == id;
    });
    return found == tracks.end() ? nullptr : &*found;
}

Clip* Project::findClip(TrackId trackId, ClipId clipId)
{
    auto* track = findTrack(trackId);
    if (track == nullptr) {
        return nullptr;
    }

    const auto found = std::find_if(track->clips.begin(), track->clips.end(), [clipId](const Clip& clip) {
        return clip.id == clipId;
    });
    return found == track->clips.end() ? nullptr : &*found;
}

const Clip* Project::findClip(TrackId trackId, ClipId clipId) const
{
    const auto* track = findTrack(trackId);
    if (track == nullptr) {
        return nullptr;
    }

    const auto found = std::find_if(track->clips.begin(), track->clips.end(), [clipId](const Clip& clip) {
        return clip.id == clipId;
    });
    return found == track->clips.end() ? nullptr : &*found;
}

TrackDefaults defaultsForTrackKind(TrackKind kind)
{
    switch (kind) {
    case TrackKind::Audio:
        return { "#4D8CFF", "#56B46E", "", "", {}, {} };
    case TrackKind::Drums:
    case TrackKind::DrumKit:
        return {
            "#F39C38",
            "#F39C38",
            "drum-machine",
            "Open Kit",
            { { "kickTune", 0.45 }, { "snareSnap", 0.62 }, { "hatTone", 0.74 }, { "room", 0.18 } },
            {},
        };
    case TrackKind::DrumRack:
        return {
            "#EF6A45",
            "#EF6A45",
            "drum-rack",
            "Punch Rack",
            { { "kickTune", 0.38 }, { "snareSnap", 0.7 }, { "hatTone", 0.82 }, { "drive", 0.18 } },
            {},
        };
    case TrackKind::BeatSequencer:
        return {
            "#E05BA6",
            "#E05BA6",
            "beat-sequencer",
            "Step Machine",
            { { "swing", 0.08 }, { "density", 0.55 }, { "drive", 0.12 }, { "room", 0.12 } },
            {},
        };
    case TrackKind::EightOhEight:
        return {
            "#D74E52",
            "#D74E52",
            "808",
            "Deep 808",
            { { "subTune", 0.32 }, { "decay", 0.68 }, { "click", 0.24 }, { "drive", 0.2 } },
            {},
        };
    case TrackKind::Keys:
    case TrackKind::Midi:
        return {
            "#C26BDB",
            "#B25FD8",
            "poly-synth",
            "Warm Keys",
            { { "attack", 0.02 }, { "release", 0.25 }, { "tone", 0.5 } },
            { { "fx-keys-reverb", "reverb", "Room Reverb", false, { { "mix", 0.22 }, { "size", 0.55 }, { "damping", 0.4 } } } },
        };
    case TrackKind::SynthLead:
        return { "#EC5E7A", "#EC5E7A", "lead-synth", "Bright Lead", { { "attack", 0.0 }, { "release", 0.14 }, { "tone", 0.82 }, { "glide", 0.12 } }, {} };
    case TrackKind::Bass:
        return { "#43A66F", "#43A66F", "bass-synth", "Round Bass", { { "attack", 0.01 }, { "release", 0.18 }, { "tone", 0.38 }, { "sub", 0.72 } }, {} };
    case TrackKind::Pad:
        return { "#5D82E6", "#5D82E6", "pad-synth", "Wide Pad", { { "attack", 0.55 }, { "release", 0.72 }, { "tone", 0.48 }, { "motion", 0.42 } }, { { "fx-pad-reverb", "reverb", "Space Reverb", false, { { "mix", 0.34 }, { "size", 0.78 } } } } };
    case TrackKind::Strings:
        return { "#B97B45", "#B97B45", "strings", "Studio Strings", { { "attack", 0.18 }, { "release", 0.45 }, { "tone", 0.56 }, { "ensemble", 0.66 } }, {} };
    case TrackKind::GuitarSynth:
        return { "#D49A3A", "#D49A3A", "guitar-synth", "Clean Synth Guitar", { { "attack", 0.01 }, { "release", 0.28 }, { "tone", 0.62 }, { "pluck", 0.5 } }, {} };
    case TrackKind::Arp:
        return { "#49A7D8", "#49A7D8", "arp-synth", "Pulse Arp", { { "rate", 0.5 }, { "gate", 0.42 }, { "tone", 0.7 }, { "motion", 0.5 } }, {} };
    case TrackKind::Pluck:
        return { "#76B852", "#76B852", "pluck-synth", "Glass Pluck", { { "attack", 0.0 }, { "release", 0.2 }, { "tone", 0.76 }, { "pluck", 0.85 } }, {} };
    case TrackKind::Sampler:
        return { "#8D78D8", "#8D78D8", "sampler", "Quick Sampler", { { "rootNote", 60.0 }, { "attack", 0.0 }, { "release", 0.12 }, { "gainDb", 0.0 } }, {} };
    case TrackKind::Master:
        return { "#A9B0BA", "#A9B0BA", "", "", {}, {} };
    }

    return { "#C26BDB", "#B25FD8", "poly-synth", "Warm Keys", { { "attack", 0.02 }, { "release", 0.25 }, { "tone", 0.5 } }, {} };
}

MidiClipData defaultStarterClipForTrackKind(TrackKind kind)
{
    MidiClipData data;
    if (isDrumTrackKind(kind)) {
        data.notes = drumPattern(kind == TrackKind::EightOhEight || kind == TrackKind::BeatSequencer || kind == TrackKind::DrumRack);
        return data;
    }

    switch (kind) {
    case TrackKind::Bass:
        data.notes = { { 36, 104, 1, 0.0, 0.75 }, { 36, 92, 1, 1.5, 0.5 }, { 33, 98, 1, 2.0, 0.75 }, { 31, 96, 1, 3.0, 0.75 } };
        break;
    case TrackKind::SynthLead:
        data.notes = { { 72, 98, 1, 0.0, 0.5 }, { 74, 92, 1, 0.5, 0.5 }, { 76, 96, 1, 1.0, 0.75 }, { 79, 102, 1, 2.0, 1.0 } };
        break;
    case TrackKind::Arp:
        for (int step = 0; step < 16; ++step) {
            const int notes[] = { 60, 64, 67, 72 };
            data.notes.push_back({ notes[step % 4], 82, 1, static_cast<double>(step) * 0.25, 0.18 });
        }
        break;
    case TrackKind::Pluck:
    case TrackKind::GuitarSynth:
        data.notes = { { 60, 96, 1, 0.0, 0.25 }, { 64, 86, 1, 0.5, 0.25 }, { 67, 92, 1, 1.0, 0.25 }, { 72, 98, 1, 1.5, 0.5 } };
        break;
    case TrackKind::Pad:
    case TrackKind::Strings:
    case TrackKind::Keys:
    case TrackKind::Midi:
        data.notes = chordPattern(60);
        break;
    case TrackKind::Sampler:
        data.notes = { { 60, 96, 1, 0.0, 0.5 }, { 60, 88, 1, 1.0, 0.5 }, { 67, 92, 1, 2.0, 0.5 }, { 65, 88, 1, 3.0, 0.5 } };
        break;
    default:
        break;
    }
    return data;
}

Track& Project::addTrack(TrackKind kind, std::string trackName)
{
    Track track;
    track.id = nextTrackId_++;
    track.kind = kind;
    track.name = std::move(trackName);
    const auto defaults = defaultsForTrackKind(kind);
    track.color = defaults.color;
    track.mixer.effects = defaults.effects;
    if (isMidiTrackKind(kind)) {
        track.instrument = {
            "instrument-" + std::to_string(track.id),
            defaults.instrumentType,
            defaults.presetName,
            defaults.instrumentParameters,
        };
    }

    tracks.push_back(std::move(track));
    return tracks.back();
}

Clip& Project::addAudioClip(TrackId trackId, std::string clipName, std::string mediaPath, double startBeat, double lengthBeats)
{
    auto* track = findTrack(trackId);
    if (track == nullptr) {
        throw std::invalid_argument("Cannot add audio clip to missing track");
    }

    Clip clip;
    clip.id = nextClipId_++;
    clip.kind = ClipKind::Audio;
    clip.name = std::move(clipName);
    clip.color = "#56B46E";
    clip.startBeat = std::max(0.0, startBeat);
    clip.lengthBeats = std::max(0.0, lengthBeats);
    clip.audio.mediaPath = std::move(mediaPath);
    track->clips.push_back(std::move(clip));
    return track->clips.back();
}

Clip& Project::addMidiClip(TrackId trackId, std::string clipName, double startBeat, double lengthBeats)
{
    auto* track = findTrack(trackId);
    if (track == nullptr) {
        throw std::invalid_argument("Cannot add MIDI clip to missing track");
    }

    Clip clip;
    clip.id = nextClipId_++;
    clip.kind = ClipKind::Midi;
    clip.name = std::move(clipName);
    clip.color = defaultsForTrackKind(track->kind).clipColor;
    clip.startBeat = std::max(0.0, startBeat);
    clip.lengthBeats = std::max(0.0, lengthBeats);
    track->clips.push_back(std::move(clip));
    return track->clips.back();
}

double Project::durationBeats() const
{
    double duration = 0.0;
    for (const auto& track : tracks) {
        for (const auto& clip : track.clips) {
            duration = std::max(duration, clip.range().endBeat());
        }
    }
    return duration;
}

double Project::bpmAt(double beat) const
{
    if (tempoMarkers.empty()) {
        return 120.0;
    }

    const TempoMarker* current = &tempoMarkers.front();
    for (const auto& marker : tempoMarkers) {
        if (marker.beat <= beat) {
            current = &marker;
        } else {
            break;
        }
    }
    return current->bpm;
}

JsonValue Project::toJson() const
{
    JsonValue::Array tempo;
    for (const auto& marker : tempoMarkers) {
        tempo.push_back(tempoMarkerToJson(marker));
    }

    JsonValue::Array signatures;
    for (const auto& marker : timeSignatures) {
        signatures.push_back(timeSignatureToJson(marker));
    }

    JsonValue::Array jsonTracks;
    for (const auto& track : tracks) {
        jsonTracks.push_back(trackToJson(track));
    }

    return JsonValue::Object {
        { "schemaVersion", schemaVersion },
        { "appVersion", appVersion },
        { "name", name },
        { "author", author },
        { "sampleRate", sampleRate },
        { "nextTrackId", static_cast<double>(nextTrackId_) },
        { "nextClipId", static_cast<double>(nextClipId_) },
        { "tempoMarkers", tempo },
        { "timeSignatures", signatures },
        { "tracks", jsonTracks },
    };
}

Project Project::fromJson(const JsonValue& json)
{
    if (!json.isObject()) {
        throw std::invalid_argument("Project JSON root must be an object");
    }

    Project project;
    project.schemaVersion = readString(json, "schemaVersion", CurrentSchemaVersion);
    project.appVersion = readString(json, "appVersion", "0.1.0");
    project.name = readString(json, "name", "Untitled BandForge Project");
    project.author = readString(json, "author");
    project.sampleRate = readNumber(json, "sampleRate", 48000.0);
    project.nextTrackId_ = static_cast<TrackId>(std::max(1, readInt(json, "nextTrackId", 1)));
    project.nextClipId_ = static_cast<ClipId>(std::max(1, readInt(json, "nextClipId", 1)));

    project.tempoMarkers.clear();
    if (const auto* tempo = json.find("tempoMarkers"); tempo != nullptr && tempo->isArray()) {
        for (const auto& marker : tempo->array()) {
            project.tempoMarkers.push_back(tempoMarkerFromJson(marker));
        }
    }
    if (project.tempoMarkers.empty()) {
        project.tempoMarkers.push_back({});
    }
    std::sort(project.tempoMarkers.begin(), project.tempoMarkers.end(), [](const auto& left, const auto& right) {
        return left.beat < right.beat;
    });

    project.timeSignatures.clear();
    if (const auto* signatures = json.find("timeSignatures"); signatures != nullptr && signatures->isArray()) {
        for (const auto& marker : signatures->array()) {
            project.timeSignatures.push_back(timeSignatureFromJson(marker));
        }
    }
    if (project.timeSignatures.empty()) {
        project.timeSignatures.push_back({});
    }
    std::sort(project.timeSignatures.begin(), project.timeSignatures.end(), [](const auto& left, const auto& right) {
        return left.beat < right.beat;
    });

    project.tracks.clear();
    if (const auto* tracksJson = json.find("tracks"); tracksJson != nullptr && tracksJson->isArray()) {
        for (const auto& track : tracksJson->array()) {
            project.tracks.push_back(trackFromJson(track));
        }
    }

    for (const auto& track : project.tracks) {
        project.nextTrackId_ = std::max(project.nextTrackId_, track.id + 1);
        for (const auto& clip : track.clips) {
            project.nextClipId_ = std::max(project.nextClipId_, clip.id + 1);
        }
    }

    return project;
}

void Project::saveBundle(const std::filesystem::path& bundleDirectory) const
{
    std::filesystem::create_directories(bundleDirectory / "Audio");
    std::filesystem::create_directories(bundleDirectory / "MIDI");
    std::filesystem::create_directories(bundleDirectory / "Renders");
    writeTextFile(bundleDirectory / ProjectFileName, toJson().stringify(2));
}

Project Project::loadBundle(const std::filesystem::path& bundleDirectory)
{
    return fromJson(JsonValue::parse(readTextFile(bundleDirectory / ProjectFileName)));
}

Project makeStarterProject()
{
    Project project;
    project.name = "New BandForge Song";
    project.author = "BandForge";
    project.tempoMarkers = { { 0.0, 120.0 } };

    auto& keys = project.addTrack(TrackKind::Keys, "Warm Keys");
    auto& keysClip = project.addMidiClip(keys.id, "Intro Chords", 0.0, 8.0);
    keysClip.midi = defaultStarterClipForTrackKind(keys.kind);

    auto& drums = project.addTrack(TrackKind::DrumKit, "Open Kit");
    auto& beat = project.addMidiClip(drums.id, "Four Beat", 0.0, 8.0);
    beat.midi = defaultStarterClipForTrackKind(drums.kind);

    auto& vocal = project.addTrack(TrackKind::Audio, "Vocal");
    vocal.mixer.recordArmed = true;
    vocal.mixer.effects.push_back({
        "fx-vocal-eq",
        "eq",
        "Clean EQ",
        false,
        { { "lowCutHz", 85.0 }, { "presenceDb", 1.8 }, { "airDb", 1.2 } },
    });

    return project;
}

} // namespace bandforge
