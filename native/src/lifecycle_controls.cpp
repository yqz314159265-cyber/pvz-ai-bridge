#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>

#include "lifecycle_controls.hpp"

namespace pvz::lifecycle_controls {
namespace {
using json = nlohmann::json;

constexpr std::uintptr_t kGlobalApp = 0x731c50;
constexpr std::uintptr_t kAppVtable = 0x6f19e0;
constexpr std::uintptr_t kAppSceneOffset = 0x91c;
constexpr std::uintptr_t kAppBoardOffset = 0x868;
constexpr std::uintptr_t kAppSelectorOffset = 0x870;
constexpr std::uintptr_t kAppCreditsScreenOffset = 0x87c;
constexpr std::uintptr_t kAppGetDialogSlot = 0x124;
constexpr std::uintptr_t kAppGetDialogCountSlot = 0x13c;
constexpr std::uintptr_t kSelectorVtable = 0x6eec88;
constexpr std::uintptr_t kSelectorListenerVtable = 0x6eeda0;
constexpr std::uintptr_t kSelectorButtonDepress = 0x450ee0;
constexpr std::uintptr_t kAppButtonDepress = 0x458590;
constexpr std::uintptr_t kQuitDialogVtable = 0x6f1d38;
constexpr std::uintptr_t kCreditsScreenVtable = 0x6ee068;
constexpr std::uintptr_t kCreditsKeyDown = 0x43c320;
constexpr std::uintptr_t kGameButtonVtable = 0x6ee7a8;
constexpr std::uintptr_t kLawnButtonVtable = 0x6ee680;
constexpr std::uintptr_t kWidgetManagerOffset = 0x320;
constexpr std::uintptr_t kWidgetManagerVtable = 0x6fc114;
constexpr std::uintptr_t kLoadingScreenVtable = 0x6f99a8;
constexpr std::uintptr_t kTitleButtonVtable = 0x6fcb2c;
constexpr std::uintptr_t kTitleListenerVtable = 0x6f9ac0;
constexpr std::uintptr_t kTitleButtonDepress = 0x49d490;
constexpr std::uintptr_t kLoadingStartAction = 0x457f20;
constexpr std::size_t kAppStartActionSlot = 0x1a8;
constexpr std::size_t kCreditsOwner = 0xa8;
constexpr std::size_t kCreditsSkipPending = 0xf9;
constexpr std::size_t kCreditsKeyDownSlot = 0xc0;
constexpr std::size_t kSelectorListener = 0xa0;
constexpr std::size_t kSelectorChildHead = 0x18;
constexpr std::size_t kSelectorChildCount = 0x1c;
constexpr std::size_t kWidgetManagerOwner = 0x20;
constexpr std::size_t kButtonId = 0xa0;
constexpr std::size_t kButtonLabel = 0xa8;
constexpr std::size_t kButtonLabelLength = 0xb8;
constexpr std::size_t kButtonLabelCapacity = 0xbc;
constexpr std::size_t kButtonVisible = 0x65;
constexpr std::size_t kButtonDisabled = 0x66;
constexpr std::size_t kQuitDialogOwner = 0xa4;
constexpr std::size_t kQuitDialogId = 0x158;
constexpr std::size_t kQuitDialogQuitButton = 0x17c;
constexpr std::size_t kQuitDialogCancelButton = 0x180;
constexpr std::size_t kDialogButtonPressId = 1000;
constexpr std::size_t kDialogButtonCancelId = 1001;
constexpr std::size_t kWidgetChildHead = 0x18;
constexpr std::size_t kWidgetChildCount = 0x1c;
constexpr std::size_t kListNodeWidget = 0x08;
constexpr std::size_t kListNodeNext = 0x00;
constexpr std::size_t kListNodePrevious = 0x04;
constexpr std::size_t kTitleChild = 0xa4;
constexpr std::size_t kTitleListener = 0xa0;
constexpr std::size_t kTitleApp = 0xdc;
constexpr std::size_t kVisible = 0x65;
constexpr std::size_t kDisabled = 0x66;
constexpr std::size_t kTitleText = 0xa8;
constexpr std::size_t kTitleTextLength = 0xb8;
constexpr std::size_t kTitleTextCapacity = 0xbc;
constexpr int kTitleButtonId = 0;
constexpr int kQuitButtonId = 105;
constexpr int kQuitConfirmAppButton = 2013;
constexpr int kQuitCancelAppButton = 3013;
constexpr int kCreditsEscapeKey = 0x1b;
constexpr int kLoadingScene = 0;
constexpr int kMenuScene = 1;
constexpr int kCreditsScene = 6;
constexpr std::uint32_t kMaxRoots = 1024;
constexpr std::uint32_t kMaxSelectorChildren = 512;
constexpr std::size_t kMaxTextLength = 256;

struct PendingAction {
    std::uintptr_t app = 0;
    std::uintptr_t screen = 0;
    std::uintptr_t button = 0;
    bool pending = false;
    bool invoked = false;
    std::string error;
    int button_id = kTitleButtonId;
    bool quit = false;
    bool quit_dialog_result = false;
    bool credits_skip = false;
};

PendingAction g_pending;

std::uintptr_t add(std::uintptr_t p, std::size_t n) {
    if (p < 0x10000 || n > std::numeric_limits<std::uintptr_t>::max() - p)
        throw std::runtime_error("Invalid lifecycle-control address.");
    return p + n;
}

bool readable(std::uintptr_t address, std::size_t bytes) {
    if (address < 0x10000 || bytes == 0 ||
        bytes > std::numeric_limits<std::uintptr_t>::max() - address) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info))) return false;
    const DWORD protection = info.Protect & 0xff;
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
        throw std::runtime_error(std::string("Unreadable lifecycle field: ") + field);
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
}

void validate_app(std::uintptr_t app) {
    if (app != read<std::uint32_t>(kGlobalApp, "global app") || !readable(app, 0x960) ||
        read<std::uint32_t>(app, "LawnApp vtable") != kAppVtable)
        throw std::runtime_error("Unexpected LawnApp pointer/layout.");
}

int dialog_count(std::uintptr_t app) {
    const auto method = read<std::uint32_t>(add(kAppVtable, kAppGetDialogCountSlot),
        "GetDialogCount slot");
    if (method < 0x401000 || method >= 0x6d0000)
        throw std::runtime_error("GetDialogCount slot is outside the verified image.");
    using GetDialogCount = int(__thiscall*)(void*);
    const int count = reinterpret_cast<GetDialogCount>(method)(reinterpret_cast<void*>(app));
    if (count < 0 || count > 0x80) throw std::runtime_error("Invalid game dialog count.");
    return count;
}

std::uintptr_t get_dialog(std::uintptr_t app, int id) {
    const auto method = read<std::uint32_t>(add(kAppVtable, kAppGetDialogSlot), "GetDialog slot");
    if (method != 0x5ce270)
        throw std::runtime_error("GetDialog slot does not match the verified 1096 target.");
    using GetDialog = void*(__thiscall*)(void*, int);
    return reinterpret_cast<std::uintptr_t>(
        reinterpret_cast<GetDialog>(method)(reinterpret_cast<void*>(app), id));
}

std::uintptr_t selector(std::uintptr_t app) {
    if (read<std::int32_t>(add(app, kAppSceneOffset), "scene") != kMenuScene)
        throw std::runtime_error("Quit confirmation is available only from the main menu.");
    const auto pointer = read<std::uint32_t>(add(app, kAppSelectorOffset), "GameSelector");
    if (!pointer || !readable(pointer, 0x170) ||
        read<std::uint32_t>(pointer, "GameSelector vtable") != kSelectorVtable)
        throw std::runtime_error("The active GameSelector does not match the verified 1096 class.");
    const auto listener = add(pointer, kSelectorListener);
    if (!readable(listener, 0x0c) ||
        read<std::uint32_t>(listener, "selector ButtonListener vtable") != kSelectorListenerVtable ||
        read<std::uint32_t>(add(kSelectorListenerVtable, 0x08), "selector ButtonDepress slot") !=
            kSelectorButtonDepress)
        throw std::runtime_error("Selector ButtonDepress does not match the verified normal callback.");
    return pointer;
}

std::uintptr_t credits_screen(std::uintptr_t app) {
    if (read<std::int32_t>(add(app, kAppSceneOffset), "scene") != kCreditsScene)
        throw std::runtime_error("Credits Escape is available only on the Credits screen.");
    if (dialog_count(app) != 0)
        throw std::runtime_error("Resolve the active dialog before skipping Credits.");
    const auto screen = read<std::uint32_t>(add(app, kAppCreditsScreenOffset), "CreditsScreen");
    if (!screen || !readable(screen, kCreditsSkipPending + sizeof(std::uint8_t)) ||
        read<std::uint32_t>(screen, "CreditsScreen vtable") != kCreditsScreenVtable ||
        read<std::uint32_t>(add(screen, kCreditsOwner), "CreditsScreen app owner") != app ||
        read<std::uint32_t>(add(kCreditsScreenVtable, kCreditsKeyDownSlot), "Credits KeyDown slot") != kCreditsKeyDown)
        throw std::runtime_error("CreditsScreen does not match the verified active screen and KeyDown callback.");
    if (read<std::uint8_t>(add(screen, kCreditsSkipPending), "Credits skip modal guard") != 0)
        throw std::runtime_error("Credits is already processing a skip request.");
    return screen;
}

std::string button_label(std::uintptr_t button) {
    const auto length = read<std::uint32_t>(add(button, kButtonLabelLength), "selector button label length");
    const auto capacity = read<std::uint32_t>(add(button, kButtonLabelCapacity), "selector button label capacity");
    if (!length || length > kMaxTextLength || capacity < length || capacity > kMaxTextLength)
        throw std::runtime_error("Selector button label metadata is invalid.");
    const auto storage = capacity < 16 ? add(button, kButtonLabel)
        : static_cast<std::uintptr_t>(read<std::uint32_t>(add(button, kButtonLabel), "selector label storage"));
    if (!storage || !readable(storage, length)) throw std::runtime_error("Selector button label is unreadable.");
    return std::string(reinterpret_cast<const char*>(storage), length);
}

std::uintptr_t quit_button(std::uintptr_t app, std::uintptr_t menu) {
    if (read<std::uint32_t>(add(app, kAppBoardOffset), "LawnApp Board") != 0)
        throw std::runtime_error("Quit confirmation is refused while a board is active.");
    if (dialog_count(app) != 0)
        throw std::runtime_error("Close existing dialogs before opening Quit confirmation.");
    const auto manager = read<std::uint32_t>(add(app, kWidgetManagerOffset), "WidgetManager");
    if (!manager || !readable(manager, 0x24) ||
        read<std::uint32_t>(manager, "WidgetManager vtable") != kWidgetManagerVtable)
        throw std::runtime_error("WidgetManager does not match the verified 1096 class.");
    const auto count = read<std::uint32_t>(add(menu, kSelectorChildCount), "selector child count");
    if (count > kMaxSelectorChildren) throw std::runtime_error("Selector child count exceeds the verified bound.");
    if (!count) throw std::runtime_error("Selector contains no controls.");
    const auto head = read<std::uint32_t>(add(menu, kSelectorChildHead), "selector child-list head");
    if (!head || !readable(head, 8)) throw std::runtime_error("Selector child-list head is unreadable.");
    const auto tail = read<std::uint32_t>(add(head, 4), "selector child-list tail");
    auto node = read<std::uint32_t>(head, "selector first child node");
    std::uintptr_t previous = head;
    std::uintptr_t found = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!node || node == head || !readable(node, 12) ||
            read<std::uint32_t>(add(node, 4), "selector previous node") != previous)
            throw std::runtime_error("Selector child-list links are inconsistent.");
        const auto child = read<std::uint32_t>(add(node, 8), "selector child widget");
        const auto next = read<std::uint32_t>(node, "selector next child node");
        if (!child || !next) throw std::runtime_error("Selector child-list entry is incomplete.");
        if (read<std::uint32_t>(child, "selector child vtable") == kLawnButtonVtable &&
            read<std::uint32_t>(add(child, kButtonId), "selector button ID") == kQuitButtonId) {
            if (found) throw std::runtime_error("Selector contains duplicate Quit buttons.");
            found = child;
        }
        previous = node;
        node = next;
    }
    if (node != head || previous != tail)
        throw std::runtime_error("Selector child count does not match its list.");
    if (!found || !readable(found, kButtonDisabled + sizeof(std::uint8_t)) ||
        read<std::uint32_t>(add(found, kWidgetManagerOwner), "Quit button manager") != manager ||
        read<std::uint32_t>(add(found, kButtonId), "Quit button ID") != kQuitButtonId)
        throw std::runtime_error("The verified selector Quit control was not found.");
    const auto visible = read<std::uint8_t>(add(found, kButtonVisible), "Quit button visibility");
    const auto disabled = read<std::uint8_t>(add(found, kButtonDisabled), "Quit button disabled state");
    if (visible != 1 || disabled != 0)
        throw std::runtime_error("Quit button is hidden or disabled.");
    return found;
}

std::uintptr_t quit_dialog_button(std::uintptr_t app, std::uintptr_t dialog, bool confirm) {
    if (read<std::int32_t>(add(app, kAppSceneOffset), "scene") != kMenuScene ||
        read<std::uint32_t>(add(app, kAppBoardOffset), "LawnApp Board") != 0 ||
        dialog_count(app) != 1 || get_dialog(app, 13) != dialog || !dialog ||
        !readable(dialog, kQuitDialogCancelButton + sizeof(std::uint32_t)) ||
        read<std::uint32_t>(dialog, "Quit dialog vtable") != kQuitDialogVtable ||
        read<std::int32_t>(add(dialog, kQuitDialogId), "Quit dialog ID") != 13 ||
        read<std::uint32_t>(add(dialog, kQuitDialogOwner), "Quit dialog owner") != app + 4)
        throw std::runtime_error("Active Quit dialog 13 no longer matches its captured owner and class.");

    const auto manager = read<std::uint32_t>(add(app, kWidgetManagerOffset), "WidgetManager");
    if (!manager || !readable(manager, 0x24) ||
        read<std::uint32_t>(manager, "WidgetManager vtable") != kWidgetManagerVtable)
        throw std::runtime_error("WidgetManager does not match the verified 1096 class.");
    const auto quit = read<std::uint32_t>(add(dialog, kQuitDialogQuitButton), "Quit confirmation button");
    const auto cancel = read<std::uint32_t>(add(dialog, kQuitDialogCancelButton), "Quit cancel button");
    const auto target = confirm ? quit : cancel;
    const auto expected_id = confirm ? kDialogButtonPressId : kDialogButtonCancelId;
    const auto expected_label = confirm ? "Quit" : "Cancel";
    for (const auto button : {quit, cancel}) {
        if (!button || !readable(button, kButtonLabelCapacity + sizeof(std::uint32_t)) ||
            read<std::uint32_t>(button, "Quit dialog child button vtable") != kGameButtonVtable ||
            read<std::uint32_t>(add(button, kWidgetManagerOwner), "Quit dialog button manager") != manager ||
            read<std::uint8_t>(add(button, kButtonVisible), "Quit dialog button visibility") != 1 ||
            read<std::uint8_t>(add(button, kButtonDisabled), "Quit dialog button disabled") != 0)
            throw std::runtime_error("Quit dialog buttons are not both visible and enabled.");
    }
    if (quit == cancel ||
        read<std::uint32_t>(add(quit, kButtonId), "Quit button ID") != kDialogButtonPressId ||
        read<std::uint32_t>(add(cancel, kButtonId), "Cancel button ID") != kDialogButtonCancelId ||
        button_label(target) != expected_label)
        throw std::runtime_error("Quit dialog buttons no longer match the captured IDs and labels.");
    if (read<std::uint32_t>(add(kAppVtable, 0x08), "LawnApp ButtonDepress slot") != kAppButtonDepress)
        throw std::runtime_error("LawnApp ButtonDepress does not match the verified normal callback.");
    return target;
}

std::uintptr_t title_screen(std::uintptr_t app) {
    if (read<std::int32_t>(add(app, kAppSceneOffset), "scene") != kLoadingScene) return 0;
    const auto manager = read<std::uint32_t>(add(app, kWidgetManagerOffset), "WidgetManager");
    if (!manager || !readable(manager, 0x24) ||
        read<std::uint32_t>(manager, "WidgetManager vtable") != kWidgetManagerVtable)
        throw std::runtime_error("WidgetManager does not match the verified 1096 layout.");
    const auto count = read<std::uint32_t>(add(manager, kWidgetChildCount), "root count");
    if (count > kMaxRoots) throw std::runtime_error("WidgetManager root count exceeds its verified limit.");
    if (!count) return 0;
    const auto head = read<std::uint32_t>(add(manager, kWidgetChildHead), "root-list head");
    if (!head || !readable(head, 8)) throw std::runtime_error("WidgetManager root-list head is unreadable.");
    const auto expected_tail = read<std::uint32_t>(add(head, 4), "root-list tail");
    auto node = read<std::uint32_t>(head, "first root-list node");
    std::uintptr_t previous = head;
    std::uintptr_t found = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!node || node == head || !readable(node, 12))
            throw std::runtime_error("WidgetManager root-list ended early.");
        if (read<std::uint32_t>(add(node, kListNodePrevious), "root-list previous") != previous)
            throw std::runtime_error("WidgetManager root-list links are inconsistent.");
        const auto widget = read<std::uint32_t>(add(node, kListNodeWidget), "root widget");
        const auto next = read<std::uint32_t>(add(node, kListNodeNext), "next root-list node");
        if (!widget || !next) throw std::runtime_error("WidgetManager root entry is incomplete.");
        if (read<std::uint32_t>(widget, "root widget vtable") == kLoadingScreenVtable) {
            if (found) throw std::runtime_error("Multiple verified loading-screen roots are active.");
            found = widget;
        }
        previous = node;
        node = next;
    }
    if (node != head || previous != expected_tail)
        throw std::runtime_error("WidgetManager root count does not match its list.");
    return found;
}

std::uintptr_t title_button(std::uintptr_t screen) {
    if (!screen) return 0;
    if (!readable(screen, kTitleApp + sizeof(std::uint32_t)) ||
        read<std::uint32_t>(screen, "LoadingScreen vtable") != kLoadingScreenVtable)
        throw std::runtime_error("LoadingScreen root does not match the verified class.");
    const auto child = read<std::uint32_t>(add(screen, kTitleChild), "title button");
    if (!child || !readable(child, kTitleTextCapacity + sizeof(std::uint32_t)) ||
        read<std::uint32_t>(child, "title widget vtable") != kTitleButtonVtable)
        throw std::runtime_error("Click-to-start widget does not match the verified class.");
    const auto length = read<std::uint32_t>(add(child, kTitleTextLength), "title text length");
    const auto capacity = read<std::uint32_t>(add(child, kTitleTextCapacity), "title text capacity");
    if (!length || length > kMaxTextLength || capacity < length || capacity > kMaxTextLength)
        throw std::runtime_error("Click-to-start text metadata is outside the verified bounds.");
    const auto text = capacity < 16 ? add(child, kTitleText)
        : static_cast<std::uintptr_t>(read<std::uint32_t>(add(child,kTitleText),"title text storage"));
    if (!readable(text, length)) throw std::runtime_error("Click-to-start label is unreadable.");
    const std::string label(reinterpret_cast<const char*>(text), length);
    if (label != "CLICK TO START!")
        throw std::runtime_error("Loading title does not show the verified Click to Start action.");
    const auto visible = read<std::uint8_t>(add(child, kVisible), "title widget visibility");
    const auto disabled = read<std::uint8_t>(add(child, kDisabled), "title widget disabled state");
    if (visible != 1 || disabled != 0)
        throw std::runtime_error("Click-to-start widget is hidden or disabled.");
    const auto app = read<std::uint32_t>(kGlobalApp, "global app");
    if (read<std::uint32_t>(add(screen, kTitleApp), "LoadingScreen app") != app)
        throw std::runtime_error("LoadingScreen no longer belongs to the active app.");
    const auto app_vtable = read<std::uint32_t>(app, "LawnApp vtable");
    if (app_vtable != kAppVtable ||
        read<std::uint32_t>(add(app_vtable, kAppStartActionSlot), "loading-start action slot") != kLoadingStartAction)
        throw std::runtime_error("LawnApp loading-start action does not match the verified normal route.");
    const auto listener = add(screen, kTitleListener);
    if (read<std::uint32_t>(listener, "title ButtonListener vtable") != kTitleListenerVtable ||
        read<std::uint32_t>(add(kTitleListenerVtable, 0x08), "title ButtonDepress slot") != kTitleButtonDepress)
        throw std::runtime_error("Title ButtonListener callback does not match the verified normal path.");
    return child;
}

void invoke_title_button(std::uintptr_t screen, int id) {
    const auto listener = add(screen, kTitleListener);
    if (read<std::uint32_t>(listener, "title listener vtable") != kTitleListenerVtable ||
        read<std::uint32_t>(add(kTitleListenerVtable, 0x08), "title callback") != kTitleButtonDepress)
        throw std::runtime_error("Title ButtonDepress callback changed before invocation.");
    const auto app = read<std::uint32_t>(kGlobalApp, "global app");
    const auto app_vtable = read<std::uint32_t>(app, "LawnApp vtable");
    if (app_vtable != kAppVtable ||
        read<std::uint32_t>(add(app_vtable, kAppStartActionSlot), "loading-start action slot") != kLoadingStartAction)
        throw std::runtime_error("LawnApp loading-start action changed before invocation.");
    using ButtonDepress = void(__thiscall*)(void*, int);
    reinterpret_cast<ButtonDepress>(kTitleButtonDepress)(reinterpret_cast<void*>(listener), id);
}

void invoke_quit_button(std::uintptr_t menu) {
    const auto listener = add(menu, kSelectorListener);
    if (read<std::uint32_t>(listener, "selector listener vtable") != kSelectorListenerVtable ||
        read<std::uint32_t>(add(kSelectorListenerVtable, 0x08), "selector callback slot") !=
            kSelectorButtonDepress)
        throw std::runtime_error("Selector callback changed before Quit was opened.");
    using ButtonDepress = void(__thiscall*)(void*, int);
    reinterpret_cast<ButtonDepress>(kSelectorButtonDepress)(reinterpret_cast<void*>(listener), kQuitButtonId);
}

void invoke_quit_dialog_result(std::uintptr_t app, int id) {
    if (read<std::uint32_t>(app, "LawnApp vtable") != kAppVtable ||
        read<std::uint32_t>(add(kAppVtable, 0x08), "LawnApp ButtonDepress slot") != kAppButtonDepress)
        throw std::runtime_error("LawnApp ButtonDepress changed before the Quit dialog action.");
    using ButtonDepress = void(__thiscall*)(void*, int);
    reinterpret_cast<ButtonDepress>(kAppButtonDepress)(reinterpret_cast<void*>(app), id);
}

void invoke_credits_escape(std::uintptr_t screen) {
    if (read<std::uint32_t>(screen, "CreditsScreen vtable") != kCreditsScreenVtable ||
        read<std::uint32_t>(add(kCreditsScreenVtable, kCreditsKeyDownSlot), "Credits KeyDown slot") != kCreditsKeyDown)
        throw std::runtime_error("Credits KeyDown callback changed before Escape.");
    using KeyDown = void(__thiscall*)(void*, int);
    reinterpret_cast<KeyDown>(kCreditsKeyDown)(reinterpret_cast<void*>(screen), kCreditsEscapeKey);
}

} // namespace

json snapshot(std::uintptr_t app) {
    try {
        validate_app(app);
        const auto scene = read<std::int32_t>(add(app, kAppSceneOffset), "scene");
        const auto screen = title_screen(app);
        const auto button = title_button(screen);
        json control = nullptr;
        if (button) control = json{{"id", "click_to_start"}, {"label", "CLICK TO START!"},
            {"address", button}, {"visible", true}, {"enabled", true}, {"button_id", kTitleButtonId}};
        json quit = nullptr;
        json quit_confirm = nullptr;
        json quit_cancel = nullptr;
        json skip_credits = nullptr;
        if (scene == kMenuScene && read<std::uint32_t>(add(app, kAppBoardOffset), "LawnApp Board") == 0 &&
            dialog_count(app) == 0) {
            const auto menu = selector(app);
            const auto quit_widget = quit_button(app, menu);
            quit = json{{"id", "quit_open"}, {"control_kind", "image_button"},
                {"address", quit_widget}, {"visible", true}, {"enabled", true},
                {"button_id", kQuitButtonId}};
        }
        if (scene == kMenuScene && read<std::uint32_t>(add(app, kAppBoardOffset), "LawnApp Board") == 0) {
            const auto dialog = get_dialog(app, 13);
            if (dialog) {
                const auto confirm = quit_dialog_button(app, dialog, true);
                const auto cancel = quit_dialog_button(app, dialog, false);
                quit_confirm = json{{"id", "quit_confirm"}, {"label", "Quit"},
                    {"address", confirm}, {"visible", true}, {"enabled", true},
                    {"button_id", kQuitConfirmAppButton}};
                quit_cancel = json{{"id", "quit_cancel"}, {"label", "Cancel"},
                    {"address", cancel}, {"visible", true}, {"enabled", true},
                    {"button_id", kQuitCancelAppButton}};
            }
        }
        if (scene == kCreditsScene) {
            const auto credits = credits_screen(app);
            skip_credits = json{{"id", "credits_skip"}, {"key", "Escape"},
                {"address", credits}, {"enabled", true}};
        }
        return json{{"ok", true}, {"scene", scene}, {"loading_screen", screen},
            {"controls", control}, {"quit_control", quit},
            {"quit_open_available", !quit.is_null()},
            {"quit_confirm_control", quit_confirm}, {"quit_cancel_control", quit_cancel},
            {"quit_confirm_available", !quit_confirm.is_null()},
            {"quit_cancel_available", !quit_cancel.is_null()},
            {"credits_skip_control", skip_credits},
            {"credits_skip_available", !skip_credits.is_null()},
            {"pending", g_pending.pending && g_pending.app == app},
            {"pending_action", g_pending.app != app || !g_pending.pending ? "" :
                (g_pending.quit_dialog_result ? (g_pending.button_id == kQuitConfirmAppButton ? "quit_confirm" : "quit_cancel") :
                    (g_pending.credits_skip ? "credits_skip" :
                        (g_pending.quit ? "quit_open" : "click_to_start")))},
            {"last_invoked", g_pending.app == app && g_pending.invoked},
            {"last_error", g_pending.app == app ? g_pending.error : std::string{}}};
    } catch (const std::exception& error) {
        return json{{"ok", false}, {"error", error.what()},
            {"pending", g_pending.pending && g_pending.app == app},
            {"last_error", g_pending.app == app ? g_pending.error : std::string{}}};
    }
}

json action(std::uintptr_t app, const std::string& name, bool confirm) {
    try {
        validate_app(app);
        if (name == "quit_open") {
            const auto menu = selector(app);
            const auto button = quit_button(app, menu);
            if (g_pending.pending) throw std::runtime_error("A lifecycle action is already pending.");
            g_pending = PendingAction{app, menu, button, true, false, {}, kQuitButtonId, true};
            return json{{"accepted", true}, {"pending", true}, {"action", name},
                {"button_id", kQuitButtonId}, {"transition_pending", true}};
        }
        if (name == "quit_cancel" || name == "quit_confirm") {
            const bool confirming = name == "quit_confirm";
            if (confirming && !confirm)
                throw std::runtime_error("Quit confirmation requires an explicit confirm=true request.");
            const auto dialog = get_dialog(app, 13);
            if (!dialog) throw std::runtime_error("Quit confirmation dialog 13 is not open.");
            const auto button = quit_dialog_button(app, dialog, confirming);
            if (g_pending.pending) throw std::runtime_error("A lifecycle action is already pending.");
            const int app_button = confirming ? kQuitConfirmAppButton : kQuitCancelAppButton;
            g_pending = PendingAction{app, dialog, button, true, false, {}, app_button, true, true};
            return json{{"accepted", true}, {"pending", true}, {"action", name},
                {"button_id", app_button}, {"dialog_id", 13}, {"transition_pending", true}};
        }
        if (name == "credits_skip") {
            const auto screen = credits_screen(app);
            if (g_pending.pending) throw std::runtime_error("A lifecycle action is already pending.");
            g_pending = PendingAction{app, screen, screen, true, false, {}, kCreditsEscapeKey,
                false, false, true};
            return json{{"accepted", true}, {"pending", true}, {"action", name},
                {"key", "Escape"}, {"transition_pending", true}};
        }
        if (name != "click_to_start")
            throw std::runtime_error("Supported lifecycle actions are click_to_start, quit_open, quit_cancel, quit_confirm, and credits_skip.");
        const auto screen = title_screen(app);
        if (!screen) throw std::runtime_error("Click to Start is available only on the loading screen.");
        const auto button = title_button(screen);
        if (!button) throw std::runtime_error("Click-to-start widget is not present.");
        if (g_pending.pending) throw std::runtime_error("A lifecycle action is already pending.");
        g_pending = PendingAction{app, screen, button, true, false, {}, kTitleButtonId, false, false};
        return json{{"accepted", true}, {"pending", true}, {"action", name},
            {"button_id", kTitleButtonId}};
    } catch (const std::exception& error) {
        return json{{"accepted", false}, {"error", error.what()}};
    }
}

void tick(std::uintptr_t app) {
    if (!g_pending.pending || g_pending.app != app) return;
    const auto scheduled = g_pending;
    g_pending.pending = false;
    g_pending.invoked = false;
    g_pending.error.clear();
    try {
        validate_app(app);
        g_pending.invoked = true;
        if (scheduled.credits_skip) {
            if (credits_screen(app) != scheduled.screen)
                throw std::runtime_error("Credits screen changed before the scheduled Escape action.");
            invoke_credits_escape(scheduled.screen);
        } else if (scheduled.quit) {
            if (scheduled.quit_dialog_result) {
                const bool confirming = scheduled.button_id == kQuitConfirmAppButton;
                if (get_dialog(app, 13) != scheduled.screen ||
                    quit_dialog_button(app, scheduled.screen, confirming) != scheduled.button)
                    throw std::runtime_error("Quit dialog or its requested button changed before the action.");
                invoke_quit_dialog_result(app, scheduled.button_id);
            } else {
                if (selector(app) != scheduled.screen || quit_button(app, scheduled.screen) != scheduled.button)
                    throw std::runtime_error("Main menu or Quit button changed before the scheduled action.");
                invoke_quit_button(scheduled.screen);
            }
        } else {
            if (title_screen(app) != scheduled.screen || title_button(scheduled.screen) != scheduled.button)
                throw std::runtime_error("Loading screen changed before the scheduled action.");
            invoke_title_button(scheduled.screen, scheduled.button_id);
        }
    } catch (const std::exception& error) {
        g_pending.invoked = false;
        g_pending.error = error.what();
    }
}

} // namespace pvz::lifecycle_controls
