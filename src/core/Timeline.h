#pragma once

#include "core/Model.h"

#include <optional>

namespace bandforge {

struct GridSettings {
    double pixelsPerBeat = 72.0;
    double snapBeats = 0.25;
    bool snapEnabled = true;

    [[nodiscard]] double beatToPixel(double beat) const noexcept;
    [[nodiscard]] double pixelToBeat(double pixel) const noexcept;
    [[nodiscard]] double snap(double beat) const noexcept;
};

class TimelineEditor {
public:
    explicit TimelineEditor(Project& project);

    bool moveClip(TrackId trackId, ClipId clipId, double newStartBeat, const GridSettings& grid = {});
    bool trimClip(TrackId trackId, ClipId clipId, double newStartBeat, double newLengthBeats, const GridSettings& grid = {});
    std::optional<ClipId> duplicateClip(TrackId trackId, ClipId clipId, double beatOffset);
    std::optional<ClipId> splitClip(TrackId trackId, ClipId clipId, double splitBeat);
    bool removeClip(TrackId trackId, ClipId clipId);
    bool quantizeMidiClip(TrackId trackId, ClipId clipId, double gridBeats, double strength = 1.0);

private:
    Project& project_;
};

} // namespace bandforge
