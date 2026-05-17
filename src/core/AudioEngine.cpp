#include "core/AudioEngine.h"

#include "core/Mixer.h"
#include "core/Transport.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace bandforge {
namespace {

constexpr double Pi = 3.14159265358979323846;

double midiNoteToFrequency(int note)
{
    return 440.0 * std::pow(2.0, (static_cast<double>(note) - 69.0) / 12.0);
}

bool anySoloed(const Project& project)
{
    return std::any_of(project.tracks.begin(), project.tracks.end(), [](const Track& track) {
        return track.mixer.solo;
    });
}

std::uint16_t readU16(std::istream& input)
{
    const auto lo = static_cast<std::uint16_t>(static_cast<unsigned char>(input.get()));
    const auto hi = static_cast<std::uint16_t>(static_cast<unsigned char>(input.get()));
    return static_cast<std::uint16_t>(lo | (hi << 8));
}

std::uint32_t readU32(std::istream& input)
{
    const auto b0 = static_cast<std::uint32_t>(static_cast<unsigned char>(input.get()));
    const auto b1 = static_cast<std::uint32_t>(static_cast<unsigned char>(input.get()));
    const auto b2 = static_cast<std::uint32_t>(static_cast<unsigned char>(input.get()));
    const auto b3 = static_cast<std::uint32_t>(static_cast<unsigned char>(input.get()));
    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

std::string readChunkId(std::istream& input)
{
    char id[4] {};
    input.read(id, 4);
    return std::string(id, id + 4);
}

std::filesystem::path resolveAudioPath(const std::string& mediaPath)
{
    if (mediaPath.empty()) {
        return {};
    }

    const std::filesystem::path path(mediaPath);
    if (path.is_absolute() && std::filesystem::exists(path)) {
        return path;
    }
    if (std::filesystem::exists(path)) {
        return path;
    }

    const auto libraryPath = std::filesystem::path("assets") / "library";
    if (std::filesystem::exists(libraryPath / path)) {
        return libraryPath / path;
    }
    if (std::filesystem::exists(libraryPath / "loops" / path.filename())) {
        return libraryPath / "loops" / path.filename();
    }

#ifdef BANDFORGE_SOURCE_DIR
    const auto sourceLibraryPath = std::filesystem::path(BANDFORGE_SOURCE_DIR) / "assets" / "library";
    if (std::filesystem::exists(sourceLibraryPath / path)) {
        return sourceLibraryPath / path;
    }
    if (std::filesystem::exists(sourceLibraryPath / "loops" / path.filename())) {
        return sourceLibraryPath / "loops" / path.filename();
    }
#endif

    return path;
}

AudioEngine::CachedAudioClip loadWavFile(const std::string& mediaPath)
{
    AudioEngine::CachedAudioClip clip;
    std::ifstream input(resolveAudioPath(mediaPath), std::ios::binary);
    if (!input) {
        return clip;
    }

    if (readChunkId(input) != "RIFF") {
        return clip;
    }
    readU32(input);
    if (readChunkId(input) != "WAVE") {
        return clip;
    }

    std::uint16_t format = 0;
    std::uint16_t channels = 0;
    std::uint32_t sampleRate = 0;
    std::uint16_t bitsPerSample = 0;
    std::vector<char> pcm;

    while (input && (!format || pcm.empty())) {
        const auto chunkId = readChunkId(input);
        if (!input) {
            break;
        }

        const auto chunkSize = readU32(input);
        const auto chunkStart = input.tellg();
        if (chunkId == "fmt " && chunkSize >= 16) {
            format = readU16(input);
            channels = readU16(input);
            sampleRate = readU32(input);
            readU32(input);
            readU16(input);
            bitsPerSample = readU16(input);
        } else if (chunkId == "data" && chunkSize > 0) {
            pcm.resize(static_cast<std::size_t>(chunkSize));
            input.read(pcm.data(), static_cast<std::streamsize>(pcm.size()));
        }

        input.clear();
        input.seekg(chunkStart + static_cast<std::streamoff>(chunkSize + (chunkSize % 2)));
    }

    if (format != 1 || channels == 0 || sampleRate == 0 || bitsPerSample != 16 || pcm.empty()) {
        return {};
    }

    clip.sampleRate = static_cast<int>(sampleRate);
    clip.channels = static_cast<int>(channels);
    const auto sampleCount = pcm.size() / 2;
    clip.samples.reserve(sampleCount);
    for (std::size_t i = 0; i < sampleCount; ++i) {
        const auto lo = static_cast<unsigned char>(pcm[i * 2]);
        const auto hi = static_cast<unsigned char>(pcm[i * 2 + 1]);
        int value = static_cast<int>(lo) | (static_cast<int>(hi) << 8);
        if (value >= 32768) {
            value -= 65536;
        }
        clip.samples.push_back(static_cast<float>(static_cast<double>(value) / 32768.0));
    }
    return clip;
}

double durationSeconds(const AudioEngine::CachedAudioClip& clip)
{
    if (clip.sampleRate <= 0 || clip.channels <= 0 || clip.samples.empty()) {
        return 0.0;
    }
    return static_cast<double>(clip.samples.size() / static_cast<std::size_t>(clip.channels)) / static_cast<double>(clip.sampleRate);
}

float audioSampleAt(const AudioEngine::CachedAudioClip& clip, double seconds, int outputChannel)
{
    if (clip.sampleRate <= 0 || clip.channels <= 0 || clip.samples.empty() || seconds < 0.0) {
        return 0.0f;
    }

    const auto frameCount = static_cast<int64_t>(
        clip.samples.size() / static_cast<std::size_t>(clip.channels));
    if (frameCount == 0) {
        return 0.0f;
    }

    const auto ch = static_cast<std::size_t>(
        std::clamp(outputChannel, 0, clip.channels - 1));
    const auto stride = static_cast<std::size_t>(clip.channels);

    const double frameF = seconds * static_cast<double>(clip.sampleRate);
    const auto f1 = static_cast<int64_t>(frameF);
    const float t = static_cast<float>(frameF - static_cast<double>(f1));

    const auto getSample = [&](int64_t f) -> float {
        if (f < 0 || f >= frameCount) return 0.0f;
        return clip.samples[static_cast<std::size_t>(f) * stride + ch];
    };

    // Catmull-Rom cubic interpolation — much smoother than nearest-neighbour
    const float s0 = getSample(f1 - 1);
    const float s1 = getSample(f1);
    const float s2 = getSample(f1 + 1);
    const float s3 = getSample(f1 + 2);
    return s1 + 0.5f * t * (s2 - s0
        + t * (2.0f * s0 - 5.0f * s1 + 4.0f * s2 - s3
            + t * (3.0f * (s1 - s2) + s3 - s0)));
}


// OLA (Overlap-Add) pitch-preserving time stretcher.
// ratio > 1 = slower playback (stretch), ratio < 1 = faster (compress).
// Window = 2048 frames, 4x overlap — good trade-off of quality vs. latency.
AudioEngine::CachedAudioClip olaStretch(const AudioEngine::CachedAudioClip& src, double ratio)
{
    if (!std::isfinite(ratio) || ratio <= 0.0 || src.sampleRate <= 0 || src.channels <= 0 || src.samples.empty()) {
        return {};
    }

    constexpr int kWindow = 2048;
    const int ch = src.channels;
    const auto srcFrames = static_cast<int64_t>(src.samples.size() / static_cast<std::size_t>(ch));
    if (srcFrames <= 0) {
        return {};
    }

    if (srcFrames < kWindow) {
        AudioEngine::CachedAudioClip out;
        out.sampleRate = src.sampleRate;
        out.channels = ch;
        const auto dstFrames = std::max<int64_t>(1, static_cast<int64_t>(std::ceil(static_cast<double>(srcFrames) * ratio)));
        out.samples.assign(static_cast<std::size_t>(dstFrames * ch), 0.0f);

        for (int64_t frame = 0; frame < dstFrames; ++frame) {
            const double sourceFrame = std::min(static_cast<double>(srcFrames - 1), static_cast<double>(frame) / ratio);
            const auto f0 = static_cast<int64_t>(sourceFrame);
            const auto f1 = std::min<int64_t>(srcFrames - 1, f0 + 1);
            const auto frac = static_cast<float>(sourceFrame - static_cast<double>(f0));
            for (int c = 0; c < ch; ++c) {
                const auto i0 = static_cast<std::size_t>(f0 * ch + c);
                const auto i1 = static_cast<std::size_t>(f1 * ch + c);
                out.samples[static_cast<std::size_t>(frame * ch + c)] =
                    src.samples[i0] + (src.samples[i1] - src.samples[i0]) * frac;
            }
        }
        return out;
    }

    const int hopOut = kWindow / 4;
    const int hopIn  = std::max(1, static_cast<int>(std::round(static_cast<double>(hopOut) / ratio)));
    const auto dstFrames = static_cast<int64_t>(std::ceil(static_cast<double>(srcFrames) * ratio)) + kWindow;

    AudioEngine::CachedAudioClip out;
    out.sampleRate = src.sampleRate;
    out.channels   = ch;
    out.samples.assign(static_cast<std::size_t>(dstFrames * ch), 0.0f);

    // Hanning window
    std::vector<float> win(static_cast<std::size_t>(kWindow));
    for (int i = 0; i < kWindow; ++i)
        win[static_cast<std::size_t>(i)] = 0.5f - 0.5f * static_cast<float>(
            std::cos(2.0 * Pi * i / static_cast<double>(kWindow - 1)));

    int64_t inPos = 0, outPos = 0;
    while (inPos + kWindow <= srcFrames && outPos + kWindow <= dstFrames) {
        for (int c = 0; c < ch; ++c) {
            for (int i = 0; i < kWindow; ++i) {
                const auto si = static_cast<std::size_t>((inPos  + i) * ch + c);
                const auto di = static_cast<std::size_t>((outPos + i) * ch + c);
                out.samples[di] += src.samples[si] * win[static_cast<std::size_t>(i)];
            }
        }
        inPos  += hopIn;
        outPos += hopOut;
    }
    // Normalise the OLA gain (Hanning 4x overlap sums to ~2.0)
    const float norm = static_cast<float>(hopOut) / (static_cast<float>(kWindow) * 0.5f);
    for (auto& s : out.samples) s = std::clamp(s * norm, -1.0f, 1.0f);
    return out;
}

float midiSampleAt(const MidiNote& note, double absoluteSeconds, double noteLocalBeat, double bpm, TrackKind kind)
{
    const double noteSeconds = (noteLocalBeat * 60.0) / bpm;
    if (noteSeconds < 0.0) {
        return 0.0f;
    }

    const double velocity = static_cast<double>(note.velocity) / 127.0;
    const double noteDurationSeconds = (note.durationBeats * 60.0) / bpm;
    const double releaseTime = std::max(0.0, noteDurationSeconds - noteSeconds);

    if (isDrumTrackKind(kind)) {
        if (kind == TrackKind::EightOhEight) {
            // 808: sub-bass with slow pitch glide down
            const double startFreq = midiNoteToFrequency(note.pitch);
            const double pitchDecay = std::exp(-noteSeconds * 4.0);
            const double freq = startFreq * (0.25 + 0.75 * pitchDecay);
            const double decay = std::exp(-noteSeconds * 3.5);
            const double body = std::sin(2.0 * Pi * freq * absoluteSeconds);
            const double click = 0.4 * std::sin(2.0 * Pi * 1800.0 * absoluteSeconds) * std::exp(-noteSeconds * 120.0);
            return static_cast<float>((body + click) * decay * velocity * 0.45f);
        }
        // kick (pitch <= 35), snare (36-45), hihat (46+)
        if (note.pitch <= 35) {
            // Kick: pitch sweep from ~180Hz to ~55Hz
            const double pitchDecay = std::exp(-noteSeconds * 16.0);
            const double freq = 55.0 + 125.0 * pitchDecay;
            const double decay = std::exp(-noteSeconds * 12.0);
            const double body = std::sin(2.0 * Pi * freq * absoluteSeconds);
            const double click = 0.5 * std::sin(2.0 * Pi * 3800.0 * absoluteSeconds) * std::exp(-noteSeconds * 180.0);
            return static_cast<float>((body * 0.85 + click) * decay * velocity * 0.55f);
        }
        if (note.pitch <= 45) {
            // Snare: body tone + noise burst
            const double decay = std::exp(-noteSeconds * 18.0);
            const double body = std::sin(2.0 * Pi * 200.0 * absoluteSeconds) * 0.4;
            // deterministic pseudo-noise from absoluteSeconds
            const double noiseSeed = absoluteSeconds * 44100.0;
            const double noiseVal = std::sin(noiseSeed * 127.1 + std::sin(noiseSeed * 311.7) * 43.7);
            const double noise = noiseVal * 0.6;
            const double noiseDecay = std::exp(-noteSeconds * 22.0);
            return static_cast<float>((body * decay + noise * noiseDecay) * velocity * 0.45f);
        }
        // Hi-hat: high-frequency noise with short decay
        const bool isOpen = note.pitch == 46 || note.pitch >= 49;
        const double decayRate = isOpen ? 9.0 : 38.0;
        const double decay = std::exp(-noteSeconds * decayRate);
        const double noiseSeed = absoluteSeconds * 44100.0;
        const double noise = std::sin(noiseSeed * 271.3 + std::sin(noiseSeed * 613.9) * 19.4)
            * std::sin(noiseSeed * 537.1 + std::sin(noiseSeed * 1277.3) * 7.1);
        return static_cast<float>(noise * decay * velocity * 0.30f);
    }

    // Attack/release envelope
    const double attackTime =
        (kind == TrackKind::Choir) ? 0.16 :
        (kind == TrackKind::Pad || kind == TrackKind::Strings || kind == TrackKind::Brass) ? 0.08 :
        (kind == TrackKind::Woodwind) ? 0.025 :
        0.008;
    const double releaseDecay =
        (kind == TrackKind::Choir || kind == TrackKind::Pad) ? 0.18 :
        (kind == TrackKind::Keys || kind == TrackKind::ElectricPiano
            || kind == TrackKind::Pluck || kind == TrackKind::GuitarSynth
            || kind == TrackKind::Mallet || kind == TrackKind::Woodwind) ? 0.12 :
        0.06;
    const double attack = std::min(noteSeconds / attackTime, 1.0);
    const double release = std::clamp(releaseTime / releaseDecay, 0.0, 1.0);
    const double envelope = attack * release;

    int pitch = note.pitch;
    if (kind == TrackKind::Bass || kind == TrackKind::EightOhEight) {
        pitch -= 12;
    } else if (kind == TrackKind::SynthLead || kind == TrackKind::Woodwind) {
        pitch += 12;
    }
    const double freq = midiNoteToFrequency(pitch);

    switch (kind) {
    case TrackKind::Bass: {
        // Sawtooth-like: fundamental + descending harmonics
        const double f1 = std::sin(2.0 * Pi * freq * absoluteSeconds);
        const double f2 = 0.50 * std::sin(2.0 * Pi * freq * 2.0 * absoluteSeconds);
        const double f3 = 0.25 * std::sin(2.0 * Pi * freq * 3.0 * absoluteSeconds);
        const double f4 = 0.12 * std::sin(2.0 * Pi * freq * 4.0 * absoluteSeconds);
        return static_cast<float>((f1 + f2 + f3 + f4) * envelope * velocity * 0.16f);
    }
    case TrackKind::Keys: {
        // Piano-like: bright attack that quickly decays to fundamental
        const double brightness = std::exp(-noteSeconds * 6.0);
        const double f1 = std::sin(2.0 * Pi * freq * absoluteSeconds);
        const double f2 = (0.5 + 0.4 * brightness) * std::sin(2.0 * Pi * freq * 2.0 * absoluteSeconds);
        const double f3 = (0.2 + 0.3 * brightness) * std::sin(2.0 * Pi * freq * 3.0 * absoluteSeconds);
        return static_cast<float>((f1 + f2 + f3) * envelope * velocity * 0.11f);
    }
    case TrackKind::ElectricPiano: {
        const double bell = std::exp(-noteSeconds * 2.4);
        const double body = std::sin(2.0 * Pi * freq * absoluteSeconds);
        const double tine = std::sin(2.0 * Pi * freq * 2.01 * absoluteSeconds) * 0.38 * bell;
        const double chime = std::sin(2.0 * Pi * freq * 3.98 * absoluteSeconds) * 0.20 * bell;
        return static_cast<float>((body + tine + chime) * envelope * velocity * 0.12f);
    }
    case TrackKind::Organ: {
        const double rotary = 1.0 + 0.08 * std::sin(2.0 * Pi * 5.6 * absoluteSeconds);
        const double f1 = std::sin(2.0 * Pi * freq * absoluteSeconds);
        const double f2 = 0.52 * std::sin(2.0 * Pi * freq * 2.0 * absoluteSeconds);
        const double f3 = 0.32 * std::sin(2.0 * Pi * freq * 3.0 * absoluteSeconds);
        const double f4 = 0.18 * std::sin(2.0 * Pi * freq * 4.0 * absoluteSeconds);
        return static_cast<float>((f1 + f2 + f3 + f4) * rotary * envelope * velocity * 0.09f);
    }
    case TrackKind::SynthLead: {
        // Buzzy lead: square-ish wave (odd harmonics)
        const double f1 = std::sin(2.0 * Pi * freq * absoluteSeconds);
        const double f3 = 0.33 * std::sin(2.0 * Pi * freq * 3.0 * absoluteSeconds);
        const double f5 = 0.20 * std::sin(2.0 * Pi * freq * 5.0 * absoluteSeconds);
        const double f7 = 0.14 * std::sin(2.0 * Pi * freq * 7.0 * absoluteSeconds);
        return static_cast<float>((f1 + f3 + f5 + f7) * envelope * velocity * 0.11f);
    }
    case TrackKind::Brass: {
        const double swell = std::min(noteSeconds / 0.18, 1.0);
        const double f1 = std::sin(2.0 * Pi * freq * absoluteSeconds);
        const double f2 = 0.55 * std::sin(2.0 * Pi * freq * 2.0 * absoluteSeconds);
        const double f3 = 0.35 * std::sin(2.0 * Pi * freq * 3.0 * absoluteSeconds);
        const double buzz = 0.18 * std::sin(2.0 * Pi * freq * 5.0 * absoluteSeconds);
        return static_cast<float>((f1 + f2 + f3 + buzz) * envelope * swell * velocity * 0.11f);
    }
    case TrackKind::Pad:
    case TrackKind::Choir:
    case TrackKind::Strings: {
        // Lush pad: slow-attack chorus of detuned oscillators
        const double detune = kind == TrackKind::Strings ? 0.007 : (kind == TrackKind::Choir ? 0.003 : 0.004);
        const double f1 = std::sin(2.0 * Pi * freq * absoluteSeconds);
        const double f2 = std::sin(2.0 * Pi * freq * (1.0 + detune) * absoluteSeconds);
        const double f3 = std::sin(2.0 * Pi * freq * (1.0 - detune) * absoluteSeconds);
        const double f4 = (kind == TrackKind::Choir ? 0.18 : 0.3) * std::sin(2.0 * Pi * freq * 2.0 * absoluteSeconds);
        return static_cast<float>(((f1 + f2 + f3) / 3.0 + f4) * envelope * velocity * 0.09f);
    }
    case TrackKind::GuitarSynth:
    case TrackKind::Pluck: {
        // Pluck: Karplus-Strong-ish using decaying harmonics
        const double pluckDecay = std::exp(-noteSeconds * (kind == TrackKind::Pluck ? 5.0 : 3.5));
        const double f1 = std::sin(2.0 * Pi * freq * absoluteSeconds);
        const double f2 = 0.6 * pluckDecay * std::sin(2.0 * Pi * freq * 2.0 * absoluteSeconds);
        const double f3 = 0.3 * pluckDecay * std::sin(2.0 * Pi * freq * 3.0 * absoluteSeconds);
        return static_cast<float>((f1 + f2 + f3) * pluckDecay * velocity * 0.13f);
    }
    case TrackKind::Mallet: {
        const double hitDecay = std::exp(-noteSeconds * 4.8);
        const double f1 = std::sin(2.0 * Pi * freq * absoluteSeconds);
        const double f2 = 0.55 * std::sin(2.0 * Pi * freq * 2.98 * absoluteSeconds);
        const double f3 = 0.26 * std::sin(2.0 * Pi * freq * 5.02 * absoluteSeconds);
        return static_cast<float>((f1 + f2 + f3) * hitDecay * velocity * 0.13f);
    }
    case TrackKind::Woodwind: {
        const double f1 = std::sin(2.0 * Pi * freq * absoluteSeconds);
        const double f2 = 0.24 * std::sin(2.0 * Pi * freq * 2.0 * absoluteSeconds);
        const double vibrato = 1.0 + 0.035 * std::sin(2.0 * Pi * 5.0 * absoluteSeconds);
        const double noiseSeed = absoluteSeconds * 44100.0;
        const double breath = 0.05 * std::sin(noiseSeed * 31.7 + std::sin(noiseSeed * 113.1) * 5.0);
        return static_cast<float>((f1 + f2 + breath) * vibrato * envelope * velocity * 0.10f);
    }
    case TrackKind::Arp: {
        // Arp: thin, slightly detuned square with short attack
        const double f1 = std::sin(2.0 * Pi * freq * absoluteSeconds);
        const double f3 = 0.33 * std::sin(2.0 * Pi * freq * 3.0 * absoluteSeconds);
        const double shimmer = 0.10 * std::sin(2.0 * Pi * freq * 2.002 * absoluteSeconds);
        return static_cast<float>((f1 + f3 + shimmer) * envelope * velocity * 0.10f);
    }
    default: {
        // Fallback: plain sine
        return static_cast<float>(std::sin(2.0 * Pi * freq * absoluteSeconds) * envelope * velocity * 0.12f);
    }
    }
}

// Wraps midiSampleAt for live keyboard notes using a fixed 120 BPM reference so
// we can express note age (seconds) as beats without a project tempo map.
static float liveSampleAt(const AudioEngine::LiveNote& ln, double absoluteSeconds)
{
    constexpr double kRefBpm = 120.0;
    constexpr double kBeatsPerSec = kRefBpm / 60.0;
    constexpr double kReleaseSec = 0.22;

    MidiNote note;
    note.pitch = ln.pitch;
    note.velocity = ln.velocity;
    note.channel = 1;
    note.startBeat = 0.0;
    // While held, duration is effectively infinite. At release, set it so the
    // release envelope begins decaying immediately (noteDuration = releaseAge).
    note.durationBeats = (ln.releaseAge < 0.0)
        ? 999.0
        : (ln.releaseAge + kReleaseSec) * kBeatsPerSec;

    return midiSampleAt(note, absoluteSeconds, ln.ageSeconds * kBeatsPerSec, kRefBpm, ln.kind);
}

// ── Parametric EQ (RBJ cookbook biquad filters) ────────────────────────────

struct BiquadCoeffs { double b0=1,b1=0,b2=0,a1=0,a2=0; };

static BiquadCoeffs computeEqBand(double freq, double gainDb, double q, int type, double sr)
{
    freq = std::clamp(freq, 10.0, sr * 0.49);
    q    = std::max(0.05, q);
    const double w0    = 2.0 * Pi * freq / sr;
    const double cosw0 = std::cos(w0);
    const double sinw0 = std::sin(w0);
    const double alpha = sinw0 / (2.0 * q);
    const double A     = std::pow(10.0, gainDb / 40.0);

    BiquadCoeffs c;
    double a0 = 1.0;
    switch (type) {
    case 0: // High-pass
        c.b0 = (1.0 + cosw0) / 2.0;
        c.b1 = -(1.0 + cosw0);
        c.b2 = (1.0 + cosw0) / 2.0;
        a0   = 1.0 + alpha;
        c.a1 = -2.0 * cosw0;
        c.a2 = 1.0 - alpha;
        break;
    case 1: { // Low shelf
        const double sqrtA = std::sqrt(A);
        const double alphaSh = sinw0 / 2.0 * std::sqrt((A + 1.0/A) * (1.0/1.0 - 1.0) + 2.0);
        c.b0 =     A * ((A+1) - (A-1)*cosw0 + 2.0*sqrtA*alphaSh);
        c.b1 = 2.0*A * ((A-1) - (A+1)*cosw0);
        c.b2 =     A * ((A+1) - (A-1)*cosw0 - 2.0*sqrtA*alphaSh);
        a0   =         (A+1) + (A-1)*cosw0 + 2.0*sqrtA*alphaSh;
        c.a1 =    -2.0*((A-1) + (A+1)*cosw0);
        c.a2 =         (A+1) + (A-1)*cosw0 - 2.0*sqrtA*alphaSh;
        break;
    }
    case 2: // Peak
        c.b0 = 1.0 + alpha * A;
        c.b1 = -2.0 * cosw0;
        c.b2 = 1.0 - alpha * A;
        a0   = 1.0 + alpha / A;
        c.a1 = -2.0 * cosw0;
        c.a2 = 1.0 - alpha / A;
        break;
    case 3: { // High shelf
        const double sqrtA = std::sqrt(A);
        const double alphaSh = sinw0 / 2.0 * std::sqrt((A + 1.0/A) * (1.0/1.0 - 1.0) + 2.0);
        c.b0 =     A * ((A+1) + (A-1)*cosw0 + 2.0*sqrtA*alphaSh);
        c.b1 =-2.0*A * ((A-1) + (A+1)*cosw0);
        c.b2 =     A * ((A+1) + (A-1)*cosw0 - 2.0*sqrtA*alphaSh);
        a0   =         (A+1) - (A-1)*cosw0 + 2.0*sqrtA*alphaSh;
        c.a1 =     2.0*((A-1) - (A+1)*cosw0);
        c.a2 =         (A+1) - (A-1)*cosw0 - 2.0*sqrtA*alphaSh;
        break;
    }
    case 4: // Low-pass
    default:
        c.b0 = (1.0 - cosw0) / 2.0;
        c.b1 =  1.0 - cosw0;
        c.b2 = (1.0 - cosw0) / 2.0;
        a0   = 1.0 + alpha;
        c.a1 = -2.0 * cosw0;
        c.a2 = 1.0 - alpha;
        break;
    }
    const double inv = 1.0 / a0;
    c.b0 *= inv; c.b1 *= inv; c.b2 *= inv;
    c.a1 *= inv; c.a2 *= inv;
    return c;
}

static double applyBiquad(double x, const BiquadCoeffs& c, AudioEngine::BiquadState& s)
{
    const double y = c.b0*x + c.b1*s.x1 + c.b2*s.x2 - c.a1*s.y1 - c.a2*s.y2;
    s.x2=s.x1; s.x1=x; s.y2=s.y1; s.y1=y;
    return y;
}

// Apply all active EQ bands for one track sample on the given channel.
// 8-band Logic-style channel EQ:
//   b0 = HPF, b1 = Low Shelf, b2..b5 = Peaks, b6 = High Shelf, b7 = LPF
static double applyTrackEq(double sample, int ch, TrackId trackId, const Track& track,
                           double sr, std::map<TrackId, std::array<AudioEngine::BiquadState, 16>>& state)
{
    static constexpr double kDefaultFreq[8] = { 30.0, 80.0, 200.0, 800.0, 2500.0, 6000.0, 10000.0, 18000.0 };
    static constexpr int    kDefaultType[8] = { 0, 1, 2, 2, 2, 2, 3, 4 };

    for (const auto& fx : track.mixer.effects) {
        if (fx.type != "eq") continue;
        auto& bands = state[trackId];
        for (int b = 0; b < 8; ++b) {
            const auto pf = [&](const std::string& k, double def) {
                const auto it = fx.parameters.find("b" + std::to_string(b) + "." + k);
                return it != fx.parameters.end() ? it->second : def;
            };
            // Default inactive — only bands explicitly enabled by the UI run.
            if (pf("active", 0.0) < 0.5) continue;
            const double freq = pf("freq", kDefaultFreq[b]);
            const double gain = pf("gain", 0.0);
            const double q    = pf("q",    0.707);
            const int    type = static_cast<int>(pf("type", static_cast<double>(kDefaultType[b])));
            const auto coeffs = computeEqBand(freq, gain, q, type, sr);
            sample = applyBiquad(sample, coeffs, bands[static_cast<std::size_t>(b * 2 + ch)]);
        }
        break; // only one EQ slot per track
    }
    return sample;
}

// Per-track insert effects applied after EQ. Operates on one channel sample;
// state writes advance once per stereo frame (caller signals via `isLastChannel`).
static double applyTrackFx(double sample, int ch, bool isLastChannel,
                           TrackId trackId, const Track& track, double sr,
                           std::map<TrackId, AudioEngine::TrackFxState>& fxStateMap)
{
    AudioEngine::TrackFxState* state = nullptr;

    const auto get = [&]() -> AudioEngine::TrackFxState& {
        if (state == nullptr) state = &fxStateMap[trackId];
        return *state;
    };

    const auto param = [](const EffectSlot& fx, const char* key, double def) {
        const auto it = fx.parameters.find(key);
        return it != fx.parameters.end() ? it->second : def;
    };

    for (const auto& fx : track.mixer.effects) {
        if (fx.type == "eq") continue; // already applied

        if (fx.type == "echo") {
            const double timeSec  = std::clamp(param(fx, "timeSec",  0.32), 0.02,  2.0);
            const double feedback = std::clamp(param(fx, "feedback", 0.35), 0.0,   0.95);
            const double mix      = std::clamp(param(fx, "mix",      0.35), 0.0,   1.0);
            auto& s = get();
            if (!s.echoReady) {
                const std::size_t cap = static_cast<std::size_t>(std::ceil(sr * 2.0)) + 16;
                for (auto& b : s.echoBuf) b.assign(cap, 0.0f);
                s.echoWritePos = 0;
                s.echoReady = true;
            }
            const std::size_t cap = s.echoBuf[0].size();
            const std::size_t chIdx = static_cast<std::size_t>(std::min(ch, 1));
            const int delaySamples = std::clamp(static_cast<int>(timeSec * sr), 1, static_cast<int>(cap - 1));
            int readPos = s.echoWritePos - delaySamples;
            if (readPos < 0) readPos += static_cast<int>(cap);
            const float delayed = s.echoBuf[chIdx][static_cast<std::size_t>(readPos)];
            s.echoBuf[chIdx][static_cast<std::size_t>(s.echoWritePos)] =
                static_cast<float>(sample + delayed * feedback);
            sample = sample * (1.0 - mix) + static_cast<double>(delayed) * mix;
            if (isLastChannel) s.echoWritePos = (s.echoWritePos + 1) % static_cast<int>(cap);
        }
        else if (fx.type == "distortion") {
            const double drive = std::clamp(param(fx, "drive", 6.0),  1.0, 40.0);
            const double mix   = std::clamp(param(fx, "mix",   1.0),  0.0, 1.0);
            const double wet   = std::tanh(sample * drive) / std::tanh(drive);
            sample = sample * (1.0 - mix) + wet * mix;
        }
        else if (fx.type == "telephone" || fx.type == "megaphone") {
            const bool mega = (fx.type == "megaphone");
            const double mix   = std::clamp(param(fx, "mix",   1.0), 0.0, 1.0);
            const double drive = std::clamp(param(fx, "drive", mega ? 8.0 : 1.0), 1.0, 30.0);
            const double hpFreq = mega ? 500.0  : 350.0;
            const double lpFreq = mega ? 2500.0 : 3000.0;
            auto& s = get();
            const std::size_t chIdx = static_cast<std::size_t>(std::min(ch, 1));
            const auto hp = computeEqBand(hpFreq, 0.0, 0.707, 0, sr);
            const auto lp = computeEqBand(lpFreq, 0.0, 0.707, 4, sr);
            double wet = applyBiquad(sample, hp, s.bandHp[chIdx]);
            wet = applyBiquad(wet, lp, s.bandLp[chIdx]);
            if (mega) wet = std::tanh(wet * drive) * 0.7;
            sample = sample * (1.0 - mix) + wet * mix;
        }
        else if (fx.type == "reverb") {
            // Schroeder reverb: 4 parallel combs + 2 series allpass per channel.
            const double sizeP = std::clamp(param(fx, "size", 0.7),  0.1, 0.95);
            const double damp  = std::clamp(param(fx, "damp", 0.45), 0.0, 1.0);
            const double mix   = std::clamp(param(fx, "mix",  0.3),  0.0, 1.0);
            auto& s = get();
            if (!s.reverbReady) {
                static constexpr int combLens[4] = { 1116, 1188, 1277, 1356 };
                static constexpr int apLens[2]   = { 225, 556 };
                const double scale = sr / 44100.0;
                for (int c = 0; c < 2; ++c) {
                    for (int i = 0; i < 4; ++i) {
                        const std::size_t len = static_cast<std::size_t>(std::lround(combLens[i] * scale)) + (c == 1 ? 23u : 0u);
                        s.combBuf[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)].assign(len, 0.0f);
                        s.combPos[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] = 0;
                        s.combHist[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] = 0.0f;
                    }
                    for (int i = 0; i < 2; ++i) {
                        const std::size_t len = static_cast<std::size_t>(std::lround(apLens[i] * scale)) + (c == 1 ? 23u : 0u);
                        s.apBuf[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)].assign(len, 0.0f);
                        s.apPos[static_cast<std::size_t>(c)][static_cast<std::size_t>(i)] = 0;
                    }
                }
                s.reverbReady = true;
            }
            const std::size_t chIdx = static_cast<std::size_t>(std::min(ch, 1));
            const double feedback = sizeP * 0.95;
            const double dampC = damp;
            double out = 0.0;
            // Combs in parallel
            for (int i = 0; i < 4; ++i) {
                auto& buf = s.combBuf[chIdx][static_cast<std::size_t>(i)];
                auto& pos = s.combPos[chIdx][static_cast<std::size_t>(i)];
                auto& hist = s.combHist[chIdx][static_cast<std::size_t>(i)];
                const float y = buf[static_cast<std::size_t>(pos)];
                hist = static_cast<float>(y * (1.0 - dampC) + hist * dampC);
                buf[static_cast<std::size_t>(pos)] = static_cast<float>(sample + hist * feedback);
                out += static_cast<double>(y);
                pos = (pos + 1) % static_cast<int>(buf.size());
            }
            // Allpass in series
            for (int i = 0; i < 2; ++i) {
                auto& buf = s.apBuf[chIdx][static_cast<std::size_t>(i)];
                auto& pos = s.apPos[chIdx][static_cast<std::size_t>(i)];
                const float bufVal = buf[static_cast<std::size_t>(pos)];
                const double in = out;
                out = -in + static_cast<double>(bufVal);
                buf[static_cast<std::size_t>(pos)] = static_cast<float>(in + bufVal * 0.5);
                pos = (pos + 1) % static_cast<int>(buf.size());
            }
            sample = sample * (1.0 - mix) + (out * 0.25) * mix;
        }
    }
    return sample;
}

} // namespace

void AudioEngine::resetEqState(TrackId id) const
{
    std::lock_guard lock(eqMutex_);
    eqState_.erase(id);
}

void AudioEngine::resetTrackFx(TrackId id) const
{
    std::lock_guard lock(fxMutex_);
    fxState_.erase(id);
}

AudioEngine::~AudioEngine()
{
    std::vector<std::thread> workers;
    {
        std::lock_guard lock(stretchThreadsMutex_);
        workers.swap(stretchThreads_);
    }

    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void AudioEngine::prepare(AudioEngineConfig config)
{
    config.channels = std::max(1, config.channels);
    config.sampleRate = std::max(1.0, config.sampleRate);
    config.blockSize = std::max(1, config.blockSize);
    config_ = config;
}

void AudioEngine::renderPreview(const Project& project, double startBeat, std::span<float> interleavedOutput) const
{
    std::fill(interleavedOutput.begin(), interleavedOutput.end(), 0.0f);
    if (config_.channels <= 0 || config_.sampleRate <= 0.0) {
        return;
    }

    const auto frameCount = interleavedOutput.size() / static_cast<std::size_t>(config_.channels);
    if (frameCount == 0) return;
    const bool soloed = anySoloed(project);
    TempoMap tempoMap(project);
    const double startSeconds = tempoMap.beatToSeconds(startBeat);
    // Compute beats per second once at block boundary and use linear interpolation
    // within the block. Valid because a ~10ms block is tiny compared to any tempo segment.
    const double endSeconds = startSeconds + static_cast<double>(frameCount) / config_.sampleRate;
    const double endBeat = tempoMap.secondsToBeat(endSeconds);
    const double beatsPerFrame = (endBeat - startBeat) / static_cast<double>(frameCount);
    const double bpm = project.bpmAt(startBeat);
    std::vector<float> mixedFrame(static_cast<std::size_t>(config_.channels), 0.0f);
    std::lock_guard<std::mutex> eqLock(eqMutex_);
    std::lock_guard<std::mutex> fxLock(fxMutex_);

    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        const double absoluteSeconds = startSeconds + (static_cast<double>(frame) / config_.sampleRate);
        const double beat = startBeat + static_cast<double>(frame) * beatsPerFrame;
        std::fill(mixedFrame.begin(), mixedFrame.end(), 0.0f);

        for (const auto& track : project.tracks) {
            if (!mixer::trackAudible(track, soloed)) {
                continue;
            }

            const auto trackGain = static_cast<float>(mixer::dbToLinear(track.mixer.volumeDb));
            const auto [panL, panR] = mixer::equalPowerPan(track.mixer.pan);
            const auto chanGain = [&](int ch) -> float {
                if (config_.channels < 2) return trackGain;
                return trackGain * static_cast<float>(ch == 0 ? panL : panR);
            };

            if (isMidiTrackKind(track.kind)) {
                float trackSample = 0.0f;
                for (const auto& clip : track.clips) {
                    if (clip.muted || clip.kind != ClipKind::Midi || !clip.range().contains(beat)) {
                        continue;
                    }

                    const double localBeat = beat - clip.startBeat;
                    for (const auto& note : clip.midi.notes) {
                        if (localBeat >= note.startBeat && localBeat < note.startBeat + note.durationBeats) {
                            trackSample += midiSampleAt(note, absoluteSeconds, localBeat - note.startBeat, bpm, track.kind);
                        }
                    }
                }

                for (int ch = 0; ch < config_.channels; ++ch) {
                    double s = applyTrackEq(static_cast<double>(trackSample), ch, track.id, track, config_.sampleRate, eqState_);
                    s = applyTrackFx(s, ch, ch == config_.channels - 1, track.id, track, config_.sampleRate, fxState_);
                    mixedFrame[static_cast<std::size_t>(ch)] += static_cast<float>(s) * chanGain(ch);
                }
                continue;
            }

            for (const auto& clip : track.clips) {
                if (clip.muted || clip.kind != ClipKind::Audio || !clip.range().contains(beat)) {
                    continue;
                }

                const auto& rawAudio = audioForPath(clip.audio.mediaPath);
                const double sourceDuration = durationSeconds(rawAudio);
                if (sourceDuration <= 0.0) {
                    continue;
                }

                const double localBeat = std::max(0.0, beat - clip.startBeat);
                double sourceSeconds = clip.audio.mediaStartSeconds;
                const AudioEngine::CachedAudioClip* audio = &rawAudio;

                if (clip.audio.stretchToProjectTempo && clip.lengthBeats > 0.0) {
                    const double clipSeconds = tempoMap.beatToSeconds(clip.startBeat + clip.lengthBeats)
                                            - tempoMap.beatToSeconds(clip.startBeat);
                    const double ratio = clipSeconds / sourceDuration;
                    if (std::abs(ratio - 1.0) > 0.005) {
                        const auto* stretched = stretchedClipIfReady(clip.audio.mediaPath, ratio);
                        if (stretched) {
                            audio = stretched;
                        } else {
                            // Not ready yet — kick off background stretch and play silence
                            requestStretch(clip.audio.mediaPath, ratio);
                            continue;
                        }
                    }
                    sourceSeconds += (localBeat / clip.lengthBeats) * durationSeconds(*audio);
                } else {
                    sourceSeconds += tempoMap.beatToSeconds(beat) - tempoMap.beatToSeconds(clip.startBeat);
                }

                const auto clipGain = static_cast<float>(mixer::dbToLinear(clip.audio.gainDb));
                for (int channel = 0; channel < config_.channels; ++channel) {
                    double s = applyTrackEq(
                        audioSampleAt(*audio, sourceSeconds, channel),
                        channel, track.id, track, config_.sampleRate, eqState_);
                    s = applyTrackFx(s, channel, channel == config_.channels - 1,
                        track.id, track, config_.sampleRate, fxState_);
                    mixedFrame[static_cast<std::size_t>(channel)] +=
                        static_cast<float>(s) * chanGain(channel) * clipGain;
                }
            }
        }

        for (int channel = 0; channel < config_.channels; ++channel) {
            interleavedOutput[(frame * static_cast<std::size_t>(config_.channels)) + static_cast<std::size_t>(channel)] =
                std::clamp(mixedFrame[static_cast<std::size_t>(channel)], -1.0f, 1.0f);
        }

        // Push mono mix into the analyzer ring buffer (SPSC).
        float mono = 0.0f;
        for (int channel = 0; channel < config_.channels; ++channel) {
            mono += mixedFrame[static_cast<std::size_t>(channel)];
        }
        if (config_.channels > 1) mono /= static_cast<float>(config_.channels);
        const std::size_t wi = recentWriteIdx_.load(std::memory_order_relaxed);
        recentBuffer_[wi & (kRecentBufferSize - 1)] = std::clamp(mono, -1.0f, 1.0f);
        recentWriteIdx_.store(wi + 1, std::memory_order_release);
    }
}

std::size_t AudioEngine::peekRecentOutput(std::span<float> dst) const noexcept
{
    const std::size_t n = std::min(dst.size(), kRecentBufferSize);
    if (n == 0) return 0;
    const std::size_t wi = recentWriteIdx_.load(std::memory_order_acquire);
    if (wi < n) return 0;
    const std::size_t start = wi - n;
    for (std::size_t i = 0; i < n; ++i) {
        dst[i] = recentBuffer_[(start + i) & (kRecentBufferSize - 1)];
    }
    return n;
}

AudioEngineConfig AudioEngine::config() const noexcept
{
    return config_;
}

void AudioEngine::noteOn(int pitch, int velocity, TrackKind kind)
{
    std::lock_guard lock(liveNotesMutex_);
    liveNotes_.erase(std::remove_if(liveNotes_.begin(), liveNotes_.end(),
        [pitch](const LiveNote& n) { return n.pitch == pitch; }), liveNotes_.end());
    liveNotes_.push_back({ pitch, velocity, kind, 0.0, -1.0 });
}

void AudioEngine::noteOff(int pitch)
{
    std::lock_guard lock(liveNotesMutex_);
    for (auto& note : liveNotes_) {
        if (note.pitch == pitch && note.releaseAge < 0.0) {
            note.releaseAge = note.ageSeconds;
        }
    }
}

void AudioEngine::clearLiveNotes()
{
    std::lock_guard lock(liveNotesMutex_);
    liveNotes_.clear();
}

void AudioEngine::renderAndAdvanceLiveNotes(std::span<float> interleavedOutput)
{
    if (config_.channels <= 0 || config_.sampleRate <= 0.0) {
        return;
    }

    constexpr double kReleaseSec = 0.22;
    const auto frameCount = interleavedOutput.size() / static_cast<std::size_t>(config_.channels);
    const double blockSeconds = static_cast<double>(frameCount) / config_.sampleRate;

    std::lock_guard lock(liveNotesMutex_);

    for (auto& ln : liveNotes_) {
        const bool done = ln.releaseAge >= 0.0 && ln.ageSeconds > ln.releaseAge + kReleaseSec + 0.05;
        if (done) {
            ln.pitch = -1;
            continue;
        }

        for (std::size_t frame = 0; frame < frameCount; ++frame) {
            const double absSeconds = liveClockSeconds_ + static_cast<double>(frame) / config_.sampleRate;
            const float sample = liveSampleAt(ln, absSeconds);
            for (int ch = 0; ch < config_.channels; ++ch) {
                const auto idx = frame * static_cast<std::size_t>(config_.channels) + static_cast<std::size_t>(ch);
                interleavedOutput[idx] = std::clamp(interleavedOutput[idx] + sample, -1.0f, 1.0f);
            }
        }
    }

    liveNotes_.erase(std::remove_if(liveNotes_.begin(), liveNotes_.end(),
        [](const LiveNote& n) { return n.pitch < 0; }), liveNotes_.end());

    for (auto& ln : liveNotes_) {
        ln.ageSeconds += blockSeconds;
    }

    liveClockSeconds_ += blockSeconds;
}

const AudioEngine::CachedAudioClip* AudioEngine::peekAudioForPath(const std::string& mediaPath) const
{
    std::lock_guard lock(cacheMutex_);
    const auto found = audioCache_.find(mediaPath);
    return found != audioCache_.end() ? &found->second : nullptr;
}

const AudioEngine::CachedAudioClip& AudioEngine::audioForPath(const std::string& mediaPath) const
{
    {
        std::lock_guard lock(cacheMutex_);
        const auto found = audioCache_.find(mediaPath);
        if (found != audioCache_.end()) {
            return found->second;
        }
    }

    CachedAudioClip loaded;
    if (juceLoader) {
        loaded = juceLoader(mediaPath);
    } else {
        loaded = loadWavFile(mediaPath);
    }
    std::lock_guard lock(cacheMutex_);
    auto [inserted, _] = audioCache_.emplace(mediaPath, std::move(loaded));
    return inserted->second;
}

void AudioEngine::requestStretch(const std::string& mediaPath, double stretchRatio) const
{
    if (!std::isfinite(stretchRatio) || stretchRatio <= 0.0 || mediaPath.empty()) {
        return;
    }

    const int ratioKey = static_cast<int>(std::round(stretchRatio * 1000.0));
    const auto key = std::make_pair(mediaPath, ratioKey);

    {
        std::lock_guard lock(stretchMutex_);
        if (stretchCache_.count(key) || stretchPending_.count(key))
            return;
        stretchPending_[key] = true;
    }

    // Capture by value so the thread owns everything it needs
    const std::string path = mediaPath;
    const double ratio = stretchRatio;
    std::thread worker([this, path, ratio, key]() {
        const auto& src = audioForPath(path);
        auto stretched = (src.sampleRate <= 0 || src.channels <= 0 || src.samples.empty())
            ? AudioEngine::CachedAudioClip {}
            : ((std::abs(ratio - 1.0) < 0.005) ? src : olaStretch(src, ratio));
        std::lock_guard lock(stretchMutex_);
        stretchCache_.emplace(key, std::move(stretched));
        stretchPending_.erase(key);
    });

    std::lock_guard lock(stretchThreadsMutex_);
    stretchThreads_.push_back(std::move(worker));
}

const AudioEngine::CachedAudioClip* AudioEngine::stretchedClipIfReady(
    const std::string& mediaPath, double stretchRatio) const
{
    const int ratioKey = static_cast<int>(std::round(stretchRatio * 1000.0));
    const auto key = std::make_pair(mediaPath, ratioKey);
    std::lock_guard lock(stretchMutex_);
    const auto found = stretchCache_.find(key);
    return found != stretchCache_.end() ? &found->second : nullptr;
}

} // namespace bandforge
