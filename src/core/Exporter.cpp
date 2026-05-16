#include "core/Exporter.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace bandforge {
namespace {

void writeU16(std::ofstream& output, std::uint16_t value)
{
    output.put(static_cast<char>(value & 0xFFU));
    output.put(static_cast<char>((value >> 8U) & 0xFFU));
}

void writeU32(std::ofstream& output, std::uint32_t value)
{
    output.put(static_cast<char>(value & 0xFFU));
    output.put(static_cast<char>((value >> 8U) & 0xFFU));
    output.put(static_cast<char>((value >> 16U) & 0xFFU));
    output.put(static_cast<char>((value >> 24U) & 0xFFU));
}

std::int16_t floatToPcm16(float sample)
{
    const float clamped = std::clamp(sample, -1.0f, 1.0f);
    return static_cast<std::int16_t>(clamped < 0.0f ? clamped * 32768.0f : clamped * 32767.0f);
}

void validateOptions(const WavExportOptions& options)
{
    if (options.sampleRate <= 0) {
        throw std::invalid_argument("WAV sample rate must be positive");
    }
    if (options.channels <= 0 || options.channels > 32) {
        throw std::invalid_argument("WAV channel count must be between 1 and 32");
    }
    if (options.bitDepth != 16) {
        throw std::invalid_argument("Only 16-bit PCM WAV export is currently implemented");
    }
}

void writeWav(std::ofstream& output, std::span<const float> samples, const WavExportOptions& options)
{
    const std::uint16_t blockAlign = static_cast<std::uint16_t>(options.channels * (options.bitDepth / 8));
    const std::uint32_t byteRate = static_cast<std::uint32_t>(options.sampleRate * blockAlign);
    const std::uint32_t dataBytes = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    const std::uint32_t riffBytes = 36U + dataBytes;

    output.write("RIFF", 4);
    writeU32(output, riffBytes);
    output.write("WAVE", 4);

    output.write("fmt ", 4);
    writeU32(output, 16);
    writeU16(output, 1);
    writeU16(output, static_cast<std::uint16_t>(options.channels));
    writeU32(output, static_cast<std::uint32_t>(options.sampleRate));
    writeU32(output, byteRate);
    writeU16(output, blockAlign);
    writeU16(output, static_cast<std::uint16_t>(options.bitDepth));

    output.write("data", 4);
    writeU32(output, dataBytes);
    for (const float sample : samples) {
        writeU16(output, static_cast<std::uint16_t>(floatToPcm16(sample)));
    }
}

} // namespace

void WavExporter::writeSilence(const std::filesystem::path& path, double seconds, WavExportOptions options)
{
    validateOptions(options);
    if (seconds < 0.0) {
        throw std::invalid_argument("WAV duration must not be negative");
    }

    const auto frames = static_cast<std::size_t>(seconds * static_cast<double>(options.sampleRate));
    std::vector<float> samples(frames * static_cast<std::size_t>(options.channels), 0.0f);
    writeInterleavedFloat(path, samples, options);
}

void WavExporter::writeInterleavedFloat(const std::filesystem::path& path, std::span<const float> samples, WavExportOptions options)
{
    validateOptions(options);
    if (samples.size() % static_cast<std::size_t>(options.channels) != 0U) {
        throw std::invalid_argument("Interleaved sample count must be divisible by channel count");
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("Could not create WAV file: " + path.string());
    }

    writeWav(output, samples, options);
}

} // namespace bandforge
