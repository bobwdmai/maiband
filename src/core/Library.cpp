#include "core/Library.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace bandforge {
namespace {

std::string lowercase(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool containsText(const std::string& haystack, const std::string& needle)
{
    if (needle.empty()) {
        return true;
    }
    return lowercase(haystack).find(lowercase(needle)) != std::string::npos;
}

bool tagsContain(const std::vector<std::string>& tags, const std::string& text)
{
    return std::any_of(tags.begin(), tags.end(), [&](const std::string& tag) {
        return containsText(tag, text);
    });
}

std::vector<std::string> stringsFromJson(const JsonValue* json)
{
    std::vector<std::string> values;
    if (json == nullptr || !json->isArray()) {
        return values;
    }
    for (const auto& value : json->array()) {
        values.push_back(value.stringValue());
    }
    return values;
}

JsonValue stringsToJson(const std::vector<std::string>& values)
{
    JsonValue::Array array;
    for (const auto& value : values) {
        array.push_back(value);
    }
    return array;
}

std::string readString(const JsonValue& object, const std::string& key, std::string fallback = {})
{
    const auto* value = object.find(key);
    return value != nullptr ? value->stringValue(std::move(fallback)) : std::move(fallback);
}

double readNumber(const JsonValue& object, const std::string& key, double fallback)
{
    const auto* value = object.find(key);
    return value != nullptr ? value->numberValue(fallback) : fallback;
}

JsonValue loopToJson(const LoopAsset& loop)
{
    JsonValue::Array notes;
    for (const auto& note : loop.midi.notes) {
        notes.push_back(JsonValue::Object {
            { "pitch", note.pitch },
            { "velocity", note.velocity },
            { "channel", note.channel },
            { "startBeat", note.startBeat },
            { "durationBeats", note.durationBeats },
        });
    }

    return JsonValue::Object {
        { "id", loop.id },
        { "name", loop.name },
        { "kind", toString(loop.kind) },
        { "path", loop.path },
        { "targetTrackKind", toString(loop.targetTrackKind) },
        { "instrument", loop.instrument },
        { "genre", loop.genre },
        { "key", loop.key },
        { "bpm", loop.bpm },
        { "beats", loop.beats },
        { "tags", stringsToJson(loop.tags) },
        { "license", loop.license },
        { "attribution", loop.attribution },
        { "midi", JsonValue::Object { { "notes", notes } } },
    };
}

LoopAsset loopFromJson(const JsonValue& json)
{
    LoopAsset loop;
    loop.id = readString(json, "id");
    loop.name = readString(json, "name");
    loop.kind = loopKindFromString(readString(json, "kind", "audio"));
    loop.path = readString(json, "path");
    loop.targetTrackKind = trackKindFromString(readString(json, "targetTrackKind", loop.kind == LoopKind::Midi ? "keys" : "audio"));
    loop.instrument = readString(json, "instrument");
    loop.genre = readString(json, "genre");
    loop.key = readString(json, "key");
    loop.bpm = readNumber(json, "bpm", 120.0);
    loop.beats = readNumber(json, "beats", 4.0);
    loop.tags = stringsFromJson(json.find("tags"));
    loop.license = readString(json, "license");
    loop.attribution = readString(json, "attribution");

    if (const auto* midi = json.find("midi"); midi != nullptr && midi->isObject()) {
        if (const auto* notes = midi->find("notes"); notes != nullptr && notes->isArray()) {
            for (const auto& note : notes->array()) {
                loop.midi.notes.push_back({
                    std::clamp(note.find("pitch") != nullptr ? note.find("pitch")->intValue(60) : 60, 0, 127),
                    std::clamp(note.find("velocity") != nullptr ? note.find("velocity")->intValue(96) : 96, 0, 127),
                    std::clamp(note.find("channel") != nullptr ? note.find("channel")->intValue(1) : 1, 1, 16),
                    note.find("startBeat") != nullptr ? note.find("startBeat")->numberValue(0.0) : 0.0,
                    note.find("durationBeats") != nullptr ? note.find("durationBeats")->numberValue(0.5) : 0.5,
                });
            }
        }
    }

    return loop;
}

JsonValue presetToJson(const Preset& preset)
{
    return JsonValue::Object {
        { "id", preset.id },
        { "name", preset.name },
        { "instrumentType", preset.instrumentType },
        { "category", preset.category },
        { "tags", stringsToJson(preset.tags) },
    };
}

Preset presetFromJson(const JsonValue& json)
{
    return {
        readString(json, "id"),
        readString(json, "name"),
        readString(json, "instrumentType"),
        readString(json, "category"),
        stringsFromJson(json.find("tags")),
    };
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

std::vector<LoopAsset> SoundLibrary::searchLoops(const LibraryQuery& query) const
{
    std::vector<LoopAsset> results;
    for (const auto& loop : loops) {
        const bool textMatches = query.text.empty()
            || containsText(loop.name, query.text)
            || containsText(toString(loop.kind), query.text)
            || containsText(loop.instrument, query.text)
            || containsText(loop.genre, query.text)
            || containsText(loop.key, query.text)
            || tagsContain(loop.tags, query.text);
        const bool instrumentMatches = query.instrument.empty() || containsText(loop.instrument, query.instrument);
        const bool genreMatches = query.genre.empty() || containsText(loop.genre, query.genre);
        const bool keyMatches = query.key.empty() || lowercase(loop.key) == lowercase(query.key);

        if (textMatches && instrumentMatches && genreMatches && keyMatches) {
            results.push_back(loop);
        }
    }
    return results;
}

std::vector<Preset> SoundLibrary::searchPresets(const LibraryQuery& query) const
{
    std::vector<Preset> results;
    for (const auto& preset : presets) {
        const bool textMatches = query.text.empty()
            || containsText(preset.name, query.text)
            || containsText(preset.instrumentType, query.text)
            || containsText(preset.category, query.text)
            || tagsContain(preset.tags, query.text);
        const bool instrumentMatches = query.instrument.empty() || containsText(preset.instrumentType, query.instrument);
        const bool genreMatches = query.genre.empty() || containsText(preset.category, query.genre);

        if (textMatches && instrumentMatches && genreMatches) {
            results.push_back(preset);
        }
    }
    return results;
}

JsonValue SoundLibrary::toJson() const
{
    JsonValue::Array loopArray;
    for (const auto& loop : loops) {
        loopArray.push_back(loopToJson(loop));
    }

    JsonValue::Array presetArray;
    for (const auto& preset : presets) {
        presetArray.push_back(presetToJson(preset));
    }

    return JsonValue::Object {
        { "schemaVersion", "1.0" },
        { "loops", loopArray },
        { "presets", presetArray },
    };
}

SoundLibrary SoundLibrary::fromJson(const JsonValue& json)
{
    SoundLibrary library;
    if (!json.isObject()) {
        return library;
    }

    if (const auto* loopsJson = json.find("loops"); loopsJson != nullptr && loopsJson->isArray()) {
        for (const auto& loop : loopsJson->array()) {
            library.loops.push_back(loopFromJson(loop));
        }
    }

    if (const auto* presetsJson = json.find("presets"); presetsJson != nullptr && presetsJson->isArray()) {
        for (const auto& preset : presetsJson->array()) {
            library.presets.push_back(presetFromJson(preset));
        }
    }

    return library;
}

SoundLibrary SoundLibrary::loadManifest(const std::filesystem::path& manifestPath)
{
    return fromJson(JsonValue::parse(readTextFile(manifestPath)));
}

void SoundLibrary::saveManifest(const std::filesystem::path& manifestPath) const
{
    writeTextFile(manifestPath, toJson().stringify(2));
}

std::string toString(LoopKind kind)
{
    return kind == LoopKind::Midi ? "midi" : "audio";
}

LoopKind loopKindFromString(const std::string& value)
{
    return value == "midi" || value == "pattern" ? LoopKind::Midi : LoopKind::Audio;
}

TrackKind preferredTrackKindForLoop(const LoopAsset& loop)
{
    if (loop.kind == LoopKind::Audio) {
        return TrackKind::Audio;
    }
    return loop.targetTrackKind == TrackKind::Audio ? TrackKind::Keys : loop.targetTrackKind;
}

} // namespace bandforge
