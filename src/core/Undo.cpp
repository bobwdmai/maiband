#include "core/Undo.h"

#include <algorithm>

namespace bandforge {

ProjectHistory::ProjectHistory(std::size_t maxSnapshots)
    : maxSnapshots_(std::max<std::size_t>(1, maxSnapshots))
{
}

void ProjectHistory::remember(const Project& project)
{
    undo_.push_back(project.toJson());
    redo_.clear();

    if (undo_.size() > maxSnapshots_) {
        undo_.erase(undo_.begin());
    }
}

bool ProjectHistory::undo(Project& project)
{
    if (undo_.empty()) {
        return false;
    }

    redo_.push_back(project.toJson());
    project = Project::fromJson(undo_.back());
    undo_.pop_back();
    return true;
}

bool ProjectHistory::redo(Project& project)
{
    if (redo_.empty()) {
        return false;
    }

    undo_.push_back(project.toJson());
    project = Project::fromJson(redo_.back());
    redo_.pop_back();
    return true;
}

void ProjectHistory::clear()
{
    undo_.clear();
    redo_.clear();
}

bool ProjectHistory::canUndo() const noexcept
{
    return !undo_.empty();
}

bool ProjectHistory::canRedo() const noexcept
{
    return !redo_.empty();
}

} // namespace bandforge
