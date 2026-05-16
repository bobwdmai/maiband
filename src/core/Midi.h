#pragma once

#include "core/Model.h"

namespace bandforge::midi {

[[nodiscard]] double quantizeBeat(double beat, double gridBeats);
void quantizeNotes(MidiClipData& clip, double gridBeats, double strength = 1.0);
void transpose(MidiClipData& clip, int semitones);
void clampAndSort(MidiClipData& clip);

} // namespace bandforge::midi
