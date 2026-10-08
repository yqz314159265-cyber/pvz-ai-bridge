#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace pvz::lifecycle_controls {

// Read-only lifecycle/UI snapshot. Loading-screen, main-menu Quit, and Credits
// Escape controls are exposed only in their verified active contexts.
nlohmann::json snapshot(std::uintptr_t app);

// Schedules a normal title-screen callback, the selector's ordinary ID-105
// Quit callback, a captured Quit dialog choice, or CreditsScreen's normal
// Escape key handler. quit_confirm requires confirm=true.
nlohmann::json action(std::uintptr_t app, const std::string& name, bool confirm = false);

// Call from the verified game-update thread. Executes at most one scheduled
// ordinary UI callback after revalidating its scene and widget identities.
void tick(std::uintptr_t app);

} // namespace pvz::lifecycle_controls
