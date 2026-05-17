#include "MainComponent.h"

#include "core/Exporter.h"
#include "core/Mixer.h"
#include "core/Timeline.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace bandforge_app {
namespace {

juce::Colour colourFromHex(const std::string& hex, juce::Colour fallback)
{
    if (hex.size() != 7 || hex.front() != '#') {
        return fallback;
    }

    const auto parseByte = [](char high, char low) -> int {
        const auto value = [](char ch) -> int {
            if (ch >= '0' && ch <= '9') {
                return ch - '0';
            }
            if (ch >= 'a' && ch <= 'f') {
                return ch - 'a' + 10;
            }
            if (ch >= 'A' && ch <= 'F') {
                return ch - 'A' + 10;
            }
            return 0;
        };
        return (value(high) << 4) + value(low);
    };

    return juce::Colour::fromRGB(
        parseByte(hex[1], hex[2]),
        parseByte(hex[3], hex[4]),
        parseByte(hex[5], hex[6]));
}

juce::String formatBeat(double beat)
{
    const int bar = static_cast<int>(beat / 4.0) + 1;
    const int beatInBar = static_cast<int>(std::fmod(std::max(0.0, beat), 4.0)) + 1;
    const int tick = static_cast<int>((beat - std::floor(beat)) * 960.0);
    return juce::String(bar) + "." + juce::String(beatInBar) + "." + juce::String(tick);
}

bandforge::SoundLibrary makeBuiltInLibrary()
{
    std::vector<std::filesystem::path> manifests {
        std::filesystem::path("assets") / "library" / "manifest.json",
#ifdef BANDFORGE_SOURCE_DIR
        std::filesystem::path(BANDFORGE_SOURCE_DIR) / "assets" / "library" / "manifest.json",
#endif
    };

    for (const auto& manifest : manifests) {
        if (!std::filesystem::exists(manifest)) {
            continue;
        }
        try {
            auto library = bandforge::SoundLibrary::loadManifest(manifest);
            library.addFactoryExpansion();
            return library;
        } catch (const std::exception&) {
        }
    }

    bandforge::SoundLibrary library;
    auto midiLoop = bandforge::LoopAsset {};
    midiLoop.id = "loop-fallback-keys";
    midiLoop.name = "Fallback Keys Pattern";
    midiLoop.kind = bandforge::LoopKind::Midi;
    midiLoop.targetTrackKind = bandforge::TrackKind::Keys;
    midiLoop.instrument = "Keys";
    midiLoop.genre = "Pop";
    midiLoop.key = "C";
    midiLoop.bpm = 120.0;
    midiLoop.beats = 4.0;
    midiLoop.tags = { "fallback", "midi", "editable" };
    midiLoop.license = "Original BandForge content";
    midiLoop.attribution = "BandForge";
    midiLoop.midi = bandforge::defaultStarterClipForTrackKind(bandforge::TrackKind::Keys);
    library.loops = { midiLoop };
    library.presets = {
        { "preset-warm-keys", "Warm Keys", "poly-synth", "Keyboards", { "soft", "starter" } },
        { "preset-bright-lead", "Bright Lead", "lead-synth", "Synth Lead", { "lead", "synth", "melody" } },
        { "preset-round-bass", "Round Bass", "bass-synth", "Bass", { "bass", "low", "warm" } },
        { "preset-wide-pad", "Wide Pad", "pad-synth", "Pads", { "pad", "ambient", "wide" } },
        { "preset-glass-ep", "Glass EP", "electric-piano", "Keyboards", { "keys", "electric", "bell" } },
        { "preset-drawbar-organ", "Drawbar Organ", "organ", "Keyboards", { "organ", "sustain", "retro" } },
        { "preset-section-brass", "Section Brass", "brass", "Brass", { "brass", "stabs", "bold" } },
        { "preset-soft-choir", "Soft Choir", "choir", "Choir", { "choir", "pad", "human" } },
        { "preset-bright-mallet", "Bright Mallet", "mallet", "Mallet", { "mallet", "bell", "percussion" } },
        { "preset-warm-woodwind", "Warm Woodwind", "woodwind", "Woodwind", { "woodwind", "lead", "breath" } },
        { "preset-studio-strings", "Studio Strings", "strings", "Strings", { "strings", "orchestral", "ensemble" } },
        { "preset-synth-guitar", "Clean Synth Guitar", "guitar-synth", "Guitar Synth", { "guitar", "pluck", "clean" } },
        { "preset-pulse-arp", "Pulse Arp", "arp-synth", "Arp", { "arp", "sequence", "pulse" } },
        { "preset-glass-pluck", "Glass Pluck", "pluck-synth", "Pluck", { "pluck", "bell", "short" } },
        { "preset-quick-sampler", "Quick Sampler", "sampler", "Sampler", { "sampler", "sample", "starter" } },
        { "preset-open-kit", "Open Kit", "drum-machine", "Drums", { "starter", "tight" } },
        { "preset-punch-rack", "Punch Rack", "drum-rack", "Drum Rack", { "drums", "rack", "punch" } },
        { "preset-step-machine", "Step Machine", "beat-sequencer", "Beat Sequencer", { "drums", "steps", "sequencer" } },
        { "preset-deep-808", "Deep 808", "808", "808", { "808", "sub", "drums" } },
        { "preset-clean-vocal", "Clean Vocal Chain", "audio-effect-chain", "Voice", { "eq", "compressor", "reverb" } },
    };
    library.addFactoryExpansion();
    return library;
}

bandforge::TrackKind trackKindForInstrumentType(const std::string& instrumentType)
{
    if (instrumentType == "lead-synth") {
        return bandforge::TrackKind::SynthLead;
    }
    if (instrumentType == "bass-synth") {
        return bandforge::TrackKind::Bass;
    }
    if (instrumentType == "pad-synth") {
        return bandforge::TrackKind::Pad;
    }
    if (instrumentType == "electric-piano") {
        return bandforge::TrackKind::ElectricPiano;
    }
    if (instrumentType == "organ") {
        return bandforge::TrackKind::Organ;
    }
    if (instrumentType == "brass") {
        return bandforge::TrackKind::Brass;
    }
    if (instrumentType == "choir") {
        return bandforge::TrackKind::Choir;
    }
    if (instrumentType == "mallet") {
        return bandforge::TrackKind::Mallet;
    }
    if (instrumentType == "woodwind") {
        return bandforge::TrackKind::Woodwind;
    }
    if (instrumentType == "strings") {
        return bandforge::TrackKind::Strings;
    }
    if (instrumentType == "guitar-synth") {
        return bandforge::TrackKind::GuitarSynth;
    }
    if (instrumentType == "arp-synth") {
        return bandforge::TrackKind::Arp;
    }
    if (instrumentType == "pluck-synth") {
        return bandforge::TrackKind::Pluck;
    }
    if (instrumentType == "sampler") {
        return bandforge::TrackKind::Sampler;
    }
    if (instrumentType == "drum-rack") {
        return bandforge::TrackKind::DrumRack;
    }
    if (instrumentType == "beat-sequencer") {
        return bandforge::TrackKind::BeatSequencer;
    }
    if (instrumentType == "808") {
        return bandforge::TrackKind::EightOhEight;
    }
    if (instrumentType == "drum-machine") {
        return bandforge::TrackKind::DrumKit;
    }
    return bandforge::TrackKind::Keys;
}

std::string loopMediaPath(const bandforge::LoopAsset& loop)
{
    if (loop.path.empty()) {
        return {};
    }

    const std::filesystem::path path(loop.path);
    if (path.is_absolute() || loop.path.rfind("assets/", 0) == 0) {
        return path.generic_string();
    }
    return (std::filesystem::path("assets") / "library" / path).generic_string();
}

// ─── GarageBand-style per-kind accent color ──────────────────────────────────
static juce::Colour colourForTrackKind(bandforge::TrackKind kind)
{
    switch (kind) {
    case bandforge::TrackKind::Keys:          return juce::Colour(0xff4A8EFF);
    case bandforge::TrackKind::SynthLead:     return juce::Colour(0xffFF4F5E);
    case bandforge::TrackKind::Bass:          return juce::Colour(0xffFF8030);
    case bandforge::TrackKind::Pad:           return juce::Colour(0xffAA6EFF);
    case bandforge::TrackKind::ElectricPiano: return juce::Colour(0xff55B8D8);
    case bandforge::TrackKind::Organ:         return juce::Colour(0xff8BC34A);
    case bandforge::TrackKind::Brass:         return juce::Colour(0xffD6A548);
    case bandforge::TrackKind::Choir:         return juce::Colour(0xff8F7AE5);
    case bandforge::TrackKind::Mallet:        return juce::Colour(0xffF1C84B);
    case bandforge::TrackKind::Woodwind:      return juce::Colour(0xff4EB7A5);
    case bandforge::TrackKind::Strings:       return juce::Colour(0xff38C89A);
    case bandforge::TrackKind::GuitarSynth:   return juce::Colour(0xffFFCC40);
    case bandforge::TrackKind::Arp:           return juce::Colour(0xff40D0FF);
    case bandforge::TrackKind::Pluck:         return juce::Colour(0xff7AE850);
    case bandforge::TrackKind::Sampler:       return juce::Colour(0xffFF70B0);
    case bandforge::TrackKind::DrumKit:
    case bandforge::TrackKind::DrumRack:      return juce::Colour(0xffFF6B35);
    case bandforge::TrackKind::BeatSequencer: return juce::Colour(0xffFFAA35);
    case bandforge::TrackKind::EightOhEight:  return juce::Colour(0xffFF3535);
    case bandforge::TrackKind::Audio:         return juce::Colour(0xff60A8FF);
    default:                                  return juce::Colour(0xff6688AA);
    }
}

static juce::Colour trackColour(const bandforge::Track& track)
{
    return colourFromHex(track.color, colourForTrackKind(track.kind));
}

std::optional<double> parseTempoText(const juce::String& rawText)
{
    auto text = rawText.trim().toStdString();
    if (text.empty()) {
        return std::nullopt;
    }

    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || !std::isfinite(parsed)) {
        return std::nullopt;
    }

    while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }

    std::string suffix(end);
    std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (!suffix.empty() && suffix != "bpm") {
        return std::nullopt;
    }

    return std::clamp(parsed, 20.0, 300.0);
}

enum class SmartControlTarget {
    InstrumentParam,
    MixerVolume,
    MixerPan,
    ReverbMix
};

struct SmartControlSpec {
    juce::String label;
    double min = 0.0;
    double max = 1.0;
    double defaultValue = 0.5;
    std::string paramKey;
    SmartControlTarget target = SmartControlTarget::InstrumentParam;
};

using SmartControlSpecs = std::array<SmartControlSpec, 4>;

SmartControlSpec instrumentSpec(const char* label, const char* key, double def, double min = 0.0, double max = 1.0)
{
    return { label, min, max, def, key, SmartControlTarget::InstrumentParam };
}

SmartControlSpec mixerSpec(const char* label, SmartControlTarget target, double min, double max, double def)
{
    return { label, min, max, def, {}, target };
}

SmartControlSpec reverbSpec(const char* label = "Reverb", double def = 0.22)
{
    return { label, 0.0, 1.0, def, "mix", SmartControlTarget::ReverbMix };
}

SmartControlSpecs smartControlSpecsForKind(bandforge::TrackKind kind)
{
    switch (kind) {
    case bandforge::TrackKind::Keys:
    case bandforge::TrackKind::Midi:
    case bandforge::TrackKind::ElectricPiano:
    case bandforge::TrackKind::Organ:
        return {
            instrumentSpec("Attack", "attack", 0.18),
            instrumentSpec("Release", "release", 0.45),
            instrumentSpec("Tone", "tone", 0.62),
            reverbSpec("Reverb", 0.22),
        };
    case bandforge::TrackKind::Bass:
    case bandforge::TrackKind::EightOhEight:
        return {
            instrumentSpec("SubTune", "subTune", 0.45),
            instrumentSpec("Drive", "drive", 0.36),
            instrumentSpec("Decay", "decay", 0.58),
            reverbSpec("Reverb", 0.08),
        };
    case bandforge::TrackKind::SynthLead:
    case bandforge::TrackKind::Brass:
    case bandforge::TrackKind::Woodwind:
        return {
            instrumentSpec("Cutoff", "cutoff", 0.72),
            instrumentSpec("Resonance", "resonance", 0.28),
            instrumentSpec("Glide", "glide", 0.16),
            reverbSpec("Reverb", 0.18),
        };
    case bandforge::TrackKind::Pad:
    case bandforge::TrackKind::Choir:
    case bandforge::TrackKind::Strings:
        return {
            instrumentSpec("Attack", "attack", 0.55),
            instrumentSpec("Chorus", "chorus", 0.42),
            reverbSpec("Reverb", 0.34),
            mixerSpec("Volume", SmartControlTarget::MixerVolume, -24.0, 6.0, 0.0),
        };
    case bandforge::TrackKind::GuitarSynth:
    case bandforge::TrackKind::Pluck:
    case bandforge::TrackKind::Mallet:
        return {
            instrumentSpec("Tone", "tone", 0.6),
            instrumentSpec("Decay", "decay", 0.42),
            reverbSpec("Reverb", 0.18),
            mixerSpec("Volume", SmartControlTarget::MixerVolume, -24.0, 6.0, 0.0),
        };
    case bandforge::TrackKind::Arp:
        return {
            instrumentSpec("Rate", "rate", 1.0, 0.25, 8.0),
            instrumentSpec("Range", "range", 2.0, 1.0, 4.0),
            instrumentSpec("Gate", "gate", 0.54),
            mixerSpec("Volume", SmartControlTarget::MixerVolume, -24.0, 6.0, 0.0),
        };
    case bandforge::TrackKind::Drums:
    case bandforge::TrackKind::DrumKit:
    case bandforge::TrackKind::DrumRack:
    case bandforge::TrackKind::BeatSequencer:
        return {
            instrumentSpec("KickTune", "kickTune", 0.46),
            instrumentSpec("SnareSnap", "snareSnap", 0.55),
            instrumentSpec("HatTone", "hatTone", 0.62),
            reverbSpec("Room", 0.16),
        };
    case bandforge::TrackKind::Audio:
    case bandforge::TrackKind::Sampler:
        return {
            mixerSpec("Gain", SmartControlTarget::MixerVolume, -24.0, 12.0, 0.0),
            instrumentSpec("Hi-pass", "hiPass", 0.0),
            instrumentSpec("Presence", "presence", 0.5),
            mixerSpec("Volume", SmartControlTarget::MixerVolume, -24.0, 6.0, 0.0),
        };
    case bandforge::TrackKind::Master:
        break;
    }

    return {
        mixerSpec("Volume", SmartControlTarget::MixerVolume, -24.0, 6.0, 0.0),
        mixerSpec("Pan", SmartControlTarget::MixerPan, -1.0, 1.0, 0.0),
        reverbSpec("Reverb", 0.12),
        instrumentSpec("Tone", "tone", 0.5),
    };
}

// ─── Keyboard callbacks bundle ────────────────────────────────────────────────
struct KeyboardCallbacks {
    std::function<void(int, int)> noteOn;    // pitch, velocity
    std::function<void(int)>      noteOff;   // pitch
    std::function<int()>          getOctave;
    std::function<void(int)>      shiftOctave; // delta
    std::function<int()>          getVelocity;
    std::function<void(int)>      shiftVelocity; // delta
    std::function<bool(int)>      isPitchActive;
};

constexpr int kPointerNoteKeyBase = 1000;

int pointerNoteKey(int pitch) noexcept
{
    return kPointerNoteKeyBase + std::clamp(pitch, 0, 127);
}

int normaliseKeyCode(int keyCode) noexcept
{
    if (keyCode >= 'a' && keyCode <= 'z') {
        return keyCode - ('a' - 'A');
    }
    return keyCode;
}

bool isNormalisedKeyCurrentlyDown(int normalisedKeyCode)
{
    if (juce::KeyPress::isKeyCurrentlyDown(normalisedKeyCode)) {
        return true;
    }

    if (normalisedKeyCode >= 'A' && normalisedKeyCode <= 'Z') {
        return juce::KeyPress::isKeyCurrentlyDown(normalisedKeyCode + ('a' - 'A'));
    }

    return false;
}

// Maps a JUCE key code (uppercase letter) to semitones above C for piano keyboard.
// Layout: A-row = white keys, W-row = black keys (like GarageBand Musical Typing).
//   W  E     T  Y  U     O  L
//  A  S  D  F  G  H  J  K
//  C  D  E  F  G  A  B  C
static int keyCodeToSemitone(int kc)
{
    kc = normaliseKeyCode(kc);
    switch (kc) {
    case 'A': return 0;   // C
    case 'W': return 1;   // C#
    case 'S': return 2;   // D
    case 'E': return 3;   // D#
    case 'D': return 4;   // E
    case 'F': return 5;   // F
    case 'T': return 6;   // F#
    case 'G': return 7;   // G
    case 'Y': return 8;   // G#
    case 'H': return 9;   // A
    case 'U': return 10;  // A#
    case 'J': return 11;  // B
    case 'K': return 12;  // C (next octave)
    case 'O': return 13;  // C#
    case 'L': return 14;  // D
    default:  return -1;
    }
}

bool containsIgnoreCase(const std::string& value, const std::string& needle)
{
    auto lower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        return text;
    };
    return lower(value).find(lower(needle)) != std::string::npos;
}

bool presetIsDrumOrBeat(const bandforge::Preset& preset)
{
    return preset.instrumentType == "drum-machine"
        || preset.instrumentType == "drum-rack"
        || preset.instrumentType == "beat-sequencer"
        || preset.instrumentType == "808"
        || containsIgnoreCase(preset.category, "drum")
        || containsIgnoreCase(preset.category, "808");
}

bool presetIsEffectChain(const bandforge::Preset& preset)
{
    return preset.instrumentType == "audio-effect-chain"
        || containsIgnoreCase(preset.category, "fx chain");
}

bool presetIsInstrument(const bandforge::Preset& preset)
{
    return !presetIsEffectChain(preset) && !presetIsDrumOrBeat(preset);
}

} // end anonymous namespace

// ─── Audio recorder (captures device input to WAV while recording) ──────────

class MainComponent::AudioRecorder final : public juce::AudioIODeviceCallback {
public:
    AudioRecorder() { thread_.startThread(); }
    ~AudioRecorder() override { stopRecording(); thread_.stopThread(3000); }

    void startRecording(const juce::File& file, double sampleRate, int numChannels)
    {
        stopRecording();
        file.deleteFile();
        auto* outStream = file.createOutputStream().release();
        if (!outStream) return;

        juce::WavAudioFormat wav;
        auto* writer = wav.createWriterFor(outStream, sampleRate,
            static_cast<unsigned int>(numChannels), 16, {}, 0);
        if (!writer) { delete outStream; return; }

        writer_.reset(new juce::AudioFormatWriter::ThreadedWriter(writer, thread_, 32768));
        samplesWritten_.store(0);
        sampleRate_ = sampleRate;
        {
            const juce::ScopedWriteLock sl(writerLock_);
            activeWriter_.store(writer_.get());
        }
    }

    void stopRecording()
    {
        {
            const juce::ScopedWriteLock sl(writerLock_);
            activeWriter_.store(nullptr);
        }
        writer_.reset();
    }

    double getRecordedSeconds() const
    {
        return static_cast<double>(samplesWritten_.load()) / std::max(1.0, sampleRate_);
    }

    void audioDeviceIOCallbackWithContext(
        const float* const* inputData, int numInputChannels,
        float* const*, int, int numSamples,
        const juce::AudioIODeviceCallbackContext&) override
    {
        const juce::ScopedReadLock sl(writerLock_);
        auto* w = activeWriter_.load();
        if (w && numInputChannels > 0) {
            w->write(inputData, numSamples);
            samplesWritten_.fetch_add(static_cast<int64_t>(numSamples));
        }
    }

    void audioDeviceAboutToStart(juce::AudioIODevice* device) override
    {
        sampleRate_ = device ? device->getCurrentSampleRate() : 44100.0;
    }

    void audioDeviceStopped() override { stopRecording(); }

private:
    juce::TimeSliceThread thread_ { "BandForge Recorder" };
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer_;
    std::atomic<juce::AudioFormatWriter::ThreadedWriter*> activeWriter_ { nullptr };
    juce::ReadWriteLock writerLock_;
    std::atomic<int64_t> samplesWritten_ { 0 };
    double sampleRate_ = 44100.0;
};

namespace { // reopen anonymous namespace for UI helpers

// ─── Toolbar icon look-and-feel ────────────────────────────────────────────

class ToolbarLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    static constexpr float kPi = juce::MathConstants<float>::pi;

    void drawButtonBackground(juce::Graphics& g, juce::Button& btn,
        const juce::Colour&, bool hover, bool down) override
    {
        auto b = btn.getLocalBounds().toFloat().reduced(1.0f);
        const bool on = btn.getToggleState();
        const bool enabled = btn.isEnabled();
        juce::Colour col = on    ? juce::Colour(0xff3a7d66)
                         : down  ? juce::Colour(0xff3a4553)
                         : hover ? juce::Colour(0xff323d4b)
                                 : juce::Colour(0xff28313d);
        if (!enabled)
            col = col.withAlpha(0.5f);
        g.setColour(col);
        g.fillRoundedRectangle(b, 6.0f);
    }

    void drawButtonText(juce::Graphics& g, juce::TextButton& btn, bool, bool) override
    {
        const bool on = btn.getToggleState();
        const float alpha = btn.isEnabled() ? 1.0f : 0.4f;
        const juce::Colour fg = juce::Colour(0xffedf3fb).withAlpha(alpha);
        paintIcon(g, btn.getButtonText(), btn.getLocalBounds().toFloat(), fg, on);
    }

private:
    static juce::Path strokeArc(float cx, float cy, float r, float from, float to)
    {
        juce::Path p;
        p.addArc(cx - r, cy - r, r * 2.0f, r * 2.0f, from, to, true);
        return p;
    }

    static void arrowHead(juce::Graphics& g, float tipX, float tipY, float angle, float size, juce::Colour col)
    {
        juce::Path p;
        p.addTriangle(tipX, tipY,
            tipX + size * std::cos(angle + 2.5f), tipY + size * std::sin(angle + 2.5f),
            tipX + size * std::cos(angle - 2.5f), tipY + size * std::sin(angle - 2.5f));
        g.setColour(col);
        g.fillPath(p);
    }

    static void iconPlay(juce::Graphics& g, float cx, float cy, float s, juce::Colour col)
    {
        juce::Path p;
        const float r = s * 0.32f;
        p.addTriangle(cx - r * 0.65f, cy - r, cx - r * 0.65f, cy + r, cx + r * 0.9f, cy);
        g.setColour(col);
        g.fillPath(p);
    }

    static void iconStop(juce::Graphics& g, float cx, float cy, float s, juce::Colour col)
    {
        const float r = s * 0.28f;
        g.setColour(col);
        g.fillRoundedRectangle(cx - r, cy - r, r * 2.0f, r * 2.0f, 2.5f);
    }

    static void iconRecord(juce::Graphics& g, float cx, float cy, float s, juce::Colour col, bool on)
    {
        const float r = s * 0.29f;
        g.setColour(on ? juce::Colour(0xffff5858) : col);
        g.fillEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f);
    }

    static void iconUndo(juce::Graphics& g, float cx, float cy, float s, juce::Colour col)
    {
        const float r = s * 0.26f;
        const float stroke = s * 0.095f;
        // Top arc: left → top → right (clockwise in JUCE: 0=top, Pi/2=right, etc.)
        // We want 9-o'clock to 3-o'clock going over the top: from 3Pi/2 to Pi/2
        // In JUCE convention: -Pi/2 to Pi/2 clockwise = top half
        auto arc = strokeArc(cx, cy + r * 0.15f, r, -kPi * 0.5f, kPi * 0.5f);
        g.setColour(col);
        g.strokePath(arc, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // Arrowhead at the left end (-Pi/2 = 9 o'clock), pointing downward
        const float lx = cx - r, ly = cy + r * 0.15f;
        arrowHead(g, lx, ly + r * 0.15f, kPi * 0.5f, s * 0.14f, col);
    }

    static void iconRedo(juce::Graphics& g, float cx, float cy, float s, juce::Colour col)
    {
        const float r = s * 0.26f;
        const float stroke = s * 0.095f;
        auto arc = strokeArc(cx, cy + r * 0.15f, r, -kPi * 0.5f, kPi * 0.5f);
        g.setColour(col);
        g.strokePath(arc, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // Arrowhead at the right end (Pi/2 = 3 o'clock), pointing downward
        const float rx = cx + r, ry = cy + r * 0.15f;
        arrowHead(g, rx, ry + r * 0.15f, kPi * 0.5f, s * 0.14f, col);
    }

    static void iconOpen(juce::Graphics& g, float cx, float cy, float s, juce::Colour col)
    {
        const float w = s * 0.58f, h = s * 0.46f;
        const float x = cx - w * 0.5f, y = cy - h * 0.5f;
        g.setColour(col.withAlpha(0.9f));
        // Body
        g.fillRoundedRectangle(x, y + h * 0.28f, w, h * 0.72f, 2.5f);
        // Tab
        g.fillRoundedRectangle(x, y, w * 0.40f, h * 0.35f, 2.5f);
        // Shading line
        g.setColour(col.withAlpha(0.25f));
        g.fillRoundedRectangle(x + w * 0.08f, y + h * 0.50f, w * 0.84f, h * 0.36f, 1.5f);
    }

    static void iconSave(juce::Graphics& g, float cx, float cy, float s, juce::Colour col)
    {
        const float w = s * 0.56f, h = s * 0.56f;
        const float x = cx - w * 0.5f, y = cy - h * 0.5f;
        // Outer body
        g.setColour(col.withAlpha(0.9f));
        g.fillRoundedRectangle(x, y, w, h, 2.5f);
        // Label window
        g.setColour(col.withAlpha(0.22f));
        g.fillRoundedRectangle(x + w * 0.12f, y + h * 0.44f, w * 0.76f, h * 0.44f, 2.0f);
        // Slider notch (top-right)
        g.setColour(col.withAlpha(0.55f));
        g.fillRoundedRectangle(x + w * 0.56f, y + 1.0f, w * 0.23f, h * 0.20f, 1.5f);
    }

    static void iconLoop(juce::Graphics& g, float cx, float cy, float s, juce::Colour col)
    {
        const float r = s * 0.25f;
        const float stroke = s * 0.09f;
        const float ah = s * 0.11f;
        g.setColour(col);
        // Top arc (left → top → right)
        auto top = strokeArc(cx, cy, r, -kPi * 0.5f, kPi * 0.5f);
        g.strokePath(top, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // Bottom arc (right → bottom → left)
        auto bot = strokeArc(cx, cy, r, kPi * 0.5f, kPi * 1.5f);
        g.strokePath(bot, juce::PathStrokeType(stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        // Arrow at right end of top arc (3 o'clock), pointing down
        arrowHead(g, cx + r, cy + ah * 0.3f, kPi * 0.5f, ah, col);
        // Arrow at left end of bottom arc (9 o'clock), pointing up
        arrowHead(g, cx - r, cy - ah * 0.3f, -kPi * 0.5f, ah, col);
    }

    static void iconMetronome(juce::Graphics& g, float cx, float cy, float s, juce::Colour col)
    {
        const float h = s * 0.52f;
        const float tw = s * 0.19f, bw = s * 0.40f;
        const float top = cy - h * 0.50f, bot = cy + h * 0.50f;
        juce::Path body;
        body.startNewSubPath(cx - tw, top);
        body.lineTo(cx + tw, top);
        body.lineTo(cx + bw, bot);
        body.lineTo(cx - bw, bot);
        body.closeSubPath();
        g.setColour(col.withAlpha(0.70f));
        g.fillPath(body);
        g.setColour(col);
        g.strokePath(body, juce::PathStrokeType(s * 0.065f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
        // Pendulum arm
        const float armX = cx - tw * 0.55f, armY = top + h * 0.22f;
        g.drawLine(cx, bot - h * 0.18f, armX, armY, s * 0.07f);
        const float br = s * 0.055f;
        g.fillEllipse(armX - br, armY - br, br * 2.0f, br * 2.0f);
    }

    static void iconSnap(juce::Graphics& g, float cx, float cy, float s, juce::Colour col)
    {
        // 3×3 dot grid with center column highlighted
        const float gap = s * 0.20f;
        const float dr = s * 0.055f;
        const float dr2 = s * 0.085f;
        g.setColour(col.withAlpha(0.45f));
        for (int row = -1; row <= 1; ++row) {
            for (int col2 = -1; col2 <= 1; ++col2) {
                const float dx = cx + col2 * gap;
                const float dy = cy + row * gap;
                const bool center = (col2 == 0);
                const float r = center ? dr2 : dr;
                g.setColour(center ? col : col.withAlpha(0.45f));
                g.fillEllipse(dx - r, dy - r, r * 2.0f, r * 2.0f);
            }
        }
        // Vertical snap line through center column
        g.setColour(col.withAlpha(0.55f));
        g.drawLine(cx, cy - gap - dr2, cx, cy + gap + dr2, s * 0.065f);
    }

    static void iconZoom(juce::Graphics& g, float cx, float cy, float s, juce::Colour col, bool plus)
    {
        const float r = s * 0.21f;
        const float ox = cx - s * 0.04f, oy = cy - s * 0.04f;
        const float stroke = s * 0.085f;
        g.setColour(col);
        g.drawEllipse(ox - r, oy - r, r * 2.0f, r * 2.0f, stroke);
        // Handle
        g.drawLine(ox + r * 0.72f, oy + r * 0.72f, ox + r * 1.40f, oy + r * 1.40f, stroke);
        // Inner symbol
        const float inner = r * 0.50f;
        g.drawLine(ox - inner, oy, ox + inner, oy, stroke * 0.9f);
        if (plus)
            g.drawLine(ox, oy - inner, ox, oy + inner, stroke * 0.9f);
    }

    static void iconExport(juce::Graphics& g, float cx, float cy, float s, juce::Colour col)
    {
        const float aw = s * 0.22f, ah = s * 0.32f;
        const float armW = aw * 0.40f;
        const float tipY = cy - ah * 0.42f;
        g.setColour(col);
        juce::Path p;
        p.startNewSubPath(cx, tipY);
        p.lineTo(cx - aw, tipY + aw * 0.85f);
        p.lineTo(cx - armW, tipY + aw * 0.85f);
        p.lineTo(cx - armW, tipY + ah);
        p.lineTo(cx + armW, tipY + ah);
        p.lineTo(cx + armW, tipY + aw * 0.85f);
        p.lineTo(cx + aw, tipY + aw * 0.85f);
        p.closeSubPath();
        g.fillPath(p);
        g.drawLine(cx - aw, cy + ah * 0.62f, cx + aw, cy + ah * 0.62f, s * 0.085f);
    }

    static void iconAddMidi(juce::Graphics& g, float cx, float cy, float s, juce::Colour col)
    {
        const float stroke = s * 0.09f;
        g.setColour(col);
        // Plus (left)
        const float ph = s * 0.22f, px = cx - s * 0.17f;
        g.drawLine(px - ph, cy, px + ph, cy, stroke);
        g.drawLine(px, cy - ph, px, cy + ph, stroke);
        // Music note (right)
        const float nx = cx + s * 0.19f, ny = cy + s * 0.05f;
        const float nr = s * 0.12f;
        juce::Path note;
        note.addEllipse(nx - nr * 1.25f, ny - nr * 0.75f, nr * 2.5f, nr * 1.5f);
        g.fillPath(note);
        g.drawLine(nx + nr, ny - nr * 0.6f, nx + nr, ny - nr * 2.9f, stroke);
        // Flag on note
        g.drawLine(nx + nr, ny - nr * 2.9f, nx + nr * 2.2f, ny - nr * 2.0f, stroke);
    }

    static void iconAddAudio(juce::Graphics& g, float cx, float cy, float s, juce::Colour col)
    {
        const float stroke = s * 0.09f;
        g.setColour(col);
        // Plus (left)
        const float ph = s * 0.22f, px = cx - s * 0.17f;
        g.drawLine(px - ph, cy, px + ph, cy, stroke);
        g.drawLine(px, cy - ph, px, cy + ph, stroke);
        // Mini waveform bars (right)
        const float wx = cx + s * 0.11f;
        const float bw = s * 0.07f;
        const float hs[] = { s * 0.11f, s * 0.23f, s * 0.15f };
        for (int i = 0; i < 3; ++i) {
            g.fillRoundedRectangle(wx + i * (bw + s * 0.055f), cy - hs[i], bw, hs[i] * 2.0f, 1.5f);
        }
    }

    static void paintIcon(juce::Graphics& g, const juce::String& id, juce::Rectangle<float> b, juce::Colour col, bool on)
    {
        const float cx = b.getCentreX(), cy = b.getCentreY();
        const float s = std::min(b.getWidth(), b.getHeight());

        if (id == "Play")     { iconPlay(g, cx, cy, s, col);        return; }
        if (id == "Stop")     { iconStop(g, cx, cy, s, col);        return; }
        if (id == "Rec")      { iconRecord(g, cx, cy, s, col, on);  return; }
        if (id == "Undo")     { iconUndo(g, cx, cy, s, col);        return; }
        if (id == "Redo")     { iconRedo(g, cx, cy, s, col);        return; }
        if (id == "Open")     { iconOpen(g, cx, cy, s, col);        return; }
        if (id == "Save")     { iconSave(g, cx, cy, s, col);        return; }
        if (id == "Cycle")    { iconLoop(g, cx, cy, s, col);        return; }
        if (id == "Metro")    { iconMetronome(g, cx, cy, s, col);   return; }
        if (id == "Snap")     { iconSnap(g, cx, cy, s, col);        return; }
        if (id == "-")        { iconZoom(g, cx, cy, s, col, false); return; }
        if (id == "+")        { iconZoom(g, cx, cy, s, col, true);  return; }
        if (id == "Export")   { iconExport(g, cx, cy, s, col);      return; }
        if (id == "+ MIDI")   { iconAddMidi(g, cx, cy, s, col);     return; }
        if (id == "+ Audio")  { iconAddAudio(g, cx, cy, s, col);    return; }

        // Fallback: render the button text
        g.setColour(col);
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(id, b.toNearestInt(), juce::Justification::centred);
    }
};

// ─── Musical Typing Keyboard GUI ─────────────────────────────────────────────
class MusicalKeyboardComponent final : public juce::Component {
public:
    explicit MusicalKeyboardComponent(const KeyboardCallbacks& cbs)
        : cbs_(cbs)
    {
        octaveDownBtn_.setButtonText("<");
        octaveUpBtn_.setButtonText(">");
        octaveDownBtn_.onClick = [this] {
            if (cbs_.shiftOctave) { cbs_.shiftOctave(-1); repaint(); }
        };
        octaveUpBtn_.onClick = [this] {
            if (cbs_.shiftOctave) { cbs_.shiftOctave(+1); repaint(); }
        };
        for (auto* btn : { &octaveDownBtn_, &octaveUpBtn_ }) {
            addAndMakeVisible(*btn);
            btn->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff252c36));
            btn->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd7dde8));
        }
    }

    void resized() override
    {
        octaveDownBtn_.setBounds(getWidth() - 88, 6, 32, 24);
        octaveUpBtn_.setBounds(getWidth() - 52, 6, 32, 24);
    }

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff161b22));
        g.setColour(juce::Colour(0xff1e2530));
        g.fillRect(0, 0, getWidth(), kHeaderH);

        g.setColour(juce::Colour(0xffeef3fa));
        g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        g.drawText("Musical Typing", 12, 0, 180, kHeaderH, juce::Justification::centredLeft);

        const int octave = cbs_.getOctave ? cbs_.getOctave() : 4;
        g.setColour(juce::Colour(0xff7090b0));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText("Octave " + juce::String(octave + 1),
            getWidth() / 2 - 55, 0, 110, kHeaderH, juce::Justification::centred);

        g.setColour(juce::Colour(0xff5a6a7a));
        g.setFont(juce::FontOptions(10.0f));
        g.drawText("Z/X shift octave  |  click keys or press QWERTY",
            12, kHeaderH - 14, getWidth() - 130, 13, juce::Justification::centredLeft);

        drawKeyboard(g);
    }

    void mouseDown(const juce::MouseEvent& e) override
    {
        const int s = semitoneAtPoint(e.getPosition());
        if (s >= 0 && cbs_.noteOn) {
            const int octave = cbs_.getOctave ? cbs_.getOctave() : 4;
            heldMousePitch_ = std::clamp((octave + 1) * 12 + s, 0, 127);
            cbs_.noteOn(heldMousePitch_, 100);
            repaint();
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override
    {
        const int s = semitoneAtPoint(e.getPosition());
        const int octave = cbs_.getOctave ? cbs_.getOctave() : 4;
        const int newPitch = s >= 0 ? std::clamp((octave + 1) * 12 + s, 0, 127) : -1;
        if (newPitch != heldMousePitch_) {
            if (heldMousePitch_ >= 0 && cbs_.noteOff) cbs_.noteOff(heldMousePitch_);
            if (newPitch >= 0 && cbs_.noteOn) cbs_.noteOn(newPitch, 100);
            heldMousePitch_ = newPitch;
            repaint();
        }
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        if (heldMousePitch_ >= 0 && cbs_.noteOff) {
            cbs_.noteOff(heldMousePitch_);
            heldMousePitch_ = -1;
            repaint();
        }
    }

private:
    static constexpr int kHeaderH      = 36;
    static constexpr int kKeyboardY    = kHeaderH + 4;
    static constexpr int kNumWhite     = 9;
    static constexpr int kNumSemitones = 15; // C..D of next octave

    static bool isBlack(int s) noexcept
    {
        constexpr bool b[kNumSemitones] = {false,true,false,true,false,false,true,false,true,false,true,false,false,true,false};
        return s >= 0 && s < kNumSemitones && b[s];
    }

    static int whiteKeyIndex(int s) noexcept
    {
        constexpr int idx[kNumSemitones] = {0,-1,1,-1,2,3,-1,4,-1,5,-1,6,7,-1,8};
        return (s >= 0 && s < kNumSemitones) ? idx[s] : -1;
    }

    static float blackKeyXFrac(int s) noexcept
    {
        constexpr float f[kNumSemitones] = {0,0.667f,0,1.667f,0,0,3.667f,0,4.667f,0,5.667f,0,0,7.667f,0};
        return (s >= 0 && s < kNumSemitones) ? f[s] : -1.0f;
    }

    static const char* qwertyLabel(int s) noexcept
    {
        constexpr const char* lbl[kNumSemitones] = {"A","W","S","E","D","F","T","G","Y","H","U","J","K","O","L"};
        return (s >= 0 && s < kNumSemitones) ? lbl[s] : "";
    }

    static const char* noteLabel(int s) noexcept
    {
        constexpr const char* nm[kNumSemitones] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B","C","C#","D"};
        return (s >= 0 && s < kNumSemitones) ? nm[s] : "";
    }

    juce::Rectangle<float> whiteKeyRect(int wi) const noexcept
    {
        const float kbdH = static_cast<float>(getHeight() - kKeyboardY - 4);
        const float ww   = static_cast<float>(getWidth()) / kNumWhite;
        return { wi * ww + 1.5f, static_cast<float>(kKeyboardY) + 1.0f, ww - 3.0f, kbdH - 2.0f };
    }

    juce::Rectangle<float> blackKeyRect(int s) const noexcept
    {
        const float frac = blackKeyXFrac(s);
        if (frac < 0.0f) return {};
        const float kbdH = static_cast<float>(getHeight() - kKeyboardY - 4);
        const float ww   = static_cast<float>(getWidth()) / kNumWhite;
        const float bw   = ww * 0.58f;
        const float bh   = kbdH * 0.62f;
        return { frac * ww - bw * 0.5f + 1.5f, static_cast<float>(kKeyboardY) + 1.0f, bw, bh };
    }

    int semitoneAtPoint(juce::Point<int> p) const noexcept
    {
        if (p.y < kKeyboardY) return -1;
        for (int s = 0; s < kNumSemitones; ++s) {
            if (!isBlack(s)) continue;
            if (blackKeyRect(s).contains(static_cast<float>(p.x), static_cast<float>(p.y))) return s;
        }
        for (int s = 0; s < kNumSemitones; ++s) {
            if (isBlack(s)) continue;
            const int wi = whiteKeyIndex(s);
            if (wi >= 0 && whiteKeyRect(wi).contains(static_cast<float>(p.x), static_cast<float>(p.y))) return s;
        }
        return -1;
    }

    void drawKeyboard(juce::Graphics& g)
    {
        const int octave = cbs_.getOctave ? cbs_.getOctave() : 4;

        for (int s = 0; s < kNumSemitones; ++s) {
            if (isBlack(s)) continue;
            const int wi = whiteKeyIndex(s);
            if (wi < 0) continue;
            auto r = whiteKeyRect(wi);
            const int pitch = (octave + 1) * 12 + s;
            const bool active = (pitch == heldMousePitch_)
                || (cbs_.isPitchActive && cbs_.isPitchActive(pitch));

            g.setColour(active ? juce::Colour(0xff5fb3ff) : juce::Colour(0xfff2f6ff));
            g.fillRoundedRectangle(r, 5.0f);
            if (!active) {
                g.setColour(juce::Colour(0x18000000));
                g.fillRoundedRectangle(r.withTop(r.getBottom() - 12.0f), 3.0f);
            }
            g.setColour(active ? juce::Colour(0xff3a90e0) : juce::Colour(0xff8090a8));
            g.drawRoundedRectangle(r, 5.0f, 1.0f);

            g.setColour(active ? juce::Colours::white : juce::Colour(0xff506080));
            g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
            g.drawText(qwertyLabel(s),
                r.withTop(r.getBottom() - 28.0f).withHeight(14.0f).reduced(2.0f, 0.0f),
                juce::Justification::centred);
            g.setColour(active ? juce::Colour(0xffd0eeff) : juce::Colour(0xff90a0b8));
            g.setFont(juce::FontOptions(9.0f));
            g.drawText(noteLabel(s),
                r.withTop(r.getBottom() - 14.0f).withHeight(13.0f).reduced(2.0f, 0.0f),
                juce::Justification::centred);
        }

        for (int s = 0; s < kNumSemitones; ++s) {
            if (!isBlack(s)) continue;
            auto r = blackKeyRect(s);
            if (r.isEmpty()) continue;
            const int pitch = (octave + 1) * 12 + s;
            const bool active = (pitch == heldMousePitch_)
                || (cbs_.isPitchActive && cbs_.isPitchActive(pitch));

            g.setColour(active ? juce::Colour(0xff3a80cc) : juce::Colour(0xff181e2a));
            g.fillRoundedRectangle(r, 4.0f);
            if (!active) {
                g.setColour(juce::Colour(0xff0d1018));
                g.fillRoundedRectangle(r.withTop(r.getBottom() - 10.0f), 3.0f);
            }
            g.setColour(juce::Colour(0xff050810));
            g.drawRoundedRectangle(r, 4.0f, 1.0f);

            g.setColour(active ? juce::Colours::white : juce::Colour(0xff8090a8));
            g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
            g.drawText(qwertyLabel(s),
                r.withTop(r.getBottom() - 18.0f).withHeight(13.0f).reduced(1.0f, 0.0f),
                juce::Justification::centred);
        }
    }

    KeyboardCallbacks cbs_;
    juce::TextButton octaveDownBtn_, octaveUpBtn_;
    int heldMousePitch_ = -1;
};

void drawTrackKindIcon(juce::Graphics& g, bandforge::TrackKind kind, juce::Rectangle<float> area)
{
    const auto accent = colourForTrackKind(kind);
    area = area.reduced(1.0f);

    auto drawSine = [&] {
        juce::Path wave;
        const auto yMid = area.getCentreY();
        const auto amp = area.getHeight() * 0.28f;
        for (int i = 0; i <= 32; ++i) {
            const auto t = static_cast<float>(i) / 32.0f;
            const auto x = area.getX() + t * area.getWidth();
            const auto y = yMid + std::sin(t * juce::MathConstants<float>::twoPi * 1.5f) * amp;
            if (i == 0) {
                wave.startNewSubPath(x, y);
            } else {
                wave.lineTo(x, y);
            }
        }
        g.setColour(accent);
        g.strokePath(wave, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    };

    switch (kind) {
    case bandforge::TrackKind::Keys:
    case bandforge::TrackKind::Midi: {
        const auto keyW = area.getWidth() / 3.0f;
        g.setColour(juce::Colours::white.withAlpha(0.95f));
        for (int i = 0; i < 3; ++i) {
            g.fillRoundedRectangle(area.getX() + keyW * static_cast<float>(i), area.getY(), keyW - 1.0f, area.getHeight(), 1.5f);
        }
        g.setColour(juce::Colour(0xff11151b));
        g.fillRoundedRectangle(area.getX() + keyW * 0.72f, area.getY(), keyW * 0.34f, area.getHeight() * 0.58f, 1.0f);
        g.fillRoundedRectangle(area.getX() + keyW * 1.72f, area.getY(), keyW * 0.34f, area.getHeight() * 0.58f, 1.0f);
        break;
    }
    case bandforge::TrackKind::Bass:
    case bandforge::TrackKind::SynthLead:
    case bandforge::TrackKind::Pad:
    case bandforge::TrackKind::ElectricPiano:
    case bandforge::TrackKind::Organ:
    case bandforge::TrackKind::Brass:
    case bandforge::TrackKind::Choir:
    case bandforge::TrackKind::Mallet:
    case bandforge::TrackKind::Woodwind:
    case bandforge::TrackKind::Strings:
    case bandforge::TrackKind::Arp:
    case bandforge::TrackKind::Pluck:
        drawSine();
        break;
    case bandforge::TrackKind::GuitarSynth:
        g.setColour(accent);
        g.fillEllipse(area.getX() + 1.0f, area.getY() + 6.0f, area.getWidth() * 0.46f, area.getHeight() * 0.55f);
        g.fillEllipse(area.getX() + area.getWidth() * 0.26f, area.getY() + 4.0f, area.getWidth() * 0.42f, area.getHeight() * 0.52f);
        g.drawLine(area.getX() + area.getWidth() * 0.58f, area.getY() + 7.0f, area.getRight(), area.getY() + 2.0f, 2.0f);
        g.drawLine(area.getX() + area.getWidth() * 0.64f, area.getY() + 10.0f, area.getRight(), area.getY() + 5.0f, 1.0f);
        break;
    case bandforge::TrackKind::Drums:
    case bandforge::TrackKind::DrumKit:
    case bandforge::TrackKind::DrumRack:
    case bandforge::TrackKind::BeatSequencer:
        g.setColour(accent.withAlpha(0.95f));
        g.fillEllipse(area);
        g.setColour(juce::Colour(0xff101419).withAlpha(0.55f));
        g.drawEllipse(area.reduced(2.0f), 1.5f);
        break;
    case bandforge::TrackKind::EightOhEight:
        g.setColour(accent);
        g.setFont(juce::Font(juce::FontOptions(9.5f, juce::Font::bold)));
        g.drawText("808", area.toNearestInt(), juce::Justification::centred, false);
        break;
    case bandforge::TrackKind::Audio: {
        juce::Path wave;
        const auto yMid = area.getCentreY();
        wave.startNewSubPath(area.getX(), yMid);
        for (int i = 0; i <= 24; ++i) {
            const auto t = static_cast<float>(i) / 24.0f;
            const auto x = area.getX() + t * area.getWidth();
            const auto bump = std::exp(-std::pow((t - 0.5f) * 4.0f, 2.0f)) * area.getHeight() * 0.35f;
            const auto ripple = std::sin(t * juce::MathConstants<float>::twoPi * 2.0f) * area.getHeight() * 0.08f;
            wave.lineTo(x, yMid - bump + ripple);
        }
        g.setColour(accent);
        g.strokePath(wave, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved));
        break;
    }
    case bandforge::TrackKind::Sampler: {
        g.setColour(accent);
        const auto gap = 2.0f;
        const auto cellW = (area.getWidth() - gap) / 2.0f;
        const auto cellH = (area.getHeight() - gap) / 2.0f;
        for (int y = 0; y < 2; ++y) {
            for (int x = 0; x < 2; ++x) {
                g.fillRoundedRectangle(area.getX() + static_cast<float>(x) * (cellW + gap),
                    area.getY() + static_cast<float>(y) * (cellH + gap),
                    cellW, cellH, 2.0f);
            }
        }
        break;
    }
    case bandforge::TrackKind::Master:
        g.setColour(accent);
        g.setFont(juce::Font(juce::FontOptions(13.0f, juce::Font::bold)));
        g.drawText("*", area.toNearestInt(), juce::Justification::centred, false);
        break;
    }
}

juce::String trackKindTagline(bandforge::TrackKind kind)
{
    switch (kind) {
    case bandforge::TrackKind::Keys:
    case bandforge::TrackKind::Midi:
        return "Warm chords";
    case bandforge::TrackKind::Pad:
        return "Wide texture";
    case bandforge::TrackKind::ElectricPiano:
        return "Bell chords";
    case bandforge::TrackKind::Organ:
        return "Drawbar sustain";
    case bandforge::TrackKind::Brass:
        return "Bold stabs";
    case bandforge::TrackKind::Choir:
        return "Airy voices";
    case bandforge::TrackKind::Mallet:
        return "Bright strikes";
    case bandforge::TrackKind::Woodwind:
        return "Breathy lead";
    case bandforge::TrackKind::Strings:
        return "Layered ensemble";
    case bandforge::TrackKind::SynthLead:
        return "Bright melody";
    case bandforge::TrackKind::Arp:
        return "Pulsing pattern";
    case bandforge::TrackKind::Pluck:
        return "Short accents";
    case bandforge::TrackKind::Bass:
        return "Low foundation";
    case bandforge::TrackKind::EightOhEight:
        return "Sub and punch";
    case bandforge::TrackKind::GuitarSynth:
        return "Picked synth";
    case bandforge::TrackKind::DrumKit:
        return "Classic kit";
    case bandforge::TrackKind::DrumRack:
        return "Pad rack";
    case bandforge::TrackKind::BeatSequencer:
        return "Step beats";
    case bandforge::TrackKind::Sampler:
        return "Playable samples";
    case bandforge::TrackKind::Audio:
    case bandforge::TrackKind::Drums:
    case bandforge::TrackKind::Master:
        break;
    }
    return "Software track";
}

int velocityForNumberKey(int keyCode)
{
    switch (keyCode) {
    case '1': return 28;
    case '2': return 40;
    case '3': return 52;
    case '4': return 64;
    case '5': return 76;
    case '6': return 88;
    case '7': return 100;
    case '8': return 112;
    case '9': return 120;
    case '0': return 127;
    default: return -1;
    }
}

class InstrumentPickerComponent final : public juce::Component {
public:
    InstrumentPickerComponent()
    {
        allCategories_ = { "All", "Keyboards", "Synths", "Orchestral", "Bass", "Guitar", "Mallets", "Drums", "Sampler" };

        addCategory("Keyboards", {
            bandforge::TrackKind::Keys,
            bandforge::TrackKind::Pad,
            bandforge::TrackKind::ElectricPiano,
            bandforge::TrackKind::Organ,
        });
        addCategory("Synths", {
            bandforge::TrackKind::SynthLead,
            bandforge::TrackKind::Arp,
            bandforge::TrackKind::Pluck,
        });
        addCategory("Orchestral", {
            bandforge::TrackKind::Strings,
            bandforge::TrackKind::Brass,
            bandforge::TrackKind::Choir,
            bandforge::TrackKind::Woodwind,
        });
        addCategory("Bass", {
            bandforge::TrackKind::Bass,
            bandforge::TrackKind::EightOhEight,
        });
        addCategory("Guitar", {
            bandforge::TrackKind::GuitarSynth,
        });
        addCategory("Mallets", {
            bandforge::TrackKind::Mallet,
        });
        addCategory("Drums", {
            bandforge::TrackKind::DrumKit,
            bandforge::TrackKind::DrumRack,
            bandforge::TrackKind::BeatSequencer,
        });
        addCategory("Sampler", {
            bandforge::TrackKind::Sampler,
        });
    }

    std::function<void(bandforge::TrackKind)> onPicked;

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff161b22));

        // Header
        auto header = getLocalBounds().removeFromTop(54).reduced(20, 10);
        g.setColour(juce::Colour(0xffeef3fa));
        g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        g.drawText("Choose an Instrument", header.withHeight(22), juce::Justification::centredLeft);
        g.setColour(juce::Colour(0xff7a8fa6));
        g.setFont(juce::FontOptions(11.0f));
        g.drawText("Select a sound — BandForge creates the matching track automatically.",
            header.translated(0, 22), juce::Justification::centredLeft);

        // Sidebar background
        g.setColour(juce::Colour(0xff1a2028));
        g.fillRect(sidebarBounds_);

        // Category buttons
        for (std::size_t i = 0; i < allCategories_.size(); ++i) {
            const auto& cat = allCategories_[i];
            const auto active = (cat == selectedCategory_);
            const auto btn = categoryButtonBounds_[i];
            if (active) {
                g.setColour(juce::Colour(0xff3a7d66));
                g.fillRoundedRectangle(btn.toFloat().reduced(4.0f, 2.0f), 6.0f);
                g.setColour(juce::Colour(0xfff0f8f0));
            } else {
                g.setColour(juce::Colour(0xff8a9bae));
            }
            g.setFont(juce::FontOptions(12.0f, active ? juce::Font::bold : juce::Font::plain));
            g.drawText(cat, btn, juce::Justification::centred, true);
        }

        // Instrument cards
        for (std::size_t i = 0; i < cards_.size(); ++i) {
            const auto& card = cards_[i];
            if (!card.visible) continue;
            const auto hovered = static_cast<int>(i) == hoveredIdx_;
            const auto accent = colourForTrackKind(card.kind);
            const auto bounds = card.bounds.toFloat();

            g.setColour(accent.withAlpha(hovered ? 0.30f : 0.14f));
            g.fillRoundedRectangle(bounds, 10.0f);
            g.setColour(accent.withAlpha(hovered ? 1.0f : 0.40f));
            g.drawRoundedRectangle(bounds.reduced(0.5f), 10.0f, hovered ? 2.0f : 1.0f);

            // Icon
            const juce::Rectangle<float> iconBox(bounds.getCentreX() - 12.0f, bounds.getY() + 16.0f, 24.0f, 24.0f);
            g.setColour(accent.withAlpha(0.18f));
            g.fillRoundedRectangle(iconBox, 5.0f);
            drawTrackKindIcon(g, card.kind, iconBox.reduced(4.0f));

            // Name
            g.setColour(hovered ? juce::Colour(0xffffffff) : juce::Colour(0xfff0f4fb));
            g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
            g.drawText(juce::String::fromUTF8(bandforge::displayName(card.kind).c_str()),
                card.bounds.withTop(card.bounds.getY() + 44).reduced(4, 0).withHeight(16),
                juce::Justification::centred, true);

            // Tagline
            g.setColour(juce::Colour(0xff8fa5bb));
            g.setFont(juce::FontOptions(9.5f));
            g.drawText(trackKindTagline(card.kind),
                card.bounds.withTop(card.bounds.getY() + 60).reduced(4, 0).withHeight(13),
                juce::Justification::centred, true);
        }
    }

    void resized() override
    {
        constexpr int kSidebarW  = 130;
        constexpr int kHeaderH   = 54;
        constexpr int kCatH      = 34;
        constexpr int kCardW     = 120;
        constexpr int kCardH     = 82;
        constexpr int kGap       = 10;
        constexpr int kPad       = 14;

        sidebarBounds_ = getLocalBounds().withTrimmedTop(kHeaderH).withWidth(kSidebarW);

        // Category button rows in sidebar
        categoryButtonBounds_.resize(allCategories_.size());
        for (std::size_t i = 0; i < allCategories_.size(); ++i) {
            categoryButtonBounds_[i] = {
                sidebarBounds_.getX(),
                sidebarBounds_.getY() + static_cast<int>(i) * kCatH,
                kSidebarW,
                kCatH,
            };
        }

        // Card grid in the remaining area
        auto gridArea = getLocalBounds().withTrimmedTop(kHeaderH).withTrimmedLeft(kSidebarW).reduced(kPad);
        const int columns = std::max(1, (gridArea.getWidth() + kGap) / (kCardW + kGap));

        for (auto& card : cards_) {
            card.visible = (selectedCategory_ == "All" || card.category == selectedCategory_);
        }

        int col = 0, row = 0;
        for (auto& card : cards_) {
            if (!card.visible) continue;
            card.bounds = {
                gridArea.getX() + col * (kCardW + kGap),
                gridArea.getY() + row * (kCardH + kGap),
                kCardW,
                kCardH,
            };
            if (++col >= columns) { col = 0; ++row; }
        }
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        // Check sidebar category clicks
        for (std::size_t i = 0; i < categoryButtonBounds_.size(); ++i) {
            if (categoryButtonBounds_[i].contains(event.getPosition())) {
                selectedCategory_ = allCategories_[i];
                hoveredIdx_ = -1;
                resized();
                repaint();
                return;
            }
        }
        // Instrument card clicks
        const int index = visibleCardIndexAt(event.getPosition());
        if (index >= 0 && onPicked) {
            onPicked(cards_[static_cast<std::size_t>(index)].kind);
        }
    }

    void mouseMove(const juce::MouseEvent& event) override
    {
        const int index = visibleCardIndexAt(event.getPosition());
        if (index == hoveredIdx_) return;
        hoveredIdx_ = index;
        setMouseCursor(hoveredIdx_ >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    void mouseExit(const juce::MouseEvent&) override
    {
        hoveredIdx_ = -1;
        setMouseCursor(juce::MouseCursor::NormalCursor);
        repaint();
    }

private:
    struct Card {
        bandforge::TrackKind kind;
        juce::String category;
        juce::Rectangle<int> bounds;
        bool visible = true;
    };

    void addCategory(const juce::String& category, std::initializer_list<bandforge::TrackKind> kinds)
    {
        for (const auto kind : kinds)
            cards_.push_back({ kind, category, {}, true });
    }

    int visibleCardIndexAt(juce::Point<int> point) const
    {
        for (std::size_t i = 0; i < cards_.size(); ++i) {
            if (cards_[i].visible && cards_[i].bounds.contains(point))
                return static_cast<int>(i);
        }
        return -1;
    }

    std::vector<Card> cards_;
    std::vector<juce::String> allCategories_;
    std::vector<juce::Rectangle<int>> categoryButtonBounds_;
    juce::Rectangle<int> sidebarBounds_;
    juce::String selectedCategory_ { "All" };
    int hoveredIdx_ = -1;
};

class PluginEditorWindow final : public juce::DocumentWindow {
public:
    PluginEditorWindow(const juce::String& name, juce::Colour backgroundColour)
        : juce::DocumentWindow(name, backgroundColour, juce::DocumentWindow::closeButton, true)
    {
    }

    std::function<void()> onClose;

    void closeButtonPressed() override
    {
        setVisible(false);
        if (onClose) {
            juce::MessageManager::callAsync(onClose);
        }
    }
};

} // namespace

class TrackListComponent final : public juce::Component, private juce::ListBoxModel {
public:
    TrackListComponent(bandforge::Project& project,
        bandforge::ProjectHistory& history,
        bandforge_app::SelectionState& selection)
        : project_(project)
        , history_(history)
        , selection_(selection)
        , listBox_("Tracks", this)
    {
        addAndMakeVisible(listBox_);
        listBox_.setRowHeight(62);
        listBox_.setOutlineThickness(0);
    }

    std::function<void(bandforge::TrackId)> onSelectTrack;
    std::function<float(bandforge::TrackId)> getTrackLevel; // 0..1, for VU meter
    std::function<void(bandforge::TrackId)> onDeleteTrack;
    std::function<void(bandforge::TrackId, std::string /*fxType*/)> onAddTrackEffect;
    std::function<void(bandforge::TrackId, std::string /*presetName*/)> onApplyVoicePreset;
    std::function<void(bandforge::TrackId)> onClearTrackEffects;

    int getNumRows() override
    {
        return static_cast<int>(project_.tracks.size());
    }

    void listBoxItemClicked(int row, const juce::MouseEvent& e) override
    {
        if (row < 0 || row >= static_cast<int>(project_.tracks.size())) return;
        if (!e.mods.isRightButtonDown() && !e.mods.isPopupMenu()) return;

        const auto trackId = project_.tracks[static_cast<std::size_t>(row)].id;
        const auto trackName = project_.tracks[static_cast<std::size_t>(row)].name;
        const bool isMaster = project_.tracks[static_cast<std::size_t>(row)].kind == bandforge::TrackKind::Master;

        juce::PopupMenu menu;
        menu.addSectionHeader(juce::String::fromUTF8(trackName.c_str()));

        juce::PopupMenu fxMenu;
        fxMenu.addItem(101, "Echo");
        fxMenu.addItem(102, "Reverb");
        fxMenu.addItem(103, "Distortion");
        fxMenu.addItem(104, "Telephone");
        fxMenu.addItem(105, "Megaphone");
        menu.addSubMenu("Add Voice Effect", fxMenu);

        juce::PopupMenu presetMenu;
        presetMenu.addItem(201, "Clean (no FX)");
        presetMenu.addItem(202, "Classic Vocal Reverb");
        presetMenu.addItem(203, "Telephone Voice");
        presetMenu.addItem(204, "Megaphone");
        presetMenu.addItem(205, "Distorted Lead");
        presetMenu.addItem(206, "Echo + Reverb");
        menu.addSubMenu("Voice Preset", presetMenu);

        menu.addSeparator();
        menu.addItem(2, "Clear All Effects");
        menu.addSeparator();
        menu.addItem(1, "Delete Track", !isMaster);

        menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(&listBox_),
            [this, trackId](int result) {
                if (result == 0) return;
                if (result == 1 && onDeleteTrack) onDeleteTrack(trackId);
                else if (result == 2 && onClearTrackEffects) onClearTrackEffects(trackId);
                else if (result >= 101 && result <= 105 && onAddTrackEffect) {
                    static const char* kFxNames[] = { "echo", "reverb", "distortion", "telephone", "megaphone" };
                    onAddTrackEffect(trackId, kFxNames[result - 101]);
                }
                else if (result >= 201 && result <= 206 && onApplyVoicePreset) {
                    static const char* kPresetNames[] = {
                        "clean", "classic-vocal", "telephone", "megaphone", "distorted-lead", "echo-reverb"
                    };
                    onApplyVoicePreset(trackId, kPresetNames[result - 201]);
                }
            });
    }

    void paintListBoxItem(int rowNumber, juce::Graphics& graphics, int width, int height, bool rowIsSelected) override
    {
        juce::ignoreUnused(rowIsSelected);
        graphics.fillAll(juce::Colour(0xff1b2028));
        if (rowNumber < 0 || rowNumber >= static_cast<int>(project_.tracks.size())) {
            return;
        }
        const auto& track = project_.tracks[static_cast<std::size_t>(rowNumber)];

        auto row = juce::Rectangle<int>(8, 6, width - 16, height - 10);
        const bool selected = selection_.selectedTrackId == track.id;

        graphics.setColour(selected ? juce::Colour(0xff2b3441) : juce::Colour(0xff252c36));
        graphics.fillRoundedRectangle(row.toFloat(), 7.0f);

        const juce::Colour accent = trackColour(track);
        graphics.setColour(accent);
        graphics.fillRoundedRectangle(row.withWidth(6).toFloat(), 3.0f);

        auto content = row.withTrimmedLeft(14);
        auto iconArea = juce::Rectangle<float>(
            static_cast<float>(content.getX()),
            static_cast<float>(row.getCentreY() - 8),
            16.0f,
            16.0f);
        drawTrackKindIcon(graphics, track.kind, iconArea);
        auto textArea = content.withTrimmedLeft(26);

        graphics.setColour(juce::Colour(0xffeef3fa));
        graphics.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        graphics.drawText(track.name, textArea.withHeight(22), juce::Justification::centredLeft);

        graphics.setColour(juce::Colour(0xff91a0b4));
        graphics.setFont(juce::FontOptions(11.0f));
        juce::String detail = juce::String::fromUTF8(bandforge::toString(track.kind).c_str());
        graphics.drawText(detail, textArea.translated(0, 20).withHeight(18), juce::Justification::centredLeft);

        // VU meter bar
        const float level = getTrackLevel ? getTrackLevel(track.id) : 0.0f;
        if (level > 0.0f) {
            const auto meterArea = textArea.translated(0, 40).withHeight(5);
            const int meterW = static_cast<int>(level * static_cast<float>(meterArea.getWidth()));
            const juce::Colour meterColour = level > 0.85f ? juce::Colour(0xffff5555)
                                           : level > 0.6f  ? juce::Colour(0xffddcc44)
                                                            : accent.withAlpha(0.8f);
            graphics.setColour(juce::Colour(0xff1a2030));
            graphics.fillRoundedRectangle(meterArea.toFloat(), 2.0f);
            graphics.setColour(meterColour);
            graphics.fillRoundedRectangle(meterArea.withWidth(meterW).toFloat(), 2.0f);
        }
    }

    juce::Component* refreshComponentForRow(int rowNumber, bool, juce::Component* existingComponentToUpdate) override
    {
        if (rowNumber < 0 || rowNumber >= static_cast<int>(project_.tracks.size())) {
            delete existingComponentToUpdate;
            return nullptr;
        }

        class TrackRowControls final : public juce::Component {
        public:
            TrackRowControls(bandforge::Project& project, bandforge::ProjectHistory& history, bandforge::TrackId trackId)
                : project_(project)
                , history_(history)
                , trackId_(trackId)
            {
                for (auto* button : { &mute_, &solo_, &arm_ }) {
                    addAndMakeVisible(*button);
                    button->setClickingTogglesState(true);
                    button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff303a47));
                    button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffd7dde8));
                }

                mute_.setButtonText("M");
                solo_.setButtonText("S");
                arm_.setButtonText("R");

                mute_.onClick = [this] { toggle(&bandforge::MixerChannel::muted, mute_.getToggleState()); };
                solo_.onClick = [this] { toggle(&bandforge::MixerChannel::solo, solo_.getToggleState()); };
                arm_.onClick = [this] { toggle(&bandforge::MixerChannel::recordArmed, arm_.getToggleState()); };
            }

            void setTrackId(bandforge::TrackId trackId)
            {
                trackId_ = trackId;
                sync();
            }

            void resized() override
            {
                auto bounds = getLocalBounds().reduced(0, 8);
                bounds.removeFromLeft(std::max(120, bounds.getWidth() - 102));
                mute_.setBounds(bounds.removeFromLeft(32).reduced(2));
                solo_.setBounds(bounds.removeFromLeft(32).reduced(2));
                arm_.setBounds(bounds.removeFromLeft(32).reduced(2));
            }

            void paint(juce::Graphics&) override
            {
                sync();
            }

        private:
            void sync()
            {
                if (auto* track = project_.findTrack(trackId_)) {
                    mute_.setToggleState(track->mixer.muted, juce::dontSendNotification);
                    solo_.setToggleState(track->mixer.solo, juce::dontSendNotification);
                    arm_.setToggleState(track->mixer.recordArmed, juce::dontSendNotification);
                }
            }

            void toggle(bool bandforge::MixerChannel::*member, bool state)
            {
                if (auto* track = project_.findTrack(trackId_)) {
                    history_.remember(project_);
                    track->mixer.*member = state;
                }
            }

            bandforge::Project& project_;
            bandforge::ProjectHistory& history_;
            bandforge::TrackId trackId_ = 0;

            juce::TextButton mute_;
            juce::TextButton solo_;
            juce::TextButton arm_;
        };

        if (existingComponentToUpdate == nullptr) {
            existingComponentToUpdate = new TrackRowControls(project_, history_, project_.tracks[static_cast<std::size_t>(rowNumber)].id);
        }

        auto* controls = dynamic_cast<TrackRowControls*>(existingComponentToUpdate);
        if (controls != nullptr) {
            controls->setTrackId(project_.tracks[static_cast<std::size_t>(rowNumber)].id);
        }

        return existingComponentToUpdate;
    }

    void selectedRowsChanged(int lastRowSelected) override
    {
        if (lastRowSelected < 0 || lastRowSelected >= static_cast<int>(project_.tracks.size())) {
            return;
        }
        const auto trackId = project_.tracks[static_cast<std::size_t>(lastRowSelected)].id;
        selection_.selectedTrackId = trackId;
        selection_.selectedClipId = 0;
        if (onSelectTrack) {
            onSelectTrack(trackId);
        }
        repaint();
    }

    void resized() override
    {
        listBox_.setBounds(getLocalBounds().withTrimmedTop(42));
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(juce::Colour(0xff1b2028));
        graphics.setColour(juce::Colour(0xffd7dde8));
        graphics.setFont(juce::FontOptions(17.0f, juce::Font::bold));
        graphics.drawText("Tracks", 16, 12, getWidth() - 32, 24, juce::Justification::centredLeft);
    }

    void refresh()
    {
        listBox_.updateContent();
        listBox_.repaint();
    }

private:
    bandforge::Project& project_;
    bandforge::ProjectHistory& history_;
    bandforge_app::SelectionState& selection_;
    juce::ListBox listBox_;
};

class TimelineComponent final : public juce::Component,
                               public juce::FileDragAndDropTarget {
public:
    TimelineComponent(bandforge::Project& project,
        bandforge::Transport& transport,
        bandforge::GridSettings& grid,
        bandforge::ProjectHistory& history,
        bandforge_app::SelectionState& selection)
        : project_(project)
        , transport_(transport)
        , grid_(grid)
        , history_(history)
        , selection_(selection)
        , editor_(project)
    {
    }

    std::function<void(double)> onSeek;
    std::function<void(bandforge::TrackId)> onSelectTrack;
    std::function<void(bandforge::TrackId, bandforge::ClipId)> onSelectClip;

    void setAudioEngine(bandforge::AudioEngine* engine) { audioEngine_ = engine; }

    // ── FileDragAndDropTarget ─────────────────────────────────────────────────
    bool isInterestedInFileDrag(const juce::StringArray& files) override
    {
        for (const auto& f : files) {
            const auto ext = juce::File(f).getFileExtension().toLowerCase();
            if (ext == ".wav" || ext == ".aif" || ext == ".aiff"
             || ext == ".mp3" || ext == ".ogg" || ext == ".flac"
             || ext == ".mid" || ext == ".midi")
                return true;
        }
        return false;
    }

    void fileDragEnter(const juce::StringArray&, int, int) override { dropHighlight_ = true;  repaint(); }
    void fileDragExit(const juce::StringArray&)              override { dropHighlight_ = false; repaint(); }

    void filesDropped(const juce::StringArray& files, int x, int y) override
    {
        dropHighlight_ = false;
        repaint();

        const double dropBeat = std::max(0.0, grid_.pixelToBeat(static_cast<double>(x)));

        // Determine which track was dropped on (if any)
        bandforge::TrackId dropTrackId = 0;
        {
            constexpr int rulerH = 34, rowH = 66;
            int ty = rulerH + 8;
            for (const auto& track : project_.tracks) {
                if (y >= ty - 4 && y < ty - 4 + rowH) { dropTrackId = track.id; break; }
                ty += rowH;
            }
        }

        for (const auto& filePath : files) {
            const juce::File f(filePath);
            const auto ext = f.getFileExtension().toLowerCase();
            const bool isMidi = (ext == ".mid" || ext == ".midi");

            history_.remember(project_);

            if (isMidi) {
                importMidiFile(f, dropBeat, dropTrackId);
            } else {
                importAudioFile(f, dropBeat, dropTrackId);
            }
        }
        repaint();
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(juce::Colour(0xff101419));

        const int rulerHeight = 34;
        const int rowHeight = 66;
        const int bars = std::max(8, static_cast<int>((project_.durationBeats() + 8.0) / 4.0));

        // Drop highlight overlay
        if (dropHighlight_) {
            graphics.setColour(juce::Colour(0x1a5fb3ff));
            graphics.fillRect(getLocalBounds());
            graphics.setColour(juce::Colour(0xff5fb3ff));
            graphics.drawRect(getLocalBounds(), 2);
        }

        graphics.setColour(juce::Colour(0xff252d38));
        graphics.fillRect(0, 0, getWidth(), rulerHeight);

        for (int beat = 0; beat <= bars * 4; ++beat) {
            const int x = static_cast<int>(grid_.beatToPixel(static_cast<double>(beat)));
            graphics.setColour(beat % 4 == 0 ? juce::Colour(0xff445162) : juce::Colour(0xff252d37));
            graphics.drawVerticalLine(x, 0.0f, static_cast<float>(getHeight()));

            if (beat % 4 == 0) {
                graphics.setColour(juce::Colour(0xffd2dae6));
                graphics.setFont(juce::FontOptions(12.0f, juce::Font::bold));
                graphics.drawText(juce::String((beat / 4) + 1), x + 6, 6, 40, 18, juce::Justification::centredLeft);
            }
        }

        int y = rulerHeight + 8;
        for (const auto& track : project_.tracks) {
            graphics.setColour(juce::Colour(0xff171c23));
            graphics.fillRect(0, y - 4, getWidth(), rowHeight);
            graphics.setColour(juce::Colour(0xff0e1218));
            graphics.drawHorizontalLine(y - 4, 0.0f, static_cast<float>(getWidth()));
            graphics.setColour(trackColour(track).withAlpha(0.9f));
            graphics.fillRect(0, y - 4, 5, rowHeight);

            if (selection_.selectedTrackId == track.id) {
                graphics.setColour(juce::Colour(0x1affffff));
                graphics.fillRect(0, y - 4, getWidth(), rowHeight);
            }

            for (const auto& clip : track.clips) {
                const int x = static_cast<int>(grid_.beatToPixel(clip.startBeat));
                const int w = std::max(22, static_cast<int>(grid_.beatToPixel(clip.lengthBeats)) - 4);
                auto clipRect = juce::Rectangle<int>(x + 2, y + 6, w, 46);

                const juce::Colour trackAccent = trackColour(track);
                auto colour = colourFromHex(clip.color, trackAccent);
                const bool selected = (selection_.selectedClipId == clip.id) && (selection_.selectedTrackId == track.id);

                // Body
                graphics.setColour(colour.withAlpha(0.18f).withMultipliedBrightness(selected ? 1.4f : 1.0f));
                graphics.fillRoundedRectangle(clipRect.toFloat(), 7.0f);

                // Header strip
                const auto headerRect = clipRect.removeFromTop(18);
                graphics.setColour(colour.withAlpha(0.82f));
                graphics.fillRoundedRectangle(headerRect.toFloat(), 7.0f);
                // Square off bottom corners of header
                graphics.fillRect(headerRect.withTrimmedTop(6));

                // Clip name in header
                graphics.setColour(colour.getBrightness() > 0.55f ? juce::Colour(0xff0f151c) : juce::Colours::white);
                graphics.setFont(juce::FontOptions(11.0f, juce::Font::bold));
                graphics.drawText(clip.name, headerRect.reduced(6, 1), juce::Justification::centredLeft, true);

                // Border
                graphics.setColour(selected ? juce::Colour(0xfff0f5ff) : colour.brighter(0.3f).withAlpha(0.7f));
                graphics.drawRoundedRectangle(
                    juce::Rectangle<int>(x + 2, y + 6, w, 46).toFloat(),
                    7.0f, selected ? 2.0f : 1.0f);

                const auto previewArea = juce::Rectangle<int>(x + 2, y + 6, w, 46).reduced(6, 0).withTrimmedTop(20);
                if (clip.kind == bandforge::ClipKind::Midi) {
                    drawMidiPreview(graphics, clip, previewArea, colour);
                } else {
                    drawAudioPreview(graphics, clip, previewArea, colour);
                }
            }

            // Automation lane overlay at bottom of track row
            if (!track.automation.empty()) {
                const int laneH = 10;
                const int laneY = y + rowHeight - laneH - 3;
                const double totalBeats = std::max(1.0, project_.durationBeats() + 4.0);
                static const juce::Colour kAutoColours[] = {
                    juce::Colour(0xff44bbff), juce::Colour(0xffff9944), juce::Colour(0xff88dd55)
                };
                for (std::size_t li = 0; li < track.automation.size() && li < 3; ++li) {
                    const auto& lane = track.automation[li];
                    if (lane.points.empty()) continue;
                    graphics.setColour(kAutoColours[li % 3].withAlpha(0.6f));
                    int prevX = 0;
                    float prevY = 0.0f;
                    for (int px = 0; px < getWidth(); px += 3) {
                        const double beat = grid_.pixelToBeat(static_cast<double>(px));
                        const float valNorm = static_cast<float>((lane.valueAt(beat, 0.0) + 1.0) * 0.5);
                        const int lineY = laneY + laneH - static_cast<int>(valNorm * laneH);
                        if (px > 0) {
                            graphics.drawLine(static_cast<float>(prevX), prevY,
                                static_cast<float>(px), static_cast<float>(lineY), 1.5f);
                        }
                        prevX = px; prevY = static_cast<float>(lineY);
                    }
                }
            }

            y += rowHeight;
        }

        // Cue markers on ruler
        for (const auto& marker : project_.markers) {
            const int mx = static_cast<int>(grid_.beatToPixel(marker.beat));
            const auto mColour = colourFromHex(marker.color, juce::Colour(0xffffd740));
            graphics.setColour(mColour.withAlpha(0.85f));
            graphics.drawVerticalLine(mx, 0.0f, static_cast<float>(rulerHeight));
            // Diamond head
            const float cx = static_cast<float>(mx);
            const float cy = 10.0f;
            juce::Path diamond;
            diamond.addTriangle(cx, cy - 6.0f, cx - 5.0f, cy + 2.0f, cx + 5.0f, cy + 2.0f);
            graphics.fillPath(diamond);
            // Label
            graphics.setFont(juce::FontOptions(9.0f, juce::Font::bold));
            graphics.setColour(mColour);
            graphics.drawText(juce::String::fromUTF8(marker.name.c_str()), mx + 4, 4, 80, 14, juce::Justification::centredLeft, true);
        }

        const int playheadX = static_cast<int>(grid_.beatToPixel(transport_.positionBeat()));
        graphics.setColour(juce::Colour(0xffff5858));
        graphics.drawVerticalLine(playheadX, 0.0f, static_cast<float>(getHeight()));
        graphics.fillEllipse(static_cast<float>(playheadX - 5), 20.0f, 10.0f, 10.0f);
    }

    void mouseDoubleClick(const juce::MouseEvent& event) override
    {
        if (event.y > 34) return; // only in ruler
        const double beat = grid_.snap(std::max(0.0, grid_.pixelToBeat(static_cast<double>(event.x))));
        history_.remember(project_);
        bandforge::Marker m;
        m.beat = beat;
        m.name = "Marker " + std::to_string(project_.markers.size() + 1);
        project_.markers.push_back(m);
        std::sort(project_.markers.begin(), project_.markers.end(), [](const auto& a, const auto& b) { return a.beat < b.beat; });
        repaint();
    }

    void mouseDown(const juce::MouseEvent& event) override
    {
        drag_ = {};
        
        // Check if clicking on playhead
        const int playheadX = static_cast<int>(grid_.beatToPixel(transport_.positionBeat()));
        const int playheadHitRadius = 10;
        if (std::abs(event.x - playheadX) <= playheadHitRadius) {
            drag_.active = true;
            drag_.isPlayheadDrag = true;
            drag_.grabBeat = grid_.pixelToBeat(static_cast<double>(event.x));
            setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
            return;
        }
        
        if (event.y <= 34) {
            if (onSeek) {
                onSeek(std::max(0.0, grid_.pixelToBeat(static_cast<double>(event.x))));
            }
            return;
        }

        auto hit = hitTest(event.getPosition());
        if (hit.trackId != 0 && onSelectTrack) {
            onSelectTrack(hit.trackId);
        }
        if (hit.clipId != 0 && onSelectClip) {
            onSelectClip(hit.trackId, hit.clipId);
        }

        if (hit.clipId != 0) {
            drag_.active = true;
            drag_.isPlayheadDrag = false;
            drag_.trackId = hit.trackId;
            drag_.clipId = hit.clipId;
            drag_.grabBeat = grid_.pixelToBeat(static_cast<double>(event.x));
            if (const auto* clip = project_.findClip(hit.trackId, hit.clipId)) {
                drag_.originalStartBeat = clip->startBeat;
                drag_.originalLengthBeats = clip->lengthBeats;
            }

            const int edgePx = 8;
            drag_.mode = DragMode::Move;
            if (hit.onLeftEdge && std::abs(event.x - hit.clipRect.getX()) <= edgePx) {
                drag_.mode = DragMode::TrimLeft;
            } else if (hit.onRightEdge && std::abs(event.x - hit.clipRect.getRight()) <= edgePx) {
                drag_.mode = DragMode::TrimRight;
            }
            setMouseCursor(drag_.mode == DragMode::Move ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::LeftRightResizeCursor);
        } else {
            setMouseCursor(juce::MouseCursor::NormalCursor);
        }
    }

    void mouseDrag(const juce::MouseEvent& event) override
    {
        if (!drag_.active) {
            return;
        }

        const double currentBeat = grid_.pixelToBeat(static_cast<double>(event.x));
        
        if (drag_.isPlayheadDrag) {
            if (onSeek) {
                onSeek(std::max(0.0, currentBeat));
            }
            repaint();
            return;
        }

        const double delta = currentBeat - drag_.grabBeat;

        if (!drag_.remembered) {
            history_.remember(project_);
            drag_.remembered = true;
        }

        if (drag_.mode == DragMode::Move) {
            editor_.moveClip(drag_.trackId, drag_.clipId, drag_.originalStartBeat + delta, grid_);
        } else if (drag_.mode == DragMode::TrimLeft) {
            const double newStart = drag_.originalStartBeat + delta;
            const double newLen = (drag_.originalStartBeat + drag_.originalLengthBeats) - newStart;
            editor_.trimClip(drag_.trackId, drag_.clipId, newStart, newLen, grid_);
        } else if (drag_.mode == DragMode::TrimRight) {
            const double newLen = drag_.originalLengthBeats + delta;
            editor_.trimClip(drag_.trackId, drag_.clipId, drag_.originalStartBeat, newLen, grid_);
        }
        repaint();
    }

    void mouseUp(const juce::MouseEvent&) override
    {
        drag_ = {};
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }

private:
    enum class DragMode {
        Move,
        TrimLeft,
        TrimRight
    };

    struct HitResult {
        bandforge::TrackId trackId = 0;
        bandforge::ClipId clipId = 0;
        juce::Rectangle<int> clipRect;
        bool onLeftEdge = false;
        bool onRightEdge = false;
    };

    struct DragState {
        bool active = false;
        bool isPlayheadDrag = false;
        DragMode mode = DragMode::Move;
        bandforge::TrackId trackId = 0;
        bandforge::ClipId clipId = 0;
        double grabBeat = 0.0;
        double originalStartBeat = 0.0;
        double originalLengthBeats = 0.0;
        bool remembered = false;
    };

    HitResult hitTest(juce::Point<int> point) const
    {
        const int rulerHeight = 34;
        const int rowHeight = 66;
        int y = rulerHeight + 8;

        for (const auto& track : project_.tracks) {
            const auto rowRect = juce::Rectangle<int>(0, y - 4, getWidth(), rowHeight);
            if (!rowRect.contains(point)) {
                y += rowHeight;
                continue;
            }

            for (const auto& clip : track.clips) {
                const int x = static_cast<int>(grid_.beatToPixel(clip.startBeat));
                const int w = std::max(18, static_cast<int>(grid_.beatToPixel(clip.lengthBeats)) - 4);
                auto clipRect = juce::Rectangle<int>(x + 2, y + 8, w, 42);
                if (clipRect.contains(point)) {
                    const int edgePx = 8;
                    return {
                        track.id,
                        clip.id,
                        clipRect,
                        std::abs(point.x - clipRect.getX()) <= edgePx,
                        std::abs(point.x - clipRect.getRight()) <= edgePx,
                    };
                }
            }

            return { track.id, 0, {}, false, false };
        }

        return {};
    }

    void drawAudioPreview(juce::Graphics& graphics, const bandforge::Clip& clip, juce::Rectangle<int> area, juce::Colour accent) const
    {
        graphics.setColour(accent.withAlpha(0.35f));
        graphics.drawHorizontalLine(area.getCentreY(), static_cast<float>(area.getX()), static_cast<float>(area.getRight()));

        const int barStep = 3;
        const int numBars = std::max(1, area.getWidth() / barStep);

        // Try to draw from real cached audio data
        const bandforge::AudioEngine::CachedAudioClip* audio = audioEngine_
            ? audioEngine_->peekAudioForPath(clip.audio.mediaPath) : nullptr;

        graphics.setColour(accent.withAlpha(0.6f));

        if (audio && audio->sampleRate > 0 && audio->channels > 0 && !audio->samples.empty()) {
            const auto totalFrames = static_cast<int64_t>(audio->samples.size() / static_cast<std::size_t>(audio->channels));
            const auto ch = static_cast<std::size_t>(std::min(1, audio->channels - 1)); // use right channel for stereo, mono otherwise
            for (int b = 0; b < numBars; ++b) {
                const double tNorm = static_cast<double>(b) / numBars;
                const auto f0 = static_cast<int64_t>(tNorm * static_cast<double>(totalFrames));
                const auto f1 = std::min(totalFrames, f0 + std::max(int64_t{1}, totalFrames / numBars));
                float peak = 0.0f;
                for (int64_t f = f0; f < f1; ++f) {
                    peak = std::max(peak, std::abs(audio->samples[static_cast<std::size_t>(f) * static_cast<std::size_t>(audio->channels) + ch]));
                }
                const int height = std::max(2, static_cast<int>(peak * static_cast<float>(area.getHeight())));
                const int x = area.getX() + b * barStep;
                graphics.drawVerticalLine(x, static_cast<float>(area.getCentreY() - height / 2), static_cast<float>(area.getCentreY() + height / 2));
            }
        } else {
            // Fallback: deterministic pseudo-random bars until audio is cached
            auto seed = static_cast<std::uint32_t>(std::hash<std::string>{}(clip.audio.mediaPath.empty() ? clip.name : clip.audio.mediaPath));
            for (int b = 0; b < numBars; ++b) {
                seed = (seed * 1664525u) + 1013904223u;
                const auto amount = 0.22f + static_cast<float>((seed >> 16) & 0xffu) / 255.0f * 0.78f;
                const int height = std::max(2, static_cast<int>(amount * static_cast<float>(area.getHeight())));
                const int x = area.getX() + b * barStep;
                graphics.drawVerticalLine(x, static_cast<float>(area.getCentreY() - height / 2), static_cast<float>(area.getCentreY() + height / 2));
            }
        }
    }

    static void drawMidiPreview(juce::Graphics& graphics, const bandforge::Clip& clip, juce::Rectangle<int> area, juce::Colour accent)
    {
        if (clip.midi.notes.empty() || clip.lengthBeats <= 0.0) return;
        int minP = 127, maxP = 0;
        for (const auto& note : clip.midi.notes) {
            minP = std::min(minP, note.pitch);
            maxP = std::max(maxP, note.pitch);
        }
        const int pitchRange = std::max(1, maxP - minP);
        graphics.setColour(accent.withAlpha(0.75f));
        for (const auto& note : clip.midi.notes) {
            const double xNorm = clip.lengthBeats <= 0.0 ? 0.0 : note.startBeat / clip.lengthBeats;
            const int x = area.getX() + static_cast<int>(xNorm * area.getWidth());
            const int y = area.getBottom() - 2 - ((note.pitch - minP) * (area.getHeight() - 4) / pitchRange);
            graphics.fillRect(x, y, std::max(3, static_cast<int>((note.durationBeats / clip.lengthBeats) * area.getWidth()) - 1), 2);
        }
    }

    void importAudioFile(const juce::File& file, double dropBeat, bandforge::TrackId targetTrackId)
    {
        // Find or create an audio track to land on
        bandforge::Track* track = project_.findTrack(targetTrackId);
        if (track == nullptr || bandforge::isMidiTrackKind(track->kind)) {
            auto& newTrack = project_.addTrack(bandforge::TrackKind::Audio,
                file.getFileNameWithoutExtension().toStdString());
            track = &newTrack;
        }

        const std::string pathStr = file.getFullPathName().toStdString();
        auto& clip = project_.addAudioClip(track->id, file.getFileNameWithoutExtension().toStdString(),
            pathStr, grid_.snap(dropBeat), 4.0);
        clip.audio.stretchToProjectTempo = true;
        selection_.selectedTrackId = track->id;
        selection_.selectedClipId  = clip.id;
    }

    void importMidiFile(const juce::File& file, double dropBeat, bandforge::TrackId targetTrackId)
    {
        juce::MidiFile midiFile;
        juce::FileInputStream stream(file);
        if (!stream.openedOk() || !midiFile.readFrom(stream)) return;

        // Read tempo from meta events before converting timestamps
        double bpm = 120.0;
        for (int ti = 0; ti < midiFile.getNumTracks(); ++ti) {
            const auto* seq = midiFile.getTrack(ti);
            if (!seq) continue;
            for (int ei = 0; ei < seq->getNumEvents(); ++ei) {
                const auto& msg = seq->getEventPointer(ei)->message;
                if (msg.isTempoMetaEvent()) {
                    bpm = 60'000'000.0 / static_cast<double>(msg.getTempoSecondsPerQuarterNote() * 1'000'000.0);
                    break;
                }
            }
        }

        midiFile.convertTimestampTicksToSeconds();
        const int numTracks = midiFile.getNumTracks();

        for (int ti = 0; ti < numTracks; ++ti) {
            const auto* seq = midiFile.getTrack(ti);
            if (seq == nullptr) continue;

            bandforge::MidiClipData data;
            double lastNoteSec = 0.0;

            std::map<int, std::pair<double, int>> activeNotes;
            for (int ei = 0; ei < seq->getNumEvents(); ++ei) {
                const auto& evt = seq->getEventPointer(ei)->message;
                const double t = evt.getTimeStamp();
                if (evt.isNoteOn()) {
                    activeNotes[evt.getNoteNumber()] = { t, evt.getVelocity() };
                } else if (evt.isNoteOff()) {
                    auto it = activeNotes.find(evt.getNoteNumber());
                    if (it != activeNotes.end()) {
                        const double startBeat = it->second.first * (bpm / 60.0);
                        const double durBeats  = (t - it->second.first) * (bpm / 60.0);
                        bandforge::MidiNote note;
                        note.pitch = evt.getNoteNumber();
                        note.velocity = it->second.second;
                        note.channel  = evt.getChannel();
                        note.startBeat = startBeat;
                        note.durationBeats = std::max(0.03125, durBeats);
                        data.notes.push_back(note);
                        lastNoteSec = std::max(lastNoteSec, t);
                        activeNotes.erase(it);
                    }
                }
            }
            if (data.notes.empty()) continue;

            const double clipBeats = std::max(4.0, lastNoteSec * (bpm / 60.0));

            bandforge::Track* track = project_.findTrack(targetTrackId);
            if (track == nullptr || !bandforge::isMidiTrackKind(track->kind)) {
                auto& newTrack = project_.addTrack(bandforge::TrackKind::Keys,
                    file.getFileNameWithoutExtension().toStdString());
                track = &newTrack;
            }

            auto& clip = project_.addMidiClip(track->id,
                file.getFileNameWithoutExtension().toStdString(),
                grid_.snap(dropBeat), clipBeats);
            clip.midi = std::move(data);
            selection_.selectedTrackId = track->id;
            selection_.selectedClipId  = clip.id;
        }
    }

    bandforge::Project& project_;
    bandforge::Transport& transport_;
    bandforge::GridSettings& grid_;
    bandforge::ProjectHistory& history_;
    bandforge_app::SelectionState& selection_;
    bandforge::TimelineEditor editor_;
    bandforge::AudioEngine* audioEngine_ = nullptr;
    DragState drag_;
    bool dropHighlight_ = false;
};

class LibraryPanelComponent final : public juce::Component, private juce::ListBoxModel {
public:
    LibraryPanelComponent(bandforge::SoundLibrary& library,
        bandforge::Project& project,
        bandforge::ProjectHistory& history,
        bandforge::Transport& transport,
        bandforge::GridSettings& grid,
        bandforge_app::SelectionState& selection)
        : library_(library)
        , project_(project)
        , history_(history)
        , transport_(transport)
        , grid_(grid)
        , selection_(selection)
        , results_("LibraryResults", this)
    {
        addAndMakeVisible(search_);
        search_.setTextToShowWhenEmpty("Search loops and presets", juce::Colour(0xff6f7f95));
        search_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff252c36));
        search_.setColour(juce::TextEditor::textColourId, juce::Colour(0xffeef3fa));
        search_.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xff303a47));
        search_.onTextChange = [this] { refreshResults(); };

        populateFilters();

        configureModeButton(audioLoopsTab_, "Loops", LibraryMode::AudioLoops);
        configureModeButton(midiLoopsTab_, "MIDI", LibraryMode::MidiLoops);
        configureModeButton(instrumentsTab_, "Inst", LibraryMode::Instruments);
        configureModeButton(drumsTab_, "Drums", LibraryMode::Drums);
        configureModeButton(presetsTab_, "Presets", LibraryMode::Presets);

        insertButton_.setButtonText("Insert");
        insertButton_.onClick = [this] { insertSelection(); };
        insertButton_.setEnabled(false);

        for (auto* button : { &audioLoopsTab_, &midiLoopsTab_, &instrumentsTab_, &drumsTab_, &presetsTab_, &insertButton_ }) {
            addAndMakeVisible(*button);
            button->setColour(juce::TextButton::buttonColourId, juce::Colour(0xff28313d));
            button->setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a7d66));
            button->setColour(juce::TextButton::textColourOffId, juce::Colour(0xffedf3fb));
            button->setColour(juce::TextButton::textColourOnId, juce::Colour(0xffffffff));
        }

        for (auto* filter : { &instrumentFilter_, &genreFilter_, &keyFilter_, &bpmFilter_, &tagFilter_ }) {
            addAndMakeVisible(*filter);
            filter->setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff252c36));
            filter->setColour(juce::ComboBox::textColourId, juce::Colour(0xffeef3fa));
            filter->setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff303a47));
            filter->onChange = [this] { refreshResults(); };
        }

        addAndMakeVisible(results_);
        results_.setRowHeight(62);
        results_.setOutlineThickness(0);
        setMode(LibraryMode::AudioLoops);
    }

    void paint(juce::Graphics& graphics) override
    {
        graphics.fillAll(juce::Colour(0xff1b2028));
        graphics.setColour(juce::Colour(0xffd7dde8));
        graphics.setFont(juce::FontOptions(17.0f, juce::Font::bold));
        graphics.drawText("Library", 16, 12, getWidth() - 32, 24, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto bounds = getLocalBounds();
        bounds.removeFromTop(42);

        auto tabs = bounds.removeFromTop(32).reduced(10, 0);
        auto tabWidth = tabs.getWidth() / 5;
        audioLoopsTab_.setBounds(tabs.removeFromLeft(tabWidth).reduced(2));
        midiLoopsTab_.setBounds(tabs.removeFromLeft(tabWidth).reduced(2));
        instrumentsTab_.setBounds(tabs.removeFromLeft(tabWidth).reduced(2));
        drumsTab_.setBounds(tabs.removeFromLeft(tabWidth).reduced(2));
        presetsTab_.setBounds(tabs.reduced(2));

        bounds.removeFromTop(10);
        search_.setBounds(bounds.removeFromTop(30).reduced(10, 0));
        bounds.removeFromTop(10);
        auto filtersA = bounds.removeFromTop(28).reduced(10, 0);
        instrumentFilter_.setBounds(filtersA.removeFromLeft(filtersA.getWidth() / 2).reduced(2, 0));
        genreFilter_.setBounds(filtersA.reduced(2, 0));
        bounds.removeFromTop(6);
        auto filtersB = bounds.removeFromTop(28).reduced(10, 0);
        const auto filterWidth = filtersB.getWidth() / 3;
        keyFilter_.setBounds(filtersB.removeFromLeft(filterWidth).reduced(2, 0));
        bpmFilter_.setBounds(filtersB.removeFromLeft(filterWidth).reduced(2, 0));
        tagFilter_.setBounds(filtersB.reduced(2, 0));
        bounds.removeFromTop(10);
        insertButton_.setBounds(bounds.removeFromBottom(34).reduced(10, 0));
        bounds.removeFromBottom(10);
        results_.setBounds(bounds);
    }

private:
    enum class LibraryMode {
        AudioLoops,
        MidiLoops,
        Instruments,
        Drums,
        Presets
    };

    enum class RowKind {
        Loop,
        Preset
    };

    struct RowItem {
        RowKind kind = RowKind::Loop;
        int index = 0;
        juce::String title;
        juce::String detail;
        juce::String badge;
        juce::Colour accent;
    };

    int getNumRows() override
    {
        return static_cast<int>(filtered_.size());
    }

    void paintListBoxItem(int rowNumber, juce::Graphics& graphics, int width, int height, bool rowIsSelected) override
    {
        juce::ignoreUnused(rowIsSelected);
        graphics.fillAll(juce::Colour(0xff1b2028));
        if (rowNumber < 0 || rowNumber >= static_cast<int>(filtered_.size())) {
            return;
        }

        const auto& item = filtered_[static_cast<std::size_t>(rowNumber)];
        auto row = juce::Rectangle<int>(10, 6, width - 20, height - 10);
        graphics.setColour(rowNumber == selectedRow_ ? juce::Colour(0xff303a47) : juce::Colour(0xff252c36));
        graphics.fillRoundedRectangle(row.toFloat(), 7.0f);

        auto badgeArea = row.removeFromRight(66).reduced(6, 16);
        graphics.setColour(item.accent.withAlpha(0.92f));
        graphics.fillRoundedRectangle(badgeArea.toFloat(), 5.0f);
        graphics.setColour(juce::Colour(0xff071015));
        graphics.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        graphics.drawText(item.badge, badgeArea, juce::Justification::centred);

        graphics.setColour(juce::Colour(0xffeef3fa));
        graphics.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        graphics.drawText(item.title, row.reduced(10, 6).withHeight(18), juce::Justification::centredLeft);

        graphics.setColour(juce::Colour(0xff91a0b4));
        graphics.setFont(juce::FontOptions(11.0f));
        graphics.drawText(item.detail, row.reduced(10, 26).withHeight(18), juce::Justification::centredLeft);
    }

    void selectedRowsChanged(int lastRowSelected) override
    {
        selectedRow_ = lastRowSelected;
        insertButton_.setEnabled(selectedRow_ >= 0 && selectedRow_ < static_cast<int>(filtered_.size()));
    }

    void configureModeButton(juce::TextButton& button, const juce::String& text, LibraryMode mode)
    {
        button.setButtonText(text);
        button.setClickingTogglesState(true);
        button.onClick = [this, mode] { setMode(mode); };
    }

    void setMode(LibraryMode mode)
    {
        mode_ = mode;
        audioLoopsTab_.setToggleState(mode_ == LibraryMode::AudioLoops, juce::dontSendNotification);
        midiLoopsTab_.setToggleState(mode_ == LibraryMode::MidiLoops, juce::dontSendNotification);
        instrumentsTab_.setToggleState(mode_ == LibraryMode::Instruments, juce::dontSendNotification);
        drumsTab_.setToggleState(mode_ == LibraryMode::Drums, juce::dontSendNotification);
        presetsTab_.setToggleState(mode_ == LibraryMode::Presets, juce::dontSendNotification);
        refreshResults();
    }

    static bool loopVisibleInMode(const bandforge::LoopAsset& loop, LibraryMode mode)
    {
        const bool drumLike = bandforge::isDrumTrackKind(loop.targetTrackKind)
            || containsIgnoreCase(loop.instrument, "drum")
            || containsIgnoreCase(loop.instrument, "808");

        switch (mode) {
        case LibraryMode::AudioLoops:
            return loop.kind == bandforge::LoopKind::Audio;
        case LibraryMode::MidiLoops:
            return loop.kind == bandforge::LoopKind::Midi;
        case LibraryMode::Drums:
            return drumLike;
        case LibraryMode::Instruments:
        case LibraryMode::Presets:
            return false;
        }
        return false;
    }

    static juce::String loopDetail(const bandforge::LoopAsset& loop)
    {
        return juce::String::fromUTF8(loop.instrument.c_str())
            + " | "
            + juce::String::fromUTF8(loop.genre.c_str())
            + " | "
            + juce::String::fromUTF8(loop.key.c_str())
            + " | "
            + juce::String(loop.bpm, 0)
            + " BPM | "
            + juce::String(loop.beats, 0)
            + " beats | licensed";
    }

    static juce::String presetDetail(const bandforge::Preset& preset)
    {
        return juce::String::fromUTF8(preset.instrumentType.c_str())
            + " | "
            + juce::String::fromUTF8(preset.category.c_str());
    }

    static std::string selectedFilter(const juce::ComboBox& box)
    {
        return box.getSelectedId() <= 1 ? std::string {} : box.getText().toStdString();
    }

    static bool tagsMatch(const std::vector<std::string>& tags, const std::string& text)
    {
        if (text.empty()) {
            return true;
        }
        return std::any_of(tags.begin(), tags.end(), [&](const std::string& tag) {
            return containsIgnoreCase(tag, text);
        });
    }

    bool bpmMatches(double bpm) const
    {
        switch (bpmFilter_.getSelectedId()) {
        case 2:
            return bpm < 100.0;
        case 3:
            return bpm >= 100.0 && bpm < 120.0;
        case 4:
            return bpm >= 120.0 && bpm < 130.0;
        case 5:
            return bpm >= 130.0;
        default:
            return true;
        }
    }

    void configureFilter(juce::ComboBox& box, const juce::String& allText)
    {
        box.addItem(allText, 1);
        box.setSelectedId(1, juce::dontSendNotification);
        box.setTextWhenNothingSelected(allText);
    }

    void populateFilters()
    {
        std::set<std::string> instruments;
        std::set<std::string> genres;
        std::set<std::string> keys;
        std::set<std::string> tags;

        for (const auto& loop : library_.loops) {
            instruments.insert(loop.instrument);
            genres.insert(loop.genre);
            keys.insert(loop.key);
            tags.insert(loop.tags.begin(), loop.tags.end());
        }
        for (const auto& preset : library_.presets) {
            instruments.insert(preset.category);
            tags.insert(preset.tags.begin(), preset.tags.end());
        }

        configureFilter(instrumentFilter_, "Instrument");
        configureFilter(genreFilter_, "Genre");
        configureFilter(keyFilter_, "Key");
        configureFilter(tagFilter_, "Tag");
        bpmFilter_.addItem("BPM", 1);
        bpmFilter_.addItem("<100", 2);
        bpmFilter_.addItem("100-119", 3);
        bpmFilter_.addItem("120-129", 4);
        bpmFilter_.addItem("130+", 5);
        bpmFilter_.setSelectedId(1, juce::dontSendNotification);
        bpmFilter_.setTextWhenNothingSelected("BPM");

        int itemId = 2;
        for (const auto& value : instruments) {
            instrumentFilter_.addItem(juce::String::fromUTF8(value.c_str()), itemId++);
        }
        itemId = 2;
        for (const auto& value : genres) {
            genreFilter_.addItem(juce::String::fromUTF8(value.c_str()), itemId++);
        }
        itemId = 2;
        for (const auto& value : keys) {
            keyFilter_.addItem(juce::String::fromUTF8(value.c_str()), itemId++);
        }
        itemId = 2;
        for (const auto& value : tags) {
            tagFilter_.addItem(juce::String::fromUTF8(value.c_str()), itemId++);
        }
    }

    int findLoopIndex(const std::string& id) const
    {
        for (std::size_t i = 0; i < library_.loops.size(); ++i) {
            if (library_.loops[i].id == id) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    int findPresetIndex(const std::string& id) const
    {
        for (std::size_t i = 0; i < library_.presets.size(); ++i) {
            if (library_.presets[i].id == id) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void refreshResults()
    {
        filtered_.clear();
        selectedRow_ = -1;

        const auto query = search_.getText().toStdString();
        const auto instrument = selectedFilter(instrumentFilter_);
        const auto genre = selectedFilter(genreFilter_);
        const auto key = selectedFilter(keyFilter_);
        const auto tag = selectedFilter(tagFilter_);
        if (mode_ == LibraryMode::AudioLoops || mode_ == LibraryMode::MidiLoops || mode_ == LibraryMode::Drums) {
            const auto results = library_.searchLoops({ query, instrument, genre, key });
            for (const auto& loop : results) {
                if (!loopVisibleInMode(loop, mode_)) {
                    continue;
                }
                if (!bpmMatches(loop.bpm) || !tagsMatch(loop.tags, tag)) {
                    continue;
                }
                const int index = findLoopIndex(loop.id);
                if (index < 0) {
                    continue;
                }
                filtered_.push_back({
                    RowKind::Loop,
                    index,
                    juce::String::fromUTF8(loop.name.c_str()),
                    loopDetail(loop),
                    loop.kind == bandforge::LoopKind::Midi ? "MIDI" : "Audio",
                    loop.kind == bandforge::LoopKind::Midi ? juce::Colour(0xff79d6ff) : juce::Colour(0xff75dda7),
                });
            }
        }

        if (mode_ == LibraryMode::Instruments || mode_ == LibraryMode::Drums || mode_ == LibraryMode::Presets) {
            const auto results = library_.searchPresets({ query, "", "", "" });
            for (const auto& preset : results) {
                if (mode_ == LibraryMode::Instruments && !presetIsInstrument(preset)) {
                    continue;
                }
                if (mode_ == LibraryMode::Drums && !presetIsDrumOrBeat(preset)) {
                    continue;
                }
                if (!instrument.empty() && !containsIgnoreCase(preset.category, instrument) && !containsIgnoreCase(preset.instrumentType, instrument)) {
                    continue;
                }
                if (!genre.empty() && !containsIgnoreCase(preset.category, genre)) {
                    continue;
                }
                if (!tagsMatch(preset.tags, tag)) {
                    continue;
                }
                const int index = findPresetIndex(preset.id);
                if (index < 0) {
                    continue;
                }
                filtered_.push_back({
                    RowKind::Preset,
                    index,
                    juce::String::fromUTF8(preset.name.c_str()),
                    presetDetail(preset),
                    presetIsEffectChain(preset) ? "FX" : (mode_ == LibraryMode::Drums ? "Beat" : "Preset"),
                    presetIsEffectChain(preset) ? juce::Colour(0xffff8fab)
                        : (mode_ == LibraryMode::Drums ? juce::Colour(0xffffbd6f) : juce::Colour(0xffc7a6ff)),
                });
            }
        }

        results_.updateContent();
        results_.repaint();
        insertButton_.setEnabled(false);
    }

    bandforge::TrackId chooseTargetTrack(bandforge::TrackKind preferred)
    {
        if (selection_.selectedTrackId != 0) {
            if (const auto* track = project_.findTrack(selection_.selectedTrackId); track != nullptr) {
                if (preferred == bandforge::TrackKind::Audio && track->kind == bandforge::TrackKind::Audio) {
                    return track->id;
                }
                if (preferred != bandforge::TrackKind::Audio && track->kind == preferred) {
                    return track->id;
                }
            }
        }

        for (const auto& track : project_.tracks) {
            if (preferred == bandforge::TrackKind::Audio && track.kind == bandforge::TrackKind::Audio) {
                return track.id;
            }
            if (preferred != bandforge::TrackKind::Audio && track.kind == preferred) {
                return track.id;
            }
        }

        auto& created = project_.addTrack(preferred, bandforge::displayName(preferred));
        return created.id;
    }

    static bandforge::EffectSlot makeEffectSlot(const std::string& fxType, bandforge::TrackId trackId, std::size_t index)
    {
        bandforge::EffectSlot slot;
        slot.id = fxType + "-" + std::to_string(trackId) + "-" + std::to_string(index);
        slot.type = fxType;
        if (fxType == "echo") {
            slot.name = "Echo";
            slot.parameters = { { "timeSec", 0.32 }, { "feedback", 0.35 }, { "mix", 0.35 } };
        } else if (fxType == "reverb") {
            slot.name = "Hall Reverb";
            slot.parameters = { { "size", 0.75 }, { "damp", 0.4 }, { "mix", 0.3 } };
        } else if (fxType == "distortion") {
            slot.name = "Drive";
            slot.parameters = { { "drive", 6.0 }, { "mix", 0.6 } };
        } else if (fxType == "telephone") {
            slot.name = "Telephone";
            slot.parameters = { { "mix", 1.0 } };
        } else if (fxType == "megaphone") {
            slot.name = "Megaphone";
            slot.parameters = { { "drive", 8.0 }, { "mix", 1.0 } };
        }
        return slot;
    }

    static bool presetHasTag(const bandforge::Preset& preset, const std::string& tag)
    {
        return std::any_of(preset.tags.begin(), preset.tags.end(), [&](const std::string& value) {
            return value == tag;
        });
    }

    void applyEffectChainPreset(const bandforge::Preset& preset)
    {
        bandforge::Track* track = project_.findTrack(selection_.selectedTrackId);
        if (track == nullptr) {
            const auto found = std::find_if(project_.tracks.begin(), project_.tracks.end(), [](const bandforge::Track& candidate) {
                return candidate.kind != bandforge::TrackKind::Master;
            });
            if (found != project_.tracks.end()) {
                track = project_.findTrack(found->id);
            }
        }
        if (track == nullptr) {
            track = &project_.addTrack(bandforge::TrackKind::Audio, "FX Target");
        }

        std::vector<std::string> fxTypes;
        for (const auto* fxType : { "echo", "reverb", "distortion", "telephone", "megaphone" }) {
            if (presetHasTag(preset, fxType)) {
                fxTypes.emplace_back(fxType);
            }
        }
        if (fxTypes.empty()) {
            fxTypes = { "reverb", "echo" };
        }

        for (const auto& fxType : fxTypes) {
            track->mixer.effects.push_back(makeEffectSlot(fxType, track->id, track->mixer.effects.size()));
        }
        selection_.selectedTrackId = track->id;
        selection_.selectedClipId = 0;
    }

    void insertSelection()
    {
        if (selectedRow_ < 0 || selectedRow_ >= static_cast<int>(filtered_.size())) {
            return;
        }

        history_.remember(project_);
        const auto& item = filtered_[static_cast<std::size_t>(selectedRow_)];
        const double beat = std::max(0.0, grid_.snap(transport_.positionBeat()));

        if (item.kind == RowKind::Loop) {
            const auto& loop = library_.loops[static_cast<std::size_t>(item.index)];
            if (loop.kind == bandforge::LoopKind::Audio) {
                const auto trackId = chooseTargetTrack(bandforge::TrackKind::Audio);
                selection_.selectedTrackId = trackId;
                selection_.selectedClipId = project_.addAudioClip(trackId,
                    loop.name,
                    loopMediaPath(loop),
                    beat,
                    std::max(0.25, loop.beats))
                                               .id;
            } else {
                const auto kind = bandforge::preferredTrackKindForLoop(loop);
                const auto trackId = chooseTargetTrack(kind);
                selection_.selectedTrackId = trackId;
                auto& clip = project_.addMidiClip(trackId, loop.name, beat, std::max(0.25, loop.beats));
                clip.midi = loop.midi.notes.empty() ? bandforge::defaultStarterClipForTrackKind(kind) : loop.midi;
                selection_.selectedClipId = clip.id;
            }
        } else {
            const auto& preset = library_.presets[static_cast<std::size_t>(item.index)];
            if (presetIsEffectChain(preset)) {
                applyEffectChainPreset(preset);
                repaint();
                if (auto* parent = getParentComponent()) {
                    parent->repaint();
                }
                return;
            }

            const auto kind = trackKindForInstrumentType(preset.instrumentType);
            const auto trackId = chooseTargetTrack(kind);
            selection_.selectedTrackId = trackId;
            auto& clip = project_.addMidiClip(trackId, preset.name + " Pattern", beat, 4.0);
            clip.midi = bandforge::defaultStarterClipForTrackKind(kind);
            if (auto* track = project_.findTrack(trackId)) {
                track->instrument.type = preset.instrumentType;
                track->instrument.presetName = preset.name;
            }
            selection_.selectedClipId = clip.id;
        }

        repaint();
        if (auto* parent = getParentComponent()) {
            parent->repaint();
        }
    }

    bandforge::SoundLibrary& library_;
    bandforge::Project& project_;
    bandforge::ProjectHistory& history_;
    bandforge::Transport& transport_;
    bandforge::GridSettings& grid_;
    bandforge_app::SelectionState& selection_;

    juce::TextButton audioLoopsTab_;
    juce::TextButton midiLoopsTab_;
    juce::TextButton instrumentsTab_;
    juce::TextButton drumsTab_;
    juce::TextButton presetsTab_;
    juce::TextEditor search_;
    juce::ComboBox instrumentFilter_;
    juce::ComboBox genreFilter_;
    juce::ComboBox keyFilter_;
    juce::ComboBox bpmFilter_;
    juce::ComboBox tagFilter_;
    juce::TextButton insertButton_;
    juce::ListBox results_;

    LibraryMode mode_ = LibraryMode::AudioLoops;
    int selectedRow_ = -1;
    std::vector<RowItem> filtered_;
};

class EditorPanelComponent final : public juce::Component {
public:
    EditorPanelComponent(bandforge::Project& project,
        bandforge::ProjectHistory& history,
        bandforge_app::SelectionState& selection,
        KeyboardCallbacks keyCallbacks = {})
        : project_(project)
        , history_(history)
        , selection_(selection)
        , keyCallbacks_(std::move(keyCallbacks))
    {
        tabs_.setTabBarDepth(32);
        tabs_.setColour(juce::TabbedComponent::backgroundColourId, juce::Colour(0xff161b22));
        tabs_.setColour(juce::TabbedComponent::outlineColourId, juce::Colour(0x00000000));
        addAndMakeVisible(tabs_);

        pianoRoll_ = std::make_unique<PianoRollView>(project_, history_, selection_);
        smartControls_ = std::make_unique<SmartControlsView>(project_, history_, selection_);
        musicalKeyboard_ = std::make_unique<MusicalKeyboardView>(keyCallbacks_);
        eqView_ = std::make_unique<EqView>(project_, history_, selection_);

        tabs_.addTab("Piano Roll",      juce::Colour(0xff161b22), pianoRoll_.get(),       false);
        tabs_.addTab("Smart Controls",  juce::Colour(0xff161b22), smartControls_.get(),   false);
        tabs_.addTab("EQ",              juce::Colour(0xff161b22), eqView_.get(),           false);
        tabs_.addTab("Musical Typing",  juce::Colour(0xff161b22), musicalKeyboard_.get(), false);
    }

    void resized() override
    {
        tabs_.setBounds(getLocalBounds().reduced(8));
    }

    void setAudioEngine(const bandforge::AudioEngine* engine)
    {
        if (eqView_) eqView_->setAudioEngine(engine);
    }

private:
    // ─── Musical Typing keyboard view ─────────────────────────────────────────
    class MusicalKeyboardView final : public juce::Component {
    public:
        explicit MusicalKeyboardView(const KeyboardCallbacks& cbs)
            : cbs_(cbs)
        {
            setWantsKeyboardFocus(false);
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xff161b22));

            auto area = getLocalBounds().reduced(14);
            g.setColour(juce::Colour(0xff202731));
            g.fillRoundedRectangle(area.toFloat(), 10.0f);

            // Header bar
            auto header = area.removeFromTop(42);
            g.setColour(juce::Colour(0xff1a2130));
            g.fillRoundedRectangle(header.toFloat(), 10.0f);

            // Octave shift buttons
            const int btnW = 28;
            const int btnH = 22;
            const int btnY = header.getCentreY() - btnH / 2;
            octDownBounds_ = juce::Rectangle<int>(header.getRight() - 72, btnY, btnW, btnH);
            octUpBounds_   = juce::Rectangle<int>(header.getRight() - 40, btnY, btnW, btnH);
            velDownBounds_ = juce::Rectangle<int>(header.getRight() - 190, btnY, btnW, btnH);
            velUpBounds_   = juce::Rectangle<int>(header.getRight() - 158, btnY, btnW, btnH);

            g.setColour(juce::Colour(0xffd7dde8));
            g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
            g.drawText("Musical Typing",
                header.withTrimmedRight(250).reduced(12, 0),
                juce::Justification::centredLeft);

            const int velocity = cbs_.getVelocity ? cbs_.getVelocity() : 100;
            const int octave = cbs_.getOctave ? cbs_.getOctave() : 4;

            g.setColour(juce::Colour(0xff90a4ba));
            g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
            g.drawText("V" + juce::String(velocity),
                juce::Rectangle<int>(header.getRight() - 250, btnY, 56, btnH),
                juce::Justification::centredRight);
            g.drawText("C" + juce::String(octave),
                juce::Rectangle<int>(header.getRight() - 128, btnY, 48, btnH),
                juce::Justification::centredRight);

            for (auto* b : { &velDownBounds_, &velUpBounds_, &octDownBounds_, &octUpBounds_ }) {
                g.setColour(juce::Colour(0xff2e3b4e));
                g.fillRoundedRectangle(b->toFloat(), 5.0f);
                g.setColour(juce::Colour(0xff5fb3ff));
                g.drawRoundedRectangle(b->toFloat(), 5.0f, 1.0f);
            }
            g.setColour(juce::Colour(0xffe8edf4));
            g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
            g.drawText("-", velDownBounds_, juce::Justification::centred);
            g.drawText("+", velUpBounds_,   juce::Justification::centred);
            g.drawText("<", octDownBounds_, juce::Justification::centred);
            g.drawText(">", octUpBounds_,   juce::Justification::centred);

            area.reduce(0, 8);
            drawKeyboard(g, area);
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            if (velDownBounds_.contains(e.getPosition()) && cbs_.shiftVelocity) {
                cbs_.shiftVelocity(-8);
                repaint();
                return;
            }
            if (velUpBounds_.contains(e.getPosition()) && cbs_.shiftVelocity) {
                cbs_.shiftVelocity(+8);
                repaint();
                return;
            }
            if (octDownBounds_.contains(e.getPosition()) && cbs_.shiftOctave) {
                cbs_.shiftOctave(-1);
                repaint();
                return;
            }
            if (octUpBounds_.contains(e.getPosition()) && cbs_.shiftOctave) {
                cbs_.shiftOctave(+1);
                repaint();
                return;
            }

            const int pitch = pitchAtPoint(e.getPosition());
            if (pitch >= 0 && cbs_.noteOn) {
                dragPitch_ = pitch;
                cbs_.noteOn(pitch, currentVelocity());
                repaint();
            }
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            const int pitch = pitchAtPoint(e.getPosition());
            if (pitch >= 0 && pitch != dragPitch_) {
                if (dragPitch_ >= 0 && cbs_.noteOff) {
                    cbs_.noteOff(dragPitch_);
                }
                dragPitch_ = pitch;
                if (cbs_.noteOn) {
                    cbs_.noteOn(pitch, currentVelocity());
                }
                repaint();
            }
        }

        void mouseUp(const juce::MouseEvent&) override
        {
            if (dragPitch_ >= 0 && cbs_.noteOff) {
                cbs_.noteOff(dragPitch_);
            }
            dragPitch_ = -1;
            repaint();
        }

    private:
        // Layout helpers: 2 octaves = 14 white keys, 10 black keys
        // White key indices in octave: 0=C,1=D,2=E,3=F,4=G,5=A,6=B
        // Black keys at positions (fractional white key x): 0.667, 1.667, 3.667, 4.667, 5.667
        static constexpr int kNumOctaves = 2;
        static constexpr int kWhitePerOctave = 7;
        static constexpr int kTotalWhite = kNumOctaves * kWhitePerOctave;

        // semitone offset within octave for each white key (C=0,D=2,E=4,F=5,G=7,A=9,B=11)
        static constexpr int kWhiteSemitone[7] = { 0, 2, 4, 5, 7, 9, 11 };
        // black keys: semitone offsets and their fractional positions relative to white key index
        struct BlackKeyDef { int semitone; float xFrac; }; // xFrac in white-key units
        static constexpr BlackKeyDef kBlack[5] = {
            { 1, 0.667f }, { 3, 1.667f }, { 6, 3.667f }, { 8, 4.667f }, { 10, 5.667f }
        };

        int currentVelocity() const
        {
            return std::clamp(cbs_.getVelocity ? cbs_.getVelocity() : 100, 1, 127);
        }

        // QWERTY labels for each pitch relative to octave base
        static const char* qwertyLabel(int semitone)
        {
            switch (semitone) {
            case  0: return "A";   // C
            case  1: return "W";   // C#
            case  2: return "S";   // D
            case  3: return "E";   // D#
            case  4: return "D";   // E
            case  5: return "F";   // F
            case  6: return "T";   // F#
            case  7: return "G";   // G
            case  8: return "Y";   // G#
            case  9: return "H";   // A
            case 10: return "U";   // A#
            case 11: return "J";   // B
            case 12: return "K";   // C+1
            case 13: return "O";   // C#+1
            case 14: return "L";   // D+1
            default: return "";
            }
        }

        void drawKeyboard(juce::Graphics& g, const juce::Rectangle<int>& area) const
        {
            const float wkW = static_cast<float>(area.getWidth()) / kTotalWhite;
            const float wkH = static_cast<float>(area.getHeight());
            const float bkW = wkW * 0.62f;
            const float bkH = wkH * 0.60f;
            const int octave = cbs_.getOctave ? cbs_.getOctave() : 4;
            const int baseNote = (octave + 1) * 12; // Scientific pitch: C4 is MIDI note 60

            // Draw white keys
            for (int oct = 0; oct < kNumOctaves; ++oct) {
                for (int w = 0; w < kWhitePerOctave; ++w) {
                    const int idx = oct * kWhitePerOctave + w;
                    const float x = area.getX() + idx * wkW;
                    const int pitch = baseNote + oct * 12 + kWhiteSemitone[w];
                    const int semitoneInLayout = oct * 12 + kWhiteSemitone[w];
                    const bool active = pitch == dragPitch_ || (cbs_.isPitchActive && cbs_.isPitchActive(pitch));

                    juce::Colour fill = active ? juce::Colour(0xff5fb3ff)
                                               : juce::Colour(0xfff5f5f0);
                    g.setColour(fill);
                    g.fillRoundedRectangle(x + 1.5f, static_cast<float>(area.getY()) + 1.0f,
                        wkW - 3.0f, wkH - 3.0f, 4.0f);
                    g.setColour(juce::Colour(0xff8899aa));
                    g.drawRoundedRectangle(x + 1.5f, static_cast<float>(area.getY()) + 1.0f,
                        wkW - 3.0f, wkH - 3.0f, 4.0f, 1.0f);

                    // QWERTY label
                    const char* label = qwertyLabel(semitoneInLayout);
                    if (label[0] != '\0') {
                        g.setColour(active ? juce::Colours::white : juce::Colour(0xff4a5566));
                        g.setFont(juce::FontOptions(wkW > 24 ? 12.0f : 9.0f, juce::Font::bold));
                        g.drawText(label,
                            static_cast<int>(x), area.getBottom() - 22,
                            static_cast<int>(wkW), 18,
                            juce::Justification::centred);
                    }
                }
            }

            // Draw black keys (on top)
            for (int oct = 0; oct < kNumOctaves; ++oct) {
                for (const auto& bk : kBlack) {
                    const float x = area.getX() + (oct * kWhitePerOctave + bk.xFrac) * wkW - bkW * 0.5f;
                    const int pitch = baseNote + oct * 12 + bk.semitone;
                    const int semitoneInLayout = oct * 12 + bk.semitone;
                    const bool active = pitch == dragPitch_ || (cbs_.isPitchActive && cbs_.isPitchActive(pitch));

                    juce::Colour fill = active ? juce::Colour(0xff3a9bff)
                                               : juce::Colour(0xff1e2530);
                    g.setColour(fill);
                    g.fillRoundedRectangle(x, static_cast<float>(area.getY()), bkW, bkH, 3.0f);
                    g.setColour(juce::Colour(0xff0a0f14));
                    g.drawRoundedRectangle(x, static_cast<float>(area.getY()), bkW, bkH, 3.0f, 0.8f);

                    // QWERTY label on black key
                    const char* label = qwertyLabel(semitoneInLayout);
                    if (label[0] != '\0') {
                        g.setColour(active ? juce::Colours::white : juce::Colour(0xff7090a8));
                        g.setFont(juce::FontOptions(wkW > 24 ? 10.0f : 8.0f));
                        g.drawText(label,
                            static_cast<int>(x), static_cast<int>(area.getY()) + static_cast<int>(bkH) - 18,
                            static_cast<int>(bkW), 15,
                            juce::Justification::centred);
                    }
                }
            }
        }

        int pitchAtPoint(const juce::Point<int>& pt) const
        {
            const auto area = getLocalBounds().reduced(14).reduced(0, 8).withTrimmedTop(42 + 8);
            if (!area.contains(pt)) {
                return -1;
            }
            const float wkW = static_cast<float>(area.getWidth()) / kTotalWhite;
            const float bkW = wkW * 0.62f;
            const float bkH = static_cast<float>(area.getHeight()) * 0.60f;
            const int octave = cbs_.getOctave ? cbs_.getOctave() : 4;
            const int baseNote = (octave + 1) * 12;

            // Check black keys first (they are on top)
            for (int oct = 0; oct < kNumOctaves; ++oct) {
                for (const auto& bk : kBlack) {
                    const float x = area.getX() + (oct * kWhitePerOctave + bk.xFrac) * wkW - bkW * 0.5f;
                    const float top = static_cast<float>(area.getY());
                    juce::Rectangle<float> r { x, top, bkW, bkH };
                    if (r.contains(pt.toFloat())) {
                        return baseNote + oct * 12 + bk.semitone;
                    }
                }
            }

            // Then white keys
            for (int oct = 0; oct < kNumOctaves; ++oct) {
                for (int w = 0; w < kWhitePerOctave; ++w) {
                    const int idx = oct * kWhitePerOctave + w;
                    const float x = area.getX() + idx * wkW;
                    juce::Rectangle<float> r {
                        x + 1.5f, static_cast<float>(area.getY()),
                        wkW - 3.0f, static_cast<float>(area.getHeight())
                    };
                    if (r.contains(pt.toFloat())) {
                        return baseNote + oct * 12 + kWhiteSemitone[w];
                    }
                }
            }
            return -1;
        }

        const KeyboardCallbacks& cbs_;
        int dragPitch_ = -1;
        mutable juce::Rectangle<int> velDownBounds_;
        mutable juce::Rectangle<int> velUpBounds_;
        mutable juce::Rectangle<int> octDownBounds_;
        mutable juce::Rectangle<int> octUpBounds_;
    };
    class PianoRollView final : public juce::Component {
    public:
        static constexpr int kPitchMin = 36;
        static constexpr int kPitchMax = 84;
        static constexpr int kPitchRange = kPitchMax - kPitchMin;
        static constexpr double kGridBeats = 16.0;
        static constexpr double kDefaultNoteDuration = 0.25;
        static constexpr int kVelocityLaneH = 56;

        PianoRollView(bandforge::Project& project, bandforge::ProjectHistory& history, bandforge_app::SelectionState& selection)
            : project_(project)
            , history_(history)
            , selection_(selection)
        {
            setMouseCursor(juce::MouseCursor::CrosshairCursor);
            setWantsKeyboardFocus(true);

            quantizeButton_.setButtonText("Q");
            quantizeButton_.setTooltip("Quantize notes");
            quantizeButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2e3b4e));
            quantizeButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xff8ab4d8));
            quantizeButton_.onClick = [this] { showQuantizeMenu(); };
            addAndMakeVisible(quantizeButton_);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced(14).reduced(12);
            quantizeButton_.setBounds(area.getRight() - 62, area.getY() + 4, 28, 22);
        }

        void paint(juce::Graphics& graphics) override
        {
            graphics.fillAll(juce::Colour(0xff161b22));
            auto area = getLocalBounds().reduced(14);
            graphics.setColour(juce::Colour(0xff202731));
            graphics.fillRoundedRectangle(area.toFloat(), 8.0f);
            graphics.setColour(juce::Colour(0xffd7dde8));
            graphics.setFont(juce::FontOptions(14.0f, juce::Font::bold));
            graphics.drawText("Piano Roll", area.reduced(12).withHeight(24), juce::Justification::centredLeft);

            // Hint for transpose shortcuts
            graphics.setColour(juce::Colour(0xff5a7090));
            graphics.setFont(juce::FontOptions(10.0f));
            graphics.drawText(juce::CharPointer_UTF8("\xe2\x87\xa7\xe2\x86\x91\xe2\x86\x93 transpose"), area.reduced(12).withHeight(24).withX(area.getX() + 100), juce::Justification::centredLeft);

            const auto* clip = selectedMidiClip();
            const double clipBeats = clip ? std::max(1.0, clip->lengthBeats) : kGridBeats;

            auto grid = noteGridArea();
            auto velArea = velocityArea();

            // Row backgrounds (black/white key alternation for chromatic)
            static constexpr bool kBlackKey[12] = { false, true, false, true, false, false, true, false, true, false, true, false };
            const int rowH = std::max(1, grid.getHeight() / kPitchRange);
            for (int i = 0; i < kPitchRange; ++i) {
                const int semitone = (kPitchMax - 1 - i) % 12;
                graphics.setColour(kBlackKey[semitone] ? juce::Colour(0xff1e2530) : juce::Colour(0xff28313d));
                graphics.fillRect(grid.getX(), grid.getY() + i * rowH, grid.getWidth(), rowH);
            }

            // Beat grid lines
            const int gridBeats = static_cast<int>(std::ceil(clipBeats));
            for (int beat = 0; beat <= gridBeats; ++beat) {
                const int x = grid.getX() + static_cast<int>((static_cast<double>(beat) / clipBeats) * grid.getWidth());
                graphics.setColour(beat % 4 == 0 ? juce::Colour(0xff4a5666) : juce::Colour(0xff343d4a));
                graphics.drawVerticalLine(x, static_cast<float>(grid.getY()), static_cast<float>(grid.getBottom()));
                graphics.drawVerticalLine(x, static_cast<float>(velArea.getY()), static_cast<float>(velArea.getBottom()));
            }

            // Velocity lane background
            graphics.setColour(juce::Colour(0xff1a2028));
            graphics.fillRect(velArea);
            graphics.setColour(juce::Colour(0xff2e3b4e));
            graphics.drawHorizontalLine(velArea.getY(), static_cast<float>(velArea.getX()), static_cast<float>(velArea.getRight()));
            graphics.setColour(juce::Colour(0xff5a7090));
            graphics.setFont(juce::FontOptions(10.0f));
            graphics.drawText("Velocity", velArea.withWidth(50).translated(4, 2), juce::Justification::centredLeft);

            if (clip == nullptr) {
                graphics.setColour(juce::Colour(0xff91a0b4));
                graphics.setFont(juce::FontOptions(12.0f));
                graphics.drawText("Select a MIDI region — click to add notes, right-click to delete.", grid, juce::Justification::centred);
                return;
            }

            for (const auto& note : clip->midi.notes) {
                const int x = grid.getX() + static_cast<int>((note.startBeat / clipBeats) * grid.getWidth());
                const int w = std::max(4, static_cast<int>((note.durationBeats / clipBeats) * grid.getWidth()) - 1);
                const int pitchRow = kPitchMax - 1 - note.pitch;
                const int y = grid.getY() + pitchRow * rowH;
                if (y < grid.getY() || y >= grid.getBottom()) continue;

                const bool sel = (note.pitch == lastClickedPitch_ && std::abs(note.startBeat - lastClickedBeat_) < 0.01);
                auto noteRect = juce::Rectangle<int>(x, y, w, std::max(3, rowH - 1));
                graphics.setColour(sel ? juce::Colour(0xffaef8ad) : juce::Colour(0xff76d275));
                graphics.fillRoundedRectangle(noteRect.toFloat(), 3.0f);
                graphics.setColour(juce::Colour(0xff3a8c39));
                graphics.drawRoundedRectangle(noteRect.toFloat(), 3.0f, 0.5f);

                // Velocity bar
                const float velNorm = static_cast<float>(note.velocity) / 127.0f;
                const int barH = std::max(2, static_cast<int>(velNorm * static_cast<float>(velArea.getHeight() - 6)));
                const int barX = x;
                const int barW = std::max(3, w - 1);
                const juce::Colour velColour = sel ? juce::Colour(0xffaef8ad)
                    : (velNorm > 0.75f ? juce::Colour(0xff5fbfff) : juce::Colour(0xff4a90c0));
                graphics.setColour(velColour.withAlpha(0.7f));
                graphics.fillRoundedRectangle(static_cast<float>(barX),
                    static_cast<float>(velArea.getBottom() - barH - 3),
                    static_cast<float>(barW), static_cast<float>(barH), 2.0f);
            }
        }

        void mouseDown(const juce::MouseEvent& event) override
        {
            auto* clip = selectedMidiClip();
            if (clip == nullptr) return;

            const double clipBeats = std::max(1.0, clip->lengthBeats);

            // Velocity lane: dragging velocity
            if (velocityArea().contains(event.getPosition()) && !event.mods.isRightButtonDown()) {
                editVelocityAt(event, *clip, clipBeats);
                return;
            }

            const auto grid = noteGridArea();
            if (!grid.contains(event.getPosition())) return;

            const double beatClicked = ((event.x - grid.getX()) / static_cast<double>(grid.getWidth())) * clipBeats;
            const int rowH = std::max(1, grid.getHeight() / kPitchRange);
            const int pitchRow = (event.y - grid.getY()) / rowH;
            const int pitch = std::clamp(kPitchMax - 1 - pitchRow, kPitchMin, kPitchMax - 1);
            const double snapBeats = 0.25;
            const double snappedBeat = std::floor(beatClicked / snapBeats) * snapBeats;

            history_.remember(project_);

            if (event.mods.isRightButtonDown()) {
                auto& notes = clip->midi.notes;
                notes.erase(std::remove_if(notes.begin(), notes.end(), [&](const bandforge::MidiNote& n) {
                    return n.pitch == pitch && n.startBeat <= snappedBeat && snappedBeat < n.startBeat + n.durationBeats;
                }), notes.end());
                lastClickedPitch_ = -1;
                lastClickedBeat_ = -1.0;
            } else {
                auto& notes = clip->midi.notes;
                const auto it = std::find_if(notes.begin(), notes.end(), [&](const bandforge::MidiNote& n) {
                    return n.pitch == pitch && n.startBeat <= snappedBeat && snappedBeat < n.startBeat + n.durationBeats;
                });
                if (it != notes.end()) {
                    lastClickedPitch_ = -1;
                    lastClickedBeat_ = -1.0;
                    notes.erase(it);
                } else {
                    bandforge::MidiNote note;
                    note.pitch = pitch;
                    note.startBeat = snappedBeat;
                    note.durationBeats = kDefaultNoteDuration;
                    note.velocity = 100;
                    note.channel = 1;
                    notes.push_back(note);
                    lastClickedPitch_ = pitch;
                    lastClickedBeat_ = snappedBeat;
                }
            }
            repaint();
        }

        void mouseDrag(const juce::MouseEvent& event) override
        {
            auto* clip = selectedMidiClip();
            if (clip == nullptr || !velocityDragging_) return;
            editVelocityAt(event, *clip, std::max(1.0, clip->lengthBeats));
        }

        void mouseUp(const juce::MouseEvent&) override
        {
            velocityDragging_ = false;
        }

        bool keyPressed(const juce::KeyPress& key) override
        {
            auto* clip = selectedMidiClip();
            if (clip == nullptr) return false;

            const bool shift = key.getModifiers().isShiftDown();
            const bool ctrl  = key.getModifiers().isCtrlDown();
            if (!shift) return false;

            const int semitones = ctrl ? 12 : 1;
            int delta = 0;
            if (key.getKeyCode() == juce::KeyPress::upKey)   delta = +semitones;
            if (key.getKeyCode() == juce::KeyPress::downKey) delta = -semitones;
            if (delta == 0) return false;

            history_.remember(project_);
            for (auto& note : clip->midi.notes) {
                note.pitch = std::clamp(note.pitch + delta, 0, 127);
            }
            if (lastClickedPitch_ >= 0) lastClickedPitch_ = std::clamp(lastClickedPitch_ + delta, 0, 127);
            repaint();
            return true;
        }

    private:
        juce::Rectangle<int> noteGridArea() const
        {
            auto inner = getLocalBounds().reduced(14).reduced(12).withTrimmedTop(34);
            return inner.withTrimmedBottom(kVelocityLaneH + 8);
        }

        juce::Rectangle<int> velocityArea() const
        {
            auto inner = getLocalBounds().reduced(14).reduced(12).withTrimmedTop(34);
            return inner.withTop(inner.getBottom() - kVelocityLaneH);
        }

        void editVelocityAt(const juce::MouseEvent& event, bandforge::Clip& clip, double clipBeats)
        {
            const auto velArea = velocityArea();
            const double beatClicked = ((event.x - velArea.getX()) / static_cast<double>(velArea.getWidth())) * clipBeats;

            // Find note closest in x to click
            bandforge::MidiNote* target = nullptr;
            double bestDist = 0.5; // beats
            for (auto& note : clip.midi.notes) {
                const double dist = std::abs(note.startBeat - beatClicked);
                if (dist < bestDist) { bestDist = dist; target = &note; }
            }
            if (target == nullptr) return;

            if (!velocityDragging_) {
                history_.remember(project_);
                velocityDragging_ = true;
            }

            const float velNorm = 1.0f - std::clamp(
                static_cast<float>(event.y - velArea.getY()) / static_cast<float>(velArea.getHeight()), 0.0f, 1.0f);
            target->velocity = std::clamp(static_cast<int>(velNorm * 127.0f + 0.5f), 1, 127);
            repaint();
        }

        void showQuantizeMenu()
        {
            juce::PopupMenu menu;
            menu.addItem(1, "1/4 note (0.25 beats)");
            menu.addItem(2, "1/8 note (0.125 beats)");
            menu.addItem(3, "1/16 note (0.0625 beats)");
            menu.addItem(4, "1/32 note (0.03125 beats)");
            menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(quantizeButton_),
                [this](int result) {
                    if (result <= 0) return;
                    const double grid[] = { 0.25, 0.125, 0.0625, 0.03125 };
                    auto* clip = selectedMidiClip();
                    if (clip == nullptr) return;
                    history_.remember(project_);
                    bandforge::TimelineEditor editor(project_);
                    editor.quantizeMidiClip(selection_.selectedTrackId, selection_.selectedClipId,
                        grid[static_cast<std::size_t>(result - 1)], 1.0);
                    repaint();
                });
        }

        bandforge::Clip* selectedMidiClip()
        {
            if (selection_.selectedTrackId == 0 || selection_.selectedClipId == 0) return nullptr;
            auto* clip = project_.findClip(selection_.selectedTrackId, selection_.selectedClipId);
            if (clip == nullptr || clip->kind != bandforge::ClipKind::Midi) return nullptr;
            return clip;
        }

        const bandforge::Clip* selectedMidiClip() const
        {
            if (selection_.selectedTrackId == 0 || selection_.selectedClipId == 0) return nullptr;
            const auto* clip = project_.findClip(selection_.selectedTrackId, selection_.selectedClipId);
            if (clip == nullptr || clip->kind != bandforge::ClipKind::Midi) return nullptr;
            return clip;
        }

        bandforge::Project& project_;
        bandforge::ProjectHistory& history_;
        bandforge_app::SelectionState& selection_;
        juce::TextButton quantizeButton_;
        int lastClickedPitch_ = -1;
        double lastClickedBeat_ = -1.0;
        bool velocityDragging_ = false;
    };

    class SmartControlsView final : public juce::Component, private juce::Timer {
    public:
        SmartControlsView(bandforge::Project& project, bandforge::ProjectHistory& history, bandforge_app::SelectionState& selection)
            : project_(project)
            , history_(history)
            , selection_(selection)
        {
            for (std::size_t i = 0; i < slots_.size(); ++i) {
                configureSlot(static_cast<int>(i));
            }

            startTimerHz(15);
        }

        void resized() override
        {
            auto bounds = getLocalBounds().reduced(14);
            auto card = bounds.reduced(0, 0);
            auto top = card.removeFromTop(28);
            juce::ignoreUnused(top);

            for (auto& slot : slots_) {
                auto line = card.removeFromTop(36);
                slot.label.setBounds(line.removeFromLeft(112));
                slot.slider.setBounds(line.reduced(10, 6));
                card.removeFromTop(6);
            }
        }

        void paint(juce::Graphics& graphics) override
        {
            graphics.fillAll(juce::Colour(0xff161b22));
            auto area = getLocalBounds().reduced(14);
            graphics.setColour(juce::Colour(0xff202731));
            graphics.fillRoundedRectangle(area.toFloat(), 8.0f);

            auto header = area.reduced(12).withHeight(28);
            // Draw instrument icon on left of header
            const auto* track = const_cast<SmartControlsView*>(this)->selectedTrack();
            if (track != nullptr) {
                const auto kind = track->kind;
                const auto accent = colourForTrackKind(kind);
                const juce::Rectangle<float> iconBox(
                    static_cast<float>(header.getX()),
                    static_cast<float>(header.getY() + 4),
                    20.0f, 20.0f);
                graphics.setColour(accent.withAlpha(0.2f));
                graphics.fillRoundedRectangle(iconBox, 4.0f);
                drawTrackKindIcon(graphics, kind, iconBox.reduced(3.0f));

                graphics.setColour(juce::Colour(0xfff4f7fb));
                graphics.setFont(juce::FontOptions(13.0f, juce::Font::bold));
                graphics.drawText(juce::String::fromUTF8(track->name.c_str()),
                    header.withTrimmedLeft(26).withHeight(16), juce::Justification::centredLeft);

                graphics.setColour(juce::Colour(0xff7a8fa6));
                graphics.setFont(juce::FontOptions(10.0f));
                graphics.drawText(juce::String::fromUTF8(bandforge::displayName(kind).c_str()),
                    header.withTrimmedLeft(26).translated(0, 14).withHeight(12), juce::Justification::centredLeft);
            } else {
                graphics.setColour(juce::Colour(0xff5a6a7e));
                graphics.setFont(juce::FontOptions(13.0f, juce::Font::bold));
                graphics.drawText("Smart Controls", header.withHeight(24), juce::Justification::centredLeft);
            }
        }

    private:
        struct ControlSlot {
            juce::Slider slider;
            juce::Label label;
        };

        void timerCallback() override
        {
            syncFromSelection();
        }

        void configureSlot(int index)
        {
            auto& slot = slots_[static_cast<std::size_t>(index)];
            auto& slider = slot.slider;
            auto& label = slot.label;

            slider.setRange(0.0, 1.0, 0.001);
            slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 22);
            slider.setColour(juce::Slider::trackColourId, juce::Colour(0xff5fb3ff));
            slider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff303a47));
            addAndMakeVisible(slider);

            slider.onDragStart = [this] { beginControlGesture(); };
            slider.onDragEnd = [this] { endControlGesture(); };
            slider.onValueChange = [this, index] {
                if (suppress_) {
                    return;
                }

                const bool oneShot = !gestureActive_;
                if (oneShot) {
                    rememberControlGesture();
                }
                writeParam(index);
                if (oneShot) {
                    gestureRemembered_ = false;
                }
            };

            label.setText({}, juce::dontSendNotification);
            label.setColour(juce::Label::textColourId, juce::Colour(0xff9ba8b9));
            label.setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(label);
        }

        bandforge::Track* selectedTrack()
        {
            if (selection_.selectedTrackId == 0) {
                return nullptr;
            }
            return project_.findTrack(selection_.selectedTrackId);
        }

        void syncFromSelection()
        {
            auto* track = selectedTrack();
            if (track == nullptr) {
                setEnabled(false);
                return;
            }
            setEnabled(true);

            juce::ScopedValueSetter<bool> guard(suppress_, true);
            applySpecsForKind(track->kind);
            for (std::size_t i = 0; i < slots_.size(); ++i) {
                slots_[i].slider.setValue(readParam(*track, specs_[i]), juce::dontSendNotification);
            }
        }

        void applySpecsForKind(bandforge::TrackKind kind)
        {
            if (specsReady_ && currentKind_ == kind) {
                return;
            }

            specs_ = smartControlSpecsForKind(kind);
            currentKind_ = kind;
            specsReady_ = true;
            const auto accent = colourForTrackKind(kind);

            for (std::size_t i = 0; i < slots_.size(); ++i) {
                const auto& spec = specs_[i];
                auto& slot = slots_[i];
                const double interval = (spec.max - spec.min) > 4.0 ? 0.01 : 0.001;
                slot.label.setText(spec.label, juce::dontSendNotification);
                slot.slider.setRange(spec.min, spec.max, interval);
                slot.slider.setNumDecimalPlacesToDisplay((spec.max - spec.min) > 4.0 ? 1 : 2);
                slot.slider.setColour(juce::Slider::trackColourId, accent);
            }
        }

        double readParam(const bandforge::Track& track, const SmartControlSpec& spec) const
        {
            switch (spec.target) {
            case SmartControlTarget::MixerVolume:
                return std::clamp(track.mixer.volumeDb, spec.min, spec.max);
            case SmartControlTarget::MixerPan:
                return std::clamp(track.mixer.pan, spec.min, spec.max);
            case SmartControlTarget::ReverbMix:
                return reverbMixValue(track, spec.defaultValue);
            case SmartControlTarget::InstrumentParam: {
                const auto found = track.instrument.parameters.find(spec.paramKey);
                return found != track.instrument.parameters.end()
                    ? std::clamp(found->second, spec.min, spec.max)
                    : spec.defaultValue;
            }
            }
            return spec.defaultValue;
        }

        double reverbMixValue(const bandforge::Track& track, double fallback) const
        {
            for (const auto& fx : track.mixer.effects) {
                if (fx.type == "reverb" || fx.name.find("Reverb") != std::string::npos) {
                    const auto found = fx.parameters.find("mix");
                    if (found != fx.parameters.end()) {
                        return std::clamp(found->second, 0.0, 1.0);
                    }
                }
            }
            return fallback;
        }

        void beginControlGesture()
        {
            gestureActive_ = true;
            rememberControlGesture();
        }

        void rememberControlGesture()
        {
            if (!gestureRemembered_) {
                history_.remember(project_);
                gestureRemembered_ = true;
            }
        }

        void endControlGesture()
        {
            gestureActive_ = false;
            gestureRemembered_ = false;
        }

        void writeParam(int index)
        {
            auto* track = selectedTrack();
            if (track == nullptr) {
                return;
            }

            const auto& spec = specs_[static_cast<std::size_t>(index)];
            const double value = std::clamp(slots_[static_cast<std::size_t>(index)].slider.getValue(), spec.min, spec.max);

            switch (spec.target) {
            case SmartControlTarget::MixerVolume:
                track->mixer.volumeDb = value;
                break;
            case SmartControlTarget::MixerPan:
                track->mixer.pan = std::clamp(value, -1.0, 1.0);
                break;
            case SmartControlTarget::InstrumentParam:
                track->instrument.parameters[spec.paramKey] = value;
                break;
            case SmartControlTarget::ReverbMix:
                setReverbMix(*track, value);
                break;
            }
        }

        void setReverbMix(bandforge::Track& track, double value)
        {
            for (auto& fx : track.mixer.effects) {
                if (fx.type == "reverb" || fx.name.find("Reverb") != std::string::npos) {
                    fx.parameters["mix"] = std::clamp(value, 0.0, 1.0);
                    return;
                }
            }

            bandforge::EffectSlot slot;
            slot.id = "fx-smart-reverb-" + std::to_string(track.id);
            slot.type = "reverb";
            slot.name = "Room Reverb";
            slot.parameters["mix"] = std::clamp(value, 0.0, 1.0);
            track.mixer.effects.push_back(std::move(slot));
        }

        bandforge::Project& project_;
        bandforge::ProjectHistory& history_;
        bandforge_app::SelectionState& selection_;
        std::array<ControlSlot, 4> slots_;
        SmartControlSpecs specs_ = smartControlSpecsForKind(bandforge::TrackKind::Audio);
        bandforge::TrackKind currentKind_ = bandforge::TrackKind::Master;
        bool suppress_ = false;
        bool specsReady_ = false;
        bool gestureActive_ = false;
        bool gestureRemembered_ = false;
    };

    // ─── Parametric EQ view ──────────────────────────────────────────────────
    class EqView final : public juce::Component, private juce::Timer {
    public:
        EqView(bandforge::Project& project, bandforge::ProjectHistory& history,
               bandforge_app::SelectionState& selection)
            : project_(project), history_(history), selection_(selection)
        {
            for (int i = 0; i < kFftSize; ++i) {
                window_[static_cast<std::size_t>(i)] = 0.5f * (1.0f - std::cos(
                    2.0f * juce::MathConstants<float>::pi * static_cast<float>(i)
                    / static_cast<float>(kFftSize - 1)));
            }
            spectrumDb_.fill(-90.0f);
            initBands();
            startTimerHz(30);
        }

        void setAudioEngine(const bandforge::AudioEngine* engine) { audioEngine_ = engine; }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xff0d1218));
            const auto area = getLocalBounds().reduced(14);
            g.setColour(juce::Colour(0xff141a23));
            g.fillRoundedRectangle(area.toFloat(), 8.0f);

            auto* track = selectedTrack();
            if (track == nullptr) {
                g.setColour(juce::Colour(0xff4a5a70));
                g.setFont(juce::FontOptions(13.0f));
                g.drawText("Select a track to edit EQ", area, juce::Justification::centred);
                return;
            }

            auto work = area.reduced(10);
            drawHeader(g, work.removeFromTop(kHeaderH), *track);
            work.removeFromTop(6);
            drawToggleRow(g, work.removeFromTop(kToggleH));
            work.removeFromTop(6);
            drawGrid(g, work);
            drawSpectrum(g, work);
            drawCurve(g, work);
            drawHandles(g, work);
        }

        void resized() override { repaint(); }

        void mouseDown(const juce::MouseEvent& e) override
        {
            auto* track = selectedTrack();
            if (track == nullptr) return;

            for (int i = 0; i < kBands; ++i) {
                if (toggleCellBoundsFor(i).contains(e.position.toInt())) {
                    history_.remember(project_);
                    bands_[static_cast<std::size_t>(i)].active = !bands_[static_cast<std::size_t>(i)].active;
                    writeBandsToTrack(*track);
                    repaint();
                    return;
                }
            }

            const auto ca = getCurveArea();
            int idx = -1;
            float best = 18.0f;
            for (int i = 0; i < kBands; ++i) {
                if (!bands_[static_cast<std::size_t>(i)].active) continue;
                const float d = e.position.getDistanceFrom(handlePos(i, ca));
                if (d < best) { best = d; idx = i; }
            }
            dragBand_ = idx;
            if (dragBand_ >= 0) history_.remember(project_);
            repaint();
        }

        void mouseMove(const juce::MouseEvent& e) override
        {
            if (selectedTrack() == nullptr) return;
            int newToggle = -1;
            for (int i = 0; i < kBands; ++i) {
                if (toggleCellBoundsFor(i).contains(e.position.toInt())) { newToggle = i; break; }
            }
            bool needRepaint = false;
            if (newToggle != toggleHover_) { toggleHover_ = newToggle; needRepaint = true; }

            const auto ca = getCurveArea();
            int idx = -1;
            float best = 18.0f;
            for (int i = 0; i < kBands; ++i) {
                if (!bands_[static_cast<std::size_t>(i)].active) continue;
                const float d = e.position.getDistanceFrom(handlePos(i, ca));
                if (d < best) { best = d; idx = i; }
            }
            if (idx != hoverBand_) { hoverBand_ = idx; needRepaint = true; }
            if (needRepaint) repaint();
        }

        void mouseExit(const juce::MouseEvent&) override
        {
            bool needRepaint = false;
            if (hoverBand_ != -1)   { hoverBand_   = -1; needRepaint = true; }
            if (toggleHover_ != -1) { toggleHover_ = -1; needRepaint = true; }
            if (needRepaint) repaint();
        }

        void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override
        {
            auto* track = selectedTrack();
            if (track == nullptr) return;
            const auto ca = getCurveArea();
            int idx = -1;
            float best = 32.0f;
            for (int i = 0; i < kBands; ++i) {
                if (!bands_[static_cast<std::size_t>(i)].active) continue;
                const float d = e.position.getDistanceFrom(handlePos(i, ca));
                if (d < best) { best = d; idx = i; }
            }
            if (idx < 0) return;
            const auto now = juce::Time::getMillisecondCounter();
            if (now - lastWheelMs_ > 500) history_.remember(project_);
            lastWheelMs_ = now;
            auto& b = bands_[static_cast<std::size_t>(idx)];
            b.q = std::clamp(b.q * std::pow(2.0, wheel.deltaY * 0.5), 0.1, 12.0);
            writeBandsToTrack(*track);
            repaint();
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            if (dragBand_ < 0) return;
            auto* track = selectedTrack();
            if (track == nullptr) return;
            const auto ca = getCurveArea();
            auto& b = bands_[static_cast<std::size_t>(dragBand_)];
            const double nx = std::clamp((e.position.x - ca.getX()) / static_cast<double>(ca.getWidth()), 0.0, 1.0);
            b.freq = std::clamp(20.0 * std::pow(1000.0, nx), 20.0, 20000.0);
            const int type = kBandTypes[dragBand_];
            if (type != kTypeHPF && type != kTypeLPF) {
                const double ny = std::clamp((e.position.y - ca.getY()) / static_cast<double>(ca.getHeight()), 0.0, 1.0);
                b.gainDb = std::clamp(24.0 * (0.5 - ny), -24.0, 24.0);
            }
            writeBandsToTrack(*track);
            repaint();
        }

        void mouseUp(const juce::MouseEvent&) override { dragBand_ = -1; repaint(); }

        void mouseDoubleClick(const juce::MouseEvent& e) override
        {
            auto* track = selectedTrack();
            if (track == nullptr) return;
            const auto ca = getCurveArea();
            for (int i = 0; i < kBands; ++i) {
                if (e.position.getDistanceFrom(handlePos(i, ca)) < 14.0f) {
                    history_.remember(project_);
                    resetBand(i);
                    writeBandsToTrack(*track);
                    repaint();
                    return;
                }
            }
        }

    private:
        static constexpr int kBands       = 8;
        static constexpr int kHeaderH     = 28;
        static constexpr int kToggleH     = 36;
        static constexpr int kTypeHPF       = 0;
        static constexpr int kTypeLowShelf  = 1;
        static constexpr int kTypePeak      = 2;
        static constexpr int kTypeHighShelf = 3;
        static constexpr int kTypeLPF       = 4;
        static constexpr int kFftOrder    = 11;
        static constexpr int kFftSize     = 1 << kFftOrder;

        struct BandState { double freq; double gainDb; double q; bool active; };

        static constexpr juce::uint32 kBandColours[kBands] = {
            0xff70dde0,  // HPF       - cyan
            0xfff0c050,  // Low shelf - gold
            0xfff04060,  // Peak 1    - red
            0xff60c050,  // Peak 2    - green
            0xff5090ff,  // Peak 3    - blue
            0xffff8030,  // Peak 4    - orange
            0xffb060e0,  // High shelf- purple
            0xff70dde0,  // LPF       - cyan
        };
        static constexpr int kBandTypes[kBands] = {
            kTypeHPF, kTypeLowShelf,
            kTypePeak, kTypePeak, kTypePeak, kTypePeak,
            kTypeHighShelf, kTypeLPF
        };
        static constexpr double kDefaultFreqs[kBands] = {
            30.0, 80.0, 200.0, 800.0, 2500.0, 6000.0, 10000.0, 18000.0
        };

        void timerCallback() override { syncFromSelection(); updateSpectrum(); }

        bandforge::Track* selectedTrack() { return project_.findTrack(selection_.selectedTrackId); }

        juce::Rectangle<int> getCurveArea() const
        {
            auto r = getLocalBounds().reduced(14).reduced(10);
            r.removeFromTop(kHeaderH + 6 + kToggleH + 6);
            return r;
        }

        juce::Rectangle<int> toggleCellBoundsFor(int band) const
        {
            auto work = getLocalBounds().reduced(14).reduced(10);
            work.removeFromTop(kHeaderH + 6);
            auto row = work.removeFromTop(kToggleH);
            const int padX = 2;
            const int cellW = (row.getWidth() - (kBands - 1) * padX) / kBands;
            const int x = row.getX() + band * (cellW + padX);
            return juce::Rectangle<int>(x, row.getY(), cellW, row.getHeight()).reduced(0, 2);
        }

        void initBands()
        {
            // Defaults: 4 mid peaks active, HPF/LPF/shelves off (like a fresh Logic Channel EQ).
            for (int i = 0; i < kBands; ++i) {
                bands_[static_cast<std::size_t>(i)] = {
                    kDefaultFreqs[i], 0.0, 0.707,
                    (i >= 2 && i <= 5)
                };
            }
        }

        void resetBand(int band)
        {
            bands_[static_cast<std::size_t>(band)] = {
                kDefaultFreqs[band], 0.0, 0.707, true
            };
        }

        void syncFromSelection()
        {
            const auto* track = selectedTrack();
            if (track == nullptr) { repaint(); return; }
            for (const auto& fx : track->mixer.effects) {
                if (fx.type != "eq") continue;
                for (int b = 0; b < kBands; ++b) {
                    const auto pf = [&](const std::string& k, double def) -> double {
                        const auto it = fx.parameters.find("b" + std::to_string(b) + "." + k);
                        return it != fx.parameters.end() ? it->second : def;
                    };
                    auto& bd = bands_[static_cast<std::size_t>(b)];
                    bd.freq   = pf("freq",   bd.freq);
                    bd.gainDb = pf("gain",   bd.gainDb);
                    bd.q      = pf("q",      bd.q);
                    bd.active = pf("active", bd.active ? 1.0 : 0.0) > 0.5;
                }
                repaint();
                return;
            }
            repaint();
        }

        void writeBandsToTrack(bandforge::Track& track)
        {
            for (auto& fx : track.mixer.effects) {
                if (fx.type == "eq") { writeToSlot(fx); return; }
            }
            bandforge::EffectSlot slot;
            slot.id   = "eq-" + std::to_string(track.id);
            slot.type = "eq";
            slot.name = "Channel EQ";
            slot.parameters["mix"] = 1.0;
            track.mixer.effects.push_back(std::move(slot));
            writeToSlot(track.mixer.effects.back());
        }

        void writeToSlot(bandforge::EffectSlot& slot)
        {
            for (int b = 0; b < kBands; ++b) {
                const std::string p = "b" + std::to_string(b) + ".";
                const auto& bd = bands_[static_cast<std::size_t>(b)];
                slot.parameters[p + "freq"]   = bd.freq;
                slot.parameters[p + "gain"]   = bd.gainDb;
                slot.parameters[p + "q"]      = bd.q;
                slot.parameters[p + "type"]   = static_cast<double>(kBandTypes[b]);
                slot.parameters[p + "active"] = bd.active ? 1.0 : 0.0;
            }
        }

        static double freqToNorm(double freq) { return std::log(freq / 20.0) / std::log(1000.0); }

        juce::Point<float> handlePos(int band, juce::Rectangle<int> area) const
        {
            const auto& b = bands_[static_cast<std::size_t>(band)];
            const float x = static_cast<float>(area.getX()) + static_cast<float>(freqToNorm(b.freq)) * static_cast<float>(area.getWidth());
            float y = static_cast<float>(area.getCentreY());
            const int t = kBandTypes[band];
            if (t != kTypeHPF && t != kTypeLPF)
                y -= static_cast<float>(b.gainDb / 24.0) * (static_cast<float>(area.getHeight()) * 0.5f);
            return { x, y };
        }

        void drawHeader(juce::Graphics& g, juce::Rectangle<int> header, const bandforge::Track& track) const
        {
            const auto accent = colourForTrackKind(track.kind);
            const juce::Rectangle<float> iconBox(
                static_cast<float>(header.getX()),
                static_cast<float>(header.getY() + 4),
                20.0f, 20.0f);
            g.setColour(accent.withAlpha(0.2f));
            g.fillRoundedRectangle(iconBox, 4.0f);
            drawTrackKindIcon(g, track.kind, iconBox.reduced(3.0f));

            g.setColour(juce::Colour(0xfff4f7fb));
            g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
            g.drawText(juce::String::fromUTF8(track.name.c_str()),
                header.withTrimmedLeft(26).withHeight(16), juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff7a8fa6));
            g.setFont(juce::FontOptions(10.0f));
            g.drawText("Channel EQ \xE2\x80\x94 8 bands \xE2\x80\xa2 click to enable \xE2\x80\xa2 drag = freq/gain \xE2\x80\xa2 scroll = Q",
                header.withTrimmedLeft(26).translated(0, 14).withHeight(12), juce::Justification::centredLeft);
        }

        static void drawTypeIcon(juce::Graphics& g, int type, juce::Rectangle<float> r)
        {
            juce::Path p;
            const float cx = r.getCentreX(), cy = r.getCentreY();
            const float hw = r.getWidth() * 0.42f;
            const float hh = r.getHeight() * 0.36f;
            const float hhs = r.getHeight() * 0.22f;
            switch (type) {
            case kTypeHPF:
                p.startNewSubPath(cx - hw, cy + hh);
                p.lineTo(cx - hw * 0.25f, cy + hh);
                p.lineTo(cx + hw * 0.25f, cy - hh);
                p.lineTo(cx + hw, cy - hh);
                break;
            case kTypeLowShelf:
                p.startNewSubPath(cx - hw, cy + hhs);
                p.lineTo(cx - hw * 0.25f, cy + hhs);
                p.lineTo(cx + hw * 0.25f, cy - hhs);
                p.lineTo(cx + hw, cy - hhs);
                break;
            case kTypePeak:
                p.startNewSubPath(cx - hw, cy + hh * 0.7f);
                p.quadraticTo(cx - hw * 0.4f, cy + hh * 0.7f, cx, cy - hh);
                p.quadraticTo(cx + hw * 0.4f, cy + hh * 0.7f, cx + hw, cy + hh * 0.7f);
                break;
            case kTypeHighShelf:
                p.startNewSubPath(cx - hw, cy - hhs);
                p.lineTo(cx - hw * 0.25f, cy - hhs);
                p.lineTo(cx + hw * 0.25f, cy + hhs);
                p.lineTo(cx + hw, cy + hhs);
                break;
            default: // LPF
                p.startNewSubPath(cx - hw, cy - hh);
                p.lineTo(cx - hw * 0.25f, cy - hh);
                p.lineTo(cx + hw * 0.25f, cy + hh);
                p.lineTo(cx + hw, cy + hh);
                break;
            }
            g.strokePath(p, juce::PathStrokeType(1.6f,
                juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        void drawToggleRow(juce::Graphics& g, juce::Rectangle<int> row)
        {
            const int padX = 2;
            const int cellW = (row.getWidth() - (kBands - 1) * padX) / kBands;
            for (int i = 0; i < kBands; ++i) {
                const int x = row.getX() + i * (cellW + padX);
                const auto cell = juce::Rectangle<int>(x, row.getY(), cellW, row.getHeight()).reduced(0, 2);
                const auto& b = bands_[static_cast<std::size_t>(i)];
                const auto col = juce::Colour(kBandColours[i]);
                const bool emphasised = (i == toggleHover_);

                if (b.active) {
                    g.setColour(col.withAlpha(emphasised ? 0.95f : 0.78f));
                    g.fillRoundedRectangle(cell.toFloat(), 4.0f);
                    g.setColour(juce::Colour(0xff0d1218));
                } else {
                    g.setColour(juce::Colour(0xff1a2230).withAlpha(emphasised ? 1.0f : 0.85f));
                    g.fillRoundedRectangle(cell.toFloat(), 4.0f);
                    g.setColour(col.withAlpha(0.4f));
                    g.drawRoundedRectangle(cell.toFloat(), 4.0f, 1.0f);
                    g.setColour(col.withAlpha(0.7f));
                }

                const auto iconArea = cell.toFloat().reduced(6.0f, 4.0f).withTrimmedBottom(11.0f);
                drawTypeIcon(g, kBandTypes[i], iconArea);

                g.setColour(b.active ? juce::Colour(0xff0d1218) : col.withAlpha(0.85f));
                g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
                g.drawText(juce::String(i + 1),
                    cell.withTrimmedTop(cell.getHeight() - 12).withHeight(11),
                    juce::Justification::centred);
            }
        }

        void drawGrid(juce::Graphics& g, juce::Rectangle<int> area) const
        {
            g.setColour(juce::Colour(0xff1f2733));
            for (int db : { -18, -12, -6, 6, 12, 18 }) {
                const float y = static_cast<float>(area.getCentreY())
                    - static_cast<float>(db / 24.0) * (static_cast<float>(area.getHeight()) * 0.5f);
                g.drawHorizontalLine(static_cast<int>(y), static_cast<float>(area.getX()), static_cast<float>(area.getRight()));
            }
            g.setColour(juce::Colour(0xff2a3548));
            g.drawHorizontalLine(area.getCentreY(), static_cast<float>(area.getX()), static_cast<float>(area.getRight()));
            g.setColour(juce::Colour(0xff1f2733));
            for (double f : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 }) {
                const float x = static_cast<float>(area.getX()) + static_cast<float>(freqToNorm(f)) * static_cast<float>(area.getWidth());
                g.drawVerticalLine(static_cast<int>(x), static_cast<float>(area.getY()), static_cast<float>(area.getBottom()));
            }

            g.setColour(juce::Colour(0xff5a6a7e));
            g.setFont(juce::FontOptions(9.5f));
            const struct { double f; const char* l; } freqs[] = {
                {50.0, "50"}, {100.0, "100"}, {200.0, "200"}, {500.0, "500"},
                {1000.0, "1k"}, {2000.0, "2k"}, {5000.0, "5k"}, {10000.0, "10k"}
            };
            for (const auto& fr : freqs) {
                const float x = static_cast<float>(area.getX()) + static_cast<float>(freqToNorm(fr.f)) * static_cast<float>(area.getWidth());
                g.drawText(fr.l, static_cast<int>(x - 16), area.getBottom() - 14, 32, 12, juce::Justification::centred);
            }
            for (int db : { -18, -12, -6, 6, 12, 18 }) {
                const float y = static_cast<float>(area.getCentreY())
                    - static_cast<float>(db / 24.0) * (static_cast<float>(area.getHeight()) * 0.5f);
                const juce::String s = (db > 0 ? juce::String("+") : juce::String()) + juce::String(db);
                g.drawText(s, area.getRight() - 26, static_cast<int>(y - 6), 22, 12, juce::Justification::centredRight);
            }
            g.drawText("0", area.getRight() - 26, area.getCentreY() - 6, 22, 12, juce::Justification::centredRight);
        }

        void drawSpectrum(juce::Graphics& g, juce::Rectangle<int> area) const
        {
            if (!spectrumReady_) return;
            const int w = area.getWidth();
            if (w <= 0) return;

            constexpr double kSr = 48000.0;
            juce::Path spectrum;
            bool started = false;
            for (int px = 0; px < w; ++px) {
                const double freq = 20.0 * std::pow(1000.0, static_cast<double>(px) / w);
                const double binF = freq * kFftSize / kSr;
                if (binF < 1.0 || binF >= kFftSize / 2 - 1) continue;
                const int bin = static_cast<int>(binF);
                const float fLevel = spectrumDb_[static_cast<std::size_t>(bin)];
                // -72..0 dB → bottom..top
                const float norm = std::clamp((fLevel + 72.0f) / 72.0f, 0.0f, 1.0f);
                const float y = area.getBottom() - norm * area.getHeight();
                const float x = static_cast<float>(area.getX() + px);
                if (!started) {
                    spectrum.startNewSubPath(x, static_cast<float>(area.getBottom()));
                    spectrum.lineTo(x, y);
                    started = true;
                } else {
                    spectrum.lineTo(x, y);
                }
            }
            spectrum.lineTo(static_cast<float>(area.getRight()), static_cast<float>(area.getBottom()));
            spectrum.closeSubPath();

            g.setColour(juce::Colour(0xff5090ff).withAlpha(0.22f));
            g.fillPath(spectrum);
            g.setColour(juce::Colour(0xff80b0ff).withAlpha(0.45f));
            g.strokePath(spectrum, juce::PathStrokeType(1.0f));
        }

        void updateSpectrum()
        {
            if (audioEngine_ == nullptr) return;
            std::array<float, kFftSize> samples{};
            const auto n = audioEngine_->peekRecentOutput(samples);
            if (n != static_cast<std::size_t>(kFftSize)) return;

            for (int i = 0; i < kFftSize; ++i) {
                fftBuf_[static_cast<std::size_t>(i)] = samples[static_cast<std::size_t>(i)]
                    * window_[static_cast<std::size_t>(i)];
            }
            std::fill(fftBuf_.begin() + kFftSize, fftBuf_.end(), 0.0f);
            fft_.performFrequencyOnlyForwardTransform(fftBuf_.data());

            for (int bin = 0; bin < kFftSize / 2; ++bin) {
                const float mag = fftBuf_[static_cast<std::size_t>(bin)] / static_cast<float>(kFftSize);
                const float db = 20.0f * std::log10(std::max(mag, 1e-7f));
                auto& smoothed = spectrumDb_[static_cast<std::size_t>(bin)];
                smoothed = 0.7f * smoothed + 0.3f * db;
            }
            spectrumReady_ = true;
        }

        void drawCurve(juce::Graphics& g, juce::Rectangle<int> area) const
        {
            const int w = area.getWidth();
            if (w <= 0) return;

            const auto yFromDb = [&](double db) {
                return static_cast<float>(area.getCentreY())
                    - static_cast<float>(std::clamp(db, -24.0, 24.0) / 24.0)
                      * (static_cast<float>(area.getHeight()) * 0.5f);
            };

            std::array<juce::Path, kBands> bandPaths;
            std::array<bool, kBands> bandHasShape{};
            juce::Path combinedPath;
            for (int px = 0; px < w; ++px) {
                const double freq = 20.0 * std::pow(1000.0, static_cast<double>(px) / w);
                double combined = 0.0;
                const float x = static_cast<float>(area.getX() + px);
                for (int i = 0; i < kBands; ++i) {
                    if (!bands_[static_cast<std::size_t>(i)].active) continue;
                    const double db = bandResponseDb(bands_[static_cast<std::size_t>(i)], kBandTypes[i], freq);
                    if (std::abs(db) > 0.08) bandHasShape[static_cast<std::size_t>(i)] = true;
                    const float y = yFromDb(db);
                    if (px == 0) bandPaths[static_cast<std::size_t>(i)].startNewSubPath(x, y);
                    else bandPaths[static_cast<std::size_t>(i)].lineTo(x, y);
                    combined += db;
                }
                const float yC = yFromDb(combined);
                if (px == 0) combinedPath.startNewSubPath(x, yC);
                else combinedPath.lineTo(x, yC);
            }

            for (int i = 0; i < kBands; ++i) {
                if (!bands_[static_cast<std::size_t>(i)].active) continue;
                if (!bandHasShape[static_cast<std::size_t>(i)]) continue;
                const bool emphasised = (i == dragBand_) || (i == hoverBand_ && dragBand_ < 0);
                const auto col = juce::Colour(kBandColours[i]).withAlpha(emphasised ? 0.85f : 0.4f);
                g.setColour(col);
                g.strokePath(bandPaths[static_cast<std::size_t>(i)],
                    juce::PathStrokeType(emphasised ? 1.8f : 1.0f));
            }

            juce::Path fill = combinedPath;
            fill.lineTo(static_cast<float>(area.getRight()), static_cast<float>(area.getCentreY()));
            fill.lineTo(static_cast<float>(area.getX()), static_cast<float>(area.getCentreY()));
            fill.closeSubPath();
            g.setColour(juce::Colour(0xff80b0ff).withAlpha(0.10f));
            g.fillPath(fill);
            g.setColour(juce::Colour(0xffe0eaff).withAlpha(0.95f));
            g.strokePath(combinedPath, juce::PathStrokeType(2.4f));
        }

        static double bandResponseDb(const BandState& b, int type, double freq)
        {
            static const double kPi = std::acos(-1.0);
            const double sr = 48000.0;
            const double w0 = 2.0 * kPi * b.freq / sr;
            const double fw = 2.0 * kPi * freq  / sr;
            const double cosw0 = std::cos(w0), sinw0 = std::sin(w0);
            const double cosfw = std::cos(fw), sinfw = std::sin(fw);
            const double A = std::pow(10.0, b.gainDb / 40.0);
            const double alpha = sinw0 / (2.0 * std::max(b.q, 0.001));
            double b0, b1, b2, a0, a1, a2;
            switch (type) {
            case kTypeHPF:
                b0 = (1.0 + cosw0) * 0.5; b1 = -(1.0 + cosw0); b2 = b0;
                a0 = 1.0 + alpha; a1 = -2.0 * cosw0; a2 = 1.0 - alpha;
                break;
            case kTypeLowShelf: {
                const double sqA  = std::sqrt(std::max(A, 1e-6));
                const double alph = sinw0 / 2.0 * std::sqrt(2.0);
                b0 = A*((A+1)-(A-1)*cosw0+2.0*sqA*alph);
                b1 = 2.0*A*((A-1)-(A+1)*cosw0);
                b2 = A*((A+1)-(A-1)*cosw0-2.0*sqA*alph);
                a0 = (A+1)+(A-1)*cosw0+2.0*sqA*alph;
                a1 = -2.0*((A-1)+(A+1)*cosw0);
                a2 = (A+1)+(A-1)*cosw0-2.0*sqA*alph;
                break;
            }
            case kTypePeak:
                b0 = 1.0+alpha*A; b1 = -2.0*cosw0; b2 = 1.0-alpha*A;
                a0 = 1.0+alpha/A; a1 = -2.0*cosw0; a2 = 1.0-alpha/A;
                break;
            case kTypeHighShelf: {
                const double sqA  = std::sqrt(std::max(A, 1e-6));
                const double alph = sinw0 / 2.0 * std::sqrt(2.0);
                b0 = A*((A+1)+(A-1)*cosw0+2.0*sqA*alph);
                b1 = -2.0*A*((A-1)+(A+1)*cosw0);
                b2 = A*((A+1)+(A-1)*cosw0-2.0*sqA*alph);
                a0 = (A+1)-(A-1)*cosw0+2.0*sqA*alph;
                a1 = 2.0*((A-1)-(A+1)*cosw0);
                a2 = (A+1)-(A-1)*cosw0-2.0*sqA*alph;
                break;
            }
            default: // LPF
                b0 = (1.0-cosw0)*0.5; b1 = 1.0-cosw0; b2 = b0;
                a0 = 1.0+alpha; a1 = -2.0*cosw0; a2 = 1.0-alpha;
                break;
            }
            const double cos2fw = 2.0*cosfw*cosfw - 1.0;
            const double sin2fw = 2.0*sinfw*cosfw;
            const double nr = b0 + b1*cosfw + b2*cos2fw;
            const double ni = -(b1*sinfw + b2*sin2fw);
            const double dr = a0 + a1*cosfw + a2*cos2fw;
            const double di = -(a1*sinfw + a2*sin2fw);
            const double mag2 = (nr*nr + ni*ni) / std::max(dr*dr + di*di, 1e-30);
            return 10.0 * std::log10(std::max(mag2, 1e-30));
        }

        void drawHandles(juce::Graphics& g, juce::Rectangle<int> area) const
        {
            for (int i = 0; i < kBands; ++i) {
                const auto& b = bands_[static_cast<std::size_t>(i)];
                const auto p = handlePos(i, area);
                const auto col = juce::Colour(kBandColours[i]);
                const float r = (i == dragBand_) ? 11.0f : (i == hoverBand_ ? 9.0f : 8.0f);
                const float alpha = b.active ? 1.0f : 0.2f;

                g.setColour(juce::Colour(0xff0d1218).withAlpha(0.7f * alpha));
                g.fillEllipse(p.x - r, p.y - r, r * 2, r * 2);
                g.setColour(col.withAlpha(0.45f * alpha));
                g.fillEllipse(p.x - r + 2, p.y - r + 2, (r - 2) * 2, (r - 2) * 2);
                g.setColour(col.withAlpha(alpha));
                g.drawEllipse(p.x - r, p.y - r, r * 2, r * 2, 1.8f);

                g.setColour(b.active ? juce::Colours::white : col.withAlpha(0.6f));
                g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
                g.drawText(juce::String(i + 1), static_cast<int>(p.x - 6), static_cast<int>(p.y - 6), 12, 12, juce::Justification::centred);
            }

            const int show = (dragBand_ >= 0) ? dragBand_ : hoverBand_;
            if (show >= 0) {
                const auto& b = bands_[static_cast<std::size_t>(show)];
                const auto p = handlePos(show, area);
                const juce::String freqStr = b.freq < 1000.0
                    ? juce::String(static_cast<int>(b.freq)) + " Hz"
                    : juce::String(b.freq / 1000.0, 2) + " kHz";
                juce::String txt = freqStr;
                if (kBandTypes[show] != kTypeHPF && kBandTypes[show] != kTypeLPF) {
                    txt += "   " + juce::String(b.gainDb >= 0 ? "+" : "") + juce::String(b.gainDb, 1) + " dB";
                }
                txt += "   Q " + juce::String(b.q, 2);
                static constexpr const char* kTypeNames[] = { "HPF", "Low Shelf", "Peak", "High Shelf", "LPF" };
                const juce::String typeTxt = kTypeNames[std::clamp(kBandTypes[show], 0, 4)];

                const int boxW = 240, boxH = 24;
                int tx = static_cast<int>(p.x + 14);
                int ty = static_cast<int>(p.y - 30);
                if (tx + boxW > area.getRight()) tx = static_cast<int>(p.x) - 14 - boxW;
                if (ty < area.getY()) ty = static_cast<int>(p.y) + 14;
                tx = std::max(tx, area.getX() + 2);
                const auto box = juce::Rectangle<int>(tx, ty, boxW, boxH);
                g.setColour(juce::Colour(0xff0d1218).withAlpha(0.95f));
                g.fillRoundedRectangle(box.toFloat(), 4.0f);
                g.setColour(juce::Colour(kBandColours[show]));
                g.drawRoundedRectangle(box.toFloat(), 4.0f, 1.0f);
                g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
                g.drawText(typeTxt, box.withWidth(72).reduced(8, 0), juce::Justification::centredLeft);
                g.setColour(juce::Colour(0xfff0f4fa));
                g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
                g.drawText(txt, box.withTrimmedLeft(72).reduced(4, 0), juce::Justification::centredLeft);
            }
        }

        bandforge::Project& project_;
        bandforge::ProjectHistory& history_;
        bandforge_app::SelectionState& selection_;
        std::array<BandState, kBands> bands_;
        int dragBand_ = -1;
        int hoverBand_ = -1;
        int toggleHover_ = -1;
        juce::uint32 lastWheelMs_ = 0;

        // FFT spectrum analyzer
        const bandforge::AudioEngine* audioEngine_ = nullptr;
        juce::dsp::FFT fft_ { kFftOrder };
        std::array<float, kFftSize * 2> fftBuf_{};
        std::array<float, kFftSize / 2> spectrumDb_{};
        std::array<float, kFftSize> window_{};
        bool spectrumReady_ = false;
    };

    // ─── Insert FX rack ──────────────────────────────────────────────────────
    class FxRackView final : public juce::Component, private juce::Timer {
    public:
        FxRackView(bandforge::Project& project, bandforge::ProjectHistory& history,
                   bandforge_app::SelectionState& selection)
            : project_(project)
            , history_(history)
            , selection_(selection)
        {
            addButton_.setButtonText("Add FX");
            addButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2e3b4e));
            addButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffedf3fb));
            addButton_.onClick = [this] { showAddMenu(); };
            addAndMakeVisible(addButton_);

            clearButton_.setButtonText("Clear");
            clearButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff2a303a));
            clearButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xffcbd5e1));
            clearButton_.onClick = [this] { clearInsertEffects(); };
            addAndMakeVisible(clearButton_);

            for (int i = 0; i < kParamSlots; ++i) {
                configureParamSlot(i);
            }
            syncParamControls(nullptr);
            addButton_.setEnabled(false);
            clearButton_.setEnabled(false);

            startTimerHz(15);
        }

        std::function<void(bandforge::TrackId)> onEffectsChanged;

        void resized() override
        {
            const auto l = layout();
            auto header = l.header;
            clearButton_.setBounds(header.removeFromRight(70).reduced(3, 3));
            addButton_.setBounds(header.removeFromRight(82).reduced(3, 3));

            auto params = l.params.reduced(10);
            params.removeFromTop(30);
            for (auto& slot : paramSlots_) {
                auto line = params.removeFromTop(34);
                slot.label.setBounds(line.removeFromLeft(104));
                slot.slider.setBounds(line.reduced(8, 5));
                params.removeFromTop(4);
            }
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xff161b22));
            const auto l = layout();
            g.setColour(juce::Colour(0xff202731));
            g.fillRoundedRectangle(l.area.toFloat(), 8.0f);

            auto* track = selectedTrack();
            drawHeader(g, l.header, track);

            g.setColour(juce::Colour(0xff171d26));
            g.fillRoundedRectangle(l.rack.toFloat(), 7.0f);
            g.setColour(juce::Colour(0xff171d26));
            g.fillRoundedRectangle(l.params.toFloat(), 7.0f);

            if (track == nullptr) {
                g.setColour(juce::Colour(0xff5a6a7e));
                g.setFont(juce::FontOptions(13.0f));
                g.drawText("Select a track to edit inserts", l.rack, juce::Justification::centred);
                return;
            }

            drawEffectRows(g, l.rack.reduced(8), *track);
            drawParamHeader(g, l.params.reduced(10), *track);
        }

        void mouseDown(const juce::MouseEvent& event) override
        {
            auto* track = selectedTrack();
            if (track == nullptr) {
                return;
            }

            const auto indices = insertEffectIndices(*track);
            if (indices.empty()) {
                return;
            }

            const auto rows = layout().rack.reduced(8);
            if (!rows.contains(event.getPosition())) {
                return;
            }

            const int visibleRows = std::max(1, rows.getHeight() / kRowH);
            rowScroll_ = std::clamp(rowScroll_, 0, std::max(0, static_cast<int>(indices.size()) - visibleRows));
            const int visibleIndex = (event.getPosition().y - rows.getY()) / kRowH;
            const int listIndex = rowScroll_ + visibleIndex;
            if (listIndex < 0 || listIndex >= static_cast<int>(indices.size())) {
                return;
            }

            const int effectIndex = indices[static_cast<std::size_t>(listIndex)];
            const auto row = rowBoundsFor(rows, visibleIndex);
            if (deleteBounds(row).contains(event.getPosition())) {
                removeEffect(effectIndex);
                return;
            }

            selectedEffectIndex_ = effectIndex;
            syncParamControls(track);
            repaint();
        }

        void mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel) override
        {
            auto* track = selectedTrack();
            if (track == nullptr || !layout().rack.contains(event.getPosition())) {
                return;
            }

            const auto indices = insertEffectIndices(*track);
            const int visibleRows = std::max(1, layout().rack.reduced(8).getHeight() / kRowH);
            const int maxScroll = std::max(0, static_cast<int>(indices.size()) - visibleRows);
            if (maxScroll <= 0) {
                return;
            }

            rowScroll_ = std::clamp(rowScroll_ + (wheel.deltaY < 0.0f ? 1 : -1), 0, maxScroll);
            repaint();
        }

    private:
        static constexpr int kParamSlots = 3;
        static constexpr int kRowH = 32;

        struct Layout {
            juce::Rectangle<int> area;
            juce::Rectangle<int> header;
            juce::Rectangle<int> rack;
            juce::Rectangle<int> params;
        };

        struct ParamSlot {
            juce::Label label;
            juce::Slider slider;
        };

        struct ParamSpec {
            std::string key;
            juce::String label;
            double min = 0.0;
            double max = 1.0;
            double defaultValue = 0.0;
            double interval = 0.001;
            int decimals = 2;
        };

        Layout layout() const
        {
            Layout l;
            l.area = getLocalBounds().reduced(14);
            auto inner = l.area.reduced(12);
            l.header = inner.removeFromTop(30);
            inner.removeFromTop(8);

            if (inner.getWidth() < 620) {
                l.rack = inner.removeFromTop(std::max(48, inner.getHeight() / 2));
                inner.removeFromTop(8);
                l.params = inner;
            } else {
                const int rackW = std::min(470, std::max(330, inner.getWidth() / 2));
                l.rack = inner.removeFromLeft(rackW);
                inner.removeFromLeft(10);
                l.params = inner;
            }

            return l;
        }

        void configureParamSlot(int index)
        {
            auto& slot = paramSlots_[static_cast<std::size_t>(index)];
            slot.label.setText({}, juce::dontSendNotification);
            slot.label.setColour(juce::Label::textColourId, juce::Colour(0xff9ba8b9));
            slot.label.setJustificationType(juce::Justification::centredLeft);
            addAndMakeVisible(slot.label);

            slot.slider.setRange(0.0, 1.0, 0.001);
            slot.slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slot.slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 20);
            slot.slider.setColour(juce::Slider::trackColourId, juce::Colour(0xff75dda7));
            slot.slider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff303a47));
            slot.slider.onDragStart = [this] { beginParamGesture(); };
            slot.slider.onDragEnd = [this] { endParamGesture(); };
            slot.slider.onValueChange = [this, index] {
                if (suppress_) {
                    return;
                }

                const bool oneShot = !gestureActive_;
                if (oneShot) {
                    rememberParamGesture();
                }
                writeSelectedParam(index);
                if (oneShot) {
                    gestureRemembered_ = false;
                }
            };
            addAndMakeVisible(slot.slider);
        }

        void timerCallback() override
        {
            auto* track = selectedTrack();
            if (track == nullptr) {
                if (lastTrackId_ != 0 || selectedEffectIndex_ != -1) {
                    lastTrackId_ = 0;
                    selectedEffectIndex_ = -1;
                    syncParamControls(nullptr);
                    repaint();
                }
                addButton_.setEnabled(false);
                clearButton_.setEnabled(false);
                return;
            }

            addButton_.setEnabled(true);
            clearButton_.setEnabled(!insertEffectIndices(*track).empty());

            if (lastTrackId_ != track->id || !selectedEffectIsValid(*track)) {
                lastTrackId_ = track->id;
                selectedEffectIndex_ = firstInsertEffectIndex(*track);
                clampScroll(*track);
                syncParamControls(track);
                repaint();
                return;
            }

            if (!gestureActive_) {
                syncParamControls(track);
            }
        }

        bandforge::Track* selectedTrack()
        {
            return project_.findTrack(selection_.selectedTrackId);
        }

        static juce::String effectTitle(const bandforge::EffectSlot& fx)
        {
            if (!fx.name.empty()) {
                return juce::String::fromUTF8(fx.name.c_str());
            }
            if (fx.type == "echo") return "Echo";
            if (fx.type == "reverb") return "Reverb";
            if (fx.type == "distortion") return "Drive";
            if (fx.type == "telephone") return "Telephone";
            if (fx.type == "megaphone") return "Megaphone";
            return juce::String::fromUTF8(fx.type.c_str());
        }

        static std::vector<ParamSpec> specsForEffect(const bandforge::EffectSlot& fx)
        {
            if (fx.type == "echo") {
                return {
                    { "timeSec", "Time",     0.02, 1.20, 0.32, 0.01, 2 },
                    { "feedback", "Feedback", 0.0, 0.95, 0.35, 0.01, 2 },
                    { "mix", "Mix",          0.0, 1.0,  0.35, 0.01, 2 },
                };
            }
            if (fx.type == "reverb") {
                return {
                    { "size", "Size", 0.10, 0.95, 0.70, 0.01, 2 },
                    { "damp", "Damp", 0.0,  1.0,  0.45, 0.01, 2 },
                    { "mix",  "Mix",  0.0,  1.0,  0.30, 0.01, 2 },
                };
            }
            if (fx.type == "distortion") {
                return {
                    { "drive", "Drive", 1.0, 30.0, 6.0, 0.1, 1 },
                    { "mix",   "Mix",   0.0, 1.0,  0.6, 0.01, 2 },
                };
            }
            if (fx.type == "telephone") {
                return {
                    { "mix", "Mix", 0.0, 1.0, 1.0, 0.01, 2 },
                };
            }
            if (fx.type == "megaphone") {
                return {
                    { "drive", "Drive", 1.0, 30.0, 8.0, 0.1, 1 },
                    { "mix",   "Mix",   0.0, 1.0,  1.0, 0.01, 2 },
                };
            }
            return {
                { "mix", "Mix", 0.0, 1.0, 1.0, 0.01, 2 },
            };
        }

        static double readParam(const bandforge::EffectSlot& fx, const ParamSpec& spec)
        {
            const auto found = fx.parameters.find(spec.key);
            return std::clamp(found != fx.parameters.end() ? found->second : spec.defaultValue,
                              spec.min, spec.max);
        }

        static juce::String effectSummary(const bandforge::EffectSlot& fx)
        {
            const auto mix = fx.parameters.find("mix");
            if (mix != fx.parameters.end()) {
                return juce::String(static_cast<int>(std::round(std::clamp(mix->second, 0.0, 1.0) * 100.0))) + "% mix";
            }
            return juce::String::fromUTF8(fx.type.c_str());
        }

        static bandforge::EffectSlot makeEffectSlot(const std::string& type, bandforge::TrackId trackId, std::size_t slotIndex)
        {
            bandforge::EffectSlot slot;
            slot.id = type + "-" + std::to_string(trackId) + "-" + std::to_string(slotIndex);
            slot.type = type;

            if (type == "echo") {
                slot.name = "Echo";
                slot.parameters = { { "timeSec", 0.32 }, { "feedback", 0.35 }, { "mix", 0.35 } };
            } else if (type == "reverb") {
                slot.name = "Hall Reverb";
                slot.parameters = { { "size", 0.75 }, { "damp", 0.4 }, { "mix", 0.3 } };
            } else if (type == "distortion") {
                slot.name = "Drive";
                slot.parameters = { { "drive", 6.0 }, { "mix", 0.6 } };
            } else if (type == "telephone") {
                slot.name = "Telephone";
                slot.parameters = { { "mix", 1.0 } };
            } else if (type == "megaphone") {
                slot.name = "Megaphone";
                slot.parameters = { { "drive", 8.0 }, { "mix", 1.0 } };
            }

            return slot;
        }

        static std::vector<int> insertEffectIndices(const bandforge::Track& track)
        {
            std::vector<int> indices;
            for (int i = 0; i < static_cast<int>(track.mixer.effects.size()); ++i) {
                if (track.mixer.effects[static_cast<std::size_t>(i)].type != "eq") {
                    indices.push_back(i);
                }
            }
            return indices;
        }

        static int firstInsertEffectIndex(const bandforge::Track& track)
        {
            const auto indices = insertEffectIndices(track);
            return indices.empty() ? -1 : indices.front();
        }

        bool selectedEffectIsValid(const bandforge::Track& track) const
        {
            return selectedEffectIndex_ >= 0
                && selectedEffectIndex_ < static_cast<int>(track.mixer.effects.size())
                && track.mixer.effects[static_cast<std::size_t>(selectedEffectIndex_)].type != "eq";
        }

        bandforge::EffectSlot* selectedEffect(bandforge::Track& track)
        {
            if (!selectedEffectIsValid(track)) {
                return nullptr;
            }
            return &track.mixer.effects[static_cast<std::size_t>(selectedEffectIndex_)];
        }

        void showAddMenu()
        {
            juce::PopupMenu menu;
            menu.addItem(1, "Echo");
            menu.addItem(2, "Reverb");
            menu.addItem(3, "Distortion");
            menu.addItem(4, "Telephone");
            menu.addItem(5, "Megaphone");

            juce::Component::SafePointer<FxRackView> safeThis(this);
            menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(addButton_),
                [safeThis](int result) {
                    auto* self = safeThis.getComponent();
                    if (self == nullptr || result <= 0) {
                        return;
                    }

                    static const char* kTypes[] = { "echo", "reverb", "distortion", "telephone", "megaphone" };
                    if (result <= 5) {
                        self->addEffect(kTypes[result - 1]);
                    }
                });
        }

        void addEffect(const std::string& type)
        {
            auto* track = selectedTrack();
            if (track == nullptr) {
                return;
            }

            history_.remember(project_);
            track->mixer.effects.push_back(makeEffectSlot(type, track->id, track->mixer.effects.size()));
            selectedEffectIndex_ = static_cast<int>(track->mixer.effects.size()) - 1;
            clampScroll(*track);
            notifyEffectsChanged(track->id);
            syncParamControls(track);
            repaintParent();
        }

        void clearInsertEffects()
        {
            auto* track = selectedTrack();
            if (track == nullptr) {
                return;
            }

            const bool hasInsert = std::any_of(
                track->mixer.effects.begin(), track->mixer.effects.end(),
                [](const bandforge::EffectSlot& fx) { return fx.type != "eq"; });
            if (!hasInsert) {
                return;
            }

            history_.remember(project_);
            const auto before = track->mixer.effects.size();
            track->mixer.effects.erase(std::remove_if(
                track->mixer.effects.begin(), track->mixer.effects.end(),
                [](const bandforge::EffectSlot& fx) { return fx.type != "eq"; }),
                track->mixer.effects.end());

            if (track->mixer.effects.size() == before) {
                return;
            }

            selectedEffectIndex_ = firstInsertEffectIndex(*track);
            rowScroll_ = 0;
            notifyEffectsChanged(track->id);
            syncParamControls(track);
            repaintParent();
        }

        void removeEffect(int effectIndex)
        {
            auto* track = selectedTrack();
            if (track == nullptr || effectIndex < 0 || effectIndex >= static_cast<int>(track->mixer.effects.size())) {
                return;
            }
            if (track->mixer.effects[static_cast<std::size_t>(effectIndex)].type == "eq") {
                return;
            }

            history_.remember(project_);
            track->mixer.effects.erase(track->mixer.effects.begin() + effectIndex);
            selectedEffectIndex_ = firstInsertEffectIndex(*track);
            clampScroll(*track);
            notifyEffectsChanged(track->id);
            syncParamControls(track);
            repaintParent();
        }

        void writeSelectedParam(int slotIndex)
        {
            auto* track = selectedTrack();
            if (track == nullptr) {
                return;
            }
            auto* fx = selectedEffect(*track);
            if (fx == nullptr) {
                return;
            }

            const auto specs = specsForEffect(*fx);
            if (slotIndex < 0 || slotIndex >= static_cast<int>(specs.size())) {
                return;
            }

            const auto& spec = specs[static_cast<std::size_t>(slotIndex)];
            fx->parameters[spec.key] = std::clamp(paramSlots_[static_cast<std::size_t>(slotIndex)].slider.getValue(),
                                                  spec.min, spec.max);
            repaint();
        }

        void syncParamControls(bandforge::Track* track)
        {
            juce::ScopedValueSetter<bool> guard(suppress_, true);
            bandforge::EffectSlot* fx = track != nullptr ? selectedEffect(*track) : nullptr;
            const auto specs = fx != nullptr ? specsForEffect(*fx) : std::vector<ParamSpec> {};
            currentParamTitle_ = fx != nullptr ? effectTitle(*fx) : juce::String();

            for (int i = 0; i < kParamSlots; ++i) {
                auto& slot = paramSlots_[static_cast<std::size_t>(i)];
                const bool active = i < static_cast<int>(specs.size());
                slot.label.setVisible(active);
                slot.slider.setVisible(active);
                slot.label.setEnabled(active);
                slot.slider.setEnabled(active);

                if (!active) {
                    slot.label.setText({}, juce::dontSendNotification);
                    continue;
                }

                const auto& spec = specs[static_cast<std::size_t>(i)];
                slot.label.setText(spec.label, juce::dontSendNotification);
                slot.slider.setRange(spec.min, spec.max, spec.interval);
                slot.slider.setNumDecimalPlacesToDisplay(spec.decimals);
                slot.slider.setValue(readParam(*fx, spec), juce::dontSendNotification);
            }
        }

        void beginParamGesture()
        {
            gestureActive_ = true;
            rememberParamGesture();
        }

        void rememberParamGesture()
        {
            if (!gestureRemembered_) {
                history_.remember(project_);
                gestureRemembered_ = true;
            }
        }

        void endParamGesture()
        {
            gestureActive_ = false;
            gestureRemembered_ = false;
        }

        void notifyEffectsChanged(bandforge::TrackId trackId)
        {
            if (onEffectsChanged) {
                onEffectsChanged(trackId);
            }
        }

        void repaintParent()
        {
            repaint();
            if (auto* parent = getParentComponent()) {
                parent->repaint();
            }
        }

        void clampScroll(const bandforge::Track& track)
        {
            const auto indices = insertEffectIndices(track);
            const int visibleRows = std::max(1, layout().rack.reduced(8).getHeight() / kRowH);
            rowScroll_ = std::clamp(rowScroll_, 0, std::max(0, static_cast<int>(indices.size()) - visibleRows));
        }

        static juce::Rectangle<int> rowBoundsFor(juce::Rectangle<int> rows, int visibleIndex)
        {
            return rows.withY(rows.getY() + visibleIndex * kRowH).withHeight(kRowH - 4);
        }

        static juce::Rectangle<int> deleteBounds(juce::Rectangle<int> row)
        {
            return row.removeFromRight(28).reduced(4, 5);
        }

        void drawHeader(juce::Graphics& g, juce::Rectangle<int> header, const bandforge::Track* track) const
        {
            if (track != nullptr) {
                const auto accent = colourForTrackKind(track->kind);
                const juce::Rectangle<float> iconBox(
                    static_cast<float>(header.getX()),
                    static_cast<float>(header.getY() + 5),
                    20.0f, 20.0f);
                g.setColour(accent.withAlpha(0.2f));
                g.fillRoundedRectangle(iconBox, 4.0f);
                drawTrackKindIcon(g, track->kind, iconBox.reduced(3.0f));

                g.setColour(juce::Colour(0xfff4f7fb));
                g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
                g.drawText(juce::String::fromUTF8(track->name.c_str()),
                    header.withTrimmedLeft(26).withTrimmedRight(160).withHeight(17),
                    juce::Justification::centredLeft);
                g.setColour(juce::Colour(0xff7a8fa6));
                g.setFont(juce::FontOptions(10.0f));
                g.drawText("Insert FX",
                    header.withTrimmedLeft(26).withTrimmedRight(160).translated(0, 15).withHeight(12),
                    juce::Justification::centredLeft);
            } else {
                g.setColour(juce::Colour(0xff5a6a7e));
                g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
                g.drawText("Insert FX", header.withTrimmedRight(160), juce::Justification::centredLeft);
            }
        }

        void drawEffectRows(juce::Graphics& g, juce::Rectangle<int> rows, const bandforge::Track& track)
        {
            const auto indices = insertEffectIndices(track);
            if (indices.empty()) {
                g.setColour(juce::Colour(0xff5a6a7e));
                g.setFont(juce::FontOptions(13.0f));
                g.drawText("No insert effects", rows, juce::Justification::centred);
                return;
            }

            const int visibleRows = std::max(1, rows.getHeight() / kRowH);
            rowScroll_ = std::clamp(rowScroll_, 0, std::max(0, static_cast<int>(indices.size()) - visibleRows));
            const int count = std::min(visibleRows, static_cast<int>(indices.size()) - rowScroll_);

            for (int i = 0; i < count; ++i) {
                const int effectIndex = indices[static_cast<std::size_t>(rowScroll_ + i)];
                const auto& fx = track.mixer.effects[static_cast<std::size_t>(effectIndex)];
                const auto row = rowBoundsFor(rows, i);
                const bool selected = effectIndex == selectedEffectIndex_;
                const auto accent = selected ? juce::Colour(0xff75dda7) : juce::Colour(0xff3a4655);

                g.setColour(selected ? juce::Colour(0xff263743) : juce::Colour(0xff222a35));
                g.fillRoundedRectangle(row.toFloat(), 5.0f);
                g.setColour(accent.withAlpha(selected ? 0.9f : 0.45f));
                g.drawRoundedRectangle(row.toFloat(), 5.0f, selected ? 1.4f : 1.0f);

                auto text = row.reduced(10, 0);
                const auto del = deleteBounds(row);
                text.removeFromRight(34);

                g.setColour(juce::Colour(0xffedf3fb));
                g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
                g.drawText(effectTitle(fx), text.withTrimmedBottom(13), juce::Justification::centredLeft);

                g.setColour(juce::Colour(0xff8ea0b4));
                g.setFont(juce::FontOptions(10.0f));
                g.drawText(effectSummary(fx), text.withTrimmedTop(14), juce::Justification::centredLeft);

                g.setColour(juce::Colour(0xff425066));
                g.fillRoundedRectangle(del.toFloat(), 4.0f);
                g.setColour(juce::Colour(0xffd7dde8));
                g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
                g.drawText("x", del, juce::Justification::centred);
            }
        }

        void drawParamHeader(juce::Graphics& g, juce::Rectangle<int> area, bandforge::Track& track)
        {
            auto* fx = selectedEffect(track);
            g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
            if (fx == nullptr) {
                g.setColour(juce::Colour(0xff5a6a7e));
                g.drawText("Effect parameters", area.removeFromTop(24), juce::Justification::centredLeft);
                g.setFont(juce::FontOptions(12.0f));
                g.drawText("Add an effect to edit its controls", area, juce::Justification::centred);
                return;
            }

            g.setColour(juce::Colour(0xfff4f7fb));
            g.drawText(currentParamTitle_, area.removeFromTop(20), juce::Justification::centredLeft);
            g.setColour(juce::Colour(0xff7a8fa6));
            g.setFont(juce::FontOptions(10.0f));
            g.drawText(effectSummary(*fx), area.removeFromTop(14), juce::Justification::centredLeft);
        }

        bandforge::Project& project_;
        bandforge::ProjectHistory& history_;
        bandforge_app::SelectionState& selection_;
        juce::TextButton addButton_;
        juce::TextButton clearButton_;
        std::array<ParamSlot, kParamSlots> paramSlots_;
        juce::String currentParamTitle_;
        bandforge::TrackId lastTrackId_ = 0;
        int selectedEffectIndex_ = -1;
        int rowScroll_ = 0;
        bool suppress_ = false;
        bool gestureActive_ = false;
        bool gestureRemembered_ = false;
    };

    bandforge::Project& project_;
    bandforge::ProjectHistory& history_;
    bandforge_app::SelectionState& selection_;
    KeyboardCallbacks keyCallbacks_;

    juce::TabbedComponent tabs_ { juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<PianoRollView> pianoRoll_;
    std::unique_ptr<SmartControlsView> smartControls_;
    std::unique_ptr<EqView> eqView_;
    std::unique_ptr<MusicalKeyboardView> musicalKeyboard_;
};

MainComponent::MainComponent()
    : project_(bandforge::makeStarterProject())
    , library_(makeBuiltInLibrary())
{
    trackList_ = std::make_unique<TrackListComponent>(project_, history_, selection_);
    trackList_->getTrackLevel = [this](bandforge::TrackId id) -> float {
        const auto it = trackDisplayLevels_.find(id);
        return it != trackDisplayLevels_.end() ? it->second : 0.0f;
    };
    timeline_ = std::make_unique<TimelineComponent>(project_, transport_, grid_, history_, selection_);
    timeline_->setAudioEngine(&audioEngine_);
    libraryPanel_ = std::make_unique<LibraryPanelComponent>(library_, project_, history_, transport_, grid_, selection_);

    KeyboardCallbacks keyCbs;
    keyCbs.noteOn = [this](int pitch, int vel) {
        const int velocity = std::clamp(vel > 0 ? vel : keyboardVelocity_, 1, 127);
        {
            const std::lock_guard<std::mutex> lock(activeNoteKeysMutex_);
            activeNoteKeys_[pointerNoteKey(pitch)] = pitch;
        }
        audioEngine_.noteOn(pitch, velocity, selectedTrackKind());
        recordMusicalTypingMessage(juce::MidiMessage::noteOn(1, pitch, static_cast<juce::uint8>(velocity)));
    };
    keyCbs.noteOff = [this](int pitch) {
        {
            const std::lock_guard<std::mutex> lock(activeNoteKeysMutex_);
            activeNoteKeys_.erase(pointerNoteKey(pitch));
        }
        audioEngine_.noteOff(pitch);
        recordMusicalTypingMessage(juce::MidiMessage::noteOff(1, pitch));
    };
    keyCbs.getOctave = [this]() -> int {
        return keyboardOctave_;
    };
    keyCbs.shiftOctave = [this](int delta) {
        releaseActiveMusicalTypingKeys();
        keyboardOctave_ = std::clamp(keyboardOctave_ + delta, 0, 8);
        refreshViews();
    };
    keyCbs.getVelocity = [this]() -> int {
        return keyboardVelocity_;
    };
    keyCbs.shiftVelocity = [this](int delta) {
        keyboardVelocity_ = std::clamp(keyboardVelocity_ + delta, 1, 127);
        refreshViews();
    };
    keyCbs.isPitchActive = [this](int pitch) -> bool {
        const std::lock_guard<std::mutex> lock(activeNoteKeysMutex_);
        for (const auto& [kc, p] : activeNoteKeys_) {
            if (p == pitch) return true;
        }
        return false;
    };

    editorPanel_ = std::make_unique<EditorPanelComponent>(project_, history_, selection_, std::move(keyCbs));
    editorPanel_->setAudioEngine(&audioEngine_);

    addAndMakeVisible(*trackList_);
    addAndMakeVisible(*timeline_);
    addAndMakeVisible(*libraryPanel_);
    addAndMakeVisible(*editorPanel_);

    toolbarLaf_ = std::make_unique<ToolbarLookAndFeel>();

    const struct { juce::TextButton* btn; const char* tip; } toolbarButtons[] = {
        { &openButton_,     "Open project  (Cmd+O)"        },
        { &saveButton_,     "Save project  (Cmd+S)"        },
        { &undoButton_,     "Undo  (Cmd+Z)"                },
        { &redoButton_,     "Redo  (Cmd+Shift+Z)"          },
        { &playButton_,     "Play  (Space)"                },
        { &stopButton_,     "Stop  (Space)"                },
        { &recordButton_,   "Record arm"                   },
        { &addMidiButton_,  "Add MIDI track"               },
        { &addAudioButton_, "Add audio track"              },
        { &loopButton_,     "Toggle loop region  (Cycle)"  },
        { &metronomeButton_,"Toggle metronome click"        },
        { &snapButton_,     "Toggle beat snap"             },
        { &zoomOutButton_,  "Zoom out"                     },
        { &zoomInButton_,   "Zoom in"                      },
        { &exportButton_,   "Export WAV  (Cmd+E)"          },
    };

    for (const auto& [btn, tip] : toolbarButtons) {
        addAndMakeVisible(*btn);
        btn->setLookAndFeel(toolbarLaf_.get());
        btn->setTooltip(tip);
    }
    loopButton_.setClickingTogglesState(true);
    metronomeButton_.setClickingTogglesState(true);
    snapButton_.setClickingTogglesState(true);

    addAndMakeVisible(positionLabel_);
    addAndMakeVisible(tempoLabel_);
    addAndMakeVisible(recordingStatusLabel_);
    positionLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffd7dde8));
    tempoLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffd7dde8));
    recordingStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffaeb9c8));
    recordingStatusLabel_.setColour(juce::Label::backgroundColourId, juce::Colour(0xff1b2028));
    positionLabel_.setJustificationType(juce::Justification::centred);
    tempoLabel_.setJustificationType(juce::Justification::centred);
    recordingStatusLabel_.setJustificationType(juce::Justification::centred);
    recordingStatusLabel_.setTooltip("Musical typing velocity: press 1-9 or 0");
    tempoLabel_.setEditable(true, false, false);
    tempoLabel_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    tempoLabel_.setTooltip("Click to edit tempo");
    tempoLabel_.setFont(juce::Font(juce::FontOptions(17.0f, juce::Font::bold)));
    tempoLabel_.onTextChange = [this] {
        const auto bpm = parseTempoText(tempoLabel_.getText());
        if (!bpm.has_value()) {
            refreshViews();
            return;
        }

        if (!project_.tempoMarkers.empty() && std::abs(project_.tempoMarkers.front().bpm - *bpm) > 0.001) {
            history_.remember(project_);
            project_.tempoMarkers.front().bpm = *bpm;
        }
        refreshViews();
    };

    openButton_.onClick = [this] { openProject(); };
    saveButton_.onClick = [this] { saveProject(); };
    undoButton_.onClick = [this] { undo(); };
    redoButton_.onClick = [this] { redo(); };
    playButton_.onClick = [this] { transport_.play(); };
    stopButton_.onClick = [this] { transport_.stop(); };
    recordButton_.onClick = [this] {
        if (transport_.state() == bandforge::TransportState::Recording) {
            transport_.stop();
            stopRecording();
        } else {
            startRecording();
            transport_.record();
        }
        refreshViews();
    };
    addMidiButton_.onClick = [this] { addMidiTrack(); };
    addAudioButton_.onClick = [this] { addAudioTrack(); };
    loopButton_.onClick = [this] {
        transport_.setLoop(loopButton_.getToggleState(), transport_.loopStartBeat(), std::max(transport_.loopEndBeat(), project_.durationBeats()));
        refreshViews();
    };
    metronomeButton_.onClick = [this] {
        metronomeEnabled_ = metronomeButton_.getToggleState();
        refreshViews();
    };
    snapButton_.onClick = [this] {
        grid_.snapEnabled = snapButton_.getToggleState();
        refreshViews();
    };
    zoomOutButton_.onClick = [this] {
        grid_.pixelsPerBeat = std::max(36.0, grid_.pixelsPerBeat * 0.82);
        refreshViews();
    };
    zoomInButton_.onClick = [this] {
        grid_.pixelsPerBeat = std::min(160.0, grid_.pixelsPerBeat * 1.22);
        refreshViews();
    };
    exportButton_.onClick = [this] { exportWav(); };
    timeline_->onSeek = [this](double beat) {
        transport_.setPositionBeat(beat);
        refreshViews();
    };
    timeline_->onSelectTrack = [this](bandforge::TrackId trackId) { setSelectedTrack(trackId); };
    timeline_->onSelectClip = [this](bandforge::TrackId trackId, bandforge::ClipId clipId) { setSelectedClip(trackId, clipId); };
    trackList_->onSelectTrack = [this](bandforge::TrackId trackId) { setSelectedTrack(trackId); };

    trackList_->onDeleteTrack = [this](bandforge::TrackId trackId) {
        auto* track = project_.findTrack(trackId);
        if (track == nullptr || track->kind == bandforge::TrackKind::Master) return;
        history_.remember(project_);

        // Clear DSP state & per-track caches before erasing the track.
        audioEngine_.resetEqState(trackId);
        audioEngine_.resetTrackFx(trackId);
        {
            std::lock_guard<std::mutex> lock(pluginMutex_);
            trackPlugins_.erase(trackId);
        }
        if (auto pw = pluginWindows_.find(trackId); pw != pluginWindows_.end()) {
            pw->second.reset();
            pluginWindows_.erase(pw);
        }
        trackDisplayLevels_.erase(trackId);
        if (selection_.selectedTrackId == trackId) {
            selection_.selectedTrackId = 0;
            selection_.selectedClipId = 0;
        }
        if (recordTargetTrack_ == trackId)        recordTargetTrack_ = 0;
        if (midiRecordTargetTrack_ == trackId)    midiRecordTargetTrack_ = 0;

        project_.removeTrack(trackId);
        refreshViews();
    };

    trackList_->onAddTrackEffect = [this](bandforge::TrackId trackId, std::string fxType) {
        auto* track = project_.findTrack(trackId);
        if (track == nullptr) return;
        history_.remember(project_);
        audioEngine_.resetTrackFx(trackId);

        bandforge::EffectSlot slot;
        slot.id   = fxType + "-" + std::to_string(trackId) + "-" + std::to_string(track->mixer.effects.size());
        slot.type = fxType;
        if (fxType == "echo") {
            slot.name = "Echo";
            slot.parameters = { {"timeSec", 0.32}, {"feedback", 0.35}, {"mix", 0.35} };
        } else if (fxType == "reverb") {
            slot.name = "Hall Reverb";
            slot.parameters = { {"size", 0.75}, {"damp", 0.4}, {"mix", 0.3} };
        } else if (fxType == "distortion") {
            slot.name = "Drive";
            slot.parameters = { {"drive", 6.0}, {"mix", 0.6} };
        } else if (fxType == "telephone") {
            slot.name = "Telephone";
            slot.parameters = { {"mix", 1.0} };
        } else if (fxType == "megaphone") {
            slot.name = "Megaphone";
            slot.parameters = { {"drive", 8.0}, {"mix", 1.0} };
        } else {
            return;
        }
        track->mixer.effects.push_back(std::move(slot));
        refreshViews();
    };

    trackList_->onClearTrackEffects = [this](bandforge::TrackId trackId) {
        auto* track = project_.findTrack(trackId);
        if (track == nullptr) return;
        history_.remember(project_);
        audioEngine_.resetTrackFx(trackId);
        // Keep EQ; remove every other insert effect.
        track->mixer.effects.erase(std::remove_if(
            track->mixer.effects.begin(), track->mixer.effects.end(),
            [](const bandforge::EffectSlot& s) { return s.type != "eq"; }),
            track->mixer.effects.end());
        refreshViews();
    };

    trackList_->onApplyVoicePreset = [this](bandforge::TrackId trackId, std::string preset) {
        auto* track = project_.findTrack(trackId);
        if (track == nullptr) return;
        history_.remember(project_);
        audioEngine_.resetTrackFx(trackId);

        // Wipe existing voice-fx (keep EQ).
        track->mixer.effects.erase(std::remove_if(
            track->mixer.effects.begin(), track->mixer.effects.end(),
            [](const bandforge::EffectSlot& s) { return s.type != "eq"; }),
            track->mixer.effects.end());

        const auto addFx = [&](const char* type, const char* name,
                               std::initializer_list<std::pair<std::string, double>> params) {
            bandforge::EffectSlot slot;
            slot.id   = std::string(type) + "-" + std::to_string(trackId) + "-" + std::to_string(track->mixer.effects.size());
            slot.type = type;
            slot.name = name;
            for (const auto& p : params) slot.parameters[p.first] = p.second;
            track->mixer.effects.push_back(std::move(slot));
        };

        if (preset == "clean") {
            // already cleared
        } else if (preset == "classic-vocal") {
            addFx("reverb", "Plate Reverb", { {"size", 0.7}, {"damp", 0.5}, {"mix", 0.28} });
        } else if (preset == "telephone") {
            addFx("telephone", "Telephone", { {"mix", 1.0} });
        } else if (preset == "megaphone") {
            addFx("megaphone", "Megaphone", { {"drive", 9.0}, {"mix", 1.0} });
            addFx("reverb", "Small Room",   { {"size", 0.55}, {"damp", 0.55}, {"mix", 0.18} });
        } else if (preset == "distorted-lead") {
            addFx("distortion", "Drive", { {"drive", 8.0}, {"mix", 0.7} });
            addFx("reverb",     "Hall",  { {"size", 0.8},  {"damp", 0.4}, {"mix", 0.22} });
        } else if (preset == "echo-reverb") {
            addFx("echo",   "Echo", { {"timeSec", 0.38}, {"feedback", 0.4}, {"mix", 0.3} });
            addFx("reverb", "Hall", { {"size", 0.78}, {"damp", 0.45}, {"mix", 0.25} });
        }
        refreshViews();
    };

    // ── Plugin hosting setup ───────────────────────────────────────────────
    formatManager_.addDefaultFormats(); // registers VST3 + LV2 on Linux

    // Wire JUCE AudioFormatManager as the audio loader for AudioEngine
    audioEngine_.juceLoader = [this](const std::string& path) -> bandforge::AudioEngine::CachedAudioClip {
        juce::AudioFormatManager mgr;
        mgr.registerBasicFormats();
        const juce::File f(juce::String::fromUTF8(path.c_str()));
        std::unique_ptr<juce::AudioFormatReader> reader(mgr.createReaderFor(f));
        if (!reader) return {};
        bandforge::AudioEngine::CachedAudioClip clip;
        clip.sampleRate = static_cast<int>(reader->sampleRate);
        clip.channels   = static_cast<int>(std::min(reader->numChannels, 2u));
        const auto numFrames = static_cast<int>(reader->lengthInSamples);
        juce::AudioBuffer<float> buf(clip.channels, numFrames);
        reader->read(&buf, 0, numFrames, 0, true, clip.channels > 1);
        clip.samples.resize(static_cast<std::size_t>(numFrames * clip.channels));
        for (int ch = 0; ch < clip.channels; ++ch)
            for (int i = 0; i < numFrames; ++i)
                clip.samples[static_cast<std::size_t>(i * clip.channels + ch)] = buf.getSample(ch, i);
        return clip;
    };

    pluginsButton_.setLookAndFeel(toolbarLaf_.get());
    pluginsButton_.setTooltip("Browse & load VST3/LV2 plugins");
    pluginsButton_.onClick = [this] { openPluginBrowser(); };
    addAndMakeVisible(pluginsButton_);

    settingsButton_.setLookAndFeel(toolbarLaf_.get());
    settingsButton_.setTooltip("Audio & MIDI device settings");
    settingsButton_.onClick = [this] { openDeviceSettings(); };
    addAndMakeVisible(settingsButton_);

    // ── Recording setup ────────────────────────────────────────────────────
    audioRecorder_ = std::make_unique<AudioRecorder>();

    setAudioChannels(2, 2); // stereo in + out for recording
    deviceManager.addAudioCallback(audioRecorder_.get());

    // Enable all available MIDI inputs and register ourselves as callback
    for (const auto& dev : juce::MidiInput::getAvailableDevices()) {
        deviceManager.setMidiInputDeviceEnabled(dev.identifier, true);
        deviceManager.addMidiInputDeviceCallback(dev.identifier, this);
    }
    midiCollector_.reset(currentSampleRate_);

    setWantsKeyboardFocus(true);
    setSize(1280, 800);
    startTimerHz(30);
    refreshViews();
}

MainComponent::~MainComponent()
{
    pluginScanPool_.removeAllJobs(true, 5000);

    for (const auto& dev : juce::MidiInput::getAvailableDevices()) {
        deviceManager.removeMidiInputDeviceCallback(dev.identifier, this);
    }
    if (audioRecorder_) {
        deviceManager.removeAudioCallback(audioRecorder_.get());
        audioRecorder_->stopRecording();
    }

    // Destroy plugin instances before shutting down audio
    {
        const std::lock_guard<std::mutex> lock(pluginMutex_);
        pluginWindows_.clear();
        trackPlugins_.clear();
    }

    for (auto* btn : { &openButton_, &saveButton_, &undoButton_, &redoButton_,
                       &playButton_, &stopButton_, &recordButton_,
                       &addMidiButton_, &addAudioButton_, &loopButton_,
                       &metronomeButton_, &snapButton_, &zoomOutButton_,
                       &zoomInButton_, &exportButton_, &pluginsButton_, &settingsButton_ }) {
        btn->setLookAndFeel(nullptr);
    }
    shutdownAudio();
}

void MainComponent::prepareToPlay(int samplesPerBlockExpected, double sampleRate)
{
    currentSampleRate_ = sampleRate;
    audioEngine_.prepare({ sampleRate, samplesPerBlockExpected, 2 });
    midiCollector_.reset(sampleRate);

    // Prepare any loaded plugin instances
    const std::lock_guard<std::mutex> lock(pluginMutex_);
    for (auto& [tid, tp] : trackPlugins_) {
        if (tp && tp->instance) {
            tp->instance->prepareToPlay(sampleRate, samplesPerBlockExpected);
            tp->pluginBuffer.setSize(2, samplesPerBlockExpected);
            tp->prepared = true;
        }
    }
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    auto* buffer = bufferToFill.buffer;
    if (buffer == nullptr) {
        return;
    }

    buffer->clear(bufferToFill.startSample, bufferToFill.numSamples);
    const int channels = std::min(2, buffer->getNumChannels());
    audioEngine_.prepare({ currentSampleRate_, bufferToFill.numSamples, channels });
    renderScratch_.assign(static_cast<std::size_t>(bufferToFill.numSamples * channels), 0.0f);

    // ── MIDI input → live notes + recording ──────────────────────────────────
    juce::MidiBuffer midiIn;
    midiCollector_.removeNextBlockOfMessages(midiIn, bufferToFill.numSamples);
    for (const auto meta : midiIn) {
        const auto msg = meta.getMessage();
        if (msg.isNoteOn()) {
            audioEngine_.noteOn(msg.getNoteNumber(), msg.getVelocity(), selectedTrackKind());
            const std::lock_guard<std::mutex> lock(activeNoteKeysMutex_);
            activeNoteKeys_[-(msg.getNoteNumber() + 1)] = msg.getNoteNumber();
        } else if (msg.isNoteOff()) {
            audioEngine_.noteOff(msg.getNoteNumber());
            const std::lock_guard<std::mutex> lock(activeNoteKeysMutex_);
            activeNoteKeys_.erase(-(msg.getNoteNumber() + 1));
        }
    }

    const bool recording = (transport_.state() == bandforge::TransportState::Recording);

    const bool playing = (transport_.state() != bandforge::TransportState::Stopped);

    const bandforge::TempoMap tempoMap(project_);

    if (playing) {
        const double startBeat = transport_.positionBeat();
        audioEngine_.renderPreview(project_, startBeat, renderScratch_);

        // ── Record incoming MIDI with beat-stamped positions ─────────────────
        if (recording && midiRecording_) {
            const double beatsPerSec = project_.bpmAt(startBeat) / 60.0;
            const std::lock_guard<std::mutex> sl(midiRecordMutex_);
            for (const auto meta : midiIn) {
                const double sampleBeat = startBeat + (static_cast<double>(meta.samplePosition) / currentSampleRate_) * beatsPerSec;
                recordedMidi_.push_back({ meta.getMessage(), sampleBeat - recordStartBeat_ });
            }
        }

        if (metronomeEnabled_) {
            constexpr double Pi = 3.14159265358979323846;
            const double beatsPerSecond = project_.bpmAt(startBeat) / 60.0;
            const double endBeat = startBeat + static_cast<double>(bufferToFill.numSamples) / currentSampleRate_ * beatsPerSecond;
            const int firstBeat = static_cast<int>(std::ceil(startBeat - 1e-9));
            for (int beatNum = firstBeat; static_cast<double>(beatNum) <= endBeat; ++beatNum) {
                const double beatOffsetSec = (static_cast<double>(beatNum) - startBeat) / beatsPerSecond;
                const int sampleIdx = static_cast<int>(beatOffsetSec * currentSampleRate_);
                if (sampleIdx < 0 || sampleIdx >= bufferToFill.numSamples) continue;
                const bool isDownbeat = (beatNum % 4 == 0);
                const double freq = isDownbeat ? 1800.0 : 1000.0;
                const double gain = isDownbeat ? 0.35 : 0.22;
                const int clickLen = std::min(static_cast<int>(currentSampleRate_ * 0.025), bufferToFill.numSamples - sampleIdx);
                for (int s = 0; s < clickLen; ++s) {
                    const double t = static_cast<double>(s) / currentSampleRate_;
                    const float click = static_cast<float>(std::sin(2.0 * Pi * freq * t) * std::exp(-t * 200.0) * gain);
                    for (int ch = 0; ch < channels; ++ch) {
                        renderScratch_[(static_cast<std::size_t>(sampleIdx + s) * static_cast<std::size_t>(channels)) + static_cast<std::size_t>(ch)] += click;
                    }
                }
            }
        }

        lastPositionBeat_ = startBeat;
        transport_.advance(tempoMap, static_cast<double>(bufferToFill.numSamples) / currentSampleRate_);
    } else {
        lastPositionBeat_ = -1.0;
    }

    // ── VST3/LV2 instrument plugins (live keyboard → plugin → latency-comp mix) ─
    {
        const std::lock_guard<std::mutex> lock(pluginMutex_);
        for (auto& [tid, tp] : trackPlugins_) {
            if (!tp || !tp->instance || !tp->prepared) continue;
            tp->pluginBuffer.clear();
            tp->pluginBuffer.setSize(2, bufferToFill.numSamples, false, false, true);
            tp->instance->processBlock(tp->pluginBuffer, midiIn);
            const float pluginGain = 0.7f;
            const int latency = std::max(0, tp->latencySamples);

            // Refresh delay buffer capacity if latency changed
            const int delayCap = std::max(1, latency + bufferToFill.numSamples + 1);
            for (auto& db : tp->delayBuffers) {
                if (static_cast<int>(db.size()) < delayCap) {
                    db.assign(static_cast<std::size_t>(delayCap), 0.0f);
                    tp->delayWritePos = 0;
                }
            }

            for (int ch = 0; ch < channels && ch < static_cast<int>(tp->delayBuffers.size()); ++ch) {
                const auto* pluginCh = tp->pluginBuffer.getReadPointer(
                    std::min(ch, tp->pluginBuffer.getNumChannels() - 1));
                auto& db = tp->delayBuffers[static_cast<std::size_t>(ch)];
                const int cap = static_cast<int>(db.size());

                for (int s = 0; s < bufferToFill.numSamples; ++s) {
                    // Write plugin output into delay ring
                    db[static_cast<std::size_t>((tp->delayWritePos + s) % cap)] = pluginCh[s];
                    // Read from (latency) samples ago
                    const int readPos = ((tp->delayWritePos + s - latency) % cap + cap) % cap;
                    renderScratch_[(static_cast<std::size_t>(s) * static_cast<std::size_t>(channels)) + static_cast<std::size_t>(ch)]
                        += db[static_cast<std::size_t>(readPos)] * pluginGain;
                }
            }
            tp->delayWritePos = (tp->delayWritePos + bufferToFill.numSamples)
                % std::max(1, static_cast<int>(tp->delayBuffers.empty() ? 1 : tp->delayBuffers[0].size()));
        }
    }

    // ── Live keyboard notes (built-in synthesis) ──────────────────────────────
    audioEngine_.renderAndAdvanceLiveNotes(renderScratch_);

    for (int ch = 0; ch < channels; ++ch) {
        auto* write = buffer->getWritePointer(ch, bufferToFill.startSample);
        for (int sample = 0; sample < bufferToFill.numSamples; ++sample) {
            write[sample] = std::clamp(
                renderScratch_[(static_cast<std::size_t>(sample) * static_cast<std::size_t>(channels)) + static_cast<std::size_t>(ch)],
                -1.0f, 1.0f);
        }
    }
}

void MainComponent::releaseResources()
{
    renderScratch_.clear();
    const std::lock_guard<std::mutex> lock(pluginMutex_);
    for (auto& [tid, tp] : trackPlugins_) {
        if (tp && tp->instance) {
            tp->instance->releaseResources();
            tp->prepared = false;
        }
    }
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();
    auto top = bounds.removeFromTop(64).reduced(10, 8);
    auto bottom = bounds.removeFromBottom(230);
    auto left = bounds.removeFromLeft(216);
    auto right = bounds.removeFromRight(330);

    openButton_.setBounds(top.removeFromLeft(58).reduced(3, 4));
    saveButton_.setBounds(top.removeFromLeft(58).reduced(3, 4));
    undoButton_.setBounds(top.removeFromLeft(54).reduced(3, 4));
    redoButton_.setBounds(top.removeFromLeft(54).reduced(3, 4));
    top.removeFromLeft(10);
    playButton_.setBounds(top.removeFromLeft(58).reduced(3, 4));
    stopButton_.setBounds(top.removeFromLeft(58).reduced(3, 4));
    recordButton_.setBounds(top.removeFromLeft(54).reduced(3, 4));
    top.removeFromLeft(12);
    addMidiButton_.setBounds(top.removeFromLeft(80).reduced(3, 4));
    addAudioButton_.setBounds(top.removeFromLeft(84).reduced(3, 4));
    top.removeFromLeft(10);
    loopButton_.setBounds(top.removeFromLeft(66).reduced(3, 4));
    metronomeButton_.setBounds(top.removeFromLeft(66).reduced(3, 4));
    snapButton_.setBounds(top.removeFromLeft(58).reduced(3, 4));
    zoomOutButton_.setBounds(top.removeFromLeft(36).reduced(3, 4));
    zoomInButton_.setBounds(top.removeFromLeft(36).reduced(3, 4));
    exportButton_.setBounds(top.removeFromLeft(76).reduced(3, 4));
    pluginsButton_.setBounds(top.removeFromLeft(76).reduced(3, 4));
    settingsButton_.setBounds(top.removeFromLeft(72).reduced(3, 4));
    tempoLabel_.setBounds(top.removeFromRight(112).reduced(4, 4));
    positionLabel_.setBounds(top.removeFromRight(144).reduced(4, 4));
    recordingStatusLabel_.setBounds(top.removeFromRight(150).reduced(4, 4));

    trackList_->setBounds(left);
    timeline_->setBounds(bounds);
    libraryPanel_->setBounds(right);
    editorPanel_->setBounds(bottom);
}

void MainComponent::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff11151b));
    graphics.setColour(juce::Colour(0xff202731));
    graphics.fillRect(0, 0, getWidth(), 64);
}

void MainComponent::timerCallback()
{
    // Update per-track VU meter levels
    const double beat = transport_.positionBeat();
    const bool isPlaying = (transport_.state() != bandforge::TransportState::Stopped);
    const bool anyTrackSoloed = std::any_of(project_.tracks.begin(), project_.tracks.end(),
        [](const bandforge::Track& t) { return t.mixer.solo; });

    for (const auto& track : project_.tracks) {
        float& level = trackDisplayLevels_[track.id];
        const bool audible = !track.mixer.muted && (!anyTrackSoloed || track.mixer.solo);
        if (isPlaying && audible) {
            bool hasActiveClip = std::any_of(track.clips.begin(), track.clips.end(),
                [&](const bandforge::Clip& c) { return !c.muted && c.range().contains(beat); });
            if (hasActiveClip) {
                level = std::min(1.0f, static_cast<float>(bandforge::mixer::dbToLinear(track.mixer.volumeDb)));
            } else {
                level *= 0.82f;
            }
        } else {
            level *= 0.82f;
        }
        if (level < 0.01f) level = 0.0f;
    }

    // Auto-save every 2 minutes (timer runs at 30 Hz → 3600 ticks)
    ++autoSaveCounterTicks_;
    if (autoSaveCounterTicks_ >= 3600) {
        autoSaveCounterTicks_ = 0;
        try {
            const auto autoSaveDir = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                .getChildFile(juce::String::fromUTF8(project_.name.c_str()) + ".bandforge.autosave");
            project_.saveBundle(autoSaveDir.getFullPathName().toStdString());
        } catch (...) {}
    }

    refreshViews();
}

void MainComponent::refreshViews()
{
    undoButton_.setEnabled(history_.canUndo());
    redoButton_.setEnabled(history_.canRedo());
    loopButton_.setToggleState(transport_.loopEnabled(), juce::dontSendNotification);
    metronomeButton_.setToggleState(metronomeEnabled_, juce::dontSendNotification);
    snapButton_.setToggleState(grid_.snapEnabled, juce::dontSendNotification);
    positionLabel_.setText(formatBeat(transport_.positionBeat()), juce::dontSendNotification);
    if (!tempoLabel_.isBeingEdited()) {
        tempoLabel_.setText(juce::String(static_cast<int>(std::round(project_.bpmAt(0.0)))) + " BPM",
                            juce::dontSendNotification);
    }

    if (transport_.state() == bandforge::TransportState::Recording) {
        bandforge::TrackKind targetKind = selectedTrackKind();
        juce::String targetName = juce::String::fromUTF8(bandforge::displayName(targetKind).c_str());
        if (midiRecordTargetTrack_ != 0) {
            if (const auto* track = project_.findTrack(midiRecordTargetTrack_)) {
                targetKind = track->kind;
                targetName = juce::String::fromUTF8(track->name.c_str());
            }
        }
        recordingStatusLabel_.setText("REC " + targetName + "  V" + juce::String(keyboardVelocity_),
            juce::dontSendNotification);
        recordingStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffffc7c7));
    } else {
        recordingStatusLabel_.setText("VEL " + juce::String(keyboardVelocity_), juce::dontSendNotification);
        recordingStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffaeb9c8));
    }
    repaint();
    if (trackList_) {
        trackList_->refresh();
    }
    if (timeline_) {
        timeline_->repaint();
    }
    if (editorPanel_) {
        editorPanel_->repaint();
    }
}

void MainComponent::setSelectedTrack(bandforge::TrackId trackId)
{
    selection_.selectedTrackId = trackId;
    if (selection_.selectedClipId != 0 && project_.findClip(trackId, selection_.selectedClipId) == nullptr) {
        selection_.selectedClipId = 0;
    }
    refreshViews();
}

void MainComponent::setSelectedClip(bandforge::TrackId trackId, bandforge::ClipId clipId)
{
    selection_.selectedTrackId = trackId;
    selection_.selectedClipId = clipId;
    refreshViews();
}

void MainComponent::openProject()
{
    projectChooser_ = std::make_unique<juce::FileChooser>(
        "Open BandForge Project",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),
        "*.bforge;*.bandforge;project.json");

    projectChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories,
        [this](const juce::FileChooser& chooser) {
            auto file = chooser.getResult();
            if (file == juce::File {}) {
                return;
            }

            const bool selectedProjectJson = file.getFileName() == "project.json";
            if (selectedProjectJson) {
                file = file.getParentDirectory();
            }

            try {
                project_ = (selectedProjectJson || file.isDirectory())
                    ? bandforge::Project::loadBundle(file.getFullPathName().toStdString())
                    : bandforge::Project::loadFile(file.getFullPathName().toStdString());
                history_.clear();
                selection_ = {};
                {
                    const std::lock_guard<std::mutex> lock(activeNoteKeysMutex_);
                    activeNoteKeys_.clear();
                }
                audioEngine_.clearLiveNotes();
                transport_.stop();
                transport_.rewind();
                refreshViews();
            } catch (const std::exception& error) {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Open failed", error.what());
            }
        });
}

void MainComponent::saveProject()
{
    projectChooser_ = std::make_unique<juce::FileChooser>(
        "Save BandForge Project",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile(juce::String::fromUTF8(project_.name.c_str()) + bandforge::Project::ProjectFileExtension),
        "*.bforge;*.bandforge");

    projectChooser_->launchAsync(
        juce::FileBrowserComponent::saveMode,
        [this](const juce::FileChooser& chooser) {
            auto file = chooser.getResult();
            if (file == juce::File {}) {
                return;
            }

            try {
                if (file.hasFileExtension(".bandforge")) {
                    project_.saveBundle(file.getFullPathName().toStdString());
                } else {
                    if (!file.hasFileExtension(bandforge::Project::ProjectFileExtension)) {
                        file = file.withFileExtension(bandforge::Project::ProjectFileExtension);
                    }
                    project_.saveFile(file.getFullPathName().toStdString());
                }
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Project saved", file.getFullPathName());
            } catch (const std::exception& error) {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Save failed", error.what());
            }
        });
}

void MainComponent::undo()
{
    if (history_.undo(project_)) {
        transport_.stop();
        refreshViews();
    }
}

void MainComponent::redo()
{
    if (history_.redo(project_)) {
        transport_.stop();
        refreshViews();
    }
}

void MainComponent::addMidiTrack()
{
    auto* picker = new InstrumentPickerComponent();
    picker->setSize(900, 460);
    juce::Component::SafePointer<InstrumentPickerComponent> safePicker(picker);
    picker->onPicked = [this, safePicker](bandforge::TrackKind kind) mutable {
        if (safePicker != nullptr) {
            if (auto* dialog = safePicker->findParentComponentOfClass<juce::DialogWindow>()) {
                dialog->exitModalState(0);
            }
        }
        createMidiTrack(kind);
    };

    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Choose an Instrument";
    options.content.setOwned(picker);
    options.componentToCentreAround = this;
    options.escapeKeyTriggersCloseButton = true;
    options.resizable = false;
    options.dialogBackgroundColour = juce::Colour(0xff161b22);
    options.launchAsync();
}

void MainComponent::createMidiTrack(bandforge::TrackKind kind)
{
    history_.remember(project_);
    auto& track = project_.addTrack(kind, bandforge::displayName(kind) + " " + std::to_string(project_.tracks.size() + 1));
    track.mixer.recordArmed = true;
    auto& clip = project_.addMidiClip(track.id, bandforge::displayName(kind) + " Pattern", std::max(0.0, grid_.snap(transport_.positionBeat())), isDrumTrackKind(kind) ? 8.0 : 4.0);
    clip.midi = bandforge::defaultStarterClipForTrackKind(kind);
    selection_.selectedTrackId = track.id;
    selection_.selectedClipId = clip.id;
    refreshViews();
}

void MainComponent::addAudioTrack()
{
    history_.remember(project_);
    auto& track = project_.addTrack(bandforge::TrackKind::Audio, "Audio " + std::to_string(project_.tracks.size() + 1));
    track.mixer.recordArmed = true;
    refreshViews();
}

// ── MidiInputCallback: routes hardware MIDI into collector ───────────────────
void MainComponent::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& msg)
{
    midiCollector_.addMessageToQueue(msg);
}

void MainComponent::recordMusicalTypingMessage(const juce::MidiMessage& msg)
{
    if (transport_.state() != bandforge::TransportState::Recording) {
        return;
    }

    if (!msg.isNoteOnOrOff()) {
        return;
    }

    if ((!midiRecording_ || midiRecordTargetTrack_ == 0) && ensureMidiRecordTarget(true) == 0) {
        return;
    }

    const double beatPosition = std::max(0.0, transport_.positionBeat() - recordStartBeat_);
    bool firstRecordedMessage = false;
    {
        const std::lock_guard<std::mutex> sl(midiRecordMutex_);
        firstRecordedMessage = recordedMidi_.empty();
    }

    if (firstRecordedMessage) {
        const auto preferred = chooseMusicalTypingRecordTarget();
        if (preferred != 0) {
            midiRecordTargetTrack_ = preferred;
            armMidiRecordTarget(midiRecordTargetTrack_, true);
        }
    }

    const std::lock_guard<std::mutex> sl(midiRecordMutex_);
    recordedMidi_.push_back({ msg, beatPosition });
    musicalTypingRecorded_ = true;
}

void MainComponent::releaseActiveMusicalTypingKeys()
{
    std::set<int> pitchesToRelease;
    {
        const std::lock_guard<std::mutex> lock(activeNoteKeysMutex_);
        for (auto it = activeNoteKeys_.begin(); it != activeNoteKeys_.end();) {
            if (it->first >= 0) {
                pitchesToRelease.insert(it->second);
                it = activeNoteKeys_.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (const auto pitch : pitchesToRelease) {
        audioEngine_.noteOff(pitch);
        recordMusicalTypingMessage(juce::MidiMessage::noteOff(1, pitch));
    }
}

bandforge::TrackId MainComponent::chooseMidiRecordTarget() const
{
    for (const auto& track : project_.tracks) {
        if (track.mixer.recordArmed && bandforge::isMidiTrackKind(track.kind)) {
            return track.id;
        }
    }

    if (selection_.selectedTrackId != 0) {
        if (const auto* selected = project_.findTrack(selection_.selectedTrackId);
            selected != nullptr && bandforge::isMidiTrackKind(selected->kind)) {
            return selected->id;
        }
    }

    for (const auto& track : project_.tracks) {
        if (bandforge::isMidiTrackKind(track.kind)) {
            return track.id;
        }
    }

    return 0;
}

bandforge::TrackId MainComponent::chooseMusicalTypingRecordTarget() const
{
    if (selection_.selectedTrackId != 0) {
        if (const auto* selected = project_.findTrack(selection_.selectedTrackId);
            selected != nullptr && bandforge::isMidiTrackKind(selected->kind)) {
            return selected->id;
        }
    }

    if (midiRecordTargetTrack_ != 0) {
        if (const auto* current = project_.findTrack(midiRecordTargetTrack_);
            current != nullptr && bandforge::isMidiTrackKind(current->kind)) {
            return current->id;
        }
    }

    return chooseMidiRecordTarget();
}

bandforge::TrackId MainComponent::ensureMidiRecordTarget(bool preferSelectedTrack)
{
    if (midiRecordTargetTrack_ != 0) {
        return midiRecordTargetTrack_;
    }

    midiRecordTargetTrack_ = preferSelectedTrack ? chooseMusicalTypingRecordTarget() : chooseMidiRecordTarget();
    if (midiRecordTargetTrack_ == 0) {
        history_.remember(project_);
        auto& track = project_.addTrack(bandforge::TrackKind::Keys, "Musical Typing");
        track.mixer.recordArmed = true;
        midiRecordTargetTrack_ = track.id;
        selection_.selectedTrackId = track.id;
        selection_.selectedClipId = 0;
        refreshViews();
    }

    if (midiRecordTargetTrack_ != 0) {
        armMidiRecordTarget(midiRecordTargetTrack_, true);
        midiRecording_ = true;
    }
    return midiRecordTargetTrack_;
}

void MainComponent::armMidiRecordTarget(bandforge::TrackId trackId, bool remember)
{
    if (trackId == 0) {
        return;
    }

    const auto* target = project_.findTrack(trackId);
    if (target == nullptr || !bandforge::isMidiTrackKind(target->kind)) {
        return;
    }

    bool changed = false;
    for (const auto& track : project_.tracks) {
        if (!bandforge::isMidiTrackKind(track.kind)) {
            continue;
        }

        const bool shouldArm = track.id == trackId;
        if (track.mixer.recordArmed != shouldArm) {
            changed = true;
            break;
        }
    }

    if (changed && remember) {
        history_.remember(project_);
    }

    for (auto& track : project_.tracks) {
        if (bandforge::isMidiTrackKind(track.kind)) {
            track.mixer.recordArmed = track.id == trackId;
        }
    }
}

void MainComponent::closeActiveMidiNotesForRecording()
{
    if (!midiRecording_ || recordedMidi_.empty()) {
        return;
    }

    const double beatPosition = std::max(0.0, transport_.positionBeat() - recordStartBeat_);
    std::vector<int> activePitches;
    {
        const std::lock_guard<std::mutex> lock(activeNoteKeysMutex_);
        activePitches.reserve(activeNoteKeys_.size());
        for (const auto& [_, pitch] : activeNoteKeys_) {
            activePitches.push_back(pitch);
        }
    }

    const std::lock_guard<std::mutex> sl(midiRecordMutex_);

    std::set<int> openPitches;
    for (const auto& tm : recordedMidi_) {
        if (tm.msg.isNoteOn()) {
            openPitches.insert(tm.msg.getNoteNumber());
        } else if (tm.msg.isNoteOff()) {
            openPitches.erase(tm.msg.getNoteNumber());
        }
    }

    for (const auto pitch : activePitches) {
        if (openPitches.find(pitch) != openPitches.end()) {
            recordedMidi_.push_back({ juce::MidiMessage::noteOff(1, pitch), beatPosition });
        }
    }
}

// ── Audio recording ──────────────────────────────────────────────────────────
void MainComponent::startRecording()
{
    // Find the first record-armed audio track
    recordTargetTrack_ = 0;
    for (const auto& track : project_.tracks) {
        if (track.mixer.recordArmed && track.kind == bandforge::TrackKind::Audio) {
            recordTargetTrack_ = track.id;
            break;
        }
    }

    const auto tmpDir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    recordingFile_ = juce::File();
    recordStartBeat_ = transport_.positionBeat();

    if (recordTargetTrack_ != 0 && audioRecorder_) {
        recordingFile_ = tmpDir.getChildFile("bandforge_rec_" + juce::String(juce::Time::currentTimeMillis()) + ".wav");
        audioRecorder_->startRecording(recordingFile_,
            currentSampleRate_, 2);
    }

    // MIDI recording: arm for the selected MIDI track too
    midiRecording_ = false;
    midiRecordTargetTrack_ = chooseMidiRecordTarget();
    musicalTypingRecorded_ = false;
    {
        const std::lock_guard<std::mutex> sl(midiRecordMutex_);
        recordedMidi_.clear();
    }
    if (midiRecordTargetTrack_ != 0) {
        armMidiRecordTarget(midiRecordTargetTrack_, true);
        midiRecording_ = true;
    }
}

void MainComponent::stopRecording()
{
    if (audioRecorder_) {
        audioRecorder_->stopRecording();
    }
    closeActiveMidiNotesForRecording();
    midiRecording_ = false;

    // ── Commit audio recording as a clip ─────────────────────────────────────
    if (recordTargetTrack_ != 0 && recordingFile_.existsAsFile() && recordingFile_.getSize() > 44) {
        const double durationSec = audioRecorder_ ? audioRecorder_->getRecordedSeconds() : 0.0;
        const double bpm = project_.bpmAt(recordStartBeat_);
        const double durationBeats = std::max(0.25, durationSec * bpm / 60.0);

        auto* targetTrack = project_.findTrack(recordTargetTrack_);
        if (targetTrack == nullptr) {
            history_.remember(project_);
            auto& newTrack = project_.addTrack(bandforge::TrackKind::Audio, "Recording");
            targetTrack = &newTrack;
        }

        // Copy temp WAV into the project bundle's Audio/ directory
        juce::File destFile;
        const auto bundlePath = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile(juce::String::fromUTF8(project_.name.c_str()) + ".bandforge")
            .getChildFile("Audio");
        if (bundlePath.isDirectory() || bundlePath.createDirectory()) {
            destFile = bundlePath.getChildFile(recordingFile_.getFileName());
            recordingFile_.copyFileTo(destFile);
        } else {
            destFile = recordingFile_; // fallback: keep temp path
        }

        history_.remember(project_);
        auto& clip = project_.addAudioClip(targetTrack->id,
            "Recording " + juce::Time::getCurrentTime().formatted("%H:%M:%S").toStdString(),
            destFile.getFullPathName().toStdString(),
            recordStartBeat_, durationBeats);
        clip.audio.stretchToProjectTempo = false;
        selection_.selectedTrackId = targetTrack->id;
        selection_.selectedClipId  = clip.id;
    }

    // ── Commit MIDI recording as a clip ──────────────────────────────────────
    std::vector<TimedMidiMsg> recordedMidi;
    {
        const std::lock_guard<std::mutex> sl(midiRecordMutex_);
        recordedMidi = recordedMidi_;
        recordedMidi_.clear();
    }

    if (!recordedMidi.empty()) {
        std::sort(recordedMidi.begin(), recordedMidi.end(), [](const TimedMidiMsg& a, const TimedMidiMsg& b) {
            return a.beatPosition < b.beatPosition;
        });

        bandforge::TrackId midiTargetId = midiRecordTargetTrack_;
        if (midiTargetId == 0) {
            midiTargetId = chooseMidiRecordTarget();
        }

        auto* midiTarget = project_.findTrack(midiTargetId);
        if (midiTarget != nullptr && bandforge::isMidiTrackKind(midiTarget->kind)) {
            double clipEnd = 0.0;
            bandforge::MidiClipData data;

            struct OpenNote {
                double startBeat = 0.0;
                int velocity = 100;
                int channel = 1;
            };
            std::map<int, OpenNote> open; // (channel,pitch) packed key

            const auto packedKey = [](const juce::MidiMessage& msg) {
                return (std::clamp(msg.getChannel(), 1, 16) << 8) | std::clamp(msg.getNoteNumber(), 0, 127);
            };

            const auto closeNote = [&](int key, double endBeat) {
                const auto it = open.find(key);
                if (it == open.end()) {
                    return;
                }

                bandforge::MidiNote note;
                note.pitch = key & 0xff;
                note.velocity = it->second.velocity;
                note.channel = it->second.channel;
                note.startBeat = std::max(0.0, it->second.startBeat);
                const double rawDuration = endBeat - note.startBeat;
                if (rawDuration < 0.015625) {
                    open.erase(it);
                    return;
                }
                note.durationBeats = std::max(0.03125, rawDuration);
                clipEnd = std::max(clipEnd, note.startBeat + note.durationBeats);
                data.notes.push_back(note);
                open.erase(it);
            };

            for (const auto& tm : recordedMidi) {
                const auto key = packedKey(tm.msg);
                if (tm.msg.isNoteOn()) {
                    closeNote(key, tm.beatPosition);
                    OpenNote openNote;
                    openNote.startBeat = std::max(0.0, tm.beatPosition);
                    openNote.velocity = std::clamp(static_cast<int>(tm.msg.getVelocity()), 1, 127);
                    openNote.channel = std::clamp(tm.msg.getChannel(), 1, 16);
                    open[key] = openNote;
                } else if (tm.msg.isNoteOff()) {
                    closeNote(key, tm.beatPosition);
                }
            }

            const double fallbackEnd = std::max(0.0, transport_.positionBeat() - recordStartBeat_);
            while (!open.empty()) {
                closeNote(open.begin()->first, fallbackEnd);
            }

            if (!data.notes.empty()) {
                std::sort(data.notes.begin(), data.notes.end(), [](const bandforge::MidiNote& a, const bandforge::MidiNote& b) {
                    if (a.startBeat == b.startBeat) {
                        return a.pitch < b.pitch;
                    }
                    return a.startBeat < b.startBeat;
                });

                const double firstNoteBeat = data.notes.front().startBeat;
                const double rawClipStart = recordStartBeat_ + firstNoteBeat;
                const double clipStart = grid_.snapEnabled && grid_.snapBeats > 0.0
                    ? grid_.snap(rawClipStart)
                    : rawClipStart;
                const double clipStartOffset = rawClipStart - clipStart;

                for (auto& note : data.notes) {
                    const double unsnappedLocalStart = std::max(0.0, note.startBeat - firstNoteBeat + clipStartOffset);
                    if (grid_.snapEnabled && grid_.snapBeats > 0.0) {
                        const double originalEnd = unsnappedLocalStart + note.durationBeats;
                        const double snappedStart = std::max(0.0, grid_.snap(unsnappedLocalStart));
                        note.startBeat = snappedStart;
                        note.durationBeats = std::max(0.03125, originalEnd - snappedStart);
                    } else {
                        note.startBeat = unsnappedLocalStart;
                    }
                }

                data.notes.erase(std::remove_if(data.notes.begin(), data.notes.end(), [](const bandforge::MidiNote& note) {
                    return note.durationBeats < 0.03125;
                }), data.notes.end());
                if (data.notes.empty()) {
                    midiRecordTargetTrack_ = 0;
                    musicalTypingRecorded_ = false;
                    refreshViews();
                    return;
                }

                double localEnd = 0.0;
                for (const auto& note : data.notes) {
                    localEnd = std::max(localEnd, note.startBeat + note.durationBeats);
                }

                std::sort(data.notes.begin(), data.notes.end(), [](const bandforge::MidiNote& a, const bandforge::MidiNote& b) {
                    if (a.startBeat == b.startBeat) {
                        return a.pitch < b.pitch;
                    }
                    return a.startBeat < b.startBeat;
                });

                const double rawLength = std::max(0.25, localEnd);
                const double snappedLength = (grid_.snapEnabled && grid_.snapBeats > 0.0)
                    ? std::ceil(rawLength / grid_.snapBeats) * grid_.snapBeats
                    : rawLength;

                const auto takeTime = juce::Time::getCurrentTime().formatted("%H:%M:%S").toStdString();
                const auto takePrefix = bandforge::displayName(midiTarget->kind) + (musicalTypingRecorded_ ? " Typing Take " : " MIDI Take ");

                history_.remember(project_);
                auto& clip = project_.addMidiClip(midiTargetId,
                    takePrefix + takeTime,
                    clipStart,
                    std::max(0.25, snappedLength));
                clip.midi = std::move(data);
                selection_.selectedTrackId = midiTargetId;
                selection_.selectedClipId = clip.id;
            }
        }
    }

    midiRecordTargetTrack_ = 0;
    musicalTypingRecorded_ = false;

    refreshViews();
}

// ── Plugin hosting ────────────────────────────────────────────────────────────
void MainComponent::openPluginBrowser()
{
    juce::Component::SafePointer<MainComponent> safeThis(this);
    const auto showMenu = [safeThis] {
        auto* self = safeThis.getComponent();
        if (self == nullptr) {
            return;
        }

        const auto types = self->knownPlugins_.getTypes();

        if (types.isEmpty()) {
            juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
                "No plugins found",
                "Place VST3 plugins in ~/.vst3 or /usr/lib/vst3\n"
                "and LV2 plugins in ~/.lv2 or /usr/lib/lv2, then click Plugins again.");
            return;
        }

        juce::PopupMenu menu;
        for (int i = 0; i < types.size(); ++i) {
            menu.addItem(i + 1, types[i].name + "  [" + types[i].pluginFormatName + "]");
        }

        const bandforge::TrackId targetTrack = self->selection_.selectedTrackId;
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(self->pluginsButton_),
            [safeThis, targetTrack, types](int result) {
                auto* owner = safeThis.getComponent();
                if (owner == nullptr) return;
                if (result <= 0 || result > types.size()) return;
                owner->loadPluginOnTrack(targetTrack, types[result - 1]);
            });
    };

    // Build search paths for all formats
    const juce::File home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
    juce::FileSearchPath paths;
    paths.add(home.getChildFile(".vst3"));
    paths.add(juce::File("/usr/lib/vst3"));
    paths.add(juce::File("/usr/local/lib/vst3"));
    paths.add(home.getChildFile(".lv2"));
    paths.add(juce::File("/usr/lib/lv2"));
    paths.add(juce::File("/usr/local/lib/lv2"));

    // Scan in a background thread, show menu when done
    struct ScanJob final : juce::ThreadPoolJob {
        ScanJob(juce::AudioPluginFormatManager& fmt, juce::KnownPluginList& known,
                juce::FileSearchPath p, std::function<void()> done)
            : juce::ThreadPoolJob("PluginScan")
            , fmt_(fmt), known_(known), paths_(std::move(p)), done_(std::move(done)) {}

        JobStatus runJob() override {
            const juce::File deadMansPedal; // empty = no dead-mans pedal file
            for (int fi = 0; fi < fmt_.getNumFormats(); ++fi) {
                auto* format = fmt_.getFormat(fi);
                juce::PluginDirectoryScanner scanner(known_, *format, paths_, true, deadMansPedal);
                juce::String pluginName;
                while (scanner.scanNextFile(true, pluginName)) {}
            }
            juce::MessageManager::callAsync(done_);
            return jobHasFinished;
        }
        juce::AudioPluginFormatManager& fmt_;
        juce::KnownPluginList& known_;
        juce::FileSearchPath paths_;
        std::function<void()> done_;
    };

    pluginScanPool_.addJob(new ScanJob(formatManager_, knownPlugins_, paths, showMenu), true);
}

void MainComponent::loadPluginOnTrack(bandforge::TrackId trackId, const juce::PluginDescription& desc)
{
    if (trackId == 0 || project_.findTrack(trackId) == nullptr) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "No track selected", "Select a track before loading a plugin.");
        return;
    }

    juce::String err;
    auto instance = formatManager_.createPluginInstance(desc, currentSampleRate_,
        static_cast<int>(currentSampleRate_ / 30), err);
    if (!instance) {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "Plugin load failed", err);
        return;
    }

    instance->prepareToPlay(currentSampleRate_, static_cast<int>(currentSampleRate_ / 30));

    auto tp = std::make_unique<TrackPlugin>();
    tp->pluginBuffer.setSize(2, static_cast<int>(currentSampleRate_ / 30));
    tp->latencySamples = instance->getLatencySamples();
    tp->instance = std::move(instance);
    tp->prepared = true;

    // Pre-allocate delay line (capacity = latency + 1 block, minimum 1)
    const int delayCap = std::max(1, tp->latencySamples + static_cast<int>(currentSampleRate_ / 30) + 1);
    tp->delayBuffers.assign(2, std::vector<float>(static_cast<std::size_t>(delayCap), 0.0f));
    tp->delayWritePos = 0;

    // Close any existing editor window for this track before opening a new one
    if (const auto it = pluginWindows_.find(trackId); it != pluginWindows_.end()) {
        it->second->setVisible(false);
        pluginWindows_.erase(it);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor;
    if (tp->instance->hasEditor()) {
        editor.reset(tp->instance->createEditor());
    }

    {
        const std::lock_guard<std::mutex> lock(pluginMutex_);
        trackPlugins_[trackId] = std::move(tp);
    }

    if (editor) {
        auto window = std::make_unique<PluginEditorWindow>(desc.name, juce::Colour(0xff1a2130));
        window->setContentOwned(editor.release(), true);
        window->setResizable(true, false);
        window->centreWithSize(window->getContentComponent()->getWidth(), window->getContentComponent()->getHeight());
        juce::Component::SafePointer<MainComponent> safeThis(this);
        window->onClose = [safeThis, trackId] {
            if (auto* owner = safeThis.getComponent()) {
                owner->pluginWindows_.erase(trackId);
            }
        };
        window->setVisible(true);
        pluginWindows_[trackId] = std::move(window);
    } else {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
            "Plugin loaded", desc.name + " loaded on selected track.");
    }
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    const bool cmd = key.getModifiers().isCommandDown();
    const bool shift = key.getModifiers().isShiftDown();
    const int kc = normaliseKeyCode(key.getKeyCode());

    // ── Global shortcuts ──────────────────────────────────────────────────────
    if (kc == juce::KeyPress::spaceKey && !cmd && !shift) {
        if (transport_.state() == bandforge::TransportState::Stopped) {
            transport_.play();
        } else {
            transport_.stop();
        }
        refreshViews();
        return true;
    }
    if (cmd && kc == 'Z') { shift ? redo() : undo(); return true; }
    if (cmd && kc == 'Y') { redo();         return true; }
    if (cmd && kc == 'S') { saveProject();  return true; }
    if (cmd && kc == 'O') { openProject();  return true; }
    if (cmd && kc == 'E') { exportWav();    return true; }

    // Delete or Backspace removes the selected track (Master is protected by handler).
    if ((kc == juce::KeyPress::deleteKey || kc == juce::KeyPress::backspaceKey)
        && !cmd && !shift
        && selection_.selectedTrackId != 0
        && trackList_ && trackList_->onDeleteTrack) {
        trackList_->onDeleteTrack(selection_.selectedTrackId);
        return true;
    }

    // ── Keyboard piano ────────────────────────────────────────────────────────
    if (cmd || shift) {
        return false;
    }

    if (const int velocity = velocityForNumberKey(kc); velocity > 0) {
        keyboardVelocity_ = velocity;
        refreshViews();
        return true;
    }

    // Octave shift (Z = down, X = up)
    if (kc == 'Z') {
        releaseActiveMusicalTypingKeys();
        keyboardOctave_ = std::max(0, keyboardOctave_ - 1);
        refreshViews();
        return true;
    }
    if (kc == 'X') {
        releaseActiveMusicalTypingKeys();
        keyboardOctave_ = std::min(8, keyboardOctave_ + 1);
        refreshViews();
        return true;
    }

    // Note key — ignore key-repeat by checking activeNoteKeys_
    const int semitone = keyCodeToSemitone(kc);
    bool shouldStartNote = false;
    if (semitone >= 0) {
        const std::lock_guard<std::mutex> lock(activeNoteKeysMutex_);
        if (activeNoteKeys_.find(kc) == activeNoteKeys_.end()) {
            const int pitch = std::clamp((keyboardOctave_ + 1) * 12 + semitone, 0, 127);
            activeNoteKeys_[kc] = pitch;
            shouldStartNote = true;
        }
    }

    if (shouldStartNote) {
        const int pitch = std::clamp((keyboardOctave_ + 1) * 12 + semitone, 0, 127);
        const int velocity = std::clamp(keyboardVelocity_, 1, 127);
        audioEngine_.noteOn(pitch, velocity, selectedTrackKind());
        recordMusicalTypingMessage(juce::MidiMessage::noteOn(1, pitch, static_cast<juce::uint8>(velocity)));
        return true;
    }

    return false;
}

bool MainComponent::keyStateChanged(bool)
{
    static const int kNoteKeys[] = { 'A','W','S','E','D','F','T','G','Y','H','U','J','K','O','L' };
    std::vector<int> pitchesToRelease;
    for (int kc : kNoteKeys) {
        if (isNormalisedKeyCurrentlyDown(kc)) {
            continue;
        }

        const std::lock_guard<std::mutex> lock(activeNoteKeysMutex_);
        if (auto it = activeNoteKeys_.find(kc); it != activeNoteKeys_.end()) {
            pitchesToRelease.push_back(it->second);
            activeNoteKeys_.erase(it);
        }
    }

    for (const auto pitch : pitchesToRelease) {
        audioEngine_.noteOff(pitch);
        recordMusicalTypingMessage(juce::MidiMessage::noteOff(1, pitch));
    }
    return false;
}

bandforge::TrackKind MainComponent::selectedTrackKind() const
{
    if (selection_.selectedTrackId != 0) {
        if (const auto* track = project_.findTrack(selection_.selectedTrackId)) {
            return track->kind;
        }
    }
    return bandforge::TrackKind::Keys;
}

void MainComponent::openDeviceSettings()
{
    // Build a dialog containing AudioDeviceSelectorComponent (covers audio + MIDI devices)
    auto* selector = new juce::AudioDeviceSelectorComponent(
        deviceManager,
        /*minAudioInputChannels=*/  0,
        /*maxAudioInputChannels=*/  2,
        /*minAudioOutputChannels=*/ 2,
        /*maxAudioOutputChannels=*/ 2,
        /*showMidiInputOptions=*/   true,
        /*showMidiOutputSelector=*/ true,
        /*showChannelsAsStereoPairs=*/ true,
        /*hideAdvancedOptionsWithButton=*/ false);

    selector->setSize(520, 420);

    auto* dw = new juce::DocumentWindow("Audio & MIDI Devices",
        juce::Colour(0xff1a2130), juce::DocumentWindow::closeButton, true);
    dw->setContentOwned(selector, true);
    dw->setResizable(false, false);
    dw->centreWithSize(520, 420);
    dw->setVisible(true);
}

void MainComponent::exportWav()
{
    exportChooser_ = std::make_unique<juce::FileChooser>(
        "Export WAV",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile(juce::String::fromUTF8(project_.name.c_str()) + ".wav"),
        "*.wav");

    exportChooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& chooser) {
            const auto file = chooser.getResult();
            if (file == juce::File {}) {
                return;
            }

            try {
                const double seconds = std::max(1.0, bandforge::TempoMap(project_).beatToSeconds(std::max(4.0, project_.durationBeats())));
                const int channels = 2;
                const int frames = static_cast<int>(seconds * currentSampleRate_);
                std::vector<float> samples(static_cast<std::size_t>(frames * channels), 0.0f);
                bandforge::AudioEngine offlineEngine;
                offlineEngine.prepare({ currentSampleRate_, 512, channels });
                offlineEngine.renderPreview(project_, 0.0, samples);
                bandforge::WavExporter::writeInterleavedFloat(
                    file.getFullPathName().toStdString(),
                    samples,
                    { static_cast<int>(currentSampleRate_), channels, 16 });

                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon, "Export complete", file.getFullPathName());
            } catch (const std::exception& error) {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon, "Export failed", error.what());
            }
        });
}

} // namespace bandforge_app
