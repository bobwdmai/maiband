#include "core/Transport.h"

#include <algorithm>

namespace bandforge {

TempoMap::TempoMap(const Project& project)
    : project_(project)
{
}

double TempoMap::beatToSeconds(double beat) const
{
    if (beat <= 0.0) {
        return 0.0;
    }

    double seconds = 0.0;
    double previousBeat = 0.0;
    double currentBpm = project_.bpmAt(0.0);

    for (const auto& marker : project_.tempoMarkers) {
        if (marker.beat <= 0.0) {
            currentBpm = marker.bpm;
            continue;
        }
        if (marker.beat >= beat) {
            break;
        }
        seconds += ((marker.beat - previousBeat) * 60.0) / currentBpm;
        previousBeat = marker.beat;
        currentBpm = marker.bpm;
    }

    seconds += ((beat - previousBeat) * 60.0) / currentBpm;
    return seconds;
}

double TempoMap::secondsToBeat(double seconds) const
{
    if (seconds <= 0.0) {
        return 0.0;
    }

    double remaining = seconds;
    double previousBeat = 0.0;
    double currentBpm = project_.bpmAt(0.0);

    for (const auto& marker : project_.tempoMarkers) {
        if (marker.beat <= 0.0) {
            currentBpm = marker.bpm;
            continue;
        }

        const double segmentSeconds = ((marker.beat - previousBeat) * 60.0) / currentBpm;
        if (remaining < segmentSeconds) {
            return previousBeat + ((remaining * currentBpm) / 60.0);
        }

        remaining -= segmentSeconds;
        previousBeat = marker.beat;
        currentBpm = marker.bpm;
    }

    return previousBeat + ((remaining * currentBpm) / 60.0);
}

double TempoMap::bpmAt(double beat) const
{
    return project_.bpmAt(beat);
}

void Transport::play()
{
    state_ = TransportState::Playing;
}

void Transport::stop()
{
    state_ = TransportState::Stopped;
}

void Transport::record()
{
    state_ = TransportState::Recording;
}

void Transport::rewind()
{
    positionBeat_ = loopEnabled_ ? loopStartBeat_ : 0.0;
}

void Transport::setPositionBeat(double beat)
{
    positionBeat_ = std::max(0.0, beat);
}

void Transport::setLoop(bool enabled, double startBeat, double endBeat)
{
    loopEnabled_ = enabled;
    loopStartBeat_ = std::max(0.0, std::min(startBeat, endBeat));
    loopEndBeat_ = std::max(loopStartBeat_ + 0.03125, std::max(startBeat, endBeat));
}

void Transport::advance(const TempoMap& tempoMap, double seconds)
{
    if (state_ == TransportState::Stopped || seconds <= 0.0) {
        return;
    }

    const double nextSeconds = tempoMap.beatToSeconds(positionBeat_) + seconds;
    positionBeat_ = tempoMap.secondsToBeat(nextSeconds);

    if (loopEnabled_ && positionBeat_ >= loopEndBeat_) {
        const double loopLength = loopEndBeat_ - loopStartBeat_;
        while (positionBeat_ >= loopEndBeat_) {
            positionBeat_ -= loopLength;
        }
    }
}

TransportState Transport::state() const noexcept
{
    return state_;
}

double Transport::positionBeat() const noexcept
{
    return positionBeat_;
}

bool Transport::loopEnabled() const noexcept
{
    return loopEnabled_;
}

double Transport::loopStartBeat() const noexcept
{
    return loopStartBeat_;
}

double Transport::loopEndBeat() const noexcept
{
    return loopEndBeat_;
}

} // namespace bandforge
