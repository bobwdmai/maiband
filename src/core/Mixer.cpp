#include "core/Mixer.h"

#include <algorithm>
#include <cmath>

namespace bandforge::mixer {

double dbToLinear(double db) noexcept
{
    if (db <= -120.0) {
        return 0.0;
    }
    return std::pow(10.0, db / 20.0);
}

double linearToDb(double linear) noexcept
{
    if (linear <= 0.000001) {
        return -120.0;
    }
    return 20.0 * std::log10(linear);
}

std::pair<double, double> equalPowerPan(double pan) noexcept
{
    const double clamped = std::clamp(pan, -1.0, 1.0);
    const double angle = (clamped + 1.0) * 0.25 * 3.14159265358979323846;
    return { std::cos(angle), std::sin(angle) };
}

bool trackAudible(const Track& track, bool anySoloed) noexcept
{
    if (track.mixer.muted) {
        return false;
    }
    if (anySoloed && !track.mixer.solo) {
        return false;
    }
    return true;
}

double peak(std::span<const float> samples) noexcept
{
    double result = 0.0;
    for (const float sample : samples) {
        result = std::max(result, static_cast<double>(std::abs(sample)));
    }
    return result;
}

void applyGain(std::span<float> samples, double gainDb) noexcept
{
    const float gain = static_cast<float>(dbToLinear(gainDb));
    for (auto& sample : samples) {
        sample *= gain;
    }
}

void mixInto(std::span<float> destination, std::span<const float> source, double gainDb) noexcept
{
    const float gain = static_cast<float>(dbToLinear(gainDb));
    const auto count = std::min(destination.size(), source.size());
    for (std::size_t i = 0; i < count; ++i) {
        destination[i] += source[i] * gain;
    }
}

} // namespace bandforge::mixer
