#pragma once

#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>

namespace pvz::shop {

using json = nlohmann::json;

// Store helpers are for the verified Steam GOTY 1.2.0.1096 x86 image and must
// only be called by the game's update thread.
json snapshot(std::uintptr_t app);
json dispatch(std::uintptr_t app, const std::string& type, const json& args);

// Called from the main-loop hook while a native modal dialog is pumping the
// game loop. Kept inert until a purchase action has an exact validated modal.
void tick(std::uintptr_t app);

} // namespace pvz::shop
