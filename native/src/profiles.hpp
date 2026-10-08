#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace pvz::profiles {

using json = nlohmann::json;

// Read-only snapshot of the verified GOTY 1.2.0.1096 ProfileMgr tree.
json snapshot(std::uintptr_t app);

// Capture the in-process baseline before any profile mutation. IDs 1 and 2
// are always protected; every profile present at baseline is protected too.
void initialize_guard(std::uintptr_t app,
                      const std::vector<std::string>& protected_file_names = {});

// Mutation policy helpers for the caller that dispatches normal game dialogs.
// A profile becomes deletable/renamable only after record_created() succeeds.
void authorize_create(std::uintptr_t app, const std::string& name);
void record_created(std::uintptr_t app, std::uint32_t id);
void authorize_test_profile(std::uintptr_t app, std::uint32_t id,
                            const std::string& operation);
void verify_baseline(std::uintptr_t app);
void verify_rename(std::uintptr_t app, std::uint32_t id,
                   const std::string& new_name);
void verify_delete(std::uintptr_t app, std::uint32_t id);

// Start an asynchronous, game-thread profile UI action or query its latest
// status. Supported operation names: profile_open, profile_select,
// profile_create, profile_rename, profile_delete, profile_delete_confirm,
// profile_close, and profile_job. Deletion always pauses for a separate
// profile_delete_confirm call after returning the resolved target identity.
json dispatch(std::uintptr_t app, const std::string& operation, const json& args);

// Advance one pending profile dialog job from the verified game-update hook.
void tick(std::uintptr_t app);

} // namespace pvz::profiles
