#include "core/AudioEngine.h"
#include "core/Exporter.h"
#include "core/InstrumentHost.h"
#include "core/Library.h"
#include "core/Midi.h"
#include "core/Model.h"
#include "core/PluginHost.h"
#include "core/Timeline.h"
#include "core/Transport.h"
#include "core/Undo.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void check(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void checkNear(double actual, double expected, double tolerance, const std::string& message)
{
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error(message + " (actual=" + std::to_string(actual) + ", expected=" + std::to_string(expected) + ")");
    }
}

std::uintmax_t fileSize(const std::filesystem::path& path)
{
    return std::filesystem::exists(path) ? std::filesystem::file_size(path) : 0;
}

std::filesystem::path sourceRoot()
{
#ifdef BANDFORGE_SOURCE_DIR
    return std::filesystem::path(BANDFORGE_SOURCE_DIR);
#else
    return std::filesystem::current_path();
#endif
}

std::filesystem::path libraryPath(const std::filesystem::path& relative)
{
    return sourceRoot() / "assets" / "library" / relative;
}

void testProjectSerialization()
{
    auto project = bandforge::makeStarterProject();
    const auto json = project.toJson().stringify(2);
    auto loaded = bandforge::Project::fromJson(bandforge::JsonValue::parse(json));

    check(loaded.name == project.name, "project name should round-trip");
    check(loaded.tracks.size() == project.tracks.size(), "track count should round-trip");
    check(loaded.tracks.front().clips.front().midi.notes.size() == project.tracks.front().clips.front().midi.notes.size(), "MIDI notes should round-trip");
}

std::vector<bandforge::TrackKind> midiTrackKinds()
{
    return {
        bandforge::TrackKind::Midi,
        bandforge::TrackKind::Drums,
        bandforge::TrackKind::Keys,
        bandforge::TrackKind::SynthLead,
        bandforge::TrackKind::Bass,
        bandforge::TrackKind::Pad,
        bandforge::TrackKind::ElectricPiano,
        bandforge::TrackKind::Organ,
        bandforge::TrackKind::Brass,
        bandforge::TrackKind::Choir,
        bandforge::TrackKind::Mallet,
        bandforge::TrackKind::Woodwind,
        bandforge::TrackKind::Strings,
        bandforge::TrackKind::GuitarSynth,
        bandforge::TrackKind::Arp,
        bandforge::TrackKind::Pluck,
        bandforge::TrackKind::Sampler,
        bandforge::TrackKind::DrumKit,
        bandforge::TrackKind::DrumRack,
        bandforge::TrackKind::BeatSequencer,
        bandforge::TrackKind::EightOhEight,
    };
}

void testTrackKinds()
{
    for (const auto kind : midiTrackKinds()) {
        const auto text = bandforge::toString(kind);
        check(bandforge::trackKindFromString(text) == kind, "track kind should round-trip: " + text);
        check(bandforge::isMidiTrackKind(kind), "track kind should be MIDI-capable: " + text);

        bandforge::Project project;
        auto& track = project.addTrack(kind, bandforge::displayName(kind));
        check(track.kind == kind, "added track should keep requested kind: " + text);
        check(!track.instrument.type.empty(), "MIDI track should receive default instrument: " + text);

        auto starter = bandforge::defaultStarterClipForTrackKind(kind);
        check(!starter.notes.empty(), "MIDI track kind should have starter notes: " + text);

        auto& clip = project.addMidiClip(track.id, "Starter", 0.0, 4.0);
        clip.midi = starter;
        const auto loaded = bandforge::Project::fromJson(project.toJson());
        check(loaded.tracks.front().kind == kind, "project save/load should preserve kind: " + text);
        check(!loaded.tracks.front().clips.front().midi.notes.empty(), "starter clip should save/load: " + text);
    }

    check(bandforge::trackKindFromString("midi") == bandforge::TrackKind::Midi, "legacy midi kind should load");
    check(bandforge::trackKindFromString("drums") == bandforge::TrackKind::Drums, "legacy drums kind should load");
}

void testProjectBundle()
{
    auto project = bandforge::makeStarterProject();
    const auto dir = std::filesystem::temp_directory_path() / "bandforge-core-test-project";
    std::filesystem::remove_all(dir);
    project.saveBundle(dir);

    check(std::filesystem::exists(dir / "Audio"), "bundle should include Audio directory");
    check(std::filesystem::exists(dir / "MIDI"), "bundle should include MIDI directory");
    check(std::filesystem::exists(dir / "Renders"), "bundle should include Renders directory");
    check(std::filesystem::exists(dir / bandforge::Project::ProjectFileName), "bundle should include project.json");

    auto loaded = bandforge::Project::loadBundle(dir);
    check(loaded.tracks.size() == project.tracks.size(), "bundle load should preserve tracks");
    std::filesystem::remove_all(dir);
}

void testProjectFile()
{
    auto project = bandforge::makeStarterProject();
    auto& brass = project.addTrack(bandforge::TrackKind::Brass, "Section Brass");
    auto& clip = project.addMidiClip(brass.id, "Brass Stabs", 0.0, 4.0);
    clip.midi = bandforge::defaultStarterClipForTrackKind(brass.kind);

    const auto file = std::filesystem::temp_directory_path() / "bandforge-core-test-project.bforge";
    std::filesystem::remove(file);
    project.saveFile(file);

    check(std::filesystem::exists(file), ".bforge project file should be written");
    check(fileSize(file) > 0, ".bforge project file should contain JSON");

    auto loaded = bandforge::Project::loadFile(file);
    check(loaded.tracks.size() == project.tracks.size(), ".bforge load should preserve tracks");
    check(loaded.tracks.back().kind == bandforge::TrackKind::Brass, ".bforge load should preserve new MIDI kind");
    std::filesystem::remove(file);
}

void testTimelineEdits()
{
    auto project = bandforge::makeStarterProject();
    bandforge::TimelineEditor editor(project);

    auto& track = project.tracks.front();
    auto& clip = track.clips.front();
    const auto clipId = clip.id;
    clip.midi.notes.clear();
    clip.midi.notes.push_back({ 60, 92, 1, 0.13, 0.5 });
    clip.midi.notes.front().startBeat = 0.13;

    check(editor.quantizeMidiClip(track.id, clipId, 0.25), "quantize should find MIDI clip");
    checkNear(clip.midi.notes.front().startBeat, 0.25, 0.0001, "quantize should snap note start");

    const auto duplicateId = editor.duplicateClip(track.id, clipId, 8.0);
    check(duplicateId.has_value(), "duplicate should create a new clip");
    check(project.findClip(track.id, *duplicateId) != nullptr, "duplicate clip should be addressable");

    const auto splitId = editor.splitClip(track.id, clipId, 4.0);
    check(splitId.has_value(), "split should create right-side clip");
    checkNear(project.findClip(track.id, clipId)->lengthBeats, 4.0, 0.0001, "left split length should shrink");
}

void testAutomationAndTempo()
{
    bandforge::AutomationLane lane;
    lane.points = { { 0.0, 0.0 }, { 4.0, 1.0 } };
    checkNear(lane.valueAt(2.0), 0.5, 0.0001, "automation should interpolate");

    bandforge::Project project;
    project.tempoMarkers = { { 0.0, 120.0 }, { 4.0, 60.0 } };
    bandforge::TempoMap tempoMap(project);
    checkNear(tempoMap.beatToSeconds(4.0), 2.0, 0.0001, "tempo map should convert first segment");
    checkNear(tempoMap.secondsToBeat(3.0), 5.0, 0.0001, "tempo map should convert second segment");

    project.tempoMarkers = { { 0.0, 0.0 } };
    checkNear(project.bpmAt(0.0), 120.0, 0.0001, "invalid tempo should fall back safely");
    bandforge::TempoMap invalidTempoMap(project);
    checkNear(invalidTempoMap.beatToSeconds(4.0), 2.0, 0.0001, "tempo map should not divide by zero");
}

void testUndoRedo()
{
    auto project = bandforge::makeStarterProject();
    const auto originalTrackCount = project.tracks.size();

    bandforge::ProjectHistory history;
    history.remember(project);
    project.addTrack(bandforge::TrackKind::Audio, "Scratch");
    check(project.tracks.size() == originalTrackCount + 1, "edit should add a track");

    check(history.undo(project), "undo should restore previous project snapshot");
    check(project.tracks.size() == originalTrackCount, "undo should restore track count");

    check(history.redo(project), "redo should restore edited project snapshot");
    check(project.tracks.size() == originalTrackCount + 1, "redo should restore added track");
}

void testLibrarySearch()
{
    bandforge::SoundLibrary library;
    auto drumLoop = bandforge::LoopAsset {};
    drumLoop.id = "loop-1";
    drumLoop.name = "Clean Drum Groove";
    drumLoop.kind = bandforge::LoopKind::Audio;
    drumLoop.path = "loops/drums.wav";
    drumLoop.targetTrackKind = bandforge::TrackKind::Audio;
    drumLoop.instrument = "Drums";
    drumLoop.genre = "Pop";
    drumLoop.key = "C";
    drumLoop.bpm = 120.0;
    drumLoop.beats = 4.0;
    drumLoop.tags = { "tight", "starter" };
    drumLoop.license = "CC0";
    drumLoop.attribution = "BandForge";
    library.loops.push_back(drumLoop);

    auto padLoop = bandforge::LoopAsset {};
    padLoop.id = "loop-2";
    padLoop.name = "Wide Pad";
    padLoop.kind = bandforge::LoopKind::Midi;
    padLoop.targetTrackKind = bandforge::TrackKind::Pad;
    padLoop.instrument = "Keys";
    padLoop.genre = "Ambient";
    padLoop.key = "A minor";
    padLoop.bpm = 90.0;
    padLoop.beats = 8.0;
    padLoop.tags = { "soft" };
    padLoop.license = "CC0";
    padLoop.attribution = "BandForge";
    padLoop.midi.notes.push_back({ 60, 90, 1, 0.0, 1.0 });
    library.loops.push_back(padLoop);

    const auto drumResults = library.searchLoops({ "drum", "", "", "" });
    check(drumResults.size() == 1 && drumResults.front().id == "loop-1", "library text search should match drums");

    const auto keyResults = library.searchLoops({ "", "", "", "A minor" });
    check(keyResults.size() == 1 && keyResults.front().id == "loop-2", "library key search should match exact key");
}

void testFactoryExpansion()
{
    bandforge::SoundLibrary library;
    library.addFactoryExpansion();

    check(library.loops.size() == 6000, "factory expansion should add 6000 MIDI loops");
    check(library.presets.size() == 4000, "factory expansion should add 3000 instruments and 1000 FX presets");

    const auto factoryLoops = library.searchLoops({ "factory", "", "", "" });
    check(factoryLoops.size() == 6000, "factory loops should be searchable");
    check(std::all_of(factoryLoops.begin(), factoryLoops.end(), [](const bandforge::LoopAsset& loop) {
        return loop.kind == bandforge::LoopKind::Midi && !loop.midi.notes.empty() && bandforge::isMidiTrackKind(loop.targetTrackKind);
    }), "factory loops should be playable MIDI loops");

    const auto fxPresets = library.searchPresets({ "fx-chain", "", "", "" });
    check(fxPresets.size() == 1000, "factory FX presets should be searchable");
    check(std::all_of(fxPresets.begin(), fxPresets.end(), [](const bandforge::Preset& preset) {
        return preset.instrumentType == "audio-effect-chain";
    }), "factory FX presets should be effect-chain presets");

    library.addFactoryExpansion();
    check(library.loops.size() == 6000 && library.presets.size() == 4000,
        "factory expansion should be idempotent");
}

void testLoopPackManifest()
{
    const auto manifestPath = libraryPath("manifest.json");
    check(std::filesystem::exists(manifestPath), "loop pack manifest should exist");

    const auto library = bandforge::SoundLibrary::loadManifest(manifestPath);
    check(library.loops.size() == 96, "loop pack should contain 96 loops");

    int audioLoops = 0;
    int midiLoops = 0;
    for (const auto& loop : library.loops) {
        check(!loop.id.empty(), "loop should have id");
        check(!loop.name.empty(), "loop should have name");
        check(!loop.instrument.empty(), "loop should have instrument");
        check(!loop.genre.empty(), "loop should have genre");
        check(!loop.key.empty(), "loop should have key");
        check(loop.bpm > 0.0, "loop should have bpm");
        check(loop.beats > 0.0, "loop should have beats");
        check(!loop.license.empty(), "loop should have license");
        check(!loop.attribution.empty(), "loop should have attribution");

        if (loop.kind == bandforge::LoopKind::Audio) {
            ++audioLoops;
            check(!loop.path.empty(), "audio loop should have path");
            check(std::filesystem::exists(libraryPath(loop.path)), "audio loop WAV should exist: " + loop.path);
            check(bandforge::preferredTrackKindForLoop(loop) == bandforge::TrackKind::Audio, "audio loop should target audio track");
        } else {
            ++midiLoops;
            check(!loop.midi.notes.empty(), "MIDI loop should contain notes: " + loop.id);
            check(bandforge::isMidiTrackKind(bandforge::preferredTrackKindForLoop(loop)), "MIDI loop should target MIDI-capable track");
        }
    }

    check(audioLoops == 48, "loop pack should contain 48 audio loops");
    check(midiLoops == 48, "loop pack should contain 48 MIDI loops");
}

void testHosts()
{
    bandforge::InstrumentHost instruments;
    const auto synth = instruments.makeSlot("poly-synth", "Warm Keys");
    check(synth.type == "poly-synth", "instrument host should create synth slots");
    check(synth.parameters.find("attack") != synth.parameters.end(), "instrument slot should include default parameters");
    check(instruments.findInstrument("808").has_value(), "instrument host should expose 808");
    check(instruments.findInstrument("pad-synth").has_value(), "instrument host should expose pad synth");

    bandforge::PluginHost plugins;
    const auto effect = plugins.makeEffectSlot("builtin-reverb");
    check(effect.name == "Room Reverb", "plugin host should create built-in effect slots");
    const auto plugin = plugins.findPlugin("builtin-poly-synth");
    check(plugin.has_value() && plugin->instrument, "plugin host should expose built-in instruments");
    check(plugins.findPlugin("builtin-drum-rack").has_value(), "plugin host should expose drum rack");
    check(plugins.findPlugin("builtin-sampler").has_value(), "plugin host should expose sampler");
}

void testAudioAndExport()
{
    auto project = bandforge::makeStarterProject();
    bandforge::AudioEngine engine;
    engine.prepare({ 48000.0, 512, 2 });
    engine.requestStretch("", 0.0);
    std::vector<float> samples(48000, 0.0f);
    engine.renderPreview(project, 0.0, samples);

    const bool hasSignal = std::any_of(samples.begin(), samples.end(), [](float sample) {
        return std::abs(sample) > 0.00001f;
    });
    check(hasSignal, "preview renderer should produce audible MIDI signal");

    const auto library = bandforge::SoundLibrary::loadManifest(libraryPath("manifest.json"));
    const auto audioLoop = std::find_if(library.loops.begin(), library.loops.end(), [](const bandforge::LoopAsset& loop) {
        return loop.kind == bandforge::LoopKind::Audio;
    });
    check(audioLoop != library.loops.end(), "test library should include an audio loop");

    bandforge::Project audioProject;
    auto& audioTrack = audioProject.addTrack(bandforge::TrackKind::Audio, "Loop Audio");
    audioProject.addAudioClip(audioTrack.id, audioLoop->name, libraryPath(audioLoop->path).generic_string(), 0.0, audioLoop->beats);
    std::vector<float> audioSamples(48000, 0.0f);
    engine.renderPreview(audioProject, 0.0, audioSamples);
    const bool hasAudioSignal = std::any_of(audioSamples.begin(), audioSamples.end(), [](float sample) {
        return std::abs(sample) > 0.00001f;
    });
    check(hasAudioSignal, "preview renderer should produce audible audio-loop signal");

    const auto midiLoop = std::find_if(library.loops.begin(), library.loops.end(), [](const bandforge::LoopAsset& loop) {
        return loop.kind == bandforge::LoopKind::Midi && !loop.midi.notes.empty();
    });
    check(midiLoop != library.loops.end(), "test library should include a MIDI loop");

    bandforge::Project midiProject;
    const auto midiKind = bandforge::preferredTrackKindForLoop(*midiLoop);
    auto& midiTrack = midiProject.addTrack(midiKind, bandforge::displayName(midiKind));
    auto& midiClip = midiProject.addMidiClip(midiTrack.id, midiLoop->name, 0.0, midiLoop->beats);
    midiClip.midi = midiLoop->midi;
    std::vector<float> midiSamples(48000, 0.0f);
    engine.renderPreview(midiProject, 0.0, midiSamples);
    const bool hasMidiLoopSignal = std::any_of(midiSamples.begin(), midiSamples.end(), [](float sample) {
        return std::abs(sample) > 0.00001f;
    });
    check(hasMidiLoopSignal, "preview renderer should produce audible MIDI-loop signal");

    const auto wav = std::filesystem::temp_directory_path() / "bandforge-core-test.wav";
    std::filesystem::remove(wav);
    bandforge::WavExporter::writeSilence(wav, 0.05, { 8000, 2, 16 });
    check(fileSize(wav) > 44, "WAV export should write data after header");
    std::filesystem::remove(wav);
}

} // namespace

int main()
{
    try {
        testProjectSerialization();
        testTrackKinds();
        testProjectBundle();
        testProjectFile();
        testTimelineEdits();
        testAutomationAndTempo();
        testUndoRedo();
        testLibrarySearch();
        testFactoryExpansion();
        testLoopPackManifest();
        testHosts();
        testAudioAndExport();
        std::cout << "BandForge core tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "BandForge core tests failed: " << error.what() << '\n';
        return 1;
    }
}
