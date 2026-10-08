#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "options_controls.hpp"

namespace pvz::options_controls {
namespace {
using json = nlohmann::json;

constexpr std::uintptr_t kGlobalApp = 0x731c50;
constexpr std::uintptr_t kOptionsVtable = 0x6f2388;
constexpr std::uintptr_t kOptionsListenerVtable = 0x6f24b8;
constexpr std::uintptr_t kOptionsButtonDepress = 0x463620;
constexpr std::uintptr_t kAppButtonDepress = 0x458590;
constexpr std::uintptr_t kAlmanacVtable = 0x6eb4f8;
constexpr std::uintptr_t kAlmanacButtonVtable = 0x6ee668;
constexpr std::uintptr_t kAlmanacSetPage = 0x403ae0;
constexpr std::uintptr_t kAlmanacMouseDown = 0x4056a0;
constexpr std::uintptr_t kAlmanacSeedHitTest = 0x4052f0;
constexpr std::uintptr_t kGameButtonVtable = 0x6ee7a8;
constexpr std::uintptr_t kLawnButtonVtable = 0x6ee680;
constexpr std::uintptr_t kSliderVtable = 0x6fc8a4;
constexpr std::uintptr_t kCheckboxVtable = 0x6fcd8c;
constexpr std::uintptr_t kOptionsSliderListenerVtable = 0x6f24d4;
constexpr std::uintptr_t kOptionsCheckboxListenerVtable = 0x6f24d8;
constexpr std::uintptr_t kSliderSetValue = 0x5514e0;
constexpr std::uintptr_t kCheckboxSetChecked = 0x5597a0;
constexpr std::uintptr_t kOptionsSliderValueChanged = 0x463240;
constexpr std::uintptr_t kOptionsCheckboxChecked = 0x4632e0;
constexpr std::size_t kAppScene = 0x91c;
constexpr std::size_t kAppVtableGetDialog = 0x124;
constexpr std::size_t kDialogButtonListener = 0xa0;
constexpr std::size_t kButtonDepressSlot = 0x08;
constexpr std::size_t kDialogStoredId = 0x158;
// ButtonDepress is called with the ButtonListener subobject (dialog+0xA0),
// so its [this+0xF8] source check is complete-dialog offset +0x198.
constexpr std::size_t kOptionsFromSelector = 0x198;
constexpr std::size_t kWidgetVisible = 0x65;
constexpr std::size_t kWidgetDisabled = 0x66;
constexpr int kDialogNewOptions = 2;
constexpr int kDialogAlmanac = 3;
constexpr int kOptionsAlmanac = 0;
constexpr int kOptionsCredits = 1;
constexpr int kDialogCloseBase = 2000;
constexpr int kAlmanacIndexPage = 0;
constexpr int kAlmanacPlantsPage = 1;
constexpr int kAlmanacZombiesPage = 2;
constexpr std::size_t kAlmanacCloseButton = 0x194;
constexpr std::size_t kAlmanacIndexButton = 0x198;
constexpr std::size_t kAlmanacPlantsButton = 0x19c;
constexpr std::size_t kAlmanacZombiesButton = 0x1a0;
constexpr std::size_t kAlmanacPage = 0x1a8;
constexpr std::size_t kAlmanacSelectedPlant = 0x1bc;
constexpr std::size_t kAlmanacSelectedZombie = 0x1c0;
constexpr std::size_t kAlmanacPlantPreview = 0x1c4;
constexpr std::size_t kAlmanacZombiePreview = 0x1c8;
constexpr std::size_t kAlmanacDialogSize = 0x80c;
constexpr std::size_t kMusicSlider = 0x178;
constexpr std::size_t kSoundSlider = 0x17c;
constexpr std::size_t kFullscreenCheckbox = 0x180;
constexpr std::size_t kHardwareCheckbox = 0x184;
constexpr std::size_t kAlmanacButton = 0x188;
constexpr std::size_t kMainMenuButton = 0x18c;
constexpr std::size_t kRestartButton = 0x190;
constexpr std::size_t kResumeButton = 0x194;
constexpr std::size_t kButtonLabel = 0xa8;
constexpr std::size_t kButtonLabelLength = 0xb8;
constexpr std::size_t kButtonLabelCapacity = 0xbc;
constexpr int kSceneMainMenu = 1;

struct PendingAlmanac {
    std::uintptr_t app = 0;
    std::uintptr_t options = 0;
    bool pending = false;
    std::string last_error;
    bool last_open_invoked = false;
    bool credits = false;
};

PendingAlmanac g_pending;

std::uintptr_t add(std::uintptr_t pointer, std::size_t offset) {
    if (pointer < 0x10000 || offset > std::numeric_limits<std::uintptr_t>::max() - pointer)
        throw std::runtime_error("Invalid options-control address.");
    return pointer + offset;
}

bool readable(std::uintptr_t address, std::size_t size) {
    if (address < 0x10000 || size == 0 ||
        size > std::numeric_limits<std::uintptr_t>::max() - address) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info))) return false;
    const DWORD protection = info.Protect & 0xFF;
    const bool can_read = protection == PAGE_READONLY || protection == PAGE_READWRITE ||
        protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
        protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
    const auto begin = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    if (info.RegionSize > std::numeric_limits<std::uintptr_t>::max() - begin) return false;
    return info.State == MEM_COMMIT && !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
        can_read && address >= begin && address + size <= begin + info.RegionSize;
}

template<class T> T read(std::uintptr_t address, const char* field) {
    if (!readable(address, sizeof(T)))
        throw std::runtime_error(std::string("Unreadable options-control field: ") + field);
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
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

std::uintptr_t options_dialog(std::uintptr_t app) {
    const auto dialog = get_dialog(app, kDialogNewOptions);
    if (!dialog) return 0;
    if (!readable(dialog, kOptionsFromSelector + sizeof(std::uint8_t)))
        throw std::runtime_error("Options dialog is not readable.");
    if (read<std::uint32_t>(dialog, "options dialog vtable") != kOptionsVtable)
        throw std::runtime_error("Options dialog vtable does not match the verified 1096 class.");
    const auto listener = add(dialog, kDialogButtonListener);
    if (read<std::uint32_t>(listener, "options listener vtable") != kOptionsListenerVtable)
        throw std::runtime_error("Options listener vtable does not match the verified 1096 class.");
    const auto callback = read<std::uint32_t>(add(kOptionsListenerVtable, kButtonDepressSlot),
        "options ButtonDepress slot");
    if (callback != kOptionsButtonDepress)
        throw std::runtime_error("Options ButtonDepress callback does not match the verified target.");
    return dialog;
}

std::uintptr_t control(std::uintptr_t dialog, std::size_t offset, const char* name) {
    const auto pointer = read<std::uint32_t>(add(dialog, offset), name);
    if (pointer && !readable(pointer, kWidgetDisabled + 1))
        throw std::runtime_error(std::string("Unreadable options control: ") + name);
    return pointer;
}

json describe_control(std::uintptr_t dialog, std::size_t offset, const char* name,
                      std::uintptr_t expected_vtable = 0, std::size_t value_offset = 0,
                      bool boolean_value = false) {
    const auto pointer = control(dialog, offset, name);
    if (!pointer) return json{{"address", 0}, {"present", false}, {"visible", false},
        {"disabled", true}, {"enabled", false}};
    const auto vtable = read<std::uint32_t>(pointer, "control vtable");
    if (expected_vtable && vtable != expected_vtable)
        throw std::runtime_error(std::string(name) + " does not match its verified control class.");
    const auto visible = read<std::uint8_t>(add(pointer, kWidgetVisible), "control visibility");
    const auto disabled = read<std::uint8_t>(add(pointer, kWidgetDisabled), "control disabled state");
    if (visible > 1 || disabled > 1)
        throw std::runtime_error(std::string("Invalid visible/disabled state for ") + name);
    json result{{"address", pointer}, {"present", true}, {"vtable", vtable},
        {"visible", visible != 0}, {"disabled", disabled != 0},
        {"enabled", visible != 0 && disabled == 0}};
    if (value_offset) {
        if (boolean_value) {
            const auto checked = read<std::uint8_t>(add(pointer, value_offset), "checkbox value");
            if (checked > 1) throw std::runtime_error(std::string("Invalid checkbox value for ") + name);
            result["value"] = checked != 0;
        } else {
            const auto current = read<double>(add(pointer, value_offset), "slider value");
            if (!std::isfinite(current) || current < 0.0 || current > 1.0)
                throw std::runtime_error(std::string("Invalid slider value for ") + name);
            result["value"] = current;
        }
    }
    return result;
}

std::string button_label(std::uintptr_t button) {
    if (!button || !readable(button, kButtonLabelCapacity + sizeof(std::uint32_t)))
        throw std::runtime_error("Options button label object is unreadable.");
    const auto length = read<std::uint32_t>(add(button, kButtonLabelLength), "button label length");
    const auto capacity = read<std::uint32_t>(add(button, kButtonLabelCapacity), "button label capacity");
    if (length > 512 || capacity < length || capacity > 4096)
        throw std::runtime_error("Options button label bounds are invalid.");
    if (!length) return {};
    const auto storage = capacity < 16 ? add(button, kButtonLabel) :
        static_cast<std::uintptr_t>(read<std::uint32_t>(add(button, kButtonLabel), "button label storage"));
    if (!storage || !readable(storage, length))
        throw std::runtime_error("Options button label storage is unreadable.");
    std::string value(length, '\0');
    std::memcpy(value.data(), reinterpret_cast<const void*>(storage), length);
    if (value.find('\0') != std::string::npos)
        throw std::runtime_error("Options button label contains an invalid terminator.");
    return value;
}

bool from_selector(std::uintptr_t dialog) {
    const auto flag = read<std::uint8_t>(add(dialog, kOptionsFromSelector), "options source flag");
    if (flag > 1) throw std::runtime_error("Options source flag is invalid.");
    return flag != 0;
}

void invoke_options_button(std::uintptr_t dialog, int button_id) {
    const auto listener = add(dialog, kDialogButtonListener);
    const auto vtable = read<std::uint32_t>(listener, "options listener vtable");
    const auto callback = read<std::uint32_t>(add(vtable, kButtonDepressSlot), "options callback slot");
    if (vtable != kOptionsListenerVtable || callback != kOptionsButtonDepress)
        throw std::runtime_error("Options ButtonDepress does not match the verified 1096 callback.");
    using ButtonDepress = void(__thiscall*)(void*, int);
    reinterpret_cast<ButtonDepress>(callback)(reinterpret_cast<void*>(listener), button_id);
}

void invoke_app_button(std::uintptr_t app, int button_id) {
    const auto vtable = read<std::uint32_t>(app, "app vtable");
    const auto callback = read<std::uint32_t>(add(vtable, kButtonDepressSlot), "app callback slot");
    if (callback != kAppButtonDepress)
        throw std::runtime_error("LawnApp ButtonDepress does not match the verified 1096 callback.");
    using ButtonDepress = void(__thiscall*)(void*, int);
    reinterpret_cast<ButtonDepress>(callback)(reinterpret_cast<void*>(app), button_id);
}

void set_slider(std::uintptr_t dialog, std::size_t offset, int id, double value) {
    const auto slider = control(dialog, offset, "options slider");
    if (!slider || read<std::uint32_t>(slider, "slider vtable") != kSliderVtable)
        throw std::runtime_error("Slider class does not match the verified target.");
    const auto vtable = read<std::uint32_t>(slider, "slider vtable");
    if (read<std::uint32_t>(add(vtable, 0x118), "Slider SetValue slot") != kSliderSetValue)
        throw std::runtime_error("Slider SetValue slot does not match the verified target.");
    const auto listener = read<std::uint32_t>(add(slider, 0xa0), "slider listener");
    if (!listener || read<std::uint32_t>(listener, "slider listener vtable") != kOptionsSliderListenerVtable ||
        read<std::uint32_t>(kOptionsSliderListenerVtable, "slider listener callback") != kOptionsSliderValueChanged)
        throw std::runtime_error("Slider listener does not match NewOptionsDialog::SliderVal.");
    if (read<std::uint32_t>(add(slider, 0xb0), "slider listener ID") != static_cast<std::uint32_t>(id))
        throw std::runtime_error("Slider listener ID does not match its NewOptionsDialog control.");
    using SetValue = void(__thiscall*)(void*, double);
    reinterpret_cast<SetValue>(kSliderSetValue)(reinterpret_cast<void*>(slider), value);
    // SetValue updates the thumb/value but is deliberately silent. Deliver the
    // same value through the verified NewOptionsDialog listener ABI afterward.
    using SliderVal = void(__thiscall*)(void*, int, double);
    reinterpret_cast<SliderVal>(kOptionsSliderValueChanged)(reinterpret_cast<void*>(listener), id, value);
}

void set_checkbox(std::uintptr_t dialog, std::size_t offset, int id, bool value) {
    const auto checkbox = control(dialog, offset, "options checkbox");
    if (!checkbox || read<std::uint32_t>(checkbox, "checkbox vtable") != kCheckboxVtable)
        throw std::runtime_error("Checkbox class does not match the verified target.");
    const auto listener = read<std::uint32_t>(add(checkbox, 0xa0), "checkbox listener");
    if (!listener || read<std::uint32_t>(listener, "checkbox listener vtable") != kOptionsCheckboxListenerVtable ||
        read<std::uint32_t>(kOptionsCheckboxListenerVtable, "checkbox listener callback") != kOptionsCheckboxChecked)
        throw std::runtime_error("Checkbox listener does not match NewOptionsDialog::CheckboxChecked.");
    if (read<std::uint32_t>(add(checkbox, 0xa4), "checkbox listener ID") != static_cast<std::uint32_t>(id))
        throw std::runtime_error("Checkbox listener ID does not match its NewOptionsDialog control.");
    using SetChecked = void(__thiscall*)(void*, std::uint8_t, std::uint8_t);
    reinterpret_cast<SetChecked>(kCheckboxSetChecked)(reinterpret_cast<void*>(checkbox), value ? 1 : 0, 1);
}

std::uintptr_t almanac_dialog(std::uintptr_t app) {
    const auto dialog = get_dialog(app, kDialogAlmanac);
    if (!dialog) return 0;
    if (!readable(dialog, kAlmanacDialogSize) ||
        read<std::uint32_t>(dialog, "Almanac vtable") != kAlmanacVtable ||
        read<std::int32_t>(add(dialog, kDialogStoredId), "Almanac ID") != kDialogAlmanac)
        throw std::runtime_error("Almanac dialog does not match the verified 1096 class/ID.");
    return dialog;
}

json describe_almanac_button(std::uintptr_t dialog, std::size_t offset, const char* name) {
    const auto button = control(dialog, offset, name);
    if (!button) return json{{"address", 0}, {"present", false}, {"visible", false},
        {"drawn", false}, {"disabled", true}, {"animating", false}, {"enabled", false}};
    if (read<std::uint32_t>(button, "Almanac button vtable") != kAlmanacButtonVtable)
        throw std::runtime_error(std::string(name) + " does not match the verified Almanac button class.");
    if (!readable(button, 0x106))
        throw std::runtime_error(std::string("Unreadable Almanac button state: ") + name);
    const auto disabled = read<std::uint8_t>(add(button, 0x22), "Almanac button disabled state");
    const auto animating = read<std::uint8_t>(add(button, 0x105), "Almanac button animation state");
    if (disabled > 1 || animating > 1)
        throw std::runtime_error("Invalid disabled/animation state for Almanac button.");
    // GameButton +0x20 is only the current hover bit. The verified Almanac
    // draw/hit paths use +0x22 for disabled and +0x105 for animating/hidden
    // page tabs; never treat hover as visibility.
    const bool drawn = animating == 0;
    json result{{"address", button}, {"present", true}, {"vtable", kAlmanacButtonVtable},
        {"visible", drawn}, {"drawn", drawn}, {"disabled", disabled != 0},
        {"animating", animating != 0}, {"enabled", drawn && disabled == 0}};
    return result;
}

void call_set_almanac_page(std::uintptr_t dialog, int page) {
#if defined(_MSC_VER) && defined(_M_IX86)
    // 0x403AE0 is a custom register ABI: ECX=this, EAX=page, no stack args.
    __asm {
        mov ecx, dialog
        mov eax, page
        mov edx, 0403AE0h
        call edx
    }
#else
    (void)dialog;
    (void)page;
    throw std::runtime_error("Almanac page callback requires the verified MSVC x86 register ABI.");
#endif
}

int almanac_seed_hit_test(std::uintptr_t dialog, int x, int y) {
#if defined(_MSC_VER) && defined(_M_IX86)
    int result = -1;
    // 0x4052F0 is custom: EBX=this, stack (x,y), callee pops 8 bytes.
    __asm {
        push y
        push x
        mov ebx, dialog
        mov eax, 04052F0h
        call eax
        mov result, eax
    }
    return result;
#else
    (void)dialog;
    (void)x;
    (void)y;
    throw std::runtime_error("Almanac hit testing requires the verified MSVC x86 register ABI.");
#endif
}

void set_almanac_page(std::uintptr_t app, std::uintptr_t dialog, int page) {
    if (page < kAlmanacIndexPage || page > kAlmanacZombiesPage)
        throw std::runtime_error("Almanac page must be index, plants, or zombies.");
    if (almanac_dialog(app) != dialog)
        throw std::runtime_error("Almanac dialog changed before page action.");
    call_set_almanac_page(dialog, page);
    if (almanac_dialog(app) != dialog ||
        read<std::int32_t>(add(dialog, kAlmanacPage), "Almanac page after SetPage") != page)
        throw std::runtime_error("Almanac page action did not reach the requested page.");
}

std::pair<int, int> entry_center(const std::string& kind, int index) {
    if (kind == "plant") {
        if (index < 0 || index >= 49) throw std::runtime_error("Plant index must be 0..48.");
        if (index == 48) return {37, 46};
        return {26 + (index % 8) * 52 + 17, 92 + (index / 8) * 78 + 23};
    }
    if (kind == "zombie") {
        if (index < 0 || index >= 34) throw std::runtime_error("Zombie index must be 0..33.");
        if (index == 25) return {209, 509};
        return {22 + (index % 5) * 85 + 30, 86 + (index / 5) * 80 + 30};
    }
    throw std::runtime_error("Entry kind must be plant or zombie.");
}

} // namespace

json snapshot(std::uintptr_t app) {
    try {
        validate_app(app);
        const int scene = read<std::int32_t>(add(app, kAppScene), "scene");
        const auto dialog = options_dialog(app);
        json controls = json::object();
        if (dialog) {
            const bool main_menu_context = from_selector(dialog);
            auto menu_button = describe_control(dialog, kMainMenuButton,
                main_menu_context ? "Credits" : "Main Menu", kGameButtonVtable);
            if (menu_button.value("present", false)) {
                menu_button["label"] = button_label(control(dialog, kMainMenuButton, "options menu button"));
                menu_button["action"] = main_menu_context ? "credits" : "main_menu";
            }
            controls = {
                {"almanac", describe_control(dialog, kAlmanacButton, "Almanac", kGameButtonVtable)},
                {"menu_button", menu_button},
                {"main_menu", main_menu_context ? json(nullptr) : menu_button},
                {"credits", main_menu_context ? menu_button : json(nullptr)},
                {"restart", describe_control(dialog, kRestartButton, "Restart", kGameButtonVtable)},
                {"resume", describe_control(dialog, kResumeButton, "Back to Game", kLawnButtonVtable)},
                {"music", describe_control(dialog, kMusicSlider, "Music slider", kSliderVtable, 0xa8)},
                {"sound", describe_control(dialog, kSoundSlider, "Sound slider", kSliderVtable, 0xa8)},
                {"fullscreen", describe_control(dialog, kFullscreenCheckbox, "Fullscreen checkbox", kCheckboxVtable, 0xa8, true)},
                {"hardware_acceleration", describe_control(dialog, kHardwareCheckbox, "Hardware checkbox", kCheckboxVtable, 0xa8, true)}
            };
        }
        const auto almanac = almanac_dialog(app);
        json almanac_state = nullptr;
        if (almanac) {
            const int page = read<std::int32_t>(add(almanac, kAlmanacPage), "Almanac page");
            const auto plant = read<std::int32_t>(add(almanac, kAlmanacSelectedPlant), "selected plant");
            const auto zombie = read<std::int32_t>(add(almanac, kAlmanacSelectedZombie), "selected zombie");
            almanac_state = json{
                {"page", page}, {"page_name", page == 0 ? "index" : page == 1 ? "plants" : page == 2 ? "zombies" : "unknown"},
                {"selected_plant", plant}, {"selected_zombie", zombie},
                {"plant_preview_present", read<std::uint32_t>(add(almanac, kAlmanacPlantPreview), "plant preview") != 0},
                {"zombie_preview_present", read<std::uint32_t>(add(almanac, kAlmanacZombiePreview), "zombie preview") != 0},
                {"controls", {
                    {"close", describe_almanac_button(almanac, kAlmanacCloseButton, "Almanac close")},
                    {"index", describe_almanac_button(almanac, kAlmanacIndexButton, "Almanac index")},
                    {"plants", describe_almanac_button(almanac, kAlmanacPlantsButton, "Almanac plants")},
                    {"zombies", describe_almanac_button(almanac, kAlmanacZombiesButton, "Almanac zombies")}
                }}
            };
        }
        return json{{"ok", true}, {"scene", scene}, {"options_dialog", dialog},
            {"options_from_selector", dialog ? from_selector(dialog) : false},
            {"almanac_dialog", almanac}, {"controls", controls},
            {"almanac", almanac_state},
            {"almanac_open_pending", g_pending.pending && g_pending.app == app && !g_pending.credits},
            {"credits_pending", g_pending.pending && g_pending.app == app && g_pending.credits},
            {"almanac_last_open_invoked", g_pending.app == app && g_pending.last_open_invoked},
            {"last_error", g_pending.app == app ? g_pending.last_error : std::string{}}};
    } catch (const std::exception& error) {
        return json{{"ok", false}, {"error", error.what()},
            {"almanac_open_pending", g_pending.pending && g_pending.app == app && !g_pending.credits},
            {"last_error", g_pending.app == app ? g_pending.last_error : std::string{}}};
    }
}

json press(std::uintptr_t app, const std::string& name) {
    try {
        validate_app(app);
        if (name == "almanac") {
            const auto dialog = options_dialog(app);
            if (!dialog) throw std::runtime_error("Options dialog is not open.");
            const auto button = describe_control(dialog, kAlmanacButton, "Almanac", kGameButtonVtable);
            if (!button.at("enabled").get<bool>())
                throw std::runtime_error("Almanac button is hidden or disabled.");
            if (get_dialog(app, kDialogAlmanac))
                throw std::runtime_error("Almanac dialog is already open.");
            if (g_pending.pending)
                throw std::runtime_error("An Almanac open action is already pending.");
            g_pending = PendingAlmanac{app, dialog, true, {}, false};
            return json{{"accepted", true}, {"transition_pending", true},
                {"action", "almanac"}, {"button_id", kOptionsAlmanac}};
        }
        if (name == "credits") {
            const auto dialog = options_dialog(app);
            if (!dialog) throw std::runtime_error("Main-menu options dialog is not open.");
            if (read<std::int32_t>(add(app, kAppScene), "scene") != kSceneMainMenu || !from_selector(dialog))
                throw std::runtime_error("Credits is available only from the main-menu options dialog.");
            const auto button = describe_control(dialog, kMainMenuButton, "Credits", kGameButtonVtable);
            if (!button.at("enabled").get<bool>() ||
                button_label(control(dialog, kMainMenuButton, "Credits button")) != "Credits")
                throw std::runtime_error("The main-menu Credits button is absent, hidden, disabled, or mislabeled.");
            if (g_pending.pending) throw std::runtime_error("An options action is already pending.");
            g_pending = PendingAlmanac{app, dialog, true, {}, false, true};
            return json{{"accepted", true}, {"pending", true}, {"action", "credits"},
                {"button_id", kOptionsCredits}};
        }
        if (name == "almanac_close") {
            const auto almanac = almanac_dialog(app);
            if (!almanac) throw std::runtime_error("Almanac dialog is not open.");
            invoke_app_button(app, kDialogCloseBase + kDialogAlmanac);
            return json{{"accepted", true}, {"transition_pending", false},
                {"action", "almanac_close"}, {"dialog_id", kDialogAlmanac}};
        }
        int page = -1;
        std::size_t button_offset = 0;
        if (name == "almanac_index") { page = kAlmanacIndexPage; button_offset = kAlmanacIndexButton; }
        else if (name == "almanac_plants") { page = kAlmanacPlantsPage; button_offset = kAlmanacPlantsButton; }
        else if (name == "almanac_zombies") { page = kAlmanacZombiesPage; button_offset = kAlmanacZombiesButton; }
        if (page >= 0) {
            const auto dialog = almanac_dialog(app);
            if (!dialog) throw std::runtime_error("Almanac dialog is not open.");
            if (read<std::int32_t>(add(dialog, kAlmanacPage), "Almanac current page") == page)
                return json{{"accepted", true}, {"action", name}, {"page", page}, {"unchanged", true}};
            const char* label = page == 0 ? "index" : page == 1 ? "plants" : "zombies";
            const auto button = describe_almanac_button(dialog, button_offset, label);
            if (!button.at("enabled").get<bool>())
                throw std::runtime_error("Almanac page button is hidden or disabled.");
            set_almanac_page(app, dialog, page);
            return json{{"accepted", true}, {"action", name}, {"page", page}};
        }
        throw std::runtime_error("Unsupported options action. Use almanac, almanac_close, or an almanac page action.");
    } catch (const std::exception& error) {
        return json{{"accepted", false}, {"error", error.what()}};
    }
}

json select_entry(std::uintptr_t app, const std::string& kind, int index) {
    try {
        validate_app(app);
        const auto dialog = almanac_dialog(app);
        if (!dialog) throw std::runtime_error("Almanac dialog is not open.");
        const auto [x, y] = entry_center(kind, index);
        const int page = kind == "plant" ? kAlmanacPlantsPage : kAlmanacZombiesPage;
        if (read<std::int32_t>(add(dialog, kAlmanacPage), "Almanac page") != page)
            set_almanac_page(app, dialog, page);
        if (almanac_dialog(app) != dialog)
            throw std::runtime_error("Almanac dialog changed before entry selection.");
        if (kind == "plant") {
            const auto hit = almanac_seed_hit_test(dialog, x, y);
            if (hit != index) throw std::runtime_error("Plant entry is hidden, locked, or outside the verified hit region.");
        }
        using MouseDown = void(__thiscall*)(void*, int, int, int);
        reinterpret_cast<MouseDown>(kAlmanacMouseDown)(reinterpret_cast<void*>(dialog), x, y, 1);
        if (almanac_dialog(app) != dialog)
            throw std::runtime_error("Almanac dialog changed during entry selection.");
        const auto selected = read<std::int32_t>(add(dialog,
            kind == "plant" ? kAlmanacSelectedPlant : kAlmanacSelectedZombie), "selected Almanac entry");
        if (selected != index) throw std::runtime_error("Entry was not selected; it may be locked or unavailable.");
        return json{{"accepted", true}, {"kind", kind}, {"index", index},
            {"selected", selected}, {"page", page}, {"coordinates", {x, y}},
            {"preview_present", read<std::uint32_t>(add(dialog,
                kind == "plant" ? kAlmanacPlantPreview : kAlmanacZombiePreview), "entry preview") != 0}};
    } catch (const std::exception& error) {
        return json{{"accepted", false}, {"error", error.what()}};
    }
}

json set(std::uintptr_t app, const std::string& name, const json& value) {
    try {
        validate_app(app);
        const auto dialog = options_dialog(app);
        if (!dialog) throw std::runtime_error("Options dialog is not open.");
        std::size_t offset = 0;
        bool boolean = false;
        if (name == "music") offset = kMusicSlider;
        else if (name == "sound") offset = kSoundSlider;
        else if (name == "fullscreen") { offset = kFullscreenCheckbox; boolean = true; }
        else if (name == "hardware_acceleration") { offset = kHardwareCheckbox; boolean = true; }
        else throw std::runtime_error("Unknown options setting.");

        const auto expected_vtable = boolean ? kCheckboxVtable : kSliderVtable;
        const auto item = describe_control(dialog, offset, name.c_str(), expected_vtable);
        if (!item.at("present").get<bool>()) throw std::runtime_error("Options setting control is absent.");
        if (!item.at("visible").get<bool>() || item.at("disabled").get<bool>())
            throw std::runtime_error("Options setting control is hidden or disabled.");
        int id = 0;
        if (name == "music") id = 4;
        else if (name == "sound") id = 5;
        else if (name == "fullscreen") id = 6;
        else id = 7;
        if (boolean) {
            if (!value.is_boolean()) throw std::runtime_error("Checkbox value must be a boolean.");
            set_checkbox(dialog, offset, id, value.get<bool>());
        } else {
            if (!value.is_number()) throw std::runtime_error("Slider value must be a number from 0 to 1.");
            const double number = value.get<double>();
            if (!std::isfinite(number) || number < 0.0 || number > 1.0)
                throw std::runtime_error("Slider value must be from 0 to 1.");
            set_slider(dialog, offset, id, number);
        }
        return json{{"accepted", true}, {"supported", true}, {"control", name},
            {"value", value}, {"handler", boolean ? kOptionsCheckboxChecked : kOptionsSliderValueChanged}};
    } catch (const std::exception& error) {
        return json{{"accepted", false}, {"error", error.what()}};
    }
}

void tick(std::uintptr_t app) {
    if (!g_pending.pending || g_pending.app != app) return;
    const auto scheduled = g_pending.options;
    // Clear first so the ordinary callback's nested WaitForResult update loop
    // can process bridge commands without recursively scheduling another open.
    g_pending.pending = false;
    try {
        validate_app(app);
        if (options_dialog(app) != scheduled)
            throw std::runtime_error("Options dialog changed before the scheduled Almanac action ran.");
        if (g_pending.credits) {
            if (read<std::int32_t>(add(app, kAppScene), "scene") != kSceneMainMenu ||
                !from_selector(scheduled))
                throw std::runtime_error("Main-menu options context changed before the Credits action.");
            const auto button = describe_control(scheduled, kMainMenuButton, "Credits", kGameButtonVtable);
            if (!button.at("enabled").get<bool>() ||
                button_label(control(scheduled, kMainMenuButton, "Credits button")) != "Credits")
                throw std::runtime_error("Credits button changed before its scheduled action.");
        } else if (get_dialog(app, kDialogAlmanac)) {
            throw std::runtime_error("Almanac dialog opened before the scheduled action ran.");
        }
        g_pending.last_error.clear();
        g_pending.last_open_invoked = true;
        invoke_options_button(scheduled, g_pending.credits ? kOptionsCredits : kOptionsAlmanac);
    } catch (const std::exception& error) {
        g_pending.last_error = error.what();
        g_pending.last_open_invoked = false;
    }
}

} // namespace pvz::options_controls
