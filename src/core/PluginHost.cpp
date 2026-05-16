#include "core/PluginHost.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace bandforge {

PluginHost::PluginHost()
{
    addSearchPath("/usr/lib/vst3");
    addSearchPath("/usr/local/lib/vst3");
    addSearchPath("/usr/lib/lv2");
    addSearchPath("/usr/local/lib/lv2");

    registerPlugin({ "builtin-eq", "Clean EQ", "BandForge", "EQ", PluginFormat::BuiltIn, {}, false });
    registerPlugin({ "builtin-compressor", "Level Compressor", "BandForge", "Dynamics", PluginFormat::BuiltIn, {}, false });
    registerPlugin({ "builtin-reverb", "Room Reverb", "BandForge", "Reverb", PluginFormat::BuiltIn, {}, false });
    registerPlugin({ "builtin-delay", "Tape Delay", "BandForge", "Delay", PluginFormat::BuiltIn, {}, false });
    registerPlugin({ "builtin-limiter", "Output Limiter", "BandForge", "Dynamics", PluginFormat::BuiltIn, {}, false });
    registerPlugin({ "builtin-poly-synth", "Poly Synth", "BandForge", "Instrument", PluginFormat::BuiltIn, {}, true });
    registerPlugin({ "builtin-lead-synth", "Lead Synth", "BandForge", "Instrument", PluginFormat::BuiltIn, {}, true });
    registerPlugin({ "builtin-bass-synth", "Bass Synth", "BandForge", "Instrument", PluginFormat::BuiltIn, {}, true });
    registerPlugin({ "builtin-pad-synth", "Pad Synth", "BandForge", "Instrument", PluginFormat::BuiltIn, {}, true });
    registerPlugin({ "builtin-strings", "Strings", "BandForge", "Instrument", PluginFormat::BuiltIn, {}, true });
    registerPlugin({ "builtin-guitar-synth", "Guitar Synth", "BandForge", "Instrument", PluginFormat::BuiltIn, {}, true });
    registerPlugin({ "builtin-arp-synth", "Arp Synth", "BandForge", "Instrument", PluginFormat::BuiltIn, {}, true });
    registerPlugin({ "builtin-pluck-synth", "Pluck Synth", "BandForge", "Instrument", PluginFormat::BuiltIn, {}, true });
    registerPlugin({ "builtin-drum-machine", "Drum Machine", "BandForge", "Instrument", PluginFormat::BuiltIn, {}, true });
    registerPlugin({ "builtin-drum-rack", "Drum Rack", "BandForge", "Instrument", PluginFormat::BuiltIn, {}, true });
    registerPlugin({ "builtin-beat-sequencer", "Beat Sequencer", "BandForge", "Instrument", PluginFormat::BuiltIn, {}, true });
    registerPlugin({ "builtin-808", "808", "BandForge", "Instrument", PluginFormat::BuiltIn, {}, true });
}

void PluginHost::addSearchPath(std::filesystem::path path)
{
    if (path.empty()) {
        return;
    }
    if (std::find(searchPaths_.begin(), searchPaths_.end(), path) == searchPaths_.end()) {
        searchPaths_.push_back(std::move(path));
    }
}

void PluginHost::registerPlugin(PluginDescriptor descriptor)
{
    if (descriptor.id.empty()) {
        throw std::invalid_argument("Plugin id must not be empty");
    }

    const auto found = std::find_if(plugins_.begin(), plugins_.end(), [&](const PluginDescriptor& plugin) {
        return plugin.id == descriptor.id;
    });

    if (found == plugins_.end()) {
        plugins_.push_back(std::move(descriptor));
    } else {
        *found = std::move(descriptor);
    }
}

const std::vector<std::filesystem::path>& PluginHost::searchPaths() const noexcept
{
    return searchPaths_;
}

std::vector<PluginDescriptor> PluginHost::plugins() const
{
    return plugins_;
}

std::optional<PluginDescriptor> PluginHost::findPlugin(const std::string& id) const
{
    const auto found = std::find_if(plugins_.begin(), plugins_.end(), [&](const PluginDescriptor& plugin) {
        return plugin.id == id;
    });
    if (found == plugins_.end()) {
        return std::nullopt;
    }
    return *found;
}

EffectSlot PluginHost::makeEffectSlot(const std::string& id) const
{
    const auto descriptor = findPlugin(id);
    if (!descriptor.has_value() || descriptor->instrument) {
        throw std::invalid_argument("Unknown effect plugin: " + id);
    }

    EffectSlot slot;
    slot.id = descriptor->id;
    slot.type = toString(descriptor->format);
    slot.name = descriptor->name;
    slot.parameters = { { "mix", 1.0 } };
    return slot;
}

InstrumentSlot PluginHost::makeInstrumentSlot(const std::string& id, const std::string& presetName) const
{
    const auto descriptor = findPlugin(id);
    if (!descriptor.has_value() || !descriptor->instrument) {
        throw std::invalid_argument("Unknown instrument plugin: " + id);
    }

    InstrumentSlot slot;
    slot.id = descriptor->id;
    slot.type = toString(descriptor->format);
    slot.presetName = presetName;
    slot.parameters = { { "gainDb", 0.0 } };
    return slot;
}

std::string toString(PluginFormat format)
{
    switch (format) {
    case PluginFormat::BuiltIn:
        return "builtin";
    case PluginFormat::Vst3:
        return "vst3";
    case PluginFormat::Lv2:
        return "lv2";
    }
    return "builtin";
}

} // namespace bandforge
