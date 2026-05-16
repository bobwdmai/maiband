#include "core/Midi.h"

#include <algorithm>
#include <cmath>

namespace bandforge::midi {

double quantizeBeat(double beat, double gridBeats)
{
    if (gridBeats <= 0.0) {
        return beat;
    }
    return std::round(beat / gridBeats) * gridBeats;
}

void quantizeNotes(MidiClipData& clip, double gridBeats, double strength)
{
    const double clampedStrength = std::clamp(strength, 0.0, 1.0);
    for (auto& note : clip.notes) {
        const double snapped = quantizeBeat(note.startBeat, gridBeats);
        note.startBeat += (snapped - note.startBeat) * clampedStrength;
        note.startBeat = std::max(0.0, note.startBeat);
        note.durationBeats = std::max(0.03125, note.durationBeats);
        note.pitch = clampMidiValue(note.pitch);
        note.velocity = clampMidiValue(note.velocity);
        note.channel = std::clamp(note.channel, 1, 16);
    }
    clampAndSort(clip);
}

void transpose(MidiClipData& clip, int semitones)
{
    for (auto& note : clip.notes) {
        note.pitch = clampMidiValue(note.pitch + semitones);
    }
    clampAndSort(clip);
}

void clampAndSort(MidiClipData& clip)
{
    for (auto& note : clip.notes) {
        note.pitch = clampMidiValue(note.pitch);
        note.velocity = clampMidiValue(note.velocity);
        note.channel = std::clamp(note.channel, 1, 16);
        note.startBeat = std::max(0.0, note.startBeat);
        note.durationBeats = std::max(0.03125, note.durationBeats);
    }

    for (auto& event : clip.controls) {
        event.controller = clampMidiValue(event.controller);
        event.value = clampMidiValue(event.value);
        event.channel = std::clamp(event.channel, 1, 16);
        event.beat = std::max(0.0, event.beat);
    }

    std::sort(clip.notes.begin(), clip.notes.end(), [](const MidiNote& left, const MidiNote& right) {
        if (left.startBeat == right.startBeat) {
            return left.pitch < right.pitch;
        }
        return left.startBeat < right.startBeat;
    });

    std::sort(clip.controls.begin(), clip.controls.end(), [](const MidiControlEvent& left, const MidiControlEvent& right) {
        return left.beat < right.beat;
    });
}

} // namespace bandforge::midi
