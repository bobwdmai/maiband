#pragma once

#include "core/Model.h"
#include "core/Types.h"

namespace bandforge {

class TempoMap {
public:
    explicit TempoMap(const Project& project);

    [[nodiscard]] double beatToSeconds(double beat) const;
    [[nodiscard]] double secondsToBeat(double seconds) const;
    [[nodiscard]] double bpmAt(double beat) const;

private:
    const Project& project_;
};

class Transport {
public:
    void play();
    void stop();
    void record();
    void rewind();

    void setPositionBeat(double beat);
    void setLoop(bool enabled, double startBeat, double endBeat);
    void advance(const Project& project, double seconds);

    [[nodiscard]] TransportState state() const noexcept;
    [[nodiscard]] double positionBeat() const noexcept;
    [[nodiscard]] bool loopEnabled() const noexcept;
    [[nodiscard]] double loopStartBeat() const noexcept;
    [[nodiscard]] double loopEndBeat() const noexcept;

private:
    TransportState state_ = TransportState::Stopped;
    double positionBeat_ = 0.0;
    bool loopEnabled_ = false;
    double loopStartBeat_ = 0.0;
    double loopEndBeat_ = 8.0;
};

} // namespace bandforge
