#include "core/AudioEngine.h"

#include "core/Mixer.h"
#include "core/Transport.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
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
    constexpr int kWindow = 2048;
    const int hopOut = kWindow / 4;
    const int hopIn  = std::max(1, static_cast<int>(std::round(static_cast<double>(hopOut) / ratio)));
    const int ch = src.channels;
    const auto srcFrames = static_cast<int64_t>(src.samples.size() / static_cast<std::size_t>(ch));
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
        const bool isOpen = note.pitch >= 46 && (note.pitch == 46 || note.pitch >= 49);
        const double decayRate = isOpen ? 9.0 : 38.0;
        const double decay = std::exp(-noteSeconds * decayRate);
        const double noiseSeed = absoluteSeconds * 44100.0;
        const double noise = std::sin(noiseSeed * 271.3 + std::sin(noiseSeed * 613.9) * 19.4)
            * std::sin(noiseSeed * 537.1 + std::sin(noiseSeed * 1277.3) * 7.1);
        return static_cast<float>(noise * decay * velocity * 0.30f);
    }

    // Attack/release envelope
    const double attackTime = (kind == TrackKind::Pad || kind == TrackKind::Strings) ? 0.08 : 0.008;
    const double releaseDecay = (kind == TrackKind::Keys || kind == TrackKind::Pluck || kind == TrackKind::GuitarSynth) ? 0.12 : 0.06;
    const double attack = std::min(noteSeconds / attackTime, 1.0);
    const double release = std::clamp(releaseTime / releaseDecay, 0.0, 1.0);
    const double envelope = attack * release;

    int pitch = note.pitch;
    if (kind == TrackKind::Bass || kind == TrackKind::EightOhEight) {
        pitch -= 12;
    } else if (kind == TrackKind::SynthLead) {
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
    case TrackKind::SynthLead: {
        // Buzzy lead: square-ish wave (odd harmonics)
        const double f1 = std::sin(2.0 * Pi * freq * absoluteSeconds);
        const double f3 = 0.33 * std::sin(2.0 * Pi * freq * 3.0 * absoluteSeconds);
        const double f5 = 0.20 * std::sin(2.0 * Pi * freq * 5.0 * absoluteSeconds);
        const double f7 = 0.14 * std::sin(2.0 * Pi * freq * 7.0 * absoluteSeconds);
        return static_cast<float>((f1 + f3 + f5 + f7) * envelope * velocity * 0.11f);
    }
    case TrackKind::Pad:
    case TrackKind::Strings: {
        // Lush pad: slow-attack chorus of detuned oscillators
        const double detune = kind == TrackKind::Strings ? 0.007 : 0.004;
        const double f1 = std::sin(2.0 * Pi * freq * absoluteSeconds);
        const double f2 = std::sin(2.0 * Pi * freq * (1.0 + detune) * absoluteSeconds);
        const double f3 = std::sin(2.0 * Pi * freq * (1.0 - detune) * absoluteSeconds);
        const double f4 = 0.3 * std::sin(2.0 * Pi * freq * 2.0 * absoluteSeconds);
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

} // namespace

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
    const bool soloed = anySoloed(project);
    TempoMap tempoMap(project);
    const double startSeconds = tempoMap.beatToSeconds(startBeat);
    std::vector<float> mixedFrame(static_cast<std::size_t>(config_.channels), 0.0f);

    for (std::size_t frame = 0; frame < frameCount; ++frame) {
        const double absoluteSeconds = startSeconds + (static_cast<double>(frame) / config_.sampleRate);
        const double beat = tempoMap.secondsToBeat(absoluteSeconds);
        std::fill(mixedFrame.begin(), mixedFrame.end(), 0.0f);

        for (const auto& track : project.tracks) {
            if (!mixer::trackAudible(track, soloed)) {
                continue;
            }

            const auto trackGain = static_cast<float>(mixer::dbToLinear(track.mixer.volumeDb));
            if (isMidiTrackKind(track.kind)) {
                float trackSample = 0.0f;
                for (const auto& clip : track.clips) {
                    if (clip.muted || clip.kind != ClipKind::Midi || !clip.range().contains(beat)) {
                        continue;
                    }

                    const double localBeat = beat - clip.startBeat;
                    for (const auto& note : clip.midi.notes) {
                        if (localBeat >= note.startBeat && localBeat < note.startBeat + note.durationBeats) {
                            trackSample += midiSampleAt(note, absoluteSeconds, localBeat - note.startBeat, project.bpmAt(beat), track.kind);
                        }
                    }
                }

                for (auto& output : mixedFrame) {
                    output += trackSample * trackGain;
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
                    // Use OLA-stretched version for pitch-preserved tempo matching
                    const double clipSeconds = tempoMap.beatToSeconds(clip.startBeat + clip.lengthBeats)
                                            - tempoMap.beatToSeconds(clip.startBeat);
                    const double ratio = clipSeconds / sourceDuration;
                    if (std::abs(ratio - 1.0) > 0.005) {
                        audio = &stretchedClipFor(clip.audio.mediaPath, ratio);
                    }
                    sourceSeconds += (localBeat / clip.lengthBeats) * durationSeconds(*audio);
                } else {
                    sourceSeconds += tempoMap.beatToSeconds(beat) - tempoMap.beatToSeconds(clip.startBeat);
                }

                const auto clipGain = static_cast<float>(mixer::dbToLinear(clip.audio.gainDb));
                for (int channel = 0; channel < config_.channels; ++channel) {
                    mixedFrame[static_cast<std::size_t>(channel)] += audioSampleAt(*audio, sourceSeconds, channel) * trackGain * clipGain;
                }
            }
        }

        for (int channel = 0; channel < config_.channels; ++channel) {
            interleavedOutput[(frame * static_cast<std::size_t>(config_.channels)) + static_cast<std::size_t>(channel)] =
                std::clamp(mixedFrame[static_cast<std::size_t>(channel)], -1.0f, 1.0f);
        }
    }
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

const AudioEngine::CachedAudioClip& AudioEngine::audioForPath(const std::string& mediaPath) const
{
    {
        std::lock_guard lock(cacheMutex_);
        const auto found = audioCache_.find(mediaPath);
        if (found != audioCache_.end()) {
            return found->second;
        }
    }

    auto loaded = loadWavFile(mediaPath);
    std::lock_guard lock(cacheMutex_);
    auto [inserted, _] = audioCache_.emplace(mediaPath, std::move(loaded));
    return inserted->second;
}

const AudioEngine::CachedAudioClip& AudioEngine::stretchedClipFor(
    const std::string& mediaPath, double stretchRatio) const
{
    // Quantise ratio to 3 decimal places so nearby ratios reuse the same entry.
    const int ratioKey = static_cast<int>(std::round(stretchRatio * 1000.0));
    const auto key = std::make_pair(mediaPath, ratioKey);

    {
        std::lock_guard lock(stretchMutex_);
        const auto found = stretchCache_.find(key);
        if (found != stretchCache_.end()) {
            return found->second;
        }
    }

    // Load source then stretch (this may take a moment; only happens once per ratio).
    const auto& src = audioForPath(mediaPath);
    auto stretched = (std::abs(stretchRatio - 1.0) < 0.005)
        ? src  // ratio ≈ 1 — no stretching needed
        : olaStretch(src, stretchRatio);

    std::lock_guard lock(stretchMutex_);
    auto [it, _] = stretchCache_.emplace(key, std::move(stretched));
    return it->second;
}

} // namespace bandforge
