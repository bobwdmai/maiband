#pragma once

#include <filesystem>
#include <span>

namespace bandforge {

struct WavExportOptions {
    int sampleRate = 48000;
    int channels = 2;
    int bitDepth = 16;
};

class WavExporter {
public:
    static void writeSilence(const std::filesystem::path& path, double seconds, WavExportOptions options = {});
    static void writeInterleavedFloat(const std::filesystem::path& path, std::span<const float> samples, WavExportOptions options = {});
};

} // namespace bandforge
