#include "core/Library.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
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

static std::string sanitisePath(const std::filesystem::path& path)
{
    std::string s = path.string();
    s.erase(std::remove_if(s.begin(), s.end(),
                           [](unsigned char c) { return c < 0x20 || c == 0x7f; }),
            s.end());
    return s;
}

std::string readTextFile(const std::filesystem::path& path)
{
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Could not open " + sanitisePath(path));
    }
    return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

void writeTextFile(const std::filesystem::path& path, const std::string& content)
{
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("Could not write " + sanitisePath(path));
    }
    output << content;
}

std::string numberedId(std::string prefix, std::size_t value, int width = 5)
{
    std::ostringstream out;
    out << std::move(prefix) << std::setw(width) << std::setfill('0') << value;
    return out.str();
}

struct FactoryInstrument {
    TrackKind kind;
    const char* instrumentType;
    const char* category;
    const char* tag;
};

constexpr std::array<FactoryInstrument, 19> kFactoryInstruments {{
    { TrackKind::Keys,          "poly-synth",      "Keyboards",   "keys" },
    { TrackKind::ElectricPiano, "electric-piano",  "Keyboards",   "electric" },
    { TrackKind::Organ,         "organ",           "Keyboards",   "organ" },
    { TrackKind::SynthLead,     "lead-synth",      "Synth Lead",  "lead" },
    { TrackKind::Bass,          "bass-synth",      "Bass",        "bass" },
    { TrackKind::Pad,           "pad-synth",       "Pads",        "pad" },
    { TrackKind::Strings,       "strings",         "Strings",     "strings" },
    { TrackKind::Brass,         "brass",           "Brass",       "brass" },
    { TrackKind::Choir,         "choir",           "Choir",       "choir" },
    { TrackKind::Woodwind,      "woodwind",        "Woodwind",    "woodwind" },
    { TrackKind::GuitarSynth,   "guitar-synth",    "Guitar Synth","guitar" },
    { TrackKind::Arp,           "arp-synth",       "Arp",         "arp" },
    { TrackKind::Pluck,         "pluck-synth",     "Pluck",       "pluck" },
    { TrackKind::Mallet,        "mallet",          "Mallet",      "mallet" },
    { TrackKind::Sampler,       "sampler",         "Sampler",     "sampler" },
    { TrackKind::DrumKit,       "drum-machine",    "Drums",       "drums" },
    { TrackKind::DrumRack,      "drum-rack",       "Drum Rack",   "rack" },
    { TrackKind::BeatSequencer, "beat-sequencer",  "Beat Sequencer", "sequencer" },
    { TrackKind::EightOhEight,  "808",             "808",         "808" },
}};

constexpr std::array<const char*, 16> kFactoryStyles {{
    "Neon", "Dusty", "Velvet", "Crystal", "Analog", "Wide", "Tight", "Midnight",
    "Solar", "Lo-Fi", "Arena", "Pocket", "Dream", "Punchy", "Soft", "Future"
}};

constexpr std::array<const char*, 12> kFactoryGenres {{
    "Pop", "Rock", "Hip-Hop", "R&B", "House", "Techno",
    "Ambient", "Cinematic", "Funk", "Trap", "Synthwave", "Soul"
}};

constexpr std::array<const char*, 12> kFactoryKeys {{
    "C", "C minor", "D", "D minor", "E minor", "F", "F minor", "G", "G minor", "A minor", "Bb", "B minor"
}};

MidiClipData factoryLoopData(TrackKind kind, std::size_t seed)
{
    auto data = defaultStarterClipForTrackKind(kind);
    const int transpose = static_cast<int>(seed % 12) - 5;
    const double microOffset = (seed % 4 == 0) ? 0.0 : static_cast<double>(seed % 4) * 0.03125;

    for (auto& note : data.notes) {
        if (!isDrumTrackKind(kind)) {
            note.pitch = clampMidiValue(note.pitch + transpose);
        }
        note.velocity = std::clamp(note.velocity + static_cast<int>(seed % 17) - 8, 1, 127);
        if (note.startBeat > 0.0) {
            note.startBeat = std::max(0.0, note.startBeat + microOffset);
        }
    }

    if (!isDrumTrackKind(kind) && seed % 5 == 0) {
        const int root = clampMidiValue(48 + static_cast<int>(seed % 24));
        data.notes.push_back({ root, 78, 1, 3.5, 0.5 });
    }

    if (data.notes.empty()) {
        data.notes = defaultStarterClipForTrackKind(TrackKind::Keys).notes;
    }

    return data;
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

void SoundLibrary::addFactoryExpansion(std::size_t midiLoopCount,
                                       std::size_t instrumentPresetCount,
                                       std::size_t effectPresetCount)
{
    const auto alreadyExpanded = [](const auto& items, const char* prefix) {
        return std::any_of(items.begin(), items.end(), [&](const auto& item) {
            return item.id.rfind(prefix, 0) == 0;
        });
    };

    if (alreadyExpanded(loops, "factory-loop-") || alreadyExpanded(presets, "factory-inst-")) {
        return;
    }

    loops.reserve(loops.size() + midiLoopCount);
    presets.reserve(presets.size() + instrumentPresetCount + effectPresetCount);

    for (std::size_t i = 0; i < midiLoopCount; ++i) {
        const auto& instrument = kFactoryInstruments[i % kFactoryInstruments.size()];
        const auto* style = kFactoryStyles[(i / kFactoryInstruments.size()) % kFactoryStyles.size()];
        const auto* genre = kFactoryGenres[(i / (kFactoryInstruments.size() * 2)) % kFactoryGenres.size()];
        const auto* key = kFactoryKeys[(i / 3) % kFactoryKeys.size()];

        LoopAsset loop;
        loop.id = numberedId("factory-loop-", i);
        loop.name = std::string(style) + " " + displayName(instrument.kind) + " Loop " + std::to_string((i / kFactoryInstruments.size()) + 1);
        loop.kind = LoopKind::Midi;
        loop.targetTrackKind = instrument.kind;
        loop.instrument = displayName(instrument.kind);
        loop.genre = genre;
        loop.key = key;
        loop.bpm = 82.0 + static_cast<double>((i * 7) % 68);
        loop.beats = isDrumTrackKind(instrument.kind) ? 8.0 : ((i % 4 == 0) ? 8.0 : 4.0);
        loop.tags = { "factory", "generated", "midi", instrument.tag, lowercase(style), lowercase(genre) };
        loop.license = "Original BandForge procedural factory content";
        loop.attribution = "BandForge Factory";
        loop.midi = factoryLoopData(instrument.kind, i);
        loops.push_back(std::move(loop));
    }

    for (std::size_t i = 0; i < instrumentPresetCount; ++i) {
        const auto& instrument = kFactoryInstruments[i % kFactoryInstruments.size()];
        const auto* style = kFactoryStyles[(i / kFactoryInstruments.size()) % kFactoryStyles.size()];
        const auto* genre = kFactoryGenres[(i / (kFactoryInstruments.size() * 3)) % kFactoryGenres.size()];

        Preset preset;
        preset.id = numberedId("factory-inst-", i);
        preset.name = std::string(style) + " " + displayName(instrument.kind) + " " + std::to_string((i / kFactoryInstruments.size()) + 1);
        preset.instrumentType = instrument.instrumentType;
        preset.category = instrument.category;
        preset.tags = { "factory", "generated", "midi-instrument", instrument.tag, lowercase(style), lowercase(genre) };
        presets.push_back(std::move(preset));
    }

    constexpr std::array<const char*, 10> kFxNames {{
        "Vocal Space", "Tape Echo", "Crunch Room", "Telephone Throw", "Megaphone Hall",
        "Dream Plate", "Slap Drive", "Wide Delay", "Small Room", "Distorted Echo"
    }};
    constexpr std::array<const char*, 5> kFxTypes {{ "echo", "reverb", "distortion", "telephone", "megaphone" }};

    for (std::size_t i = 0; i < effectPresetCount; ++i) {
        const auto* style = kFactoryStyles[i % kFactoryStyles.size()];
        const auto* fxName = kFxNames[(i / kFactoryStyles.size()) % kFxNames.size()];
        const auto primary = std::string(kFxTypes[i % kFxTypes.size()]);
        const auto secondary = std::string(kFxTypes[(i / 3 + 1) % kFxTypes.size()]);

        Preset preset;
        preset.id = numberedId("factory-fx-", i);
        preset.name = std::string(style) + " " + fxName + " " + std::to_string((i / kFxNames.size()) + 1);
        preset.instrumentType = "audio-effect-chain";
        preset.category = "FX Chain";
        preset.tags = { "factory", "generated", "fx-chain", primary, secondary, lowercase(style) };
        presets.push_back(std::move(preset));
    }
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
