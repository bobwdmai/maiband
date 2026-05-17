#pragma once

#include "core/Model.h"

#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace bandforge {

struct AudioEngineConfig {
    double sampleRate = 48000.0;
    int blockSize = 512;
    int channels = 2;
};

class AudioEngine {
public:
    struct BiquadState { double x1=0,x2=0,y1=0,y2=0; };

    struct TrackFxState {
        // Echo (stereo delay)
        std::array<std::vector<float>, 2> echoBuf;
        int echoWritePos = 0;
        // Telephone / megaphone biquads
        BiquadState bandHp[2]{}, bandLp[2]{};
        // Reverb (Schroeder: 4 combs + 2 allpass per channel)
        std::array<std::array<std::vector<float>, 4>, 2> combBuf;
        std::array<std::array<int, 4>, 2>   combPos{};
        std::array<std::array<float, 4>, 2> combHist{};
        std::array<std::array<std::vector<float>, 2>, 2> apBuf;
        std::array<std::array<int, 2>, 2> apPos{};
        bool reverbReady = false;
        bool echoReady = false;
    };

    struct CachedAudioClip {
        int sampleRate = 0;
        int channels = 0;
        std::vector<float> samples;
    };

    struct LiveNote {
        int pitch = 60;
        int velocity = 100;
        TrackKind kind = TrackKind::Keys;
        double ageSeconds = 0.0;
        double releaseAge = -1.0; // -1 = still held
    };

    ~AudioEngine();

    void prepare(AudioEngineConfig config);
    void renderPreview(const Project& project, double startBeat, std::span<float> interleavedOutput) const;

    // Live keyboard note playback (call from audio thread)
    void renderAndAdvanceLiveNotes(std::span<float> interleavedOutput);

    // Called from UI thread
    void noteOn(int pitch, int velocity, TrackKind kind);
    void noteOff(int pitch);
    void clearLiveNotes();

    [[nodiscard]] AudioEngineConfig config() const noexcept;

    // Returns cached audio for path if already loaded, without triggering a load.
    // Safe to call from any thread including the UI/paint thread.
    [[nodiscard]] const CachedAudioClip* peekAudioForPath(const std::string& mediaPath) const;

    // Optional JUCE-backed audio loader. When set, used instead of the built-in WAV parser.
    // Must be set before any audio clips are rendered. Not called on the audio thread.
    std::function<CachedAudioClip(const std::string& path)> juceLoader;

    // Pre-compute a stretched clip in a background thread so the audio thread
    // never blocks. Until ready, renderPreview plays silence for that clip.
    void requestStretch(const std::string& mediaPath, double stretchRatio) const;

    // Returns a time-stretched copy of the audio at the given ratio (ratio = dstLen/srcLen).
    // Result is cached keyed by (path, ratio). Thread-safe.
    [[nodiscard]] const CachedAudioClip* stretchedClipIfReady(const std::string& mediaPath,
                                                               double stretchRatio) const;

    // ── Per-track parametric EQ (8 bands, 2 channels) ─────────────────────────
    // EQ parameters are read from the track's EffectSlot with type=="eq".
    // Band parameters: "b<N>.freq", "b<N>.gain", "b<N>.q", "b<N>.type", "b<N>.active"
    //   N: 0..7  (b0=HPF, b1=LowShelf, b2..b5=Peaks, b6=HighShelf, b7=LPF)
    //   type: 0=HPF  1=LowShelf  2=Peak  3=HighShelf  4=LPF
    void resetEqState(TrackId id) const;

    // ── Per-track voice/insert effects ────────────────────────────────────────
    // Supported EffectSlot.type values:
    //   "echo"       — stereo delay. Params: "timeSec" (0.05..2.0), "feedback" (0..0.95), "mix" (0..1)
    //   "distortion" — soft-clip waveshaper. Params: "drive" (1..40), "mix" (0..1)
    //   "telephone"  — narrow band-pass. Params: "mix" (0..1)
    //   "megaphone"  — band-pass + drive. Params: "drive" (1..30), "mix" (0..1)
    //   "reverb"     — Schroeder reverb. Params: "size" (0.1..0.95), "damp" (0..1), "mix" (0..1)
    void resetTrackFx(TrackId id) const;

    // ── Recent output ring buffer (for spectrum analyzer overlay) ─────────────
    // After every renderPreview the mono mix of the last block is pushed into a
    // power-of-two ring buffer that the UI can sample without blocking.
    static constexpr std::size_t kRecentBufferSize = 4096;
    std::size_t peekRecentOutput(std::span<float> dst) const noexcept;

private:
    [[nodiscard]] const CachedAudioClip& audioForPath(const std::string& mediaPath) const;

    AudioEngineConfig config_;
    mutable std::mutex cacheMutex_;
    mutable std::map<std::string, CachedAudioClip> audioCache_;

    // Biquad state: 8 bands × 2 channels per track (Logic-style channel EQ).
    mutable std::mutex eqMutex_;
    mutable std::map<TrackId, std::array<BiquadState, 16>> eqState_; // [band*2 + ch]

    // Per-track voice/insert effect state, lazily allocated.
    mutable std::mutex fxMutex_;
    mutable std::map<TrackId, TrackFxState> fxState_;

    // Keyed by {mediaPath, ratio_quantised_to_3dp}
    mutable std::mutex stretchMutex_;
    mutable std::map<std::pair<std::string, int>, CachedAudioClip> stretchCache_;
    mutable std::map<std::pair<std::string, int>, bool> stretchPending_;
    mutable std::mutex stretchThreadsMutex_;
    mutable std::vector<std::thread> stretchThreads_;

    std::mutex liveNotesMutex_;
    std::vector<LiveNote> liveNotes_;
    double liveClockSeconds_ = 0.0;

    // Recent output ring buffer (SPSC, written by renderPreview, read by UI).
    mutable std::array<float, kRecentBufferSize> recentBuffer_{};
    mutable std::atomic<std::size_t> recentWriteIdx_{ 0 };
};

} // namespace bandforge
