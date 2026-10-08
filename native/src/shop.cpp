#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

#include "shop.hpp"

namespace pvz::shop {
namespace {
constexpr std::uintptr_t kImageBase = 0x400000;
constexpr std::uintptr_t kAppPointer = 0x731c50;
constexpr std::uintptr_t kShowStoreScreen = 0x454a80;
constexpr std::uintptr_t kStoreVtable = 0x6f9700;
constexpr std::uintptr_t kStoreSecondaryVtable = 0x6f9830;
constexpr std::uintptr_t kStoreButtonDepress = 0x49b050;
constexpr std::uintptr_t kStoreMouseDown = 0x49be60;
constexpr std::uintptr_t kDialogKeyDown = 0x45d520;
constexpr std::uintptr_t kPageAvailable = 0x49afd0;
constexpr std::uintptr_t kWaitForResult = 0x55ca10;
constexpr std::uintptr_t kGetItemAt = 0x4992d0;
constexpr std::uintptr_t kGetItemCost = 0x49b1a0;
constexpr std::uintptr_t kIsFullVersionOnly = 0x499440;
constexpr std::uintptr_t kIsSoldOut = 0x499530;
constexpr std::uintptr_t kIsUnavailable = 0x499630;
constexpr std::uintptr_t kIsComingSoon = 0x4994a0;
constexpr std::size_t kMaxVisibleItems = 8;

// PurchaseItem enters a nested game wait loop. The main-loop hook calls tick()
// during that loop, and only this one pending purchase may be confirmed there.
bool purchase_pending = false;
bool purchase_confirmed = false;
bool purchase_confirmation_sent = false;
int pending_item_id = -1;
int pending_cost = 0;
int pending_raw_coins = 0;
int pending_max_total_cost = 0;
std::uintptr_t pending_store = 0;
std::chrono::steady_clock::time_point pending_deadline{};

void clear_pending_purchase() {
    purchase_pending = false;
    purchase_confirmed = false;
    purchase_confirmation_sent = false;
    pending_item_id = -1;
    pending_cost = 0;
    pending_raw_coins = 0;
    pending_max_total_cost = 0;
    pending_store = 0;
}

struct PendingPurchaseScope {
    ~PendingPurchaseScope() { clear_pending_purchase(); }
};

bool readable(std::uintptr_t address, std::size_t bytes) {
    if (address < 0x10000 || bytes == 0 || address + bytes < address) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info))) return false;
    const DWORD protection = info.Protect & 0xff;
    const bool can_read = protection == PAGE_READONLY || protection == PAGE_READWRITE ||
        protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
    const auto begin = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    const auto end = begin + info.RegionSize;
    return info.State == MEM_COMMIT && !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
        can_read && address >= begin && address + bytes <= end;
}

template<class T> T read_value(std::uintptr_t address, const char* description) {
    if (!readable(address, sizeof(T)))
        throw std::runtime_error(std::string("Unreadable shop ") + description + ".");
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
}

bool valid_app(std::uintptr_t application) {
    return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) == kImageBase &&
        application != 0 && application == read_value<std::uint32_t>(kAppPointer, "application pointer");
}

bool valid_store(std::uintptr_t application, std::uintptr_t store) {
    return store && readable(store, 0x250) &&
        read_value<std::uint32_t>(store, "screen vtable") == kStoreVtable &&
        read_value<std::uint32_t>(store + 0xa0, "screen secondary vtable") == kStoreSecondaryVtable &&
        read_value<std::uint32_t>(store + 0x16c, "screen owner") == application;
}

std::uintptr_t __declspec(noinline) show_store(std::uintptr_t application) {
    const std::uintptr_t function = kShowStoreScreen;
    std::uintptr_t result = 0;
    __asm {
        mov edi, application
        call function
        mov result, eax
    }
    return result;
}

std::uintptr_t __declspec(noinline) app_dialog(std::uintptr_t application, int dialog_id) {
    const std::uintptr_t vtable = read_value<std::uint32_t>(application, "app vtable");
    const std::uintptr_t function = read_value<std::uint32_t>(vtable + 0x124, "dialog lookup");
    std::uintptr_t result = 0;
    __asm {
        mov ecx, application
        push dialog_id
        call function
        mov result, eax
    }
    return result;
}

int __declspec(noinline) app_dialog_count(std::uintptr_t application) {
    const std::uintptr_t vtable = read_value<std::uint32_t>(application, "app vtable");
    const std::uintptr_t function = read_value<std::uint32_t>(vtable + 0x13c, "dialog count");
    int result = 0;
    __asm {
        mov ecx, application
        call function
        mov result, eax
    }
    return result;
}

bool store_ready(std::uintptr_t store) {
    return read_value<std::int32_t>(store + 0x180, "screen animation ticks") >= 120 &&
        read_value<std::uint8_t>(store + 0x248, "screen interaction lock") == 0 &&
        read_value<std::int32_t>(store + 0x1b8, "screen transition timer") <= 0 &&
        read_value<std::uint8_t>(store + 0x1cd, "screen transition flag") == 0 &&
        read_value<std::uint8_t>(store + 0x1a4, "screen speech flag") == 0;
}

bool close_button_ready(std::uintptr_t store) {
    const auto button = read_value<std::uint32_t>(store + 0x170, "store back button");
    return button && readable(button, 0x68) &&
        read_value<std::uint8_t>(button + 0x65, "store back button visibility") != 0 &&
        read_value<std::uint8_t>(button + 0x66, "store back button disabled state") == 0;
}

int __declspec(noinline) store_wait_for_result(std::uintptr_t store) {
    const auto vtable = read_value<std::uint32_t>(store, "screen vtable");
    const auto function = read_value<std::uint32_t>(vtable + 0x12c, "screen wait-for-result callback");
    if (function != kWaitForResult)
        throw std::runtime_error("The verified store result-wait callback is unavailable.");
    int result = -1;
    __asm {
        mov ecx, store
        push 1
        call function
        mov result, eax
    }
    return result;
}

void __declspec(noinline) dialog_key_down(std::uintptr_t dialog, int key) {
    const std::uintptr_t function = kDialogKeyDown;
    __asm {
        mov ecx, dialog
        push key
        call function
    }
}

void __declspec(noinline) store_mouse_down(std::uintptr_t store, int x, int y) {
    const std::uintptr_t function = kStoreMouseDown;
    __asm {
        mov ecx, store
        push 1
        push y
        push x
        call function
    }
}

bool __declspec(noinline) page_available(std::uintptr_t store, int page) {
    const std::uintptr_t function = kPageAvailable;
    unsigned char result = 0;
    __asm {
        mov edi, store
        mov ebx, page
        call function
        mov result, al
    }
    return result != 0;
}

void __declspec(noinline) store_button_depress(std::uintptr_t store, int button_id) {
    const auto secondary = store + 0xa0;
    const auto vtable = read_value<std::uint32_t>(secondary, "screen secondary vtable");
    if (vtable != kStoreSecondaryVtable ||
        read_value<std::uint32_t>(vtable + 8, "store button callback") != kStoreButtonDepress)
        throw std::runtime_error("The verified store page-button callback is unavailable.");
    const std::uintptr_t function = kStoreButtonDepress;
    __asm {
        mov ecx, secondary
        push button_id
        call function
    }
}

int next_available_page(std::uintptr_t store, int current, int direction) {
    for (int step = 1; step <= 4; ++step) {
        int candidate = (current + direction * step) % 4;
        if (candidate < 0) candidate += 4;
        if (page_available(store, candidate)) return candidate;
    }
    return current;
}

int __declspec(noinline) item_at(std::uintptr_t store, int visible_index) {
    const std::uintptr_t function = kGetItemAt;
    int result = -1;
    __asm {
        mov eax, visible_index
        mov edx, store
        call function
        mov result, eax
    }
    return result;
}

int __declspec(noinline) item_cost(int item_id) {
    const std::uintptr_t function = kGetItemCost;
    int result = 0;
    __asm {
        mov ecx, item_id
        call function
        mov result, eax
    }
    if (result < 0 || result > (std::numeric_limits<int>::max() / 10))
        throw std::runtime_error("Invalid shop item cost.");
    return result * 10;
}

bool __declspec(noinline) is_full_version_only(std::uintptr_t store, int item_id) {
    const std::uintptr_t function = kIsFullVersionOnly;
    unsigned char result = 0;
    __asm {
        mov edi, store
        mov ebx, item_id
        call function
        mov result, al
    }
    return result != 0;
}

bool __declspec(noinline) is_sold_out(std::uintptr_t store, int item_id) {
    const std::uintptr_t function = kIsSoldOut;
    unsigned char result = 0;
    __asm {
        mov edi, item_id
        mov ebx, store
        call function
        mov result, al
    }
    return result != 0;
}

bool __declspec(noinline) is_unavailable(std::uintptr_t store, int item_id) {
    const std::uintptr_t function = kIsUnavailable;
    unsigned char result = 0;
    __asm {
        mov eax, item_id
        mov ecx, store
        call function
        mov result, al
    }
    return result != 0;
}

bool __declspec(noinline) is_coming_soon(std::uintptr_t store, int item_id) {
    const std::uintptr_t function = kIsComingSoon;
    unsigned char result = 0;
    __asm {
        mov eax, item_id
        mov ecx, store
        call function
        mov result, al
    }
    return result != 0;
}

std::uintptr_t get_store(std::uintptr_t application) {
    const auto store = app_dialog(application, 4);
    return valid_store(application, store) ? store : 0;
}

json item_record(std::uintptr_t store, int visible_index, int item_id, int coins) {
    const bool full_version_only = is_full_version_only(store, item_id);
    const bool sold_out = is_sold_out(store, item_id);
    const bool unavailable = is_unavailable(store, item_id);
    const bool coming_soon = is_coming_soon(store, item_id);
    const int cost = item_cost(item_id);
    const bool affordable = coins >= cost;
    const bool eligible = !full_version_only && !sold_out && !unavailable && !coming_soon;
    return {{"visible_index", visible_index}, {"item_id", item_id},
        {"cost", cost}, {"affordable", affordable}, {"can_buy", eligible && affordable},
        {"full_version_only", full_version_only},
        {"sold_out", sold_out}, {"unavailable", unavailable},
        {"coming_soon", coming_soon},
        {"purchasable", eligible}};
}
} // namespace

json snapshot(std::uintptr_t application) {
    if (!valid_app(application)) throw std::runtime_error("Shop query refused for an unexpected game app.");
    json result = {{"open", false}, {"items", json::array()}};
    const auto profile = read_value<std::uint32_t>(application + 0x94c, "profile pointer");
    int coins = 0;
    if (profile && readable(profile, 0x58)) {
        const auto raw_coins = read_value<std::int32_t>(profile + 0x54, "coin balance");
        if (raw_coins < 0 || raw_coins > std::numeric_limits<int>::max() / 10)
            throw std::runtime_error("Invalid shop coin balance.");
        coins = raw_coins * 10;
        result["coins"] = coins;
    }
    const auto store = get_store(application);
    if (!store) return result;

    result["open"] = true;
    result["ready"] = store_ready(store);
    const int page = read_value<std::int32_t>(store + 0x1b0, "page");
    if (page < 0 || page > 3) throw std::runtime_error("Unexpected store page value.");
    result["page"] = page;
    result["available_pages"] = json::array();
    for (int candidate = 0; candidate < 4; ++candidate)
        if (page_available(store, candidate)) result["available_pages"].push_back(candidate);
    for (int index = 0; index < static_cast<int>(kMaxVisibleItems); ++index) {
        const int item_id = item_at(store, index);
        if (item_id < 0 || item_id > 0x40) continue;
        result["items"].push_back(item_record(store, index, item_id, coins));
    }
    return result;
}

json dispatch(std::uintptr_t application, const std::string& type, const json& args) {
    if (!valid_app(application)) throw std::runtime_error("Shop action refused for an unexpected game app.");
    if (purchase_pending) throw std::runtime_error("A shop purchase is already awaiting its verified game confirmation.");
    if (type == "open_shop") {
        if (!args.empty()) throw std::runtime_error("open_shop takes no arguments.");
        if (get_store(application)) return {{"accepted", true}, {"open", true}, {"already_open", true}};
        const int scene = read_value<std::int32_t>(application + 0x91c, "scene");
        const int mode = read_value<std::int32_t>(application + 0x918, "mode");
        if (scene != 1 && !(scene == 3 && mode == 43))
            throw std::runtime_error("The store is only supported from the main menu or Zen Garden.");
        const auto manager = read_value<std::uint32_t>(application + 0x320, "widget manager");
        if (!manager || !readable(manager, 0xa4) || app_dialog_count(application) != 0)
            throw std::runtime_error("Close active dialogs before opening the store.");
        const std::uintptr_t created = show_store(application);
        if (!valid_store(application, created))
            throw std::runtime_error("The verified store screen was not created.");
        if (app_dialog(application, 4) != created)
            throw std::runtime_error("The game did not register the expected store screen.");
        return {{"accepted", true}, {"open", true}, {"transition_pending", true}};
    }
    if (type == "shop_buy") {
        if (args.size() != 2 || !args.contains("item_id") || !args.contains("max_total_cost") ||
            !args["item_id"].is_number_integer() || !args["max_total_cost"].is_number_integer())
            throw std::runtime_error("shop_buy requires integer item_id and max_total_cost.");
        const int item_id = args["item_id"].get<int>();
        const int max_total_cost = args["max_total_cost"].get<int>();
        if (item_id < 0 || item_id > 0x40 || max_total_cost <= 0)
            throw std::runtime_error("Invalid item_id or max_total_cost.");
        const auto store = get_store(application);
        if (!store) throw std::runtime_error("Open the store before requesting a purchase.");
        if (!store_ready(store)) throw std::runtime_error("Wait for the store's normal open/transition animation to finish.");
        if (app_dialog_count(application) != 1)
            throw std::runtime_error("Close other active dialogs before requesting a purchase.");
        const auto profile = read_value<std::uint32_t>(application + 0x94c, "profile pointer");
        if (!profile || !readable(profile, 0x58))
            throw std::runtime_error("The active player profile is unavailable.");
        const int raw_coins = read_value<std::int32_t>(profile + 0x54, "coin balance");
        if (raw_coins < 0 || raw_coins > std::numeric_limits<int>::max() / 10)
            throw std::runtime_error("Invalid shop coin balance.");
        const int coins = raw_coins * 10;
        int visible_index = -1;
        for (int index = 0; index < static_cast<int>(kMaxVisibleItems); ++index) {
            if (item_at(store, index) == item_id) { visible_index = index; break; }
        }
        if (visible_index < 0) throw std::runtime_error("That item is not visible on the current store page.");
        const auto item = item_record(store, visible_index, item_id, coins);
        if (!item["purchasable"].get<bool>())
            throw std::runtime_error("The game marks this shop item unavailable.");
        const int cost = item["cost"].get<int>();
        if (cost > max_total_cost)
            throw std::runtime_error("The current quote exceeds max_total_cost.");
        if (cost > coins) throw std::runtime_error("The player cannot afford this shop item.");

        // The coordinates are the center of the verified MouseDown hitbox for
        // this visible tile. MouseDown repeats all game eligibility checks and
        // enters the original PurchaseItem confirmation flow.
        const int tile = visible_index < 4 ? visible_index : visible_index - 4;
        const int x = (visible_index < 4 ? 0x1a6 : 0x4c) + tile * 0x4a + 25;
        const int y = (visible_index < 4 ? 0xce : 0x136) + 43;
        purchase_confirmed = false;
        pending_item_id = item_id;
        pending_cost = cost;
        pending_raw_coins = raw_coins;
        pending_max_total_cost = max_total_cost;
        pending_store = store;
        pending_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        purchase_confirmation_sent = false;
        purchase_pending = true;
        PendingPurchaseScope pending_scope;
        store_mouse_down(store, x, y);
        const bool confirmed = purchase_confirmed;
        const int after_raw_coins = read_value<std::int32_t>(profile + 0x54, "coin balance after purchase");
        if (after_raw_coins < 0 || after_raw_coins > std::numeric_limits<int>::max() / 10)
            throw std::runtime_error("Invalid shop coin balance after the game dialog returned.");
        const bool purchased = confirmed && after_raw_coins == raw_coins - cost / 10;
        return {{"accepted", purchased}, {"item_id", item_id}, {"cost", cost},
            {"purchased", purchased},
            {"reason", purchased ? "confirmed" : (confirmed ? "game_did_not_complete_purchase" : "confirmation_not_accepted")},
            {"coins", after_raw_coins * 10}};
    }
    if (type == "close_shop") {
        if (!args.empty()) throw std::runtime_error("close_shop takes no arguments.");
        const auto store = get_store(application);
        if (!store) return {{"accepted", true}, {"open", false}, {"already_closed", true}};
        if (!store_ready(store)) throw std::runtime_error("Wait for the store's normal open/transition animation to finish.");
        if (!close_button_ready(store)) throw std::runtime_error("The game's normal store-close button is hidden or disabled.");
        if (app_dialog_count(application) != 1)
            throw std::runtime_error("Resolve the active dialog before closing the store.");
        store_button_depress(store, 100); // Ordinary StoreScreen back/close button.
        if (read_value<std::int32_t>(store + 0x160, "screen result") != 0x3e8)
            throw std::runtime_error("The normal store-close button did not set its result.");
        const int result = store_wait_for_result(store);
        const bool open = get_store(application) != 0;
        return {{"accepted", result == 0x3e8 && !open}, {"open", open},
            {"result", result}, {"close_pending", open}};
    }
    if (type == "shop_page") {
        if (args.size() != 1 || !args.contains("page") || !args["page"].is_number_integer())
            throw std::runtime_error("shop_page requires integer page.");
        const int target = args["page"].get<int>();
        if (target < 0 || target > 3) throw std::runtime_error("Store page must be between 0 and 3.");
        const auto store = get_store(application);
        if (!store) throw std::runtime_error("Open the store before changing its page.");
        if (!store_ready(store)) throw std::runtime_error("Wait for the store's normal open/transition animation to finish.");
        if (app_dialog_count(application) != 1)
            throw std::runtime_error("Close other active dialogs before changing the store page.");
        const int current = read_value<std::int32_t>(store + 0x1b0, "page");
        if (current < 0 || current > 3) throw std::runtime_error("Unexpected current store page.");
        if (target == current) return {{"accepted", true}, {"page", current}, {"already_selected", true}};
        if (!page_available(store, target)) throw std::runtime_error("The requested store page is still locked in the game.");
        const int next_forward = next_available_page(store, current, 1);
        const int next_backward = next_available_page(store, current, -1);
        constexpr int kNextPageButton = 102;
        constexpr int kPreviousPageButton = 101;
        if (target == next_forward) store_button_depress(store, kNextPageButton);
        else if (target == next_backward) store_button_depress(store, kPreviousPageButton);
        else throw std::runtime_error("Use the normal store page arrow one page at a time.");
        const int after = read_value<std::int32_t>(store + 0x1b0, "page after navigation");
        if (after != target)
            throw std::runtime_error("The game's normal page button did not select the requested page.");
        return {{"accepted", true}, {"page", after}, {"transition_pending", true}};
    }
    throw std::runtime_error("Unsupported shop action: " + type);
}

void tick(std::uintptr_t application) {
    if (!purchase_pending || purchase_confirmation_sent || application == 0 ||
        application != read_value<std::uint32_t>(kAppPointer, "application pointer"))
        return;
    // 0x2E is the affordable BUY_ITEM confirmation created by PurchaseItem.
    // Dialog 0x19 is the insufficient-funds branch and must never be accepted.
    auto dialog = app_dialog(application, 0x2e);
    if (!dialog && std::chrono::steady_clock::now() >= pending_deadline) {
        // An insufficient-funds modal can only appear if the preflight state
        // changed between the quote and PurchaseItem. Cancel that known normal
        // dialog through its verified key handler so the nested wait can exit.
        dialog = app_dialog(application, 0x19);
        if (!dialog || !readable(dialog, 0x170) || app_dialog_count(application) != 2) return;
        const auto vtable = read_value<std::uint32_t>(dialog, "insufficient-funds dialog vtable");
        if (read_value<std::uint32_t>(vtable + 0xc0, "dialog key handler") != kDialogKeyDown ||
            read_value<std::int32_t>(dialog + 0x158, "dialog id") != 0x19 ||
            read_value<std::uint32_t>(dialog + 0x16c, "dialog owner") != application) return;
        purchase_confirmation_sent = true;
        dialog_key_down(dialog, 27);
        return;
    }
    if (!dialog || !readable(dialog, 0x170) || app_dialog_count(application) != 2) return;

    // LawnDialog::KeyDown checks the object identity then routes Enter/Space/y
    // to the ordinary ButtonDepress(0x3E8) path; Escape routes to 0x3E9.
    const auto vtable = read_value<std::uint32_t>(dialog, "purchase dialog vtable");
    const auto key_handler = read_value<std::uint32_t>(vtable + 0xc0, "purchase dialog key handler");
    const auto dialog_id = read_value<std::int32_t>(dialog + 0x158, "purchase dialog id");
    const auto dialog_app = read_value<std::uint32_t>(dialog + 0x16c, "purchase dialog owner");
    if (dialog_id != 0x2e || dialog_app != application || key_handler != kDialogKeyDown) return;

    bool quote_matches = false;
    if (get_store(application) == pending_store && pending_item_id >= 0 && pending_cost > 0 &&
        pending_cost <= pending_max_total_cost) {
        const auto profile = read_value<std::uint32_t>(application + 0x94c, "profile pointer");
        if (profile && readable(profile, 0x58)) {
            const auto coins = read_value<std::int32_t>(profile + 0x54, "coin balance");
            quote_matches = coins == pending_raw_coins &&
                item_cost(pending_item_id) == pending_cost &&
                pending_raw_coins >= pending_cost / 10;
        }
    }
    if (quote_matches) {
        purchase_confirmed = true;
        purchase_confirmation_sent = true;
        dialog_key_down(dialog, 13);
    } else {
        // Never confirm a changed quote; cancel the exact expected modal via
        // the game's ordinary key handler so PurchaseItem can unwind.
        purchase_confirmation_sent = true;
        dialog_key_down(dialog, 27);
    }
}

} // namespace pvz::shop
