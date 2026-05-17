#include "core/InstrumentHost.h"

#include <stdexcept>
#include <utility>

namespace bandforge {

InstrumentHost::InstrumentHost()
{
    registerInstrument({
        "poly-synth",
        "Poly Synth",
        "Keyboards",
        { { "attack", 0.02 }, { "decay", 0.18 }, { "sustain", 0.72 }, { "release", 0.28 }, { "tone", 0.55 } },
    });
    registerInstrument({
        "lead-synth",
        "Lead Synth",
        "Synths",
        { { "attack", 0.0 }, { "release", 0.14 }, { "tone", 0.82 }, { "glide", 0.12 } },
    });
    registerInstrument({
        "bass-synth",
        "Bass Synth",
        "Bass",
        { { "attack", 0.01 }, { "release", 0.18 }, { "tone", 0.38 }, { "sub", 0.72 } },
    });
    registerInstrument({
        "pad-synth",
        "Pad Synth",
        "Pads",
        { { "attack", 0.55 }, { "release", 0.72 }, { "tone", 0.48 }, { "motion", 0.42 } },
    });
    registerInstrument({
        "electric-piano",
        "Electric Piano",
        "Keyboards",
        { { "attack", 0.01 }, { "release", 0.35 }, { "tone", 0.58 }, { "bell", 0.62 } },
    });
    registerInstrument({
        "organ",
        "Organ",
        "Keyboards",
        { { "attack", 0.0 }, { "release", 0.18 }, { "drawbar", 0.72 }, { "rotary", 0.38 } },
    });
    registerInstrument({
        "brass",
        "Brass",
        "Orchestral",
        { { "attack", 0.08 }, { "release", 0.26 }, { "tone", 0.68 }, { "swell", 0.48 } },
    });
    registerInstrument({
        "choir",
        "Choir",
        "Orchestral",
        { { "attack", 0.45 }, { "release", 0.82 }, { "tone", 0.44 }, { "ensemble", 0.72 } },
    });
    registerInstrument({
        "mallet",
        "Mallet",
        "Percussion",
        { { "attack", 0.0 }, { "release", 0.28 }, { "tone", 0.78 }, { "strike", 0.7 } },
    });
    registerInstrument({
        "woodwind",
        "Woodwind",
        "Orchestral",
        { { "attack", 0.04 }, { "release", 0.32 }, { "tone", 0.52 }, { "breath", 0.38 } },
    });
    registerInstrument({
        "strings",
        "Strings",
        "Orchestral",
        { { "attack", 0.18 }, { "release", 0.45 }, { "tone", 0.56 }, { "ensemble", 0.66 } },
    });
    registerInstrument({
        "guitar-synth",
        "Guitar Synth",
        "Guitars",
        { { "attack", 0.01 }, { "release", 0.28 }, { "tone", 0.62 }, { "pluck", 0.5 } },
    });
    registerInstrument({
        "arp-synth",
        "Arp Synth",
        "Synths",
        { { "rate", 0.5 }, { "gate", 0.42 }, { "tone", 0.7 }, { "motion", 0.5 } },
    });
    registerInstrument({
        "pluck-synth",
        "Pluck Synth",
        "Synths",
        { { "attack", 0.0 }, { "release", 0.2 }, { "tone", 0.76 }, { "pluck", 0.85 } },
    });
    registerInstrument({
        "drum-machine",
        "Drum Machine",
        "Drums",
        { { "kickTune", 0.45 }, { "snareSnap", 0.62 }, { "hatTone", 0.74 }, { "room", 0.18 } },
    });
    registerInstrument({
        "drum-rack",
        "Drum Rack",
        "Drums",
        { { "kickTune", 0.38 }, { "snareSnap", 0.7 }, { "hatTone", 0.82 }, { "drive", 0.18 } },
    });
    registerInstrument({
        "beat-sequencer",
        "Beat Sequencer",
        "Drums",
        { { "swing", 0.08 }, { "density", 0.55 }, { "drive", 0.12 }, { "room", 0.12 } },
    });
    registerInstrument({
        "808",
        "808",
        "Drums",
        { { "subTune", 0.32 }, { "decay", 0.68 }, { "click", 0.24 }, { "drive", 0.2 } },
    });
    registerInstrument({
        "sampler",
        "Sampler",
        "Samples",
        { { "rootNote", 60.0 }, { "attack", 0.0 }, { "release", 0.12 }, { "gainDb", 0.0 } },
    });
}

void InstrumentHost::registerInstrument(InstrumentDefinition definition)
{
    if (definition.type.empty()) {
        throw std::invalid_argument("Instrument type must not be empty");
    }
    instruments_[definition.type] = std::move(definition);
}

std::vector<InstrumentDefinition> InstrumentHost::instruments() const
{
    std::vector<InstrumentDefinition> result;
    result.reserve(instruments_.size());
    for (const auto& [_, definition] : instruments_) {
        result.push_back(definition);
    }
    return result;
}

std::optional<InstrumentDefinition> InstrumentHost::findInstrument(const std::string& type) const
{
    const auto found = instruments_.find(type);
    if (found == instruments_.end()) {
        return std::nullopt;
    }
    return found->second;
}

InstrumentSlot InstrumentHost::makeSlot(const std::string& type, const std::string& presetName) const
{
    const auto definition = findInstrument(type);
    if (!definition.has_value()) {
        throw std::invalid_argument("Unknown instrument type: " + type);
    }

    InstrumentSlot slot;
    slot.id = "instrument-" + type;
    slot.type = type;
    slot.presetName = presetName;
    slot.parameters = definition->defaultParameters;
    return slot;
}

} // namespace bandforge
