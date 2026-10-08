#pragma once

#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>

namespace pvz::selector_controls {

// Main-menu metadata and navigation for the verified Steam GOTY 1.2.0.1096
// build. Call on the verified update thread. `scope` must be rotated whenever
// the selector/dialog generation changes, just like ui_controls::snapshot.
nlohmann::json snapshot(std::uintptr_t app, const std::string& scope,
                        std::uint32_t main_thread_id);

// Queue a semantic selector action. Navigation rechecks the visible/enabled
// selector widget and ordinary GameSelector callback. Achievement return uses
// the verified selector transition helper; bounded scroll uses
// its normal signed wheel handler only while that screen owns input focus. tick() performs
// each action after this request has returned to its caller.
nlohmann::json action(std::uintptr_t app, const nlohmann::json& request,
                      const std::string& scope, std::uint32_t main_thread_id);

// Advance at most one queued action on the verified game-update thread.
void tick(std::uintptr_t app, const std::string& scope,
          std::uint32_t main_thread_id);

} // namespace pvz::selector_controls
