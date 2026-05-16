#include "MainComponent.h"

#include "core/Exporter.h"
#include "core/Mixer.h"
#include "core/Timeline.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <functional>
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
            return bandforge::SoundLibrary::loadManifest(manifest);
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

// ─── Keyboard callbacks bundle ────────────────────────────────────────────────
struct KeyboardCallbacks {
    std::function<void(int, int)> noteOn;    // pitch, velocity
    std::function<void(int)>      noteOff;   // pitch
    std::function<int()>          getOctave;
    std::function<void(int)>      shiftOctave; // delta
    std::function<bool(int)>      isPitchActive;
};

// Maps a JUCE key code (uppercase letter) to semitones above C for piano keyboard.
// Layout: A-row = white keys, W-row = black keys (like GarageBand Musical Typing).
//   W  E     T  Y  U     O  L
//  A  S  D  F  G  H  J  K
//  C  D  E  F  G  A  B  C
static int keyCodeToSemitone(int kc)
{
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

bool presetIsInstrument(const bandforge::Preset& preset)
{
    return preset.instrumentType != "audio-effect-chain" && !presetIsDrumOrBeat(preset);
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

    int getNumRows() override
    {
        return static_cast<int>(project_.tracks.size());
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

        graphics.setColour(juce::Colour(0xffeef3fa));
        graphics.setFont(juce::FontOptions(13.0f, juce::Font::bold));
        graphics.drawText(track.name, row.withTrimmedLeft(14).withHeight(22), juce::Justification::centredLeft);

        graphics.setColour(juce::Colour(0xff91a0b4));
        graphics.setFont(juce::FontOptions(11.0f));
        juce::String detail = juce::String::fromUTF8(bandforge::toString(track.kind).c_str());
        graphics.drawText(detail, row.withTrimmedLeft(14).translated(0, 20).withHeight(18), juce::Justification::centredLeft);

        // VU meter bar
        const float level = getTrackLevel ? getTrackLevel(track.id) : 0.0f;
        if (level > 0.0f) {
            const auto meterArea = row.withTrimmedLeft(14).translated(0, 40).withHeight(5);
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
        if (clip.midi.notes.empty()) return;
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
                    mode_ == LibraryMode::Drums ? "Beat" : "Preset",
                    mode_ == LibraryMode::Drums ? juce::Colour(0xffffbd6f) : juce::Colour(0xffc7a6ff),
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

        tabs_.addTab("Piano Roll",      juce::Colour(0xff161b22), pianoRoll_.get(),       false);
        tabs_.addTab("Smart Controls",  juce::Colour(0xff161b22), smartControls_.get(),   false);
        tabs_.addTab("Musical Typing",  juce::Colour(0xff161b22), musicalKeyboard_.get(), false);
    }

    void resized() override
    {
        tabs_.setBounds(getLocalBounds().reduced(8));
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
            g.setColour(juce::Colour(0xffd7dde8));
            g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
            g.drawText("Musical Typing", header.reduced(12, 0), juce::Justification::centredLeft);

            // Octave display
            const int octave = cbs_.getOctave ? cbs_.getOctave() : 4;
            const juce::String octLabel = "C" + juce::String(octave);
            g.setColour(juce::Colour(0xff5fb3ff));
            g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
            g.drawText(octLabel, header.reduced(12, 0), juce::Justification::centredRight);

            // Octave shift buttons
            const int btnW = 28;
            const int btnH = 22;
            const int btnY = header.getCentreY() - btnH / 2;
            octDownBounds_ = juce::Rectangle<int>(header.getRight() - 80, btnY, btnW, btnH);
            octUpBounds_   = juce::Rectangle<int>(header.getRight() - 48, btnY, btnW, btnH);

            for (auto* b : { &octDownBounds_, &octUpBounds_ }) {
                g.setColour(juce::Colour(0xff2e3b4e));
                g.fillRoundedRectangle(b->toFloat(), 5.0f);
                g.setColour(juce::Colour(0xff5fb3ff));
                g.drawRoundedRectangle(b->toFloat(), 5.0f, 1.0f);
            }
            g.setColour(juce::Colour(0xffe8edf4));
            g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
            g.drawText(juce::CharPointer_UTF8("\xe2\x97\x84"), octDownBounds_, juce::Justification::centred);
            g.drawText(juce::CharPointer_UTF8("\xe2\x96\xba"), octUpBounds_,   juce::Justification::centred);

            area.reduce(0, 8);
            drawKeyboard(g, area);
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
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
                cbs_.noteOn(pitch, 100);
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
                    cbs_.noteOn(pitch, 100);
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
            const int baseNote = (octave) * 12; // MIDI C of octave

            // Draw white keys
            for (int oct = 0; oct < kNumOctaves; ++oct) {
                for (int w = 0; w < kWhitePerOctave; ++w) {
                    const int idx = oct * kWhitePerOctave + w;
                    const float x = area.getX() + idx * wkW;
                    const int pitch = baseNote + oct * 12 + kWhiteSemitone[w];
                    const int semitoneInLayout = oct * 12 + kWhiteSemitone[w];
                    const bool active = cbs_.isPitchActive && cbs_.isPitchActive(pitch);

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
                    const bool active = cbs_.isPitchActive && cbs_.isPitchActive(pitch);

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
            const int baseNote = octave * 12;

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
            configureSlider(volumeLabel_, volume_, -60.0, 6.0, "Volume (dB)");
            configureSlider(panLabel_, pan_, -1.0, 1.0, "Pan");
            configureSlider(reverbLabel_, reverbMix_, 0.0, 1.0, "Reverb Mix");

            volume_.onValueChange = [this] { applyMixer(); };
            pan_.onValueChange = [this] { applyMixer(); };
            reverbMix_.onValueChange = [this] { applyReverb(); };

            startTimerHz(15);
        }

        void resized() override
        {
            auto bounds = getLocalBounds().reduced(14);
            auto card = bounds.reduced(0, 0);
            auto top = card.removeFromTop(28);
            juce::ignoreUnused(top);

            auto row = [&](juce::Label& label, juce::Slider& slider) {
                auto line = card.removeFromTop(44);
                label.setBounds(line.removeFromLeft(120));
                slider.setBounds(line.reduced(10, 8));
                card.removeFromTop(6);
            };

            row(volumeLabel_, volume_);
            row(panLabel_, pan_);
            row(reverbLabel_, reverbMix_);
        }

        void paint(juce::Graphics& graphics) override
        {
            graphics.fillAll(juce::Colour(0xff161b22));
            auto area = getLocalBounds().reduced(14);
            graphics.setColour(juce::Colour(0xff202731));
            graphics.fillRoundedRectangle(area.toFloat(), 8.0f);
            graphics.setColour(juce::Colour(0xffd7dde8));
            graphics.setFont(juce::FontOptions(14.0f, juce::Font::bold));
            graphics.drawText("Smart Controls", area.reduced(12).withHeight(24), juce::Justification::centredLeft);
        }

    private:
        void timerCallback() override
        {
            syncFromSelection();
        }

        void configureSlider(juce::Label& label, juce::Slider& slider, double min, double max, const char* labelText)
        {
            slider.setRange(min, max, 0.001);
            slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 72, 22);
            slider.setColour(juce::Slider::trackColourId, juce::Colour(0xff5fb3ff));
            slider.setColour(juce::Slider::backgroundColourId, juce::Colour(0xff303a47));
            addAndMakeVisible(slider);

            label.setText(labelText, juce::dontSendNotification);
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
            volume_.setValue(track->mixer.volumeDb, juce::dontSendNotification);
            pan_.setValue(track->mixer.pan, juce::dontSendNotification);

            const double mix = reverbMixValue(*track);
            reverbMix_.setValue(mix, juce::dontSendNotification);
        }

        double reverbMixValue(const bandforge::Track& track) const
        {
            for (const auto& fx : track.mixer.effects) {
                if (fx.type == "reverb" || fx.name.find("Reverb") != std::string::npos) {
                    const auto found = fx.parameters.find("mix");
                    if (found != fx.parameters.end()) {
                        return std::clamp(found->second, 0.0, 1.0);
                    }
                }
            }
            return 0.0;
        }

        void applyMixer()
        {
            if (suppress_) {
                return;
            }
            auto* track = selectedTrack();
            if (track == nullptr) {
                return;
            }
            history_.remember(project_);
            track->mixer.volumeDb = volume_.getValue();
            track->mixer.pan = std::clamp(pan_.getValue(), -1.0, 1.0);
        }

        void applyReverb()
        {
            if (suppress_) {
                return;
            }
            auto* track = selectedTrack();
            if (track == nullptr) {
                return;
            }
            history_.remember(project_);

            for (auto& fx : track->mixer.effects) {
                if (fx.type == "reverb" || fx.name.find("Reverb") != std::string::npos) {
                    fx.parameters["mix"] = std::clamp(reverbMix_.getValue(), 0.0, 1.0);
                    return;
                }
            }

            bandforge::EffectSlot slot;
            slot.id = "fx-reverb";
            slot.type = "reverb";
            slot.name = "Room Reverb";
            slot.parameters["mix"] = std::clamp(reverbMix_.getValue(), 0.0, 1.0);
            track->mixer.effects.push_back(std::move(slot));
        }

        bandforge::Project& project_;
        bandforge::ProjectHistory& history_;
        bandforge_app::SelectionState& selection_;
        juce::Slider volume_;
        juce::Slider pan_;
        juce::Slider reverbMix_;
        juce::Label volumeLabel_;
        juce::Label panLabel_;
        juce::Label reverbLabel_;
        bool suppress_ = false;
    };

    bandforge::Project& project_;
    bandforge::ProjectHistory& history_;
    bandforge_app::SelectionState& selection_;
    KeyboardCallbacks keyCallbacks_;

    juce::TabbedComponent tabs_ { juce::TabbedButtonBar::TabsAtTop };
    std::unique_ptr<PianoRollView> pianoRoll_;
    std::unique_ptr<SmartControlsView> smartControls_;
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
        audioEngine_.noteOn(pitch, vel, selectedTrackKind());
    };
    keyCbs.noteOff = [this](int pitch) {
        audioEngine_.noteOff(pitch);
    };
    keyCbs.getOctave = [this]() -> int {
        return keyboardOctave_;
    };
    keyCbs.shiftOctave = [this](int delta) {
        keyboardOctave_ = std::clamp(keyboardOctave_ + delta, 0, 8);
        audioEngine_.clearLiveNotes();
        activeNoteKeys_.clear();
        refreshViews();
    };
    keyCbs.isPitchActive = [this](int pitch) -> bool {
        for (const auto& [kc, p] : activeNoteKeys_) {
            if (p == pitch) return true;
        }
        return false;
    };

    editorPanel_ = std::make_unique<EditorPanelComponent>(project_, history_, selection_, std::move(keyCbs));

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
    positionLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffd7dde8));
    tempoLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffd7dde8));
    positionLabel_.setJustificationType(juce::Justification::centred);
    tempoLabel_.setJustificationType(juce::Justification::centred);

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
            activeNoteKeys_[-(msg.getNoteNumber() + 1)] = msg.getNoteNumber();
        } else if (msg.isNoteOff()) {
            audioEngine_.noteOff(msg.getNoteNumber());
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
    tempoLabel_.setText(juce::String(project_.bpmAt(transport_.positionBeat()), 1) + " BPM", juce::dontSendNotification);
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
        "*.bandforge;project.json");

    projectChooser_->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories,
        [this](const juce::FileChooser& chooser) {
            auto file = chooser.getResult();
            if (file == juce::File {}) {
                return;
            }

            if (file.getFileName() == "project.json") {
                file = file.getParentDirectory();
            }

            try {
                project_ = bandforge::Project::loadBundle(file.getFullPathName().toStdString());
                history_.clear();
                selection_ = {};
                activeNoteKeys_.clear();
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
            .getChildFile(juce::String::fromUTF8(project_.name.c_str()) + ".bandforge"),
        "*.bandforge");

    projectChooser_->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectDirectories,
        [this](const juce::FileChooser& chooser) {
            auto file = chooser.getResult();
            if (file == juce::File {}) {
                return;
            }

            if (!file.hasFileExtension(".bandforge")) {
                file = file.withFileExtension(".bandforge");
            }

            try {
                project_.saveBundle(file.getFullPathName().toStdString());
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
    juce::PopupMenu menu;
    const std::vector<bandforge::TrackKind> kinds {
        bandforge::TrackKind::Keys,
        bandforge::TrackKind::SynthLead,
        bandforge::TrackKind::Bass,
        bandforge::TrackKind::Pad,
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

    for (int index = 0; index < static_cast<int>(kinds.size()); ++index) {
        menu.addItem(index + 1, juce::String::fromUTF8(bandforge::displayName(kinds[static_cast<std::size_t>(index)]).c_str()));
    }

    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(addMidiButton_),
        [this, kinds](int result) {
            if (result <= 0 || result > static_cast<int>(kinds.size())) {
                return;
            }
            createMidiTrack(kinds[static_cast<std::size_t>(result - 1)]);
        });
}

void MainComponent::createMidiTrack(bandforge::TrackKind kind)
{
    history_.remember(project_);
    auto& track = project_.addTrack(kind, bandforge::displayName(kind) + " " + std::to_string(project_.tracks.size() + 1));
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
    recordingFile_ = tmpDir.getChildFile("bandforge_rec_" + juce::String(juce::Time::currentTimeMillis()) + ".wav");
    recordStartBeat_ = transport_.positionBeat();

    if (audioRecorder_) {
        audioRecorder_->startRecording(recordingFile_,
            currentSampleRate_, 2);
    }

    // MIDI recording: arm for the selected MIDI track too
    midiRecording_ = false;
    for (const auto& track : project_.tracks) {
        if (track.mixer.recordArmed && bandforge::isMidiTrackKind(track.kind)) {
            {
                const std::lock_guard<std::mutex> sl(midiRecordMutex_);
                recordedMidi_.clear();
            }
            midiRecording_ = true;
            break;
        }
    }
}

void MainComponent::stopRecording()
{
    if (audioRecorder_) {
        audioRecorder_->stopRecording();
    }
    midiRecording_ = false;

    // ── Commit audio recording as a clip ─────────────────────────────────────
    if (recordingFile_.existsAsFile() && recordingFile_.getSize() > 44) {
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
    {
        const std::lock_guard<std::mutex> sl(midiRecordMutex_);
        if (!recordedMidi_.empty()) {
            bandforge::TrackId midiTargetId = 0;
            for (const auto& track : project_.tracks) {
                if (track.mixer.recordArmed && bandforge::isMidiTrackKind(track.kind)) {
                    midiTargetId = track.id;
                    break;
                }
            }
            if (midiTargetId != 0) {
                double clipEnd = 0.0;
                bandforge::MidiClipData data;
                // Build note objects from paired note-on / note-off messages
                std::map<int, std::pair<double, int>> open; // pitch → {startBeat, vel}
                for (const auto& tm : recordedMidi_) {
                    if (tm.msg.isNoteOn()) {
                        open[tm.msg.getNoteNumber()] = { tm.beatPosition, tm.msg.getVelocity() };
                    } else if (tm.msg.isNoteOff()) {
                        auto it = open.find(tm.msg.getNoteNumber());
                        if (it != open.end()) {
                            bandforge::MidiNote note;
                            note.pitch         = tm.msg.getNoteNumber();
                            note.velocity      = it->second.second;
                            note.channel       = 1;
                            note.startBeat     = it->second.first;
                            note.durationBeats = std::max(0.03125, tm.beatPosition - it->second.first);
                            clipEnd = std::max(clipEnd, note.startBeat + note.durationBeats);
                            data.notes.push_back(note);
                            open.erase(it);
                        }
                    }
                }
                if (!data.notes.empty()) {
                    history_.remember(project_);
                    auto& clip = project_.addMidiClip(midiTargetId, "MIDI Take",
                        recordStartBeat_, std::max(4.0, clipEnd));
                    clip.midi = std::move(data);
                }
            }
            recordedMidi_.clear();
        }
    }

    refreshViews();
}

// ── Plugin hosting ────────────────────────────────────────────────────────────
void MainComponent::openPluginBrowser()
{
    const auto showMenu = [this] {
        const auto types = knownPlugins_.getTypes();

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

        const bandforge::TrackId targetTrack = selection_.selectedTrackId;
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(pluginsButton_),
            [this, targetTrack, types](int result) {
                if (result <= 0 || result > types.size()) return;
                loadPluginOnTrack(targetTrack, types[result - 1]);
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

    static juce::ThreadPool pool(1);
    pool.addJob(new ScanJob(formatManager_, knownPlugins_, paths, showMenu), true);
}

void MainComponent::loadPluginOnTrack(bandforge::TrackId trackId, const juce::PluginDescription& desc)
{
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

    {
        const std::lock_guard<std::mutex> lock(pluginMutex_);
        trackPlugins_[trackId] = std::move(tp);
    }

    // Close any existing editor window for this track before opening a new one
    {
        const auto it = pluginWindows_.find(trackId);
        if (it != pluginWindows_.end()) {
            it->second->setVisible(false);
            pluginWindows_.erase(it);
        }
    }

    if (trackPlugins_[trackId] && trackPlugins_[trackId]->instance->hasEditor()) {
        auto* editor = trackPlugins_[trackId]->instance->createEditor();
        if (editor) {
            auto* dw = new juce::DocumentWindow(desc.name,
                juce::Colour(0xff1a2130), juce::DocumentWindow::closeButton, true);
            dw->setContentOwned(editor, true);
            dw->setResizable(true, false);
            dw->centreWithSize(editor->getWidth(), editor->getHeight());
            dw->setVisible(true);
            pluginWindows_[trackId] = dw;
        }
    } else {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::InfoIcon,
            "Plugin loaded", desc.name + " loaded on selected track.");
    }
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    const bool cmd = key.getModifiers().isCommandDown();
    const bool shift = key.getModifiers().isShiftDown();
    const int kc = key.getKeyCode();

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

    // ── Keyboard piano ────────────────────────────────────────────────────────
    if (cmd || shift) {
        return false;
    }

    // Octave shift (Z = down, X = up)
    if (kc == 'Z') {
        keyboardOctave_ = std::max(0, keyboardOctave_ - 1);
        return true;
    }
    if (kc == 'X') {
        keyboardOctave_ = std::min(8, keyboardOctave_ + 1);
        return true;
    }

    // Note key — ignore key-repeat by checking activeNoteKeys_
    const int semitone = keyCodeToSemitone(kc);
    if (semitone >= 0 && activeNoteKeys_.find(kc) == activeNoteKeys_.end()) {
        const int pitch = std::clamp((keyboardOctave_ + 1) * 12 + semitone, 0, 127);
        activeNoteKeys_[kc] = pitch;
        audioEngine_.noteOn(pitch, 100, selectedTrackKind());
        return true;
    }

    return false;
}

bool MainComponent::keyStateChanged(bool)
{
    static const int kNoteKeys[] = { 'A','W','S','E','D','F','T','G','Y','H','U','J','K','O','L' };
    for (int kc : kNoteKeys) {
        auto it = activeNoteKeys_.find(kc);
        if (it != activeNoteKeys_.end() && !juce::KeyPress::isKeyCurrentlyDown(kc)) {
            audioEngine_.noteOff(it->second);
            activeNoteKeys_.erase(it);
        }
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
