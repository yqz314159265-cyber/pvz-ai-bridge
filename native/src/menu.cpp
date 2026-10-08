#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

#include "menu.hpp"

namespace pvz::menu {
namespace {
using json = nlohmann::json;

constexpr std::uintptr_t kGlobalApp = 0x731c50;
constexpr std::uintptr_t kOpenOptions = 0x454e10;
constexpr std::uintptr_t kNewOptionsButtonDepress = 0x463620;
constexpr std::uintptr_t kAppButtonDepress = 0x458590;
constexpr std::uintptr_t kGameSelectorButtonDepress = 0x450ee0;
constexpr std::uintptr_t kGameSelectorVtable = 0x6eec88;
constexpr std::uintptr_t kGameSelectorListenerVtable = 0x6eeda0;
constexpr std::uintptr_t kChallengeScreenVtable = 0x6ed9f0;
constexpr std::uintptr_t kChallengeListenerVtable = 0x6edb08;
constexpr std::uintptr_t kChallengeButtonDepress = 0x4334f0;
constexpr std::uintptr_t kChallengeButtonVtable = 0x6fc38c;
constexpr std::uintptr_t kChallengeDefinitions = 0x72a370;
constexpr std::uintptr_t kRestartConfirmDialogVtable = 0x6f1d38;
constexpr std::uintptr_t kRestartConfirmKeyDown = 0x45d520;
constexpr std::size_t kAppBoard = 0x868;
constexpr std::size_t kAppSelector = 0x870;
constexpr std::size_t kAppChooser = 0x874;
constexpr std::size_t kAppChallengeScreen = 0x880;
constexpr std::size_t kAppMode = 0x918;
constexpr std::size_t kAppScene = 0x91c;
constexpr std::size_t kAppPlayerInfo = 0x94c;
constexpr std::size_t kAppVtableGetDialog = 0x124;
constexpr std::size_t kAppVtableGetDialogCount = 0x13c;
constexpr std::size_t kDialogButtonListener = 0xa0;
constexpr std::size_t kSelectorButtonListener = 0xa0;
constexpr std::size_t kButtonDepressSlot = 0x08;
constexpr std::size_t kDialogKeyDownSlot = 0xc0;
constexpr std::size_t kAppBoardResult = 0x9a8;
constexpr std::size_t kBoardMainCounter = 0x5580;
constexpr std::int32_t kRestartIntroTickMax = 1;
constexpr int kSceneLevelIntro = 2;
constexpr int kScenePlaying = 3;
constexpr int kDialogNewOptions = 2;
constexpr int kDialogConfirmBackToMain = 22;
constexpr int kDialogConfirmRestart = 23;
constexpr int kRestartConfirmEnter = 13;
constexpr int kOptionsButtonMainMenu = 1;
constexpr int kOptionsButtonRestart = 2;
constexpr int kDialogIdYesBase = 2000;
constexpr int kButtonBackToGame = 1000;
constexpr int kModeZenGarden = 43;
constexpr int kSceneMenu = 1;
constexpr int kSceneChallenge = 7;
constexpr int kSelectorButtonZenGarden = 109;
constexpr int kSelectorButtonSurvival = 110;
constexpr int kModeSurvivalEndless = 13;
constexpr std::size_t kChallengeListener = 0xa0;
constexpr std::size_t kChallengeButtons = 0xb8;
constexpr std::size_t kChallengePage = 0x1e0;
constexpr std::size_t kChallengeButtonCount = 72;
constexpr std::size_t kChallengeDefinitionStride = 0x18;
constexpr std::size_t kChallengeDefinitionPage = 0x08;
constexpr std::size_t kChallengeEndlessButtonIndex = 12;
constexpr std::size_t kChallengeButtonVisible = 0x65;
constexpr std::size_t kChallengeButtonDisabled = 0x66;
constexpr int kChallengeButtonIdBase = 200;
constexpr int kSelectorButtonMiniGames = 101;
constexpr int kSelectorButtonPuzzle = 102;

int selector_button_for_page(int page) {
    switch (page) {
    case 0: return kSelectorButtonSurvival;
    case 1: return kSelectorButtonMiniGames;
    case 3: return kSelectorButtonPuzzle;
    default: return 0;
    }
}

struct PendingRestart {
    std::uintptr_t app = 0;
    std::uintptr_t board = 0;
    bool confirmation_sent = false;
};

PendingRestart g_pending_restart;

std::uintptr_t add(std::uintptr_t p, std::size_t n) {
    if (p < 0x10000 || n > std::numeric_limits<std::uintptr_t>::max() - p)
        throw std::runtime_error("Invalid app/dialog address.");
    return p + n;
}

bool readable(std::uintptr_t address, std::size_t bytes) {
    if (address < 0x10000 || bytes == 0 ||
        bytes > std::numeric_limits<std::uintptr_t>::max() - address) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info))) return false;
    const DWORD protection = info.Protect & 0xFF;
    const bool can_read = protection == PAGE_READONLY || protection == PAGE_READWRITE ||
        protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
    const auto begin = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    if (info.RegionSize > std::numeric_limits<std::uintptr_t>::max() - begin) return false;
    return info.State == MEM_COMMIT && !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
        can_read && address >= begin && address + bytes <= begin + info.RegionSize;
}

template<class T> T read(std::uintptr_t address, const char* field) {
    if (!readable(address, sizeof(T)))
        throw std::runtime_error(std::string("Unreadable menu field: ") + field);
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
}

int challenge_definition_page(int mode_id) {
    if (mode_id < 1 || mode_id > static_cast<int>(kChallengeButtonCount)) return -1;
    const auto definition = add(kChallengeDefinitions,
        static_cast<std::size_t>(mode_id - 1) * kChallengeDefinitionStride);
    const auto mode = read<std::uint32_t>(definition, "challenge definition mode id");
    if (mode != static_cast<std::uint32_t>(mode_id))
        throw std::runtime_error("Challenge definition table does not match the verified 1096 mode order.");
    return read<std::int32_t>(add(definition, kChallengeDefinitionPage), "challenge definition page");
}

void validate_app(std::uintptr_t app) {
    if (app != read<std::uint32_t>(kGlobalApp, "global app") || !readable(app, 0x960))
        throw std::runtime_error("Unexpected LawnApp pointer/layout.");
}

std::uintptr_t get_dialog(std::uintptr_t app, int dialog_id) {
    const auto vtable = read<std::uint32_t>(app, "app vtable");
    const auto method = read<std::uint32_t>(add(vtable, kAppVtableGetDialog), "GetDialog slot");
    if (method < 0x401000 || method >= 0x6d0000)
        throw std::runtime_error("Unexpected GetDialog virtual target.");
    using GetDialog = void*(__thiscall*)(void*, int);
    return reinterpret_cast<std::uintptr_t>(
        reinterpret_cast<GetDialog>(method)(reinterpret_cast<void*>(app), dialog_id));
}

int dialog_count(std::uintptr_t app) {
    const auto vtable = read<std::uint32_t>(app, "app vtable");
    const auto method = read<std::uint32_t>(add(vtable, kAppVtableGetDialogCount), "GetDialogCount slot");
    if (method < 0x401000 || method >= 0x6d0000)
        throw std::runtime_error("Unexpected GetDialogCount virtual target.");
    using GetDialogCount = int(__thiscall*)(void*);
    return reinterpret_cast<GetDialogCount>(method)(reinterpret_cast<void*>(app));
}

json describe_dialog(std::uintptr_t dialog) {
    if (!dialog) return nullptr;
    if (!readable(dialog, 0x20)) throw std::runtime_error("Dialog pointer is not readable.");
    const auto vtable = read<std::uint32_t>(dialog, "dialog vtable");
    json methods = json::array();
    for (std::size_t offset = 0; offset < 0x40; offset += 4) {
        const auto entry = read<std::uint32_t>(add(vtable, offset), "dialog vtable entry");
        methods.push_back({{"offset", offset}, {"target", entry}});
    }
    const auto listener_vtable = read<std::uint32_t>(add(dialog, kDialogButtonListener), "dialog ButtonListener vtable");
    json listener_methods = json::array();
    for (std::size_t offset = 0; offset < 0x20; offset += 4) {
        const auto entry = read<std::uint32_t>(add(listener_vtable, offset), "ButtonListener vtable entry");
        listener_methods.push_back({{"offset", offset}, {"target", entry}});
    }
    return json{{"address", dialog}, {"vtable", vtable}, {"vtable_prefix", methods},
        {"button_listener_vtable", listener_vtable}, {"button_listener_vtable_prefix", listener_methods}};
}

json describe_challenge_screen(std::uintptr_t app) {
    const auto challenge = read<std::uint32_t>(add(app, kAppChallengeScreen), "ChallengeScreen pointer");
    if (!challenge) return nullptr;
    if (!readable(challenge, kChallengePage + sizeof(std::int32_t)))
        throw std::runtime_error("ChallengeScreen pointer is not readable.");
    const auto primary = read<std::uint32_t>(challenge, "ChallengeScreen primary vtable");
    const auto listener = add(challenge, kChallengeListener);
    const auto secondary = read<std::uint32_t>(listener, "ChallengeScreen ButtonListener vtable");
    if (primary != kChallengeScreenVtable || secondary != kChallengeListenerVtable)
        throw std::runtime_error("ChallengeScreen vtables do not match the verified 1096 constructor layout.");
    const auto callback = read<std::uint32_t>(add(secondary, kButtonDepressSlot), "ChallengeScreen ButtonDepress slot");
    if (callback != kChallengeButtonDepress)
        throw std::runtime_error("ChallengeScreen ButtonDepress target does not match the live-verified 1096 callback.");
    const auto page = read<std::int32_t>(add(challenge, kChallengePage), "ChallengeScreen page");
    json buttons = json::array();
    for (std::size_t index = 0; index < kChallengeButtonCount; ++index) {
        const auto button = read<std::uint32_t>(add(challenge, kChallengeButtons + index * sizeof(std::uint32_t)),
            "ChallengeScreen button pointer");
        const int mode_id = static_cast<int>(index + 1);
        const int definition_page = challenge_definition_page(mode_id);
        json item{{"index", index}, {"mode_id", mode_id}, {"definition_page", definition_page},
            {"ordinary_page", definition_page == 0 || definition_page == 1 || definition_page == 3},
            {"address", button}};
        if (button && readable(button, kChallengeButtonDisabled + 1)) {
            item["vtable"] = read<std::uint32_t>(button, "challenge button vtable");
            item["visible"] = read<std::uint8_t>(add(button, kChallengeButtonVisible), "challenge button visible");
            item["disabled"] = read<std::uint8_t>(add(button, kChallengeButtonDisabled), "challenge button disabled");
        }
        buttons.push_back(item);
    }
    return json{{"address", challenge}, {"primary_vtable", primary},
        {"listener", listener}, {"listener_vtable", secondary},
        {"button_depress", callback}, {"page", page},
        {"pool_endless_button", buttons[kChallengeEndlessButtonIndex]},
        {"buttons", buttons}, {"button_prefix", buttons}};
}

void invoke_options_button(std::uintptr_t dialog, int button_id) {
    const auto listener = add(dialog, kDialogButtonListener);
    const auto vtable = read<std::uint32_t>(listener, "options ButtonListener vtable");
    const auto callback = read<std::uint32_t>(add(vtable, kButtonDepressSlot), "options ButtonDepress slot");
    if (callback != kNewOptionsButtonDepress)
        throw std::runtime_error("Options ButtonDepress target does not match the verified 1096 callback.");
    using ButtonDepress = void(__thiscall*)(void*, int);
    reinterpret_cast<ButtonDepress>(callback)(reinterpret_cast<void*>(listener), button_id);
}

void invoke_app_button(std::uintptr_t app, int button_id) {
    const auto vtable = read<std::uint32_t>(app, "app ButtonListener vtable");
    const auto callback = read<std::uint32_t>(add(vtable, kButtonDepressSlot), "app ButtonDepress slot");
    if (callback != kAppButtonDepress)
        throw std::runtime_error("LawnApp ButtonDepress target does not match the verified 1096 callback.");
    using ButtonDepress = void(__thiscall*)(void*, int);
    reinterpret_cast<ButtonDepress>(callback)(reinterpret_cast<void*>(app), button_id);
}

void invoke_selector_button(std::uintptr_t selector, int button_id) {
    if (read<std::uint32_t>(selector, "GameSelector primary vtable") != kGameSelectorVtable)
        throw std::runtime_error("GameSelector primary vtable does not match the verified 1096 layout.");
    const auto listener = add(selector, kSelectorButtonListener);
    const auto vtable = read<std::uint32_t>(listener, "selector ButtonListener vtable");
    if (vtable != kGameSelectorListenerVtable)
        throw std::runtime_error("GameSelector ButtonListener vtable does not match the verified 1096 layout.");
    const auto callback = read<std::uint32_t>(add(vtable, kButtonDepressSlot), "selector ButtonDepress slot");
    if (callback != kGameSelectorButtonDepress)
        throw std::runtime_error("GameSelector ButtonDepress target does not match the verified 1096 callback.");
    using ButtonDepress = void(__thiscall*)(void*, int);
    reinterpret_cast<ButtonDepress>(callback)(reinterpret_cast<void*>(listener), button_id);
}

void invoke_challenge_button(std::uintptr_t challenge, int button_id) {
    if (read<std::uint32_t>(challenge, "ChallengeScreen primary vtable") != kChallengeScreenVtable)
        throw std::runtime_error("ChallengeScreen primary vtable does not match the verified 1096 layout.");
    const auto listener = add(challenge, kChallengeListener);
    const auto vtable = read<std::uint32_t>(listener, "ChallengeScreen ButtonListener vtable");
    if (vtable != kChallengeListenerVtable)
        throw std::runtime_error("ChallengeScreen ButtonListener vtable does not match the verified 1096 layout.");
    const auto callback = read<std::uint32_t>(add(vtable, kButtonDepressSlot), "ChallengeScreen ButtonDepress slot");
    if (callback != kChallengeButtonDepress)
        throw std::runtime_error("ChallengeScreen ButtonDepress target does not match the live-verified 1096 callback.");
    using ButtonDepress = void(__thiscall*)(void*, int);
    reinterpret_cast<ButtonDepress>(callback)(reinterpret_cast<void*>(listener), button_id);
}

void accept_restart_dialog(std::uintptr_t app, std::uintptr_t dialog) {
    if (read<std::int32_t>(add(dialog, 0x158), "restart dialog ID") != kDialogConfirmRestart)
        throw std::runtime_error("Restart dialog's stored ID does not match ID 23.");
    if (read<std::uint32_t>(add(dialog, 0x16c), "restart dialog owner") != app)
        throw std::runtime_error("Restart confirmation is not owned by the active LawnApp.");
    const auto vtable = read<std::uint32_t>(dialog, "restart confirmation vtable");
    if (vtable != kRestartConfirmDialogVtable)
        throw std::runtime_error("Restart confirmation does not match the verified 1096 dialog class.");
    const auto key_down = read<std::uint32_t>(add(vtable, kDialogKeyDownSlot),
        "restart confirmation KeyDown slot");
    if (key_down != kRestartConfirmKeyDown)
        throw std::runtime_error("Restart confirmation KeyDown target does not match the verified callback.");

    using KeyDown = void(__thiscall*)(void*, int);
    reinterpret_cast<KeyDown>(key_down)(reinterpret_cast<void*>(dialog), kRestartConfirmEnter);
}

} // namespace

json open_options(std::uintptr_t app) {
    validate_app(app);
    const int scene = read<std::int32_t>(add(app, kAppScene), "game scene");
    const int from_selector = scene == kSceneMenu ? 1 : 0;
    if (!from_selector && scene != kSceneLevelIntro && scene != kScenePlaying)
        throw std::runtime_error("Options can only be opened from a level intro or active level.");
    if (!from_selector && !read<std::uint32_t>(add(app, kAppBoard), "board pointer"))
        throw std::runtime_error("No active Board is available.");
    if (from_selector && !read<std::uint32_t>(add(app,kAppSelector),"selector pointer"))
        throw std::runtime_error("Main-menu selector is not active.");
    if (dialog_count(app) != 0)
        throw std::runtime_error("Dismiss existing dialogs before opening options.");

    // 1096 Board::MouseUp callsite at 0x415B97 sets EDI=app, pushes the
    // from-game-selector bool, calls 0x454E10, then restores EDI. Preserve the
    // caller's EDI because this internal routine uses the same custom ABI.
#if defined(_MSC_VER) && defined(_M_IX86)
    __asm {
        push edi
        mov edi, app
        push from_selector
        mov eax, 0454E10h
        call eax
        pop edi
    }
#else
    throw std::runtime_error("Opening options requires the verified MSVC x86 ABI.");
#endif

    const auto dialog = get_dialog(app, kDialogNewOptions);
    if (!dialog) throw std::runtime_error("Game did not create the options dialog.");
    return json{{"accepted", true}, {"dialog_id", kDialogNewOptions},
        {"dialog", describe_dialog(dialog)}};
}

json back_to_menu(std::uintptr_t app) {
    validate_app(app);
    const int scene = read<std::int32_t>(add(app, kAppScene), "game scene");
    const int mode = read<std::int32_t>(add(app, kAppMode), "game mode");
    if (scene == kSceneChallenge) {
        if (dialog_count(app) != 0) throw std::runtime_error("Dismiss dialogs before leaving ChallengeScreen.");
        const auto challenge = read<std::uint32_t>(add(app, kAppChallengeScreen), "ChallengeScreen pointer");
        if (!challenge) throw std::runtime_error("ChallengeScreen is not active.");
        invoke_challenge_button(challenge, 100); // ordinary ChallengeScreen Back button
        return json{{"accepted", true}, {"route", "normal_challenge_back_button"},
            {"scene", read<std::int32_t>(add(app, kAppScene), "game scene")}};
    }
    if (mode == kModeZenGarden || mode == 50) {
        if (scene != kScenePlaying || !read<std::uint32_t>(add(app, kAppBoard), "board pointer"))
            throw std::runtime_error("Zen Garden Board is not active.");
        if (dialog_count(app) != 0) throw std::runtime_error("Dismiss existing dialogs before leaving Zen Garden.");
#if defined(_MSC_VER) && defined(_M_IX86)
        __asm {
            push eax
            mov eax, app
            mov dword ptr [eax + 09A8h], 4
            mov edx, 0454BF0h
            call edx
            pop eax
        }
#else
        throw std::runtime_error("Leaving Zen Garden requires the verified MSVC x86 ABI.");
#endif
        return json{{"accepted", true}, {"route", "zen_garden_normal_exit"},
            {"scene", read<std::int32_t>(add(app, kAppScene), "game scene")}};
    }
    if (scene != kSceneLevelIntro && scene != kScenePlaying)
        throw std::runtime_error("Main Menu can only be requested from an active level.");
    const auto options = get_dialog(app, kDialogNewOptions);
    if (!options) throw std::runtime_error("Open the options dialog first.");
    if (get_dialog(app, kDialogConfirmBackToMain) || get_dialog(app, kDialogConfirmRestart))
        throw std::runtime_error("A confirmation dialog is already open.");
    invoke_options_button(options, kOptionsButtonMainMenu);
    const auto confirmation = get_dialog(app, kDialogConfirmBackToMain);
    const auto still_options = get_dialog(app, kDialogNewOptions);
    return json{{"accepted", true}, {"route", "normal_options_main_menu"},
        {"confirmation_required", confirmation != 0},
        {"options_dialog_open", still_options != 0},
        {"scene", read<std::int32_t>(add(app, kAppScene), "game scene")}};
}

json confirm_back_to_menu(std::uintptr_t app) {
    validate_app(app);
    if (!get_dialog(app, kDialogConfirmBackToMain))
        throw std::runtime_error("Leave Game confirmation is not open.");
    invoke_app_button(app, kDialogIdYesBase + kDialogConfirmBackToMain);
    return json{{"accepted", true}, {"route", "normal_leave_game_yes"},
        {"scene", read<std::int32_t>(add(app, kAppScene), "game scene")},
        {"dialog_count", dialog_count(app)}};
}

json close_options(std::uintptr_t app) {
    validate_app(app);
    const auto options = get_dialog(app, kDialogNewOptions);
    if (!options) throw std::runtime_error("Options dialog is not open.");
    if (get_dialog(app, kDialogConfirmBackToMain) || get_dialog(app, kDialogConfirmRestart))
        throw std::runtime_error("Dismiss the open confirmation dialog first.");
    invoke_options_button(options, kButtonBackToGame);
    return json{{"accepted", true}, {"route", "normal_back_to_game_button"},
        {"options_dialog_open", get_dialog(app, kDialogNewOptions) != 0}};
}

json restart_level(std::uintptr_t app) {
    validate_app(app);
    if (g_pending_restart.app)
        throw std::runtime_error("A restart confirmation is already being handled.");
    const auto options = get_dialog(app, kDialogNewOptions);
    if (!options) throw std::runtime_error("Options dialog is not open.");
    if (get_dialog(app, kDialogConfirmBackToMain) || get_dialog(app, kDialogConfirmRestart))
        throw std::runtime_error("A confirmation dialog is already open.");

    const auto board_before = read<std::uint32_t>(add(app, kAppBoard), "board before restart");
    if (!board_before) throw std::runtime_error("No active Board is available to restart.");
    const auto tick_before = read<std::int32_t>(add(board_before, kBoardMainCounter),
        "board tick before restart");
    g_pending_restart = PendingRestart{app, board_before, false};
    struct ClearPendingRestart {
        ~ClearPendingRestart() { g_pending_restart = PendingRestart{}; }
    } clear_pending;

    invoke_options_button(options, kOptionsButtonRestart);
    if (!g_pending_restart.confirmation_sent)
        throw std::runtime_error("Restart confirmation did not pass through the verified dialog KeyDown path.");
    const auto scene = read<std::int32_t>(add(app, kAppScene), "scene after restart");
    const auto board_after = read<std::uint32_t>(add(app, kAppBoard), "board after restart");
    if (scene != kSceneLevelIntro || !board_after)
        throw std::runtime_error("The normal restart callback did not enter the level intro scene with an active Board.");
    const auto chooser = read<std::uint32_t>(add(app, kAppChooser), "seed chooser after restart");
    if (!chooser || !readable(chooser, 0xd44))
        throw std::runtime_error("The level intro scene does not have a readable SeedChooser.");
    const auto tick_after = read<std::int32_t>(add(board_after, kBoardMainCounter),
        "board tick after restart");
    const bool tick_reset = tick_after >= 0 && tick_after <= kRestartIntroTickMax;
    if (!tick_reset)
        throw std::runtime_error("The level intro Board tick did not reset to its initial state.");
    return json{{"accepted", true}, {"confirmation_required", false},
        {"route", "normal_options_restart_and_enter"}, {"scene", scene},
        {"board_changed", board_after != board_before},
        {"seed_chooser_active", true},
        {"tick_before", tick_before}, {"tick_after", tick_after},
        {"tick_reset", tick_reset}};
}

void tick(std::uintptr_t app) {
    if (!g_pending_restart.app || app != g_pending_restart.app ||
        g_pending_restart.confirmation_sent) return;
    validate_app(app);
    if (read<std::uint32_t>(add(app, kAppBoard), "board during restart") != g_pending_restart.board)
        return;
    const auto confirmation = get_dialog(app, kDialogConfirmRestart);
    if (!confirmation) return;

    // Set before entering KeyDown because the ordinary callback can service a
    // nested update itself. That prevents duplicate Enter events in recursion.
    g_pending_restart.confirmation_sent = true;
    accept_restart_dialog(app, confirmation);
}

json confirm_restart(std::uintptr_t app) {
    validate_app(app);
    throw std::runtime_error("Restart confirmation is handled inside restart_level through the normal dialog KeyDown path.");
}

json snapshot(std::uintptr_t app) {
    validate_app(app);
    const auto board = read<std::uint32_t>(add(app, kAppBoard), "board pointer");
    return json{{"scene", read<std::int32_t>(add(app, kAppScene), "game scene")},
        {"mode", read<std::int32_t>(add(app, kAppMode), "game mode")},
        {"board", board},
        {"dialog_count", dialog_count(app)},
        {"options_dialog", describe_dialog(get_dialog(app, kDialogNewOptions))},
        {"back_confirmation_dialog", describe_dialog(get_dialog(app, kDialogConfirmBackToMain))},
        {"challenge_screen", describe_challenge_screen(app)}};
}

json enter_mode(std::uintptr_t app, int mode_id) {
    validate_app(app);
    const int scene = read<std::int32_t>(add(app, kAppScene), "game scene");
    if (dialog_count(app) != 0)
        throw std::runtime_error("Dismiss existing dialogs before entering a mode.");

    if (mode_id >= 1 && mode_id <= static_cast<int>(kChallengeButtonCount) &&
        scene == kSceneChallenge) {
        const auto challenge = read<std::uint32_t>(add(app, kAppChallengeScreen), "ChallengeScreen pointer");
        if (!challenge || !readable(challenge, kChallengePage + sizeof(std::int32_t)))
            throw std::runtime_error("Survival ChallengeScreen is not active.");
        if (read<std::uint32_t>(challenge, "ChallengeScreen primary vtable") != kChallengeScreenVtable ||
            read<std::uint32_t>(add(challenge, kChallengeListener), "ChallengeScreen ButtonListener vtable") != kChallengeListenerVtable)
            throw std::runtime_error("Active ChallengeScreen does not match the verified 1096 layout.");
        const int page = read<std::int32_t>(add(challenge, kChallengePage), "ChallengeScreen page");
        const int definition_page = challenge_definition_page(mode_id);
        if (definition_page == 2 || (page != 0 && page != 1 && page != 3))
            throw std::runtime_error("Only ordinary challenge pages 0, 1, and 3 are supported; hidden pages are not exposed.");
        if (page != definition_page)
            throw std::runtime_error("Requested mode does not belong to the active ChallengeScreen page.");
        const std::size_t index = static_cast<std::size_t>(mode_id - 1);
        const auto button_address = read<std::uint32_t>(add(challenge,
            kChallengeButtons + index * sizeof(std::uint32_t)), "Challenge button pointer");
        if (!button_address || !readable(button_address, kChallengeButtonDisabled + 1))
            throw std::runtime_error("Challenge button is not readable.");
        const auto visible = read<std::uint8_t>(add(button_address, kChallengeButtonVisible), "Challenge button visible");
        const auto disabled = read<std::uint8_t>(add(button_address, kChallengeButtonDisabled), "Challenge button disabled");
        if (read<std::uint32_t>(button_address, "Challenge button vtable") != kChallengeButtonVtable)
            throw std::runtime_error("Challenge button vtable does not match the verified 1096 object.");
        if (visible != 1 || disabled != 0)
            throw std::runtime_error("Challenge button is not currently visible and enabled in the ordinary UI.");
        invoke_challenge_button(challenge, kChallengeButtonIdBase + static_cast<int>(index));
        return json{{"accepted", true}, {"pending", false}, {"stage", "challenge_selected"},
            {"mode_id", mode_id}, {"challenge_button_id", kChallengeButtonIdBase + (mode_id - 1)},
            {"visible", visible}, {"disabled", disabled},
            {"scene", read<std::int32_t>(add(app, kAppScene), "game scene")},
            {"mode", read<std::int32_t>(add(app, kAppMode), "game mode")}};
    }

    if (scene != kSceneMenu)
        throw std::runtime_error("Mode entry requires the ordinary main-menu selector scene or its verified challenge stage.");
    const auto selector = read<std::uint32_t>(add(app, kAppSelector), "GameSelector pointer");
    if (!selector || !readable(selector, 0x100))
        throw std::runtime_error("GameSelector is not active.");

    if (mode_id == kModeSurvivalEndless) {
        // GameSelector::ButtonDepress checks the selector's real survival lock
        // before opening its ordinary Survival challenge page (button ID 110).
        invoke_selector_button(selector, kSelectorButtonSurvival);
        return json{{"accepted", true}, {"pending", true}, {"stage", "survival_page"},
            {"mode_id", mode_id}, {"selector_button_id", kSelectorButtonSurvival},
            {"challenge_screen", read<std::uint32_t>(add(app, kAppChallengeScreen), "ChallengeScreen pointer")}};
    }
    if (mode_id == kModeZenGarden) {
        // The selector callback itself does not reject a disabled Zen Garden
        // button. Require a completed adventure (conservative unlock condition)
        // before invoking the ordinary ID 109 route.
        const auto profile = read<std::uint32_t>(add(app, kAppPlayerInfo), "PlayerInfo pointer");
        if (!profile || !readable(profile, 0x5c))
            throw std::runtime_error("A readable player profile is required for Zen Garden entry.");
        const int completed = read<std::int32_t>(add(profile, 0x58), "completed adventure count");
        if (completed < 1)
            throw std::runtime_error("Zen Garden is gated until at least one adventure completion.");
        invoke_selector_button(selector, kSelectorButtonZenGarden);
        return json{{"accepted", true}, {"pending", false}, {"mode_id", mode_id},
            {"selector_button_id", kSelectorButtonZenGarden},
            {"scene", read<std::int32_t>(add(app, kAppScene), "game scene")},
            {"mode", read<std::int32_t>(add(app, kAppMode), "game mode")}};
    }
    if (mode_id >= 1 && mode_id <= static_cast<int>(kChallengeButtonCount)) {
        const int page = challenge_definition_page(mode_id);
        const int selector_button = selector_button_for_page(page);
        if (!selector_button) {
            if (mode_id == 50)
                throw std::runtime_error("Tree of Wisdom is entered through the owned garden's ordinary Next Garden button, not the hidden selector page.");
            throw std::runtime_error("This challenge definition belongs to a hidden page and is not exposed by the ordinary selector API.");
        }
        invoke_selector_button(selector, selector_button);
        return json{{"accepted", true}, {"pending", true}, {"stage", "challenge_page"},
            {"mode_id", mode_id}, {"challenge_page", page},
            {"selector_button_id", selector_button},
            {"challenge_screen", read<std::uint32_t>(add(app, kAppChallengeScreen), "ChallengeScreen pointer")}};
    }
    throw std::runtime_error("Unsupported mode for the verified selector entry helper.");
}

} // namespace pvz::menu
