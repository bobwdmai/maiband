#pragma once

#include "core/Json.h"
#include "core/Types.h"

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bandforge {

struct TempoMarker {
    double beat = 0.0;
    double bpm = 120.0;
};

struct TimeSignatureMarker {
    double beat = 0.0;
    int numerator = 4;
    int denominator = 4;
};

struct AutomationPoint {
    double beat = 0.0;
    double value = 0.0;
};

struct AutomationLane {
    std::string parameterId;
    std::vector<AutomationPoint> points;

    [[nodiscard]] double valueAt(double beat, double fallback = 0.0) const;
};

struct MidiNote {
    int pitch = 60;
    int velocity = 96;
    int channel = 1;
    double startBeat = 0.0;
    double durationBeats = 1.0;
};

struct MidiControlEvent {
    int controller = 64;
    int value = 0;
    int channel = 1;
    double beat = 0.0;
};

struct AudioClipData {
    std::string mediaPath;
    double mediaStartSeconds = 0.0;
    double gainDb = 0.0;
    bool stretchToProjectTempo = true;
};

struct MidiClipData {
    std::vector<MidiNote> notes;
    std::vector<MidiControlEvent> controls;
};

struct Clip {
    ClipId id = 0;
    ClipKind kind = ClipKind::Audio;
    std::string name;
    std::string color = "#48A868";
    double startBeat = 0.0;
    double lengthBeats = 4.0;
    bool muted = false;
    AudioClipData audio;
    MidiClipData midi;

    [[nodiscard]] BeatRange range() const noexcept;
};

struct EffectSlot {
    std::string id;
    std::string type;
    std::string name;
    bool bypassed = false;
    std::map<std::string, double> parameters;
};

struct InstrumentSlot {
    std::string id;
    std::string type;
    std::string presetName;
    std::map<std::string, double> parameters;
};

struct MixerChannel {
    double volumeDb = 0.0;
    double pan = 0.0;
    double sendLevel = 0.0;
    bool muted = false;
    bool solo = false;
    bool recordArmed = false;
    bool inputMonitoring = false;
    std::vector<EffectSlot> effects;
};

struct Track {
    TrackId id = 0;
    TrackKind kind = TrackKind::Audio;
    std::string name;
    std::string color = "#4D8CFF";
    MixerChannel mixer;
    InstrumentSlot instrument;
    std::vector<Clip> clips;
    std::vector<AutomationLane> automation;
};

struct TrackDefaults {
    std::string color;
    std::string clipColor;
    std::string instrumentType;
    std::string presetName;
    std::map<std::string, double> instrumentParameters;
    std::vector<EffectSlot> effects;
};

class Project {
public:
    static constexpr const char* CurrentSchemaVersion = "1.0";
    static constexpr const char* ProjectFileName = "project.json";

    std::string schemaVersion = CurrentSchemaVersion;
    std::string appVersion = "0.1.0";
    std::string name = "Untitled BandForge Project";
    std::string author;
    double sampleRate = 48000.0;
    std::vector<TempoMarker> tempoMarkers { TempoMarker {} };
    std::vector<TimeSignatureMarker> timeSignatures { TimeSignatureMarker {} };
    std::vector<Track> tracks;

    [[nodiscard]] Track* findTrack(TrackId id);
    [[nodiscard]] const Track* findTrack(TrackId id) const;
    [[nodiscard]] Clip* findClip(TrackId trackId, ClipId clipId);
    [[nodiscard]] const Clip* findClip(TrackId trackId, ClipId clipId) const;

    Track& addTrack(TrackKind kind, std::string name);
    Clip& addAudioClip(TrackId trackId, std::string name, std::string mediaPath, double startBeat, double lengthBeats);
    Clip& addMidiClip(TrackId trackId, std::string name, double startBeat, double lengthBeats);

    [[nodiscard]] double durationBeats() const;
    [[nodiscard]] double bpmAt(double beat) const;

    [[nodiscard]] JsonValue toJson() const;
    static Project fromJson(const JsonValue& json);

    void saveBundle(const std::filesystem::path& bundleDirectory) const;
    static Project loadBundle(const std::filesystem::path& bundleDirectory);

private:
    TrackId nextTrackId_ = 1;
    ClipId nextClipId_ = 1;

    friend JsonValue toJson(const Project& project);
};

[[nodiscard]] TrackDefaults defaultsForTrackKind(TrackKind kind);
[[nodiscard]] MidiClipData defaultStarterClipForTrackKind(TrackKind kind);
Project makeStarterProject();

} // namespace bandforge
