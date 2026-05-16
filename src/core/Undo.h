#pragma once

#include "core/Model.h"

#include <cstddef>
#include <vector>

namespace bandforge {

class ProjectHistory {
public:
    explicit ProjectHistory(std::size_t maxSnapshots = 64);

    void remember(const Project& project);
    bool undo(Project& project);
    bool redo(Project& project);
    void clear();

    [[nodiscard]] bool canUndo() const noexcept;
    [[nodiscard]] bool canRedo() const noexcept;

private:
    std::size_t maxSnapshots_;
    std::vector<JsonValue> undo_;
    std::vector<JsonValue> redo_;
};

} // namespace bandforge
