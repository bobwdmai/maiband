#pragma once

#include "core/Model.h"

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace bandforge {

struct InstrumentDefinition {
    std::string type;
    std::string displayName;
    std::string category;
    std::map<std::string, double> defaultParameters;
};

class InstrumentHost {
public:
    InstrumentHost();

    void registerInstrument(InstrumentDefinition definition);

    [[nodiscard]] std::vector<InstrumentDefinition> instruments() const;
    [[nodiscard]] std::optional<InstrumentDefinition> findInstrument(const std::string& type) const;
    [[nodiscard]] InstrumentSlot makeSlot(const std::string& type, const std::string& presetName) const;

private:
    std::map<std::string, InstrumentDefinition> instruments_;
};

} // namespace bandforge
