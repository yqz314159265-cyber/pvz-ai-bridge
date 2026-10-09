#pragma once

#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>

namespace pvz::game_input {

// Internal WidgetManager pointer input for the active Board or SeedChooser.
// Only snapshot/tick and action's main-thread preflight inspect game memory.
nlohmann::json snapshot(std::uintptr_t app, const std::string& board_scope,
                        std::uint32_t main_thread_id);
nlohmann::json action(std::uintptr_t app, const nlohmann::json& args,
                      const std::string& board_scope,
                      std::uint32_t main_thread_id);
nlohmann::json hammer(std::uintptr_t app, const nlohmann::json& args,
                      const std::string& board_scope,
                      std::uint32_t main_thread_id);
nlohmann::json tick(std::uintptr_t app, const std::string& board_scope,
                    std::uint32_t main_thread_id);

} // namespace pvz::game_input
