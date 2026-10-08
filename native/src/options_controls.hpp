#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace pvz::options_controls {

// Read-only snapshot of the currently open NewOptionsDialog and its controls.
nlohmann::json snapshot(std::uintptr_t app);

// Schedules the ordinary Almanac action, or the main-menu Credits callback;
// closes an open Almanac through LawnApp's verified dialog-close callback.
// Almanac opening is deferred to tick() because it waits synchronously.
nlohmann::json press(std::uintptr_t app, const std::string& name);

// Selects an Almanac plant/zombie by its game enum through the original
// Almanac hit-test and MouseDown handlers.
nlohmann::json select_entry(std::uintptr_t app, const std::string& kind,
                            int index);

// Changes use the verified Slider::SetValue/Checkbox::SetChecked paths and
// deliver their ordinary NewOptionsDialog listeners.
nlohmann::json set(std::uintptr_t app, const std::string& name,
                   const nlohmann::json& value);

// Call on the game's update thread. This may enter the normal nested wait loop.
void tick(std::uintptr_t app);

} // namespace pvz::options_controls
