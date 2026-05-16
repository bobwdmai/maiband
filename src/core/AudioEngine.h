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

private:
    [[nodiscard]] const CachedAudioClip& audioForPath(const std::string& mediaPath) const;

    AudioEngineConfig config_;
    mutable std::mutex cacheMutex_;
    mutable std::map<std::string, CachedAudioClip> audioCache_;

    // Keyed by {mediaPath, ratio_quantised_to_3dp}
    mutable std::mutex stretchMutex_;
    mutable std::map<std::pair<std::string, int>, CachedAudioClip> stretchCache_;
    mutable std::map<std::pair<std::string, int>, bool> stretchPending_;

    std::mutex liveNotesMutex_;
    std::vector<LiveNote> liveNotes_;
    double liveClockSeconds_ = 0.0;
};

} // namespace bandforge
