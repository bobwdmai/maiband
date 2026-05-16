#pragma once

#include "core/Model.h"

#include <map>
#include <mutex>
#include <span>
#include <string>
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

    // Returns a time-stretched copy of the audio at the given ratio (ratio = dstLen/srcLen).
    // Result is cached keyed by (path, ratio). Thread-safe.
    [[nodiscard]] const CachedAudioClip& stretchedClipFor(const std::string& mediaPath,
                                                           double stretchRatio) const;

private:
    [[nodiscard]] const CachedAudioClip& audioForPath(const std::string& mediaPath) const;

    AudioEngineConfig config_;
    mutable std::mutex cacheMutex_;
    mutable std::map<std::string, CachedAudioClip> audioCache_;

    // Keyed by {mediaPath, ratio_quantised_to_3dp}
    mutable std::mutex stretchMutex_;
    mutable std::map<std::pair<std::string, int>, CachedAudioClip> stretchCache_;

    std::mutex liveNotesMutex_;
    std::vector<LiveNote> liveNotes_;
    double liveClockSeconds_ = 0.0;
};

} // namespace bandforge
