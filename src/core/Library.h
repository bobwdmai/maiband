#pragma once

#include "core/Json.h"
#include "core/Model.h"

#include <filesystem>
#include <string>
#include <vector>

namespace bandforge {

enum class LoopKind {
    Audio,
    Midi
};

struct LoopAsset {
    std::string id;
    std::string name;
    LoopKind kind = LoopKind::Audio;
    std::string path;
    TrackKind targetTrackKind = TrackKind::Audio;
    std::string instrument;
    std::string genre;
    std::string key;
    double bpm = 120.0;
    double beats = 4.0;
    std::vector<std::string> tags;
    std::string license;
    std::string attribution;
    MidiClipData midi;
};

struct Preset {
    std::string id;
    std::string name;
    std::string instrumentType;
    std::string category;
    std::vector<std::string> tags;
};

struct LibraryQuery {
    std::string text;
    std::string instrument;
    std::string genre;
    std::string key;
};

class SoundLibrary {
public:
    std::vector<LoopAsset> loops;
    std::vector<Preset> presets;

    [[nodiscard]] std::vector<LoopAsset> searchLoops(const LibraryQuery& query) const;
    [[nodiscard]] std::vector<Preset> searchPresets(const LibraryQuery& query) const;

    [[nodiscard]] JsonValue toJson() const;
    static SoundLibrary fromJson(const JsonValue& json);
    static SoundLibrary loadManifest(const std::filesystem::path& manifestPath);
    void saveManifest(const std::filesystem::path& manifestPath) const;
};

[[nodiscard]] std::string toString(LoopKind kind);
[[nodiscard]] LoopKind loopKindFromString(const std::string& value);
[[nodiscard]] TrackKind preferredTrackKindForLoop(const LoopAsset& loop);

} // namespace bandforge
