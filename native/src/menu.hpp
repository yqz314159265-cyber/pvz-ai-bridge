#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>

namespace pvz::menu {

// Call only from the verified game-update thread. Opens the game's ordinary
// options dialog in an active level; it does not select a menu item or dismiss
// any confirmation dialog.
nlohmann::json open_options(std::uintptr_t app);

// Runs the real Main Menu button callback from an already-open options dialog.
// A save confirmation is reported as pending and is never auto-accepted here.
nlohmann::json back_to_menu(std::uintptr_t app);

// Accept the game's existing Leave Game confirmation through LawnApp's normal
// ButtonListener callback (dialog ID 22, Yes). Requires that dialog to exist.
nlohmann::json confirm_back_to_menu(std::uintptr_t app);

// Routes through the options dialog's ordinary Back to Game button (ID 1000).
nlohmann::json close_options(std::uintptr_t app);

// Restart follows the options dialog's ordinary callback. Its nested update
// loop is serviced by tick(), which confirms the game's restart dialog through
// the dialog's ordinary Enter key path.
nlohmann::json restart_level(std::uintptr_t app);
// Call from every verified game update, including nested dialog updates.
void tick(std::uintptr_t app);
// Kept for API compatibility; restart confirmation is handled by restart_level.
nlohmann::json confirm_restart(std::uintptr_t app);

// Read-only dialog/layout diagnostic. Reports the current scene and the live
// vtable prefix for the requested ordinary options/return-confirm dialogs.
nlohmann::json snapshot(std::uintptr_t app);

// Starts the game's ordinary selector flow. Challenge modes are admitted only
// when their current ChallengeScreen button is visible and enabled. Tree of
// Wisdom mode 50 is intentionally reached through the garden API's ordinary
// Next Garden route; it is not opened through a hidden challenge page.
nlohmann::json enter_mode(std::uintptr_t app, int mode_id);

} // namespace pvz::menu
