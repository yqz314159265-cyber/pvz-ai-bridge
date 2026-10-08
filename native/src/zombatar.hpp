#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>

namespace pvz::zombatar {

// Read-only state of the main selector, editor, saved-avatar list and verified
// selector routes. Requires the verified 1.2.0.1096 app/object layouts.
nlohmann::json snapshot(std::uintptr_t app);

// Dispatch one explicit user operation through the target's normal selector or
// Zombatar callback. Save-new's target-native JPG side effect is reported in
// results; an unmapped standalone export action remains unsupported.
nlohmann::json action(std::uintptr_t app, const nlohmann::json& request);

// Service operations that need to run on a later normal game update. Must be
// called from the verified game-update thread; exceptions are captured for the
// next snapshot rather than escaping through the game update hook.
void tick(std::uintptr_t app);

} // namespace pvz::zombatar
