#pragma once

#include <cstdint>
#include <nlohmann/json.hpp>

namespace pvz::garden {

enum class CareKind { water, fertilize, bug_spray, phonograph };

// Read-only snapshot of the persistent potted-plant inventory and the active
// garden board. Throws std::runtime_error when memory/layout bounds fail.
nlohmann::json snapshot(std::uintptr_t app);

// Read-only state for the separate Tree of Wisdom mode (mode 50).
nlohmann::json tree_snapshot(std::uintptr_t app);

// Advance a queued garden care action on the game's main thread once per
// framework update. Integrators should call this before draining new requests.
void tick(std::uintptr_t app);

// Uses the supported build's normal in-game pointer-move and ZenGarden feeding
// tool handlers (water, fertilizer, bug spray, or phonograph). Must be called
// on the game's main thread. It selects the requested tool through the normal
// board hit-test, moves the in-game pointer to the plant, and queues
// revalidation/dispatch for a later update tick. It does not write inventory
// or plant fields directly.
nlohmann::json care(std::uintptr_t app, std::uint32_t plant_id, CareKind kind);

// Feeds the visible Stinky snail with chocolate through the ordinary garden
// feeding-tool path. The stable GridItem DataArray ID must come from a current
// snapshot; the queued update verifies the snail highlight and no overlapping
// potted-plant highlight before dispatch.
nlohmann::json feed_snail(std::uintptr_t app, std::uint32_t stinky_id);

// Wakes a sleeping Stinky with a normal Board click and no chocolate. Requires
// the normal cursor and an exact currently sleeping Stinky GridItem ID.
nlohmann::json wake_snail(std::uintptr_t app, std::uint32_t stinky_id);

// Cancels the current garden/tool cursor with the target's native right-click
// route (WidgetManager button value -1); no OS mouse event is generated.
nlohmann::json cursor_cancel(std::uintptr_t app);

// Completes only the pending sale for this exact full Plant DataArray ID.
// Confirmation and cancellation route through the ordinary verified dialog
// key handler (Yes=Enter, No=Escape) and refuse a changed potted-plant target.
nlohmann::json confirm_sale(std::uintptr_t app, std::uint32_t plant_id);
nlohmann::json cancel_sale(std::uintptr_t app, std::uint32_t plant_id);

// Requests the game's ordinary money-sign sale flow for one current potted
// plant. The normal Zen Garden Yes/No confirmation is shown; this wrapper
// never confirms it on the player's behalf.
nlohmann::json sell_plant(std::uintptr_t app, std::uint32_t plant_id);

// Transfers one current potted plant into an empty owned wheelbarrow using the
// game's ordinary wheelbarrow tool. Placing it into another garden is separate.
nlohmann::json put_in_wheelbarrow(std::uintptr_t app, std::uint32_t plant_id);

// Feeds the Tree of Wisdom with one owned Tree Food item through the normal
// Tree Food toolbar and Board mouse-down route.
nlohmann::json feed_tree(std::uintptr_t app);

// Moves a potted plant to an empty cell through the ordinary garden glove
// selection and Board plant-placement route. row/col are 1-based.
nlohmann::json move_plant(std::uintptr_t app, std::uint32_t plant_id,
                          std::int32_t row, std::int32_t col);

// Places the plant currently stored in the wheelbarrow into an empty cell
// through the ordinary wheelbarrow tool. row/col are 1-based.
nlohmann::json place_wheelbarrow_plant(std::uintptr_t app,
                                       std::int32_t row, std::int32_t col);

// Clicks the ordinary Next Garden button. The game chooses the next owned
// garden or Tree of Wisdom using its normal cycle.
nlohmann::json next_garden(std::uintptr_t app);

// Queues normal Next Garden clicks until the requested owned public garden
// (0 main, 1 mushroom, 2 aquarium) is active. Never enters a mode directly.
nlohmann::json enter_garden(std::uintptr_t app, std::int32_t garden_id);

// Feeds one current potted plant chocolate via the normal feeding tool.
nlohmann::json feed_plant_chocolate(std::uintptr_t app, std::uint32_t plant_id);

} // namespace pvz::garden
