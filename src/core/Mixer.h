#pragma once

#include "core/Model.h"

#include <span>
#include <utility>

namespace bandforge::mixer {

[[nodiscard]] double dbToLinear(double db) noexcept;
[[nodiscard]] double linearToDb(double linear) noexcept;
[[nodiscard]] std::pair<double, double> equalPowerPan(double pan) noexcept;
[[nodiscard]] bool trackAudible(const Track& track, bool anySoloed) noexcept;
[[nodiscard]] double peak(std::span<const float> samples) noexcept;

void applyGain(std::span<float> samples, double gainDb) noexcept;
void mixInto(std::span<float> destination, std::span<const float> source, double gainDb) noexcept;

} // namespace bandforge::mixer
