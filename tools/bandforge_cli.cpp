#include "core/AudioEngine.h"
#include "core/Exporter.h"
#include "core/Model.h"
#include "core/Transport.h"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {

void printUsage()
{
    std::cout
        << "BandForge CLI\n\n"
        << "Usage:\n"
        << "  bandforge_cli new <project.bandforge>\n"
        << "  bandforge_cli export <project.bandforge> <output.wav>\n";
}

int createProject(const std::filesystem::path& path)
{
    auto project = bandforge::makeStarterProject();
    project.saveBundle(path);
    std::cout << "Created " << path << '\n';
    return 0;
}

int exportProject(const std::filesystem::path& projectPath, const std::filesystem::path& outputPath)
{
    auto project = bandforge::Project::loadBundle(projectPath);
    const int sampleRate = static_cast<int>(project.sampleRate);
    const int channels = 2;
    const double seconds = std::max(1.0, bandforge::TempoMap(project).beatToSeconds(std::max(4.0, project.durationBeats())));
    const auto frames = static_cast<std::size_t>(seconds * static_cast<double>(sampleRate));

    std::vector<float> samples(frames * static_cast<std::size_t>(channels), 0.0f);
    bandforge::AudioEngine engine;
    engine.prepare({ static_cast<double>(sampleRate), 512, channels });
    engine.renderPreview(project, 0.0, samples);
    bandforge::WavExporter::writeInterleavedFloat(outputPath, samples, { sampleRate, channels, 16 });

    std::cout << "Exported " << outputPath << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        if (argc < 2) {
            printUsage();
            return 1;
        }

        const std::string command = argv[1];
        if (command == "new" && argc == 3) {
            return createProject(argv[2]);
        }
        if (command == "export" && argc == 4) {
            return exportProject(argv[2], argv[3]);
        }

        printUsage();
        return 1;
    } catch (const std::exception& error) {
        std::cerr << "bandforge_cli: " << error.what() << '\n';
        return 1;
    }
}
