# Human Operation Matrix (English)

This is the English equivalent of the [Chinese operation matrix](HUMAN_OPERATIONS.zh-CN.md). It covers Steam Plants vs. Zombies GOTY for Windows 1.2.0.1096. Game source, a listed mode, or an accepted API request does not by itself prove that the operation is available in the current profile or that it succeeded. See the [validation summary](../VALIDATION.md) for scoped observations.

| Feature | Ordinary action and constraints | Current bridge coverage |
|---|---|---|
| Title screen | Click “Click to Start” | `click_to_start` is implemented and live-verified. |
| Main-menu routes | Use visible, enabled Adventure, Mini-Games, Puzzle, Options, Help, Shop, Almanac, Zen Garden, Survival, and Achievements controls | `navigate(route)` uses the current UI snapshot and normal selector callbacks. Quit uses a separate lifecycle operation. Change User, Quick Play, and selector Zombatar do not use this selector route. |
| Achievements | Open, inspect earned/shown state, scroll, and return normally | State can be read; scrolling in both directions and the normal back control were live-verified. No API writes achievement state; not every achievement detail route was covered. |
| Adventure | Enter, choose cards, start/continue, and collect a result | Entry, card choice, start, and continue are implemented. One daytime Adventure level was won; the reward transition required human assistance, so this was not a fully automated win. |
| Level catalog | Read game mode definitions and native entry metadata | The runtime catalog returned 73 entries. It is not an unlock list or a proof that every mode can be entered or won. |
| Ordinary challenge pages | Select a currently visible and enabled button in Survival, minigame, or puzzle pages | Entry checks current page and button visibility/enabled state. Pool Endless (mode 13) entry and planting were live-verified; other modes were not individually exercised. |
| Hidden/internal modes | Do not bypass locks or enter through Limbo | Hidden page 2 entries are refused. Zen Garden mode 43 uses its ordinary selector route. Tree of Wisdom mode 50 was reached through the owned garden's normal Next Garden cycle and exited through the ordinary route. |
| Seed selection | Choose currently available cards within slot and unlock rules | `choose_cards` and `start_level` are implemented; Adventure and Pool Endless selection were tested. Imitater target selection is refused. A dedicated cancel-selected-card action is not provided. |
| Planting | Place a selected card on a legal cell | `plant` is implemented and tested in ordinary Adventure and Pool Endless. Special-mode rules are only enabled when native advertises them. |
| Shovel | Remove an ordinary plant from its cell | Semantic `shovel` is implemented for the plant layer. A common board input test selected and right-click-cancelled the shovel without removing a plant. Graves and unsupported layers are not covered. |
| Collectibles | Collect a currently valid sun/coin entity | `collect` is implemented; sun was tested. Automatic coin collection can race a requested collect. Garden collection has not been separately verified. |
| General board/chooser input | Use ordinary left/right-click routes where supported | `game_input` routes common board and seed-chooser input with revalidation. Shovel cancel was tested; other board and chooser interactions need mode-specific verification. A generic click is not proof that every minigame works. |
| Cob Cannon | Aim, fire, or cancel targeting | No dedicated semantic action or live verification for these operations. |
| Wall-nut Bowling | Release nuts and observe collisions | No minigame-specific action has been verified. |
| Vasebreaker | Break a visible vase and observe only the resulting visible state | No dedicated vase interaction is verified. Hidden vase contents must not be exposed. |
| I, Zombie | Select and deploy zombies | No dedicated rules/deployment action is verified. |
| Match-3, slot machine, and other minigames | Exchange, click, or release according to the game's rules | Menu entry exists for some modes; the individual gameplay interactions are not verified. |
| Zombiquarium | Feed fish, tap the glass, and collect coins | The mode is catalogued; gameplay actions are not verified. This is distinct from Zen Garden aquarium plants. |
| Conveyor-belt modes | Use supplied packets and place or cancel them | Planting recognizes conveyor packets, but individual conveyor rules and cancellation are not fully verified. |
| Pause/resume | Pause the bridge or resume it | Bridge `pause`/`resume` and background progress have been tested. These are not a universal in-game pause-menu control. |
| Options dialog | Open, close, or return to play through ordinary controls | Open/close routes are implemented; the almanac flow exercised ordinary dialog handling. |
| Music and sound sliders | Drag ordinary UI sliders | Both sliders were dragged to requested values and restored. Game values are pixel-quantized, so requested and read-back values may differ. |
| Fullscreen/hardware acceleration | Toggle the ordinary checkboxes | Both controls were toggled and restored in a live check. Results are specific to that test environment. |
| Almanac | Open/close, choose plant/zombie entries, and change Index/Plants/Zombies pages | The 10-step ordinary-control flow was live-verified, including previews. The normal 10-step UI sequence was completed, including entry selection and previews. |
| Credits | Enter/play/skip/exit credits | Normal entry, playback, Escape confirmation, and return to the main menu were tested. Replay/Main Menu controls during playback were not verified; bonus-video replay remains untested. |
| Help | Open Help and continue through its normal control | Open and Continue were live-verified; individual help content was not reviewed. |
| Quit | Open quit dialog, cancel, or confirm exit | Opening, canceling, and normal confirmation were live-verified; Steam also restarted successfully afterward. The generic selector `navigate(quit)` is unsupported. |
| Return to menu/restart | Use ordinary save/confirmation controls or Restart | Restart rebuilt the board, returned to card selection, and reset the board tick. Adventure and Garden return flows have partial checks; Tree mode 50 exited to the main menu through the ordinary route. |
| Profile list/select | View profiles and select a verified identity | Full test-profile lifecycle covered list/open/select/close, including selection of a newly created profile. |
| Create profile | Enter a valid name through the game's UI | Creation of a session test profile was live-verified. Protected original profiles cannot be mutated through this workflow. |
| Rename profile | Rename a session-created test profile | Live-verified on the test profile; game name and duplicate-name checks apply. |
| Delete profile | Cancel or confirm deletion of a session-created test profile | Both cancel and confirm routes were tested. At the end of that profile test, 19 original save files matched a fresh backup; later normal Garden/avatar actions changed game data, so that comparison is limited to its timestamp. Original profiles remain protected. |
| Backup/restore | Create a save copy and verify its hashes | Backup copies discovered saves and writes a SHA-256 manifest. Automatic restore is not provided. |
| Crazy Dave's shop | Browse, page, purchase one item, and close | Browsing, page changes, budget rejection, one Tree Food purchase, and close were tested. Purchase still depends on stock, price, coins, and unlocks. |
| Zen Garden state | Read garden, plants, needs, and inventory | Main Garden state and plants were read. Other garden types require ownership and were not all available in the test profile. |
| Garden entry/switching | Enter an owned garden and use normal Next Garden cycling | Main Garden and owned Tree of Wisdom entry/return were tested through normal routes. Mushroom/Aquarium gardens were not owned; those switches remain unverified. |
| Garden care | Water, fertilize, spray, or play music when applicable | `garden_care` accepts `water`, `fertilizer`, `bug_spray`, and `phonograph`. Watering changed needs to none on two plants; the other tools were not individually tested. Owned tools and inventory limit what is usable. |
| Move pots/wheelbarrow | Pick up, move, and place owned plants on valid empty cells | Semantic operations are implemented with ownership/space checks; not fully live-tested. |
| Sell a plant | Use money bag and confirm/cancel through normal UI | Implemented with test-profile safeguards; no live test claimed. |
| Stinky the snail | Feed chocolate or wake a sleeping snail without chocolate | Feed/wake actions exist with cursor and inventory checks; not live-tested. The exact Windows readme describes coin collection and chocolate to keep Stinky active, but does not establish a separate ordinary release/recall control. |
| Chocolate | Feed eligible garden plants or Stinky | Implemented with target and inventory checks; not individually live-tested. |
| Garden drops | Collect garden coins or diamonds | The general collect action can target a current valid collectible; garden-specific collection is not live-verified. |
| Tree of Wisdom | Enter via owned garden, leave normally, and feed Tree Food | Ordinary entry, return to Garden, re-entry, and main-menu exit were tested. Tree height remained unchanged and no Tree Food was spent. Feeding remains untested. Hidden mode entry is not used. |
| Zombatar opening/terms | Open from the main menu and accept terms | Open and terms actions are implemented; term acceptance is not separately live-verified. |
| Zombatar parts/palette | Select a part category, palette color, page, and visible item | Skin palette color, one part category across two pages, and a background item were live-tested. Other category semantics are not inferred. |
| Save/browse avatar | Save a new avatar, browse saved avatars, and return | Save and return were tested; the produced 180×180 JPG was inspected. New-avatar creation and switching among multiple saved avatars are not separately verified. |
| Delete avatar | Delete only the new avatar created by this bridge session | The session-created avatar was deleted after confirmation and count returned from one to zero. Existing avatars are protected. |
| Export avatar | Save/export an image file | The normal save path produced a desktop JPG. A separate export action is not provided. |
| Generic UI controls | Activate visible/enabled controls or drag a slider in the current scope | UI snapshot and delayed revalidation are implemented with protective restrictions. Generic text entry, arbitrary keyboard/scroll input, and arbitrary mouse-message scripting are not provided. |

## Remaining limits

Many special-mode gameplay interactions, seed-selection cancellation, garden drops, unowned garden switching, garden moving/selling/tree-feeding, and untested Zombatar categories remain incomplete or unverified. These are operation-coverage gaps, not a request to invent automated strategies. No all-mode automatic-win claim is made.
