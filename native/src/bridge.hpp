#pragma once

// Called only by the verified game-update hook on the game's main thread.
namespace bridge {
void process_pending();
}
