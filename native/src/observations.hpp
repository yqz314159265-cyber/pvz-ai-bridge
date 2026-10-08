#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>

namespace pvz::observations {

// Read-only entity snapshot for the supported Steam GOTY 1.2.0.1096 Board.
// Throws std::runtime_error if any required game-memory region is invalid.
nlohmann::json snapshot(std::uintptr_t board);

} // namespace pvz::observations
