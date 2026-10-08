#pragma once

#include <cstdint>
#include <string>
#include <nlohmann/json.hpp>

namespace pvz::ui_controls {

// Read-only widget metadata and queued semantic actions. Call snapshot/tick
// only from the verified game-update thread. dispatch only queues validated
// semantic arguments and does not inspect or mutate game memory. The internal
// scope token must change whenever the active scene/dialog generation changes.
nlohmann::json snapshot(std::uintptr_t app, const std::string& scope,
                       std::uint32_t main_thread_id);
nlohmann::json dispatch(std::uintptr_t app, const nlohmann::json& args,
                        const std::string& scope, std::uint32_t main_thread_id);
nlohmann::json tick(std::uintptr_t app, const std::string& scope,
                    std::uint32_t main_thread_id);

} // namespace pvz::ui_controls
