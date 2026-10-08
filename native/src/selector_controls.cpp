#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>

#include "selector_controls.hpp"
#include "ui_controls.hpp"

namespace pvz::selector_controls {
namespace {
using json = nlohmann::json;

constexpr std::uintptr_t kImageBase = 0x400000;
constexpr std::uintptr_t kGlobalApp = 0x731c50;
constexpr std::uintptr_t kAppVtable = 0x6f19e0;
constexpr std::uintptr_t kSelectorVtable = 0x6eec88;
constexpr std::uintptr_t kSelectorListenerVtable = 0x6eeda0;
constexpr std::uintptr_t kSelectorButtonDepress = 0x450ee0;
constexpr std::uintptr_t kAchievementWheel = 0x54fdd0;
constexpr std::size_t kAppSelector = 0x870;
constexpr std::size_t kAppScene = 0x91c;
constexpr std::size_t kAppPlayerInfo = 0x94c;
constexpr std::size_t kAppGetDialogCountSlot = 0x13c;
constexpr std::size_t kProfileEarnedAchievements = 0x24;
constexpr std::size_t kProfileShownAchievements = 0x38;
constexpr std::size_t kAchievementCount = 20;
constexpr std::size_t kSelectorListener = 0xa0;
constexpr std::size_t kSelectorAchievementsScreen = 0x16c;
constexpr std::size_t kSelectorX = 0x40;
constexpr std::size_t kSelectorY = 0x44;
constexpr std::size_t kSelectorTargetX = 0x160;
constexpr std::size_t kSelectorTargetY = 0x164;
constexpr std::size_t kWidgetManager = 0x320;
constexpr std::uintptr_t kWidgetManagerVtable = 0x6fc114;
constexpr std::size_t kWidgetOwner = 0x20;
constexpr std::size_t kWidgetParent = 0x24;
constexpr std::size_t kWidgetX = 0x40;
constexpr std::size_t kWidgetY = 0x44;
constexpr std::size_t kWidgetWidth = 0x48;
constexpr std::size_t kWidgetHeight = 0x4c;
constexpr std::size_t kWidgetVisible = 0x65;
constexpr std::size_t kWidgetDisabled = 0x66;
constexpr std::uintptr_t kAchievementScreenVtable = 0x6eb1d0;
constexpr std::size_t kManagerDownButtons = 0x110;
constexpr std::size_t kManagerKeyFocus = 0xa0;
constexpr std::size_t kAchievementScrollDirection = 0xac;
constexpr std::size_t kAchievementScrollRemaining = 0xb0;
constexpr std::size_t kAchievementScrollStep = 0xb4;
constexpr std::size_t kAchievementScrollLimit = 0xb8;
constexpr std::size_t kButtonDepressSlot = 0x08;
constexpr int kSceneMenu = 1;
constexpr int kFirstSelectorButton = 100;
constexpr int kLastSelectorButton = 114;

struct Route {
    int id;
    const char* name;
    const char* label;
    bool supported;
};

// The ID->code block mapping is from the actual 15-DWORD jump table at
// 0x451160, reached after ButtonDepress subtracts 0x64. Names come from the
// selector route audit; uncertain special routes remain explicitly unknown.
constexpr Route kRoutes[] = {
    {100, "adventure", "Adventure", true},
    {101, "mini_games", "Mini-Games", true},
    {102, "puzzle", "Puzzle", true},
    {103, "options", "Options", true},
    {104, "help", "Help", true},
    {105, "quit", "Quit", false},
    {106, "change_user", "Change User", false},
    {107, "shop", "Shop", true},
    {108, "almanac", "Almanac", true},
    {109, "zen_garden", "Zen Garden", true},
    {110, "survival", "Survival", true},
    {111, "zombatar", "Zombatar", false},
    {112, "selector_origin_transition", "Selector Origin Transition", false},
    {113, "achievements", "Achievements", true},
    {114, "quick_play", "Quick Play", false},
};

constexpr const char* kAchievementNames[kAchievementCount] = {
    "home_lawn_security", "nobel_peas_prize", "better_off_dead", "china_shop",
    "spudow", "explodonator", "morticulturalist", "dont_pea_in_the_pool",
    "roll_some_heads", "grounded", "zombologist", "penny_pincher",
    "sunny_days", "popcorn_party", "good_morning", "no_fungus_among_us",
    "beyond_the_grave", "immortal", "towering_wisdom", "mustache_mode"
};

struct PendingAction {
    std::uintptr_t app = 0;
    std::string scope;
    std::uint32_t thread_id = 0;
    std::uint64_t id = 0;
    int button_id = -1;
    std::string route;
    std::uintptr_t selector = 0;
    std::uintptr_t achievement_screen = 0;
    std::uintptr_t widget_manager = 0;
    bool achievements_back = false;
    bool achievements_scroll = false;
    int scroll_delta = 0;
    int scroll_ticks_total = 0;
    int scroll_ticks_remaining = 0;
    std::string state = "idle";
    std::string detail;
};

PendingAction g_pending;
std::uint64_t g_next_id = 1;
bool g_ticking = false;

std::uintptr_t add(std::uintptr_t base, std::size_t offset) {
    if (base < 0x10000 || offset > UINTPTR_MAX - base)
        throw std::runtime_error("Invalid selector address.");
    return base + offset;
}

bool readable(std::uintptr_t address, std::size_t size) {
    if (address < 0x10000 || size == 0 || size > UINTPTR_MAX - address) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info))) return false;
    const DWORD protection = info.Protect & 0xff;
    const bool can_read = protection == PAGE_READONLY || protection == PAGE_READWRITE ||
        protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
    const auto begin = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    return info.State == MEM_COMMIT && !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
        can_read && address >= begin && info.RegionSize <= UINTPTR_MAX - begin &&
        address + size <= begin + info.RegionSize;
}

template<class T> T read(std::uintptr_t address, const char* field) {
    if (!readable(address, sizeof(T)))
        throw std::runtime_error(std::string("Unreadable selector field: ") + field);
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
}

void require_main_thread(std::uint32_t main_thread_id) {
    if (!main_thread_id || GetCurrentThreadId() != main_thread_id)
        throw std::runtime_error("Selector operation refused outside the verified game-update thread.");
}

void validate_app(std::uintptr_t app) {
    if (reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) != kImageBase ||
        app != read<std::uint32_t>(kGlobalApp, "global LawnApp") ||
        !readable(app, 0x960) || read<std::uint32_t>(app, "LawnApp vtable") != kAppVtable)
        throw std::runtime_error("The active app does not match Steam GOTY 1.2.0.1096.");
}

int dialog_count(std::uintptr_t app) {
    const auto vtable = read<std::uint32_t>(app, "LawnApp vtable");
    const auto callback = read<std::uint32_t>(add(vtable, kAppGetDialogCountSlot),
        "GetDialogCount slot");
    if (callback < 0x401000 || callback >= 0x6d0000)
        throw std::runtime_error("GetDialogCount target is outside the verified image.");
    using GetDialogCount = int(__thiscall*)(void*);
    const int count = reinterpret_cast<GetDialogCount>(callback)(reinterpret_cast<void*>(app));
    if (count < 0 || count > 0x80)
        throw std::runtime_error("The game reported an invalid dialog count.");
    return count;
}

std::uintptr_t selector_for(std::uintptr_t app) {
    if (read<std::int32_t>(add(app, kAppScene), "game scene") != kSceneMenu)
        throw std::runtime_error("Selector controls require the main-menu scene.");
    const auto selector = read<std::uint32_t>(add(app, kAppSelector), "GameSelector pointer");
    if (!selector || !readable(selector, 0x170) ||
        read<std::uint32_t>(selector, "GameSelector vtable") != kSelectorVtable)
        throw std::runtime_error("GameSelector is not active or does not match the verified 1096 class.");
    return selector;
}

std::uintptr_t achievement_screen_for(std::uintptr_t app, std::uintptr_t selector) {
    const auto manager = read<std::uint32_t>(add(app, kWidgetManager), "WidgetManager pointer");
    if (!manager || !readable(manager, 0x220) ||
        read<std::uint32_t>(manager, "WidgetManager vtable") != kWidgetManagerVtable)
        throw std::runtime_error("The active WidgetManager does not match the verified 1096 object.");
    const auto screen = read<std::uint32_t>(add(selector, kSelectorAchievementsScreen),
        "AchievementScreen pointer");
    if (!screen || !readable(screen, 0xd0) ||
        read<std::uint32_t>(screen, "AchievementScreen vtable") != kAchievementScreenVtable ||
        read<std::uint32_t>(add(screen, kWidgetOwner), "AchievementScreen WidgetManager") != manager ||
        read<std::uint32_t>(add(screen, kWidgetParent), "AchievementScreen parent") != selector)
        throw std::runtime_error("The verified AchievementScreen is not attached to the active selector.");
    if (read<std::int32_t>(add(selector, kSelectorX), "selector x") != -800 ||
        read<std::int32_t>(add(selector, kSelectorY), "selector y") != -600 ||
        read<std::int32_t>(add(selector, kSelectorTargetX), "selector target x") != -800 ||
        read<std::int32_t>(add(selector, kSelectorTargetY), "selector target y") != -600)
        throw std::runtime_error("The AchievementScreen is not the stable visible selector page.");
    const auto screen_y = read<std::int32_t>(add(screen, kWidgetY), "AchievementScreen y");
    const auto viewport_height = read<std::int32_t>(add(app, 0xc4), "app viewport height");
    const auto content_height = read<std::int32_t>(add(screen, kWidgetHeight), "AchievementScreen height");
    const auto lowest_y = 2 * viewport_height + 50 - content_height;
    if (read<std::int32_t>(add(screen, kWidgetX), "AchievementScreen x") != 800 ||
        screen_y < lowest_y || screen_y > 600 ||
        read<std::int32_t>(add(screen, kWidgetWidth), "AchievementScreen width") != 800 ||
        read<std::int32_t>(add(screen, kWidgetHeight), "AchievementScreen height") != 16150 ||
        read<std::uint8_t>(add(screen, kWidgetVisible), "AchievementScreen visibility") != 1 ||
        read<std::uint8_t>(add(screen, kWidgetDisabled), "AchievementScreen disabled state") != 0)
        throw std::runtime_error("AchievementScreen geometry or state does not match the verified 1096 page.");
    return screen;
}

#if defined(_MSC_VER) && defined(_M_IX86)
__declspec(naked) void __stdcall invoke_selector_return_helper(std::uintptr_t) {
    // Target ABI is EAX=GameSelector*, no stack arguments, plain RET. Verified
    // callsites are 0x451154 (selector button 112) and 0x53328D.
    __asm {
        mov eax, dword ptr [esp + 4]
        mov ecx, 00451AE0h
        call ecx
        ret 4
    }
}
#else
void invoke_selector_return_helper(std::uintptr_t) {
    throw std::runtime_error("The verified selector helper requires the target's 32-bit MSVC ABI.");
}
#endif

void request_selector_achievement_return(std::uintptr_t app, std::uintptr_t selector,
                                         std::uintptr_t screen, std::uintptr_t manager) {
    if (read<std::uint32_t>(add(app, kAppSelector), "current selector") != selector ||
        achievement_screen_for(app, selector) != screen || dialog_count(app) != 0 ||
        read<std::uint32_t>(add(app, kWidgetManager), "current WidgetManager") != manager)
        throw std::runtime_error("Achievement return context changed before dispatch.");
    if (read<std::uint32_t>(add(manager, kManagerDownButtons), "held buttons") != 0)
        throw std::runtime_error("Refusing to interfere with an already-held mouse button.");
    invoke_selector_return_helper(selector);
    if (read<std::int32_t>(add(selector, kSelectorTargetX), "return target x") != -800 ||
        read<std::int32_t>(add(selector, kSelectorTargetY), "return target y") != 0)
        throw std::runtime_error("The normal selector return helper did not request its verified target page.");
    if (read<std::int32_t>(add(selector, 0x154), "return animation duration") != 75)
        throw std::runtime_error("The normal selector return helper did not start its verified transition.");
}

void apply_achievement_scroll_tick(std::uintptr_t app, std::uintptr_t selector,
                                  std::uintptr_t screen, std::uintptr_t manager,
                                  int delta, const std::string& scope) {
    if (scope != g_pending.scope ||
        read<std::uint32_t>(add(app, kAppSelector), "current selector") != selector ||
        achievement_screen_for(app, selector) != screen || dialog_count(app) != 0 ||
        read<std::uint32_t>(add(app, kWidgetManager), "current WidgetManager") != manager)
        throw std::runtime_error("Achievement scroll context changed before its next tick.");
    if (read<std::uint32_t>(add(manager, kManagerKeyFocus), "key-focus widget") != screen)
        throw std::runtime_error("The AchievementScreen is no longer the active input-focus widget.");
    if (read<std::uint32_t>(add(manager, kManagerDownButtons), "held mouse buttons") != 0)
        throw std::runtime_error("Refusing to scroll while a mouse button is held.");
    const auto expected_direction = delta;
    using Wheel = void(__thiscall*)(void*, int);
    // 0x54FDD0 forwards the signed wheel delta to the focused widget's +0xEC
    // handler. AchievementScreen::0x4018D0 consumes its sign and sets direction.
    reinterpret_cast<Wheel>(kAchievementWheel)(reinterpret_cast<void*>(manager), delta);
    if (read<std::uint32_t>(add(manager, kManagerKeyFocus), "input-focus after wheel") != screen ||
        read<std::int32_t>(add(screen, kAchievementScrollDirection), "scroll direction") != expected_direction)
        throw std::runtime_error("AchievementScreen did not retain the requested verified arrow direction.");
    const auto remaining = read<std::int32_t>(add(screen, kAchievementScrollRemaining), "scroll ticks remaining");
    const auto step = read<std::int32_t>(add(screen, kAchievementScrollStep), "scroll step");
    const auto limit = read<std::int32_t>(add(screen, kAchievementScrollLimit), "scroll tick limit");
    if (remaining <= 0 || remaining > 60 || step <= 0 || step > 60 || limit <= 0 || limit > 60)
        throw std::runtime_error("AchievementScreen scroll animation is outside the verified bounds.");
}

void require_no_dialogs(std::uintptr_t app) {
    if (dialog_count(app) != 0)
        throw std::runtime_error("Close the open game dialog before using selector navigation.");
}

void verify_callback(std::uintptr_t selector) {
    const auto listener = add(selector, kSelectorListener);
    if (!readable(listener, 0x0c) ||
        read<std::uint32_t>(listener, "GameSelector listener vtable") != kSelectorListenerVtable ||
        read<std::uint32_t>(add(kSelectorListenerVtable, kButtonDepressSlot), "ButtonDepress slot") !=
            kSelectorButtonDepress)
        throw std::runtime_error("GameSelector ButtonDepress does not match the verified 1096 callback.");
}

const Route* route_for_name(const std::string& name) {
    const auto found = std::find_if(std::begin(kRoutes), std::end(kRoutes),
        [&name](const Route& route) { return name == route.name; });
    return found == std::end(kRoutes) ? nullptr : found;
}

json route_list() {
    json routes = json::array();
    for (const auto& route : kRoutes)
        routes.push_back({{"button_id", route.id}, {"name", route.name},
            {"label", route.label}, {"supported", route.supported}});
    return routes;
}

json ui_snapshot(std::uintptr_t app, const std::string& scope, std::uint32_t thread_id) {
    return ui_controls::snapshot(app, scope, thread_id);
}

json achievement_state(std::uintptr_t app) {
    const auto profile = read<std::uint32_t>(add(app, kAppPlayerInfo), "active profile");
    if (!profile || !readable(profile, kProfileShownAchievements + kAchievementCount))
        throw std::runtime_error("Active profile achievement fields are unavailable.");
    json entries = json::array();
    for (std::size_t i = 0; i < kAchievementCount; ++i) {
        const auto earned = read<std::uint8_t>(add(profile, kProfileEarnedAchievements + i),
            "earned achievement flag");
        const auto shown = read<std::uint8_t>(add(profile, kProfileShownAchievements + i),
            "shown achievement flag");
        if (earned > 1 || shown > 1)
            throw std::runtime_error("Profile contains a non-boolean achievement flag.");
        entries.push_back({{"id", i}, {"name", kAchievementNames[i]},
            {"earned", earned != 0}, {"shown", shown != 0}});
    }
    return {{"supported", true}, {"source", "active PlayerInfo fields"},
        {"items", std::move(entries)}};
}

json decorate_snapshot(std::uintptr_t app, const std::string& scope,
                       std::uint32_t thread_id) {
    const int scene = read<std::int32_t>(add(app, kAppScene), "game scene");
    json controls = json::array();
    json result{{"build", "1.2.0.1096"}, {"scene", scene},
        {"main_menu", scene == kSceneMenu}, {"routes", route_list()}};
    result["achievement_state"] = achievement_state(app);

    const auto base = ui_snapshot(app, scope, thread_id);
    result["control_snapshot"] = base;
    if (!base.value("ok", false)) {
        result["controls"] = controls;
        return result;
    }
    const auto all_controls = base.value("controls", json::array());
    for (const auto& control : all_controls) {
        if (!control.is_object() || !control.contains("control_code") ||
            !control["control_code"].is_number_integer()) continue;
        const int code = control["control_code"].get<int>();
        if (code < kFirstSelectorButton || code > kLastSelectorButton) continue;
        const auto route = std::find_if(std::begin(kRoutes), std::end(kRoutes),
            [code](const Route& entry) { return entry.id == code; });
        if (route == std::end(kRoutes)) continue;
        auto item = control;
        item["route"] = route->name;
        item["route_supported"] = route->supported;
        controls.push_back(std::move(item));
    }
    result["controls"] = std::move(controls);
    result["dialog_count"] = dialog_count(app);
    json achievement_controls = json::array();
    for (const auto& item : result["controls"]) {
        if (item.value("route", std::string{}) == "achievements" ||
            item.value("route", std::string{}) == "achievements_back")
            achievement_controls.push_back(item);
    }
    result["achievement_state"]["navigation_controls"] = std::move(achievement_controls);
    result["credits"] = {{"supported", true}, {"action", "open_credits"},
        {"entry", "verified MainMenu options button"}, {"replay_supported", false},
        {"replay_reason", "Replaying the end-game bonus video is a separate, unmapped action."}};
    try {
        const auto selector = selector_for(app);
        const auto screen = achievement_screen_for(app, selector);
        result["achievement_page"] = {{"active", true}, {"screen_available", true},
            {"back_action", "achievements_back"}, {"scroll_action", "achievements_scroll"},
            {"scroll_methods", json::array({"up", "down"})}};
        (void)screen;
    } catch (const std::exception& error) {
        result["achievement_page"] = {{"active", false}, {"screen_available", false},
            {"detail", error.what()}};
    }
    result["pending_action"] = {{"id", g_pending.id}, {"state", g_pending.state},
        {"route", g_pending.route}, {"detail", g_pending.detail},
        {"scroll_ticks_total", g_pending.scroll_ticks_total},
        {"scroll_ticks_remaining", g_pending.scroll_ticks_remaining}};
    result["profile_available"] = true;
    return result;
}

void invoke_selector(std::uintptr_t selector, int id) {
    verify_callback(selector);
    const auto listener = add(selector, kSelectorListener);
    using ButtonDepress = void(__thiscall*)(void*, int);
    reinterpret_cast<ButtonDepress>(kSelectorButtonDepress)(reinterpret_cast<void*>(listener), id);
}

json fail(const std::string& message) {
    return {{"ok", false}, {"accepted", false}, {"detail", message}};
}

} // namespace

json snapshot(std::uintptr_t app, const std::string& scope,
              std::uint32_t main_thread_id) {
    try {
        require_main_thread(main_thread_id);
        validate_app(app);
        selector_for(app);
        return decorate_snapshot(app, scope, main_thread_id);
    } catch (const std::exception& error) {
        return {{"ok", false}, {"build", "1.2.0.1096"},
            {"controls", json::array()}, {"detail", error.what()}};
    } catch (...) {
        return {{"ok", false}, {"controls", json::array()},
            {"detail", "Unknown selector metadata failure."}};
    }
}

json action(std::uintptr_t app, const json& request, const std::string& scope,
            std::uint32_t main_thread_id) {
    try {
        require_main_thread(main_thread_id);
        validate_app(app);
        if (!request.is_object())
            throw std::runtime_error("Selector action must be a JSON object.");
        if (scope.empty() || scope.size() > 128)
            throw std::runtime_error("A current UI scope token is required.");
        const auto type = request.value("type", std::string{});
        const bool is_scroll = type == "achievements_scroll";
        const bool is_back = type == "achievements_back" ||
            (type == "navigate" && request.contains("route") && request["route"].is_string() &&
             request["route"].get<std::string>() == "achievements_back");
        if (!is_scroll && !is_back && type != "navigate")
            throw std::runtime_error("Selector action type must be navigate, achievements_back, or achievements_scroll.");
        const auto selector = selector_for(app);
        require_no_dialogs(app);
        if (g_pending.state == "queued" || g_pending.state == "dispatching")
            throw std::runtime_error("A selector action is already pending.");
        PendingAction scheduled{};
        scheduled.app = app;
        scheduled.scope = scope;
        scheduled.thread_id = main_thread_id;
        scheduled.id = g_next_id++;
        if (is_back) {
            const auto screen = achievement_screen_for(app, selector);
            const auto manager = read<std::uint32_t>(add(app, kWidgetManager), "WidgetManager pointer");
            if (read<std::uint32_t>(add(manager, kManagerDownButtons), "held mouse buttons") != 0)
                throw std::runtime_error("Refusing to queue while a mouse button is held.");
            scheduled.achievements_back = true;
            scheduled.selector = selector;
            scheduled.achievement_screen = screen;
            scheduled.widget_manager = manager;
            scheduled.route = "achievements_back";
        } else if (is_scroll) {
            if (!request.contains("direction") || !request["direction"].is_string() ||
                !request.contains("ticks") || !request["ticks"].is_number_integer() ||
                request["ticks"].is_boolean())
                throw std::runtime_error("Achievement scroll requires direction='up'|'down' and integer ticks.");
            const auto direction = request["direction"].get<std::string>();
            const auto ticks = request["ticks"].get<int>();
            if (direction != "up" && direction != "down")
                throw std::runtime_error("Achievement scroll direction must be 'up' or 'down'.");
            if (ticks < 1 || ticks > 60)
                throw std::runtime_error("Achievement scroll ticks must be between 1 and 60.");
            const auto screen = achievement_screen_for(app, selector);
            const auto manager = read<std::uint32_t>(add(app, kWidgetManager), "WidgetManager pointer");
            if (read<std::uint32_t>(add(manager, kManagerKeyFocus), "key-focus widget") != screen)
                throw std::runtime_error("The AchievementScreen must own input focus before scrolling.");
            if (read<std::uint32_t>(add(manager, kManagerDownButtons), "held mouse buttons") != 0)
                throw std::runtime_error("Refusing to queue scroll while a mouse button is held.");
            scheduled.achievements_scroll = true;
            scheduled.selector = selector;
            scheduled.achievement_screen = screen;
            scheduled.widget_manager = manager;
            scheduled.scroll_delta = direction == "up" ? 1 : -1;
            scheduled.scroll_ticks_total = ticks;
            scheduled.scroll_ticks_remaining = ticks;
            scheduled.route = "achievements_scroll_" + direction;
        } else {
            if (!request.contains("route") || !request["route"].is_string())
                throw std::runtime_error("Selector navigation requires a route name.");
            const auto route_name = request.at("route").get<std::string>();
            const auto* route = route_for_name(route_name);
            if (!route) throw std::runtime_error("Unknown selector route.");
            if (!route->supported)
                throw std::runtime_error(std::string("This selector route is not exposed: ") + route->name);
            verify_callback(selector);

            // Read the complete live widget tree now; tick() repeats this check
            // immediately before dispatch so stale or hidden controls fail closed.
            const auto live = ui_snapshot(app, scope, main_thread_id);
            if (!live.value("ok", false) || live.value("truncated", true) ||
                live.value("skipped_widgets", 0) != 0)
                throw std::runtime_error("Selector widget metadata is incomplete.");
            int matches = 0;
            bool enabled = false;
            for (const auto& item : live.value("controls", json::array())) {
                if (!item.is_object() || !item.contains("control_code") ||
                    !item["control_code"].is_number_integer() ||
                    item["control_code"].get<int>() != route->id) continue;
                ++matches;
                enabled = item.value("visible", false) && item.value("enabled", false);
            }
            if (matches != 1) throw std::runtime_error("Selector route does not resolve to one current control.");
            if (!enabled) throw std::runtime_error("Selector route is not currently visible and enabled.");
            scheduled.button_id = route->id;
            scheduled.route = route->name;
        }
        scheduled.state = "queued";
        g_pending = std::move(scheduled);
        return {{"ok", true}, {"accepted", true}, {"pending", true},
            {"job_id", g_pending.id}, {"route", g_pending.route},
            {"button_id", is_back || is_scroll ? json(nullptr) : json(g_pending.button_id)},
            {"ticks", is_scroll ? json(g_pending.scroll_ticks_total) : json(nullptr)}};
    } catch (const std::exception& error) {
        return fail(error.what());
    } catch (...) {
        return fail("Unknown selector action failure.");
    }
}

void tick(std::uintptr_t app, const std::string& scope,
          std::uint32_t main_thread_id) {
    if (g_ticking || g_pending.state != "queued") return;
    g_ticking = true;
    // Clear the queued state before invoking game code. If the ordinary action
    // enters the game's nested update loop, reentrant ticks cannot repeat it.
    g_pending.state = "dispatching";
    try {
        require_main_thread(main_thread_id);
        if (app != g_pending.app || scope != g_pending.scope ||
            main_thread_id != g_pending.thread_id)
            throw std::runtime_error("Selector action scope changed before dispatch.");
        validate_app(app);
        const auto selector = selector_for(app);
        require_no_dialogs(app);
        if (g_pending.achievements_back) {
            if (selector != g_pending.selector ||
                achievement_screen_for(app, selector) != g_pending.achievement_screen ||
                read<std::uint32_t>(add(app, kWidgetManager), "current WidgetManager") != g_pending.widget_manager)
                throw std::runtime_error("Queued AchievementScreen return target became stale.");
            request_selector_achievement_return(app, selector, g_pending.achievement_screen,
                g_pending.widget_manager);
            g_pending.state = "dispatched";
            g_pending.detail = "The normal selector return helper started the AchievementScreen transition.";
        } else if (g_pending.achievements_scroll) {
            if (selector != g_pending.selector ||
                achievement_screen_for(app, selector) != g_pending.achievement_screen ||
                read<std::uint32_t>(add(app, kWidgetManager), "current WidgetManager") != g_pending.widget_manager)
                throw std::runtime_error("Queued AchievementScreen scroll target became stale.");
            apply_achievement_scroll_tick(app, selector, g_pending.achievement_screen,
                g_pending.widget_manager, g_pending.scroll_delta, scope);
            --g_pending.scroll_ticks_remaining;
            if (g_pending.scroll_ticks_remaining > 0) {
                g_pending.state = "queued";
                g_pending.detail = "Applied one bounded normal AchievementScreen wheel tick; more ticks remain.";
            } else {
                g_pending.state = "dispatched";
                g_pending.detail = "All requested bounded normal AchievementScreen wheel ticks were applied.";
            }
        } else {
            verify_callback(selector);
            const auto* route = route_for_name(g_pending.route);
            if (!route || !route->supported || route->id != g_pending.button_id)
                throw std::runtime_error("Pending selector route is no longer supported.");

            const auto live = ui_snapshot(app, scope, main_thread_id);
            if (!live.value("ok", false) || live.value("truncated", true) ||
                live.value("skipped_widgets", 0) != 0)
                throw std::runtime_error("Selector widget metadata became incomplete before dispatch.");
            int matches = 0;
            bool enabled = false;
            for (const auto& item : live.value("controls", json::array())) {
                if (!item.is_object() || !item.contains("control_code") ||
                    !item["control_code"].is_number_integer() ||
                    item["control_code"].get<int>() != route->id) continue;
                ++matches;
                enabled = item.value("visible", false) && item.value("enabled", false);
            }
            if (matches != 1 || !enabled)
                throw std::runtime_error("Selector control changed or became unavailable before dispatch.");

            // GameSelector::ButtonDepress is the ordinary in-game callback. The
            // pending marker was consumed before entering it to prevent reentry.
            g_pending.state = "dispatched";
            invoke_selector(selector, route->id);
            g_pending.detail = "The verified normal GameSelector callback was invoked.";
        }
    } catch (const std::exception& error) {
        g_pending.state = "failed";
        g_pending.detail = error.what();
    } catch (...) {
        g_pending.state = "failed";
        g_pending.detail = "Unknown selector dispatch failure.";
    }
    g_ticking = false;
}

} // namespace pvz::selector_controls
