#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>

namespace pvz::continue_dialog {
bool is_open(std::uintptr_t app);

// Runs ContinueDialog's ordinary Continue button (ID 0) on the verified game
// update thread. It requires the current 1.2.0.1096 dialog ID 37 and verified
// class/listener vtables; it never chooses Restart (button ID 1).
nlohmann::json continue_saved_game(std::uintptr_t app);

} // namespace pvz::continue_dialog
