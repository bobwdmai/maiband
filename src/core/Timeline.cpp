#include "core/Timeline.h"

#include "core/Midi.h"
#include "core/Transport.h"

#include <algorithm>
#include <utility>

namespace bandforge {

double GridSettings::beatToPixel(double beat) const noexcept
{
    return beat * pixelsPerBeat;
}

double GridSettings::pixelToBeat(double pixel) const noexcept
{
    return pixelsPerBeat <= 0.0 ? 0.0 : pixel / pixelsPerBeat;
}

double GridSettings::snap(double beat) const noexcept
{
    if (!snapEnabled || snapBeats <= 0.0) {
        return beat;
    }
    return midi::quantizeBeat(beat, snapBeats);
}

TimelineEditor::TimelineEditor(Project& project)
    : project_(project)
{
}

bool TimelineEditor::moveClip(TrackId trackId, ClipId clipId, double newStartBeat, const GridSettings& grid)
{
    auto* clip = project_.findClip(trackId, clipId);
    if (clip == nullptr) {
        return false;
    }

    clip->startBeat = std::max(0.0, grid.snap(newStartBeat));
    return true;
}

bool TimelineEditor::trimClip(TrackId trackId, ClipId clipId, double newStartBeat, double newLengthBeats, const GridSettings& grid)
{
    auto* clip = project_.findClip(trackId, clipId);
    if (clip == nullptr) {
        return false;
    }

    const double minLength = grid.snapEnabled && grid.snapBeats > 0.0 ? grid.snapBeats : 0.03125;
    clip->startBeat = std::max(0.0, grid.snap(newStartBeat));
    clip->lengthBeats = std::max(minLength, newLengthBeats);
    return true;
}

std::optional<ClipId> TimelineEditor::duplicateClip(TrackId trackId, ClipId clipId, double beatOffset)
{
    auto* original = project_.findClip(trackId, clipId);
    if (original == nullptr) {
        return std::nullopt;
    }

    Clip copy = *original;
    copy.startBeat = std::max(0.0, copy.startBeat + beatOffset);

    const std::string copyName = copy.name + " Copy";
    Clip* inserted = nullptr;
    if (copy.kind == ClipKind::Midi) {
        inserted = &project_.addMidiClip(trackId, copyName, copy.startBeat, copy.lengthBeats);
    } else {
        inserted = &project_.addAudioClip(trackId, copyName, copy.audio.mediaPath, copy.startBeat, copy.lengthBeats);
    }

    const ClipId newId = inserted->id;
    *inserted = copy;
    inserted->id = newId;
    inserted->name = copyName;
    return newId;
}

std::optional<ClipId> TimelineEditor::splitClip(TrackId trackId, ClipId clipId, double splitBeat)
{
    auto* clip = project_.findClip(trackId, clipId);
    if (clip == nullptr || splitBeat <= clip->startBeat || splitBeat >= clip->range().endBeat()) {
        return std::nullopt;
    }

    Clip right = *clip;
    const double localSplit = splitBeat - clip->startBeat;
    const double originalEnd = clip->range().endBeat();

    clip->lengthBeats = localSplit;
    right.startBeat = splitBeat;
    right.lengthBeats = originalEnd - splitBeat;
    right.name += " Split";

    if (clip->kind == ClipKind::Midi) {
        MidiClipData leftMidi;
        MidiClipData rightMidi;

        for (const auto& note : clip->midi.notes) {
            const double noteEnd = note.startBeat + note.durationBeats;
            if (note.startBeat < localSplit) {
                auto leftNote = note;
                leftNote.durationBeats = std::min(noteEnd, localSplit) - note.startBeat;
                if (leftNote.durationBeats > 0.0) {
                    leftMidi.notes.push_back(leftNote);
                }
            }
            if (noteEnd > localSplit) {
                auto rightNote = note;
                rightNote.startBeat = std::max(0.0, note.startBeat - localSplit);
                rightNote.durationBeats = noteEnd - std::max(note.startBeat, localSplit);
                if (rightNote.durationBeats > 0.0) {
                    rightMidi.notes.push_back(rightNote);
                }
            }
        }

        for (const auto& event : clip->midi.controls) {
            if (event.beat < localSplit) {
                leftMidi.controls.push_back(event);
            } else {
                auto rightEvent = event;
                rightEvent.beat -= localSplit;
                rightMidi.controls.push_back(rightEvent);
            }
        }

        clip->midi = std::move(leftMidi);
        right.midi = std::move(rightMidi);
    } else {
        TempoMap tempoMap(project_);
        const double splitSeconds = tempoMap.beatToSeconds(splitBeat);
        const double clipStartSeconds = tempoMap.beatToSeconds(clip->startBeat);
        right.audio.mediaStartSeconds += splitSeconds - clipStartSeconds;
    }

    Clip* inserted = nullptr;
    if (right.kind == ClipKind::Midi) {
        inserted = &project_.addMidiClip(trackId, right.name, right.startBeat, right.lengthBeats);
    } else {
        inserted = &project_.addAudioClip(trackId, right.name, right.audio.mediaPath, right.startBeat, right.lengthBeats);
    }

    const ClipId newId = inserted->id;
    *inserted = std::move(right);
    inserted->id = newId;
    return newId;
}

bool TimelineEditor::removeClip(TrackId trackId, ClipId clipId)
{
    auto* track = project_.findTrack(trackId);
    if (track == nullptr) {
        return false;
    }

    const auto oldSize = track->clips.size();
    track->clips.erase(std::remove_if(track->clips.begin(), track->clips.end(), [clipId](const Clip& clip) {
        return clip.id == clipId;
    }), track->clips.end());
    return track->clips.size() != oldSize;
}

bool TimelineEditor::quantizeMidiClip(TrackId trackId, ClipId clipId, double gridBeats, double strength)
{
    auto* clip = project_.findClip(trackId, clipId);
    if (clip == nullptr || clip->kind != ClipKind::Midi) {
        return false;
    }
    midi::quantizeNotes(clip->midi, gridBeats, strength);
    return true;
}

} // namespace bandforge
