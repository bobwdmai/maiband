#pragma once

#include "core/Model.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace bandforge {

enum class PluginFormat {
    BuiltIn,
    Vst3,
    Lv2
};

struct PluginDescriptor {
    std::string id;
    std::string name;
    std::string manufacturer;
    std::string category;
    PluginFormat format = PluginFormat::BuiltIn;
    std::filesystem::path path;
    bool instrument = false;
};

class PluginHost {
public:
    PluginHost();

    void addSearchPath(std::filesystem::path path);
    void registerPlugin(PluginDescriptor descriptor);

    [[nodiscard]] const std::vector<std::filesystem::path>& searchPaths() const noexcept;
    [[nodiscard]] std::vector<PluginDescriptor> plugins() const;
    [[nodiscard]] std::optional<PluginDescriptor> findPlugin(const std::string& id) const;

    [[nodiscard]] EffectSlot makeEffectSlot(const std::string& id) const;
    [[nodiscard]] InstrumentSlot makeInstrumentSlot(const std::string& id, const std::string& presetName) const;

private:
    std::vector<std::filesystem::path> searchPaths_;
    std::vector<PluginDescriptor> plugins_;
};

[[nodiscard]] std::string toString(PluginFormat format);

} // namespace bandforge
