#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "zombatar.hpp"

namespace pvz::zombatar {
namespace {
using json = nlohmann::json;

constexpr std::uintptr_t kGlobalApp = 0x731c50;
constexpr std::uintptr_t kSelectorButtonDepress = 0x450ee0;
constexpr std::uintptr_t kSelectorVtable = 0x6eec88;
constexpr std::uintptr_t kSelectorListenerVtable = 0x6eeda0;
constexpr std::uintptr_t kZombatarVtable = 0x6fabe0;
constexpr std::uintptr_t kZombatarButtonDepress = 0x532f30;
constexpr std::uintptr_t kZombatarMouseHandler = 0x533300;
constexpr std::uintptr_t kZombatarItemHitTest = 0x533ce0;
constexpr std::uintptr_t kPaletteVtable = 0x6fa490;
constexpr std::uintptr_t kPaletteHitTest = 0x5315b0;
constexpr std::size_t kPaletteMouseDownSlot = 0xe0;
constexpr std::uintptr_t kPaletteMouseDown = 0x531480;
constexpr std::size_t kZombatarButtonDepressSlot = 0x118;
constexpr std::size_t kZombatarMouseHandlerSlot = 0xe0;
constexpr std::uintptr_t kEditorVtable = 0x6fad08;
constexpr std::uintptr_t kEditorListenerVtable = 0x6fae40;
constexpr std::uintptr_t kTermsDialogVtable = 0x6fae88;
constexpr std::uintptr_t kTermsListenerVtable = 0x6fafc0;
constexpr std::uintptr_t kTermsButtonDepress = 0x536900;
constexpr std::uintptr_t kGameButtonVtable = 0x6ee668;
constexpr std::uintptr_t kDeleteSelected = 0x535760;
constexpr std::uintptr_t kSaveProfile = 0x470430;
constexpr std::size_t kAppSelector = 0x870;
constexpr std::size_t kAppScene = 0x91c;
constexpr std::size_t kAppProfile = 0x94c;
constexpr std::size_t kAppGetDialog = 0x124;
constexpr std::size_t kAppGetDialogCount = 0x13c;
constexpr std::size_t kSelectorListener = 0xa0;
constexpr std::size_t kSelectorEditor = 0x168;
constexpr std::size_t kEditorListener = 0xa0;
constexpr std::size_t kEditorChild = 0x19c;
constexpr std::size_t kWidgetApp = 0xa0;
constexpr std::size_t kWidgetMode = 0xf5c;
constexpr std::size_t kWidgetSelectedSaved = 0xf64;
constexpr std::size_t kWidgetPage = 0xf0;
constexpr std::size_t kWidgetPageCount = 0xf4;
constexpr std::size_t kWidgetCategory = 0xf10;
constexpr std::size_t kWidgetCategoryItems = 0xf14;
constexpr std::size_t kWidgetItemCount = 0xfc;
constexpr std::size_t kWidgetVisibleItemCount = 0x100;
constexpr std::size_t kWidgetSelectedItems = 0xf38;
constexpr std::size_t kWidgetPageControls = 0xa4;
constexpr std::size_t kProfileCount = 0x4848;
constexpr std::size_t kProfileEntries = 0x484c;
constexpr std::size_t kAvatarBytes = 0x48;
constexpr std::uint32_t kMaxAvatars = 100;
constexpr int kSceneMenu = 1;
constexpr int kTermsDialogId = 0x34;
constexpr int kSaveNoticeDialogId = 0x37;
constexpr int kTermsAcceptId = 0x1f6;
constexpr int kTermsCancelId = 0x1f5;
constexpr int kSelectorZombatarId = 111;
constexpr int kButtonPageNext = 0x3e8;
constexpr int kButtonPagePrevious = 0x3e9;
constexpr int kButtonSaveNew = 0x3eb;
constexpr int kButtonBack = 0x3ef;

struct OwnedAvatar {
    std::uint32_t index = 0;
    std::array<std::uint8_t, kAvatarBytes> bytes{};
};
struct ProfileGuard {
    std::uint32_t baseline_count = 0;
    std::vector<std::array<std::uint8_t, kAvatarBytes>> baseline;
    std::vector<OwnedAvatar> created;
};
struct PendingDelete {
    std::uintptr_t app = 0;
    std::uintptr_t profile = 0;
    std::uintptr_t selector = 0;
    std::uintptr_t editor = 0;
    std::uintptr_t child = 0;
    std::uint32_t index = 0;
    std::array<std::uint8_t, kAvatarBytes> bytes{};
};
struct PendingSaveNew {
    std::uintptr_t app = 0;
    std::uintptr_t profile = 0;
    std::uintptr_t selector = 0;
    std::uintptr_t editor = 0;
    std::uintptr_t child = 0;
    std::uint32_t count_before = 0;
    std::uint64_t started_ms = 0;
};
struct ExpectedJpgExport {
    std::uint32_t avatar_index = 0;
    bool notice_seen = false;
};

std::map<std::uintptr_t, ProfileGuard> g_profiles;
PendingDelete g_pending_delete;
PendingSaveNew g_pending_save;
std::map<std::uintptr_t, ExpectedJpgExport> g_expected_jpg_exports;
std::string g_last_error;
json g_last_result = nullptr;

std::uintptr_t add(std::uintptr_t p, std::size_t n) {
    if (p < 0x10000 || n > std::numeric_limits<std::uintptr_t>::max() - p)
        throw std::runtime_error("Invalid Zombatar object address.");
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
        throw std::runtime_error(std::string("Unreadable Zombatar field: ") + field);
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
}

void validate_app(std::uintptr_t app) {
    if (app != read<std::uint32_t>(kGlobalApp, "global LawnApp") || !readable(app, 0x960))
        throw std::runtime_error("LawnApp pointer/layout does not match the active 1.2.0.1096 process.");
}

int dialog_count(std::uintptr_t app) {
    const auto vtable = read<std::uint32_t>(app, "LawnApp vtable");
    const auto method = read<std::uint32_t>(add(vtable, kAppGetDialogCount), "GetDialogCount slot");
    if (method < 0x401000 || method >= 0x6d0000)
        throw std::runtime_error("Unexpected LawnApp GetDialogCount target.");
    using Method = int(__thiscall*)(void*);
    return reinterpret_cast<Method>(method)(reinterpret_cast<void*>(app));
}

std::uintptr_t get_dialog(std::uintptr_t app, int id) {
    const auto vtable = read<std::uint32_t>(app, "LawnApp vtable");
    const auto method = read<std::uint32_t>(add(vtable, kAppGetDialog), "GetDialog slot");
    if (method < 0x401000 || method >= 0x6d0000)
        throw std::runtime_error("Unexpected LawnApp GetDialog target.");
    using Method = void*(__thiscall*)(void*, int);
    return reinterpret_cast<std::uintptr_t>(reinterpret_cast<Method>(method)(reinterpret_cast<void*>(app), id));
}

std::uintptr_t active_profile(std::uintptr_t app) {
    const auto profile = read<std::uint32_t>(add(app, kAppProfile), "active PlayerInfo");
    if (!profile || !readable(profile, kProfileEntries + kMaxAvatars * kAvatarBytes))
        throw std::runtime_error("No readable active profile with the verified Zombatar layout.");
    return profile;
}

std::uint32_t avatar_count(std::uintptr_t profile) {
    const auto count = read<std::uint32_t>(add(profile, kProfileCount), "saved Zombatar count");
    if (count > kMaxAvatars) throw std::runtime_error("Profile saved-Zombatar count exceeds the target limit.");
    return count;
}

std::array<std::uint8_t, kAvatarBytes> avatar_bytes(std::uintptr_t profile, std::uint32_t index) {
    if (index >= kMaxAvatars) throw std::runtime_error("Saved-Zombatar index is out of range.");
    std::array<std::uint8_t, kAvatarBytes> value{};
    const auto address = add(profile, kProfileEntries + index * kAvatarBytes);
    if (!readable(address, value.size())) throw std::runtime_error("Saved-Zombatar record is unreadable.");
    std::memcpy(value.data(), reinterpret_cast<const void*>(address), value.size());
    return value;
}

ProfileGuard& guard_for(std::uintptr_t profile) {
    auto found = g_profiles.find(profile);
    if (found != g_profiles.end()) return found->second;
    ProfileGuard guard;
    guard.baseline_count = avatar_count(profile);
    guard.baseline.reserve(guard.baseline_count);
    for (std::uint32_t i = 0; i < guard.baseline_count; ++i)
        guard.baseline.push_back(avatar_bytes(profile, i));
    return g_profiles.emplace(profile, std::move(guard)).first->second;
}

void verify_baseline(std::uintptr_t profile, const ProfileGuard& guard) {
    if (avatar_count(profile) < guard.baseline_count)
        throw std::runtime_error("Saved-Zombatar count fell below this session's protected baseline.");
    for (std::uint32_t i = 0; i < guard.baseline_count; ++i) {
        if (avatar_bytes(profile, i) != guard.baseline[i])
            throw std::runtime_error("A preexisting saved Zombatar changed; destructive operations are locked.");
    }
}

std::uintptr_t selector_for(std::uintptr_t app) {
    const auto selector = read<std::uint32_t>(add(app, kAppSelector), "GameSelector pointer");
    if (!selector || !readable(selector, 0x170)) throw std::runtime_error("GameSelector is not active/readable.");
    if (read<std::uint32_t>(selector, "GameSelector vtable") != kSelectorVtable)
        throw std::runtime_error("GameSelector primary vtable does not match the 1.2.0.1096 target.");
    const auto listener = add(selector, kSelectorListener);
    const auto listener_vtable = read<std::uint32_t>(listener, "GameSelector listener vtable");
    if (listener_vtable != kSelectorListenerVtable ||
        read<std::uint32_t>(add(listener_vtable, 8), "GameSelector ButtonDepress") != kSelectorButtonDepress)
        throw std::runtime_error("GameSelector listener callback does not match the verified target.");
    return selector;
}

bool editor_is_open(std::uintptr_t selector) {
    // Selector targets are distinct: Zombatar is (-1600, 0), whereas
    // achievements is (-800, -600) and the main-menu return is (-800, 0).
    return read<std::int32_t>(add(selector,0x160), "selector target x") == -1600 &&
        read<std::int32_t>(add(selector,0x164), "selector target y") == 0;
}

std::uintptr_t editor_for(std::uintptr_t selector) {
    const auto editor = read<std::uint32_t>(add(selector, kSelectorEditor), "Zombatar editor pointer");
    if (!editor || !readable(editor, kEditorChild + sizeof(std::uint32_t)) ||
        read<std::uint32_t>(editor, "Zombatar editor vtable") != kEditorVtable)
        throw std::runtime_error("Zombatar editor object is missing or has an unexpected vtable.");
    const auto editor_listener = add(editor, kEditorListener);
    if (read<std::uint32_t>(editor_listener, "Zombatar editor listener vtable") != kEditorListenerVtable)
        throw std::runtime_error("Zombatar editor listener does not match the verified class.");
    return editor;
}

std::uintptr_t child_for(std::uintptr_t editor) {
    const auto child = read<std::uint32_t>(add(editor, kEditorChild), "Zombatar child widget");
    if (!child || !readable(child, 0xf68) || read<std::uint32_t>(child, "Zombatar widget vtable") != kZombatarVtable)
        throw std::runtime_error("Zombatar child widget is missing or has an unexpected vtable.");
    const auto vtable = read<std::uint32_t>(child, "Zombatar widget vtable");
    if (read<std::uint32_t>(add(vtable,kZombatarButtonDepressSlot), "Zombatar ButtonDepress slot") != kZombatarButtonDepress ||
        read<std::uint32_t>(add(vtable,kZombatarMouseHandlerSlot), "Zombatar mouse handler slot") != kZombatarMouseHandler)
        throw std::runtime_error("Zombatar ButtonDepress target does not match the verified build.");
    return child;
}

void invoke_selector(std::uintptr_t selector, int id) {
    const auto listener = add(selector, kSelectorListener);
    using Callback = void(__thiscall*)(void*, int);
    reinterpret_cast<Callback>(kSelectorButtonDepress)(reinterpret_cast<void*>(listener), id);
}

void invoke_widget(std::uintptr_t child, int id) {
    using Callback = void(__thiscall*)(void*, int);
    reinterpret_cast<Callback>(kZombatarButtonDepress)(reinterpret_cast<void*>(child), id);
}

int item_hit_test(std::uintptr_t child, int x, int y) {
#if defined(_MSC_VER) && defined(_M_IX86)
    int result = -1;
    int point_x = x;
    int point_y = y;
    __asm {
        push esi
        mov esi, child
        lea eax, point_y
        push eax
        lea eax, point_x
        push eax
        mov eax, 0533ce0h
        call eax
        mov result, eax
        pop esi
    }
    return result;
#else
    (void)child; (void)x; (void)y;
    throw std::runtime_error("Zombatar item hit-test requires the verified MSVC x86 ABI.");
#endif
}

// The editor owns a separate Zombatar palette widget at child+0xEC. Its
// primary vtable is 0x6FA490; slot +0xE0 is the ordinary MouseDown method
// 0x531480, which calls this pure hit-test and stores the hit swatch.
int palette_hit_test(std::uintptr_t palette, int x, int y) {
#if defined(_MSC_VER) && defined(_M_IX86)
    int result = -1;
    int point_x = x;
    int point_y = y;
    __asm {
        push esi
        mov esi, palette
        lea eax, point_y
        push eax
        lea eax, point_x
        push eax
        mov eax, 05315b0h
        call eax
        mov result, eax
        pop esi
    }
    return result;
#else
    (void)palette; (void)x; (void)y;
    throw std::runtime_error("Zombatar palette hit-test requires the verified MSVC x86 ABI.");
#endif
}

std::uintptr_t palette_for(std::uintptr_t child) {
    const auto palette = read<std::uint32_t>(add(child,0xec),"Zombatar color-palette widget");
    if (!palette || !readable(palette,0xe0) || read<std::uint32_t>(palette,"palette widget vtable") != kPaletteVtable ||
        read<std::uint32_t>(add(kPaletteVtable,kPaletteMouseDownSlot),"palette MouseDown slot") != kPaletteMouseDown)
        throw std::runtime_error("Zombatar palette widget or its 1.2.0.1096 MouseDown ABI is not verified.");
    return palette;
}

std::pair<int,int> palette_hit_point(std::uintptr_t palette,int index) {
    const auto mode=read<std::int32_t>(add(palette,0xd8),"active palette mode");
    const auto count=read<std::int32_t>(add(palette,0xac),"palette swatch count");
    const auto first=read<std::int32_t>(add(palette,0xcc),"palette first swatch index");
    const int width=read<std::int32_t>(add(palette,0x48),"palette width");
    const int height=read<std::int32_t>(add(palette,0x4c),"palette height");
    if (mode<0 || mode>2 || first<0 || count<1 || count>256 || index<0 || index>=count ||
        width<2 || width>1024 || height<2 || height>512)
        throw std::runtime_error("The target palette is inactive or the requested swatch is outside its live bounds.");
    const int target_index=first+index;
    for(int y=0;y<height;++y)
        for(int x=0;x<width;++x)
            if(palette_hit_test(palette,x,y)==target_index) return {x,y};
    throw std::runtime_error("The target palette hit-test found no point for the requested swatch.");
}

void invoke_palette_mouse_down(std::uintptr_t palette,int x,int y) {
    const auto vtable=read<std::uint32_t>(palette,"palette widget vtable");
    const auto method=read<std::uint32_t>(add(vtable,kPaletteMouseDownSlot),"palette MouseDown slot");
    if(vtable!=kPaletteVtable || method!=kPaletteMouseDown)
        throw std::runtime_error("Zombatar palette MouseDown ABI does not match target 1.2.0.1096.");
    // Target function has ret 0xC: three stack arguments. It consumes x/y
    // and ignores the third value; preserve the exact cleanup ABI.
    using MouseDown=void(__thiscall*)(void*,int,int,int);
    reinterpret_cast<MouseDown>(method)(reinterpret_cast<void*>(palette),x,y,0);
}

std::pair<int,int> palette_screen_point(std::uintptr_t palette,int local_x,int local_y) {
    const auto owner=read<std::uint32_t>(add(palette,0x24),"palette coordinate owner");
    if(!owner || !readable(owner,0x48)) throw std::runtime_error("Palette coordinate owner is unreadable.");
    const int x=read<std::int32_t>(add(owner,0x40),"palette owner screen x")+local_x;
    const int y=read<std::int32_t>(add(owner,0x44),"palette owner screen y")+local_y;
    if(x<0 || y<0 || x>4096 || y>4096) throw std::runtime_error("Palette screen coordinate is outside the target window bounds.");
    return {x,y};
}

std::pair<int,int> item_center(std::uintptr_t child, int local_index, int visible_count) {
    if (local_index < 0 || local_index >= visible_count || visible_count < 1 || visible_count > 18)
        throw std::runtime_error("The requested item is outside the current Zombatar grid.");
    constexpr std::size_t first_record = 0x104;
    constexpr std::size_t record_size = 0x1c;
    const auto record = add(child, first_record + static_cast<std::size_t>(local_index) * record_size);
    const int x = read<std::int32_t>(add(record,4),"Zombatar item cell x");
    const int y = read<std::int32_t>(add(record,8),"Zombatar item cell y");
    // The generated 0x1C-byte cell record stores rendered width/height at
    // +0x14/+0x18. This also handles a single-row page, where no neighboring
    // row exists from which to infer a vertical step.
    const int width = read<std::int32_t>(add(record,0x14),"Zombatar item cell width");
    const int height = read<std::int32_t>(add(record,0x18),"Zombatar item cell height");
    if (x < 0 || y < 0 || width < 2 || width > 256 || height < 2 || height > 256)
        throw std::runtime_error("The target did not provide a valid Zombatar item cell rectangle.");
    return {x + width/2, y + height/2};
}

std::pair<int,int> find_item_hit_point(std::uintptr_t child, int item, int local_index, int visible_count) {
    const auto record = add(child,0x104+static_cast<std::size_t>(local_index)*0x1c);
    const int x = read<std::int32_t>(add(record,4),"Zombatar item cell x");
    const int y = read<std::int32_t>(add(record,8),"Zombatar item cell y");
    const int width = read<std::int32_t>(add(record,0x14),"Zombatar item cell width");
    const int height = read<std::int32_t>(add(record,0x18),"Zombatar item cell height");
    if (width < 2 || width > 256 || height < 2 || height > 256)
        throw std::runtime_error("The target did not provide a bounded Zombatar hit-test rectangle.");
    const auto center = item_center(child,local_index,visible_count);
    if (item_hit_test(child,center.first,center.second) == item) return center;
    // Hit-test is a read-only target routine: probe only pixels inside the
    // live item rectangle, and dispatch MouseDown only after it resolves the
    // exact requested item index.
    for (int py=y;py<y+height;++py) {
        for (int px=x;px<x+width;++px) {
            if (px==center.first && py==center.second) continue;
            if (item_hit_test(child,px,py)==item) return {px,py};
        }
    }
    throw std::runtime_error("The target hit-test found no point inside the requested visible item rectangle.");
}

void invoke_item_mouse_down(std::uintptr_t child, int x, int y) {
    const auto vtable = read<std::uint32_t>(child,"Zombatar widget vtable for item input");
    const auto method = read<std::uint32_t>(add(vtable,kZombatarMouseHandlerSlot),"Zombatar MouseDown slot");
    if (vtable != kZombatarVtable || method != kZombatarMouseHandler)
        throw std::runtime_error("Zombatar MouseDown ABI does not match target 1.2.0.1096.");
    // 0x5336FC ends with ret 0xC: x, y, and clickCount are all stack args.
    using MouseDown = void(__thiscall*)(void*,int,int,int);
    reinterpret_cast<MouseDown>(method)(reinterpret_cast<void*>(child),x,y,1);
}

void invoke_terms_accept(std::uintptr_t app, std::uintptr_t dialog) {
    if (read<std::uint32_t>(dialog, "terms dialog vtable") != kTermsDialogVtable)
        throw std::runtime_error("The open dialog is not the verified Zombatar terms dialog.");
    if (read<std::uint32_t>(add(dialog, 0xa0), "terms listener vtable") != kTermsListenerVtable ||
        read<std::uint32_t>(add(kTermsListenerVtable, 8), "terms callback") != kTermsButtonDepress)
        throw std::runtime_error("Terms dialog listener does not match the verified accept callback.");
    if (read<std::uint32_t>(add(dialog, 0x194), "terms owner app") != app)
        throw std::runtime_error("Terms dialog belongs to another LawnApp instance.");
    const auto accept_button = read<std::uint32_t>(add(dialog,0x1a0),"terms accept button");
    if (!accept_button || !readable(accept_button,0x106) ||
        read<std::uint32_t>(accept_button,"terms accept button vtable") != kGameButtonVtable ||
        read<std::uint32_t>(add(accept_button,0x84),"terms accept button ID") != kTermsAcceptId ||
        read<std::uint8_t>(add(accept_button,0x22),"terms accept button disabled") != 0 ||
        read<std::uint8_t>(add(accept_button,0x105),"terms accept button animating") != 0)
        throw std::runtime_error("The terms dialog's ordinary Accept button is not enabled and idle.");
    using Callback = void(__thiscall*)(void*, int);
    reinterpret_cast<Callback>(kTermsButtonDepress)(reinterpret_cast<void*>(add(dialog, 0xa0)), kTermsAcceptId);
}

void invoke_delete_selected(std::uintptr_t child) {
#if defined(_MSC_VER) && defined(_M_IX86)
    // 0x535760's target ABI takes its child widget in EDI. It calls the
    // profile's normal 0x470AC0 delete routine and refreshes editor state.
    __asm {
        push edi
        mov edi, child
        mov eax, 0535760h
        call eax
        pop edi
    }
#else
    (void)child;
    throw std::runtime_error("Deleting a test-created avatar requires the verified MSVC x86 ABI.");
#endif
}

json controls(std::uintptr_t child) {
    const std::array<std::pair<const char*, std::size_t>, 19> fields{{
        {"field_0xa4",0xa4},{"field_0xa8",0xa8},{"field_0xac",0xac},{"field_0xb0",0xb0},
        {"field_0xb4",0xb4},{"field_0xb8",0xb8},{"field_0xbc",0xbc},{"field_0xc0",0xc0},
        {"field_0xc4",0xc4},{"field_0xc8",0xc8},{"field_0xcc",0xcc},{"field_0xd0",0xd0},
        {"field_0xd4",0xd4},{"field_0xd8",0xd8},{"field_0xdc",0xdc},{"field_0xe0",0xe0},
        {"field_0xe4",0xe4},{"field_0xe8",0xe8},{"field_0xec",0xec}
    }};
    json result = json::object();
    for (const auto& item : fields) {
        const auto button = read<std::uint32_t>(add(child, item.second), "Zombatar control pointer");
        json state{{"address",button}};
        if (button && readable(button, 0x23)) {
            state["vtable"] = read<std::uint32_t>(button, "Zombatar button vtable");
            if (state["vtable"] == kGameButtonVtable && readable(button,0x106)) {
                state["id"] = read<std::uint32_t>(add(button,0x84),"Zombatar button ID");
                state["hovered"] = read<std::uint8_t>(add(button, 0x20), "Zombatar button hover state");
                state["disabled"] = read<std::uint8_t>(add(button, 0x22), "Zombatar button disabled");
                state["animating"] = read<std::uint8_t>(add(button,0x105),"Zombatar button animation state");
            }
        }
        result[item.first] = std::move(state);
    }
    return result;
}

json visible_items(std::uintptr_t child, int category, int page) {
    const int total = read<std::int32_t>(add(child,kWidgetItemCount),"current category item count");
    const int visible = read<std::int32_t>(add(child,kWidgetVisibleItemCount),"visible grid item count");
    if (category < 1 || category > 8 || page < 1 || visible < 0 || visible > 18 || total < 0 || total > 128)
        return json::array();
    const int selected = read<std::int32_t>(add(child,kWidgetSelectedItems+4*category),"selected item for snapshot");
    json result = json::array();
    constexpr std::size_t first_record = 0x104;
    constexpr std::size_t record_size = 0x1c;
    const int offset = (page-1)*visible;
    for (int local = 0; local < visible && offset+local < total; ++local) {
        const auto record = add(child,first_record+static_cast<std::size_t>(local)*record_size);
        result.push_back({{"index",offset+local},
            {"cell_x",read<std::int32_t>(add(record,4),"grid item cell x")},
            {"cell_y",read<std::int32_t>(add(record,8),"grid item cell y")},
            {"selected",selected==offset+local}});
    }
    return result;
}

json export_metadata(std::uintptr_t app, bool notification_open) {
    auto found = g_expected_jpg_exports.find(app);
    if (found == g_expected_jpg_exports.end()) return json{{"pending",false},{"expected",false}};
    if (notification_open) found->second.notice_seen = true;
    const auto child = child_for(editor_for(selector_for(app)));
    // 0x532836..0x53284C clears this flag after the normal JPG writer returns.
    // Dialog 55 is only shown once: 0x535809 checks PlayerInfo+0x380.
    const bool writer_pending = read<std::uint8_t>(add(child,0xf6e),"JPG writer pending") != 0;
    const bool notice_previously_acknowledged = read<std::uint8_t>(add(active_profile(app),0x380),"JPG notice acknowledged") != 0;
    return json{{"pending",g_pending_save.app==app || writer_pending},{"file_verified",false},{"expected",true},
        {"writer_pending",writer_pending},{"file_verification_required",true},
        {"notification_previously_acknowledged",notice_previously_acknowledged},
        {"path_template","<desktop-dir>\\Zombatar_%d.jpg"},
        {"saved_avatar_index",found->second.avatar_index},
        {"filename_number",found->second.avatar_index+1},
        {"notification_dialog_id",kSaveNoticeDialogId},
        {"notification_open",notification_open},
        {"notification_seen",found->second.notice_seen}};
}

void require_enabled_button(std::uintptr_t child, int id) {
    for (std::size_t offset = 0xa4; offset <= 0xec; offset += sizeof(std::uint32_t)) {
        const auto button = read<std::uint32_t>(add(child,offset),"Zombatar child control pointer");
        if (!button || !readable(button,0x106) || read<std::uint32_t>(button,"Zombatar child button vtable") != kGameButtonVtable)
            continue;
        if (read<std::uint32_t>(add(button,0x84),"Zombatar child button ID") != static_cast<std::uint32_t>(id))
            continue;
        // This is a semantic callback, not a simulated click. Match the exact
        // owned control and reject disabled or animating controls; callers also
        // validate the editor mode. +0x20 is the current hover bit, not visibility.
        if (read<std::uint8_t>(add(button,0x22),"Zombatar button disabled") == 0 &&
            read<std::uint8_t>(add(button,0x105),"Zombatar button animation state") == 0)
            return;
    }
    throw std::runtime_error("The requested Zombatar button is not present, enabled, and idle.");
}

void require_enabled_control(std::uintptr_t child, std::size_t offset, std::uint32_t expected_id) {
    const auto button = read<std::uint32_t>(add(child,offset),"Zombatar control pointer");
    if (!button || !readable(button,0x106) ||
        read<std::uint32_t>(button,"Zombatar control vtable") != kGameButtonVtable ||
        read<std::uint32_t>(add(button,0x84),"Zombatar control ID") != expected_id ||
        read<std::uint8_t>(add(button,0x22),"Zombatar control disabled") != 0 ||
        read<std::uint8_t>(add(button,0x105),"Zombatar control animating") != 0)
        throw std::runtime_error("The expected Zombatar control is not present, enabled, and idle.");
}

} // namespace

json snapshot(std::uintptr_t app) {
    validate_app(app);
    const auto scene = read<std::int32_t>(add(app, kAppScene), "game scene");
    const auto profile = active_profile(app);
    auto& guard = guard_for(profile);
    verify_baseline(profile, guard);
    const auto selector_address = read<std::uint32_t>(add(app,kAppSelector),"GameSelector pointer");
    if (scene != kSceneMenu && (!selector_address || !readable(selector_address,0x170) ||
        read<std::uint32_t>(selector_address,"scene selector vtable") != kSelectorVtable)) {
        return json{{"build","1.2.0.1096"},{"scene",scene},{"dialog_count",dialog_count(app)},
            {"selector",{{"address",selector_address},{"active",false}}},
            {"zombatar",nullptr},{"terms_dialog",nullptr},
            {"profile_protection",{{"baseline_count",guard.baseline_count},{"created_by_this_session",json::array()}}},
            {"pending_delete",g_pending_delete.app==app},{"pending_save_new",g_pending_save.app==app},
            {"last_tick_error",g_last_error},
            {"last_tick_result",g_last_result}};
    }
    const auto selector = selector_for(app);
    const auto editor = editor_for(selector);
    const auto child = child_for(editor);
    const auto count = avatar_count(profile);
    const auto selected = read<std::int32_t>(add(child, kWidgetSelectedSaved), "selected saved avatar");
    const auto mode = read<std::int32_t>(add(child, kWidgetMode), "editor mode");
    const auto category = read<std::int32_t>(add(child, kWidgetCategory), "selected category");
    const auto page = read<std::int32_t>(add(child, kWidgetPage), "item page");
    const auto pages = read<std::int32_t>(add(child, kWidgetPageCount), "item page count");
    json colors=json::array();
    for(int i=0;i<9;++i)
        colors.push_back(read<std::int32_t>(add(child,0xf14+4*i),"selected palette color by category"));
    const json selected_color=(category>=0 && category<9)?colors[static_cast<std::size_t>(category)]:json(nullptr);
    const auto dialog = get_dialog(app, kTermsDialogId);
    const auto save_notice_open = get_dialog(app,kSaveNoticeDialogId) != 0;
    const auto editor_open = editor_is_open(selector);
    json owned = json::array();
    for (const auto& item : guard.created)
        owned.push_back({{"index",item.index},{"verified_current",item.index < count && avatar_bytes(profile,item.index)==item.bytes}});
    return json{
        {"build", "1.2.0.1096"}, {"scene",scene}, {"dialog_count",dialog_count(app)},
        {"selector",{{"address",selector},{"vtable",kSelectorVtable},{"active",scene==kSceneMenu}}},
        {"zombatar",{{"editor_address",editor},{"editor_vtable",kEditorVtable},
            {"widget_address",child},{"widget_vtable",kZombatarVtable},
            {"button_callback",read<std::uint32_t>(add(kZombatarVtable,kZombatarButtonDepressSlot),"Zombatar button callback")},
            {"mouse_handler",read<std::uint32_t>(add(kZombatarVtable,kZombatarMouseHandlerSlot),"Zombatar mouse handler")},
            {"open",editor_open},
            {"mode",mode},{"category",category},{"category_item_count",
                read<std::int32_t>(add(child,kWidgetItemCount),"current category item count")},
            {"page",page},{"page_count",pages},
            {"palette",[&](){
                const auto p=palette_for(child);
                return json{{"address",p},{"vtable",kPaletteVtable},
                    {"mode",read<std::int32_t>(add(p,0xd8),"palette mode")},
                    {"selected_index",read<std::int32_t>(add(p,0xd4),"selected palette swatch")},
                    {"dirty",read<std::uint8_t>(add(p,0xdc),"palette dirty flag")!=0},
                    {"swatch_count",read<std::int32_t>(add(p,0xac),"palette swatch count")},
                    {"first_index",read<std::int32_t>(add(p,0xcc),"first palette swatch")},
                    {"mouse_down",kPaletteMouseDown}};
            }()},
            {"selected_colors_by_category",colors},
            {"selected_color_for_category",selected_color},
            {"visible_items",visible_items(child,category,page)},
            {"jpg_export",export_metadata(app,save_notice_open)},
            {"selected_saved_index",selected},{"saved_count",count},{"saved_limit",kMaxAvatars},
            {"controls",controls(child)}}},
        {"terms_dialog",dialog?json{{"address",dialog},{"id",kTermsDialogId},
            {"verified",read<std::uint32_t>(dialog,"terms dialog vtable")==kTermsDialogVtable}}:json(nullptr)},
        {"profile_protection",{{"baseline_count",guard.baseline_count},{"created_by_this_session",owned}}},
        {"pending_delete",g_pending_delete.app==app},{"pending_save_new",g_pending_save.app==app},
        {"last_tick_error",g_last_error},
        {"last_tick_result",g_last_result}
    };
}

json action(std::uintptr_t app, const json& request) {
    validate_app(app);
    if (!request.is_object() || !request.contains("type") || !request["type"].is_string())
        throw std::runtime_error("Zombatar action requires a string type.");
    const auto type = request.at("type").get<std::string>();
    const auto scene = read<std::int32_t>(add(app, kAppScene), "game scene");
    const auto profile = active_profile(app);
    auto& guard = guard_for(profile);
    verify_baseline(profile, guard);

    if (type == "open_zombatar") {
        if (scene != kSceneMenu || dialog_count(app) != 0)
            throw std::runtime_error("Zombatar entry requires the ordinary main menu with no open dialogs.");
        invoke_selector(selector_for(app),kSelectorZombatarId);
        return {{"accepted",true},{"selector_button_id",kSelectorZombatarId},
            {"terms_acceptance_required",get_dialog(app,kTermsDialogId)!=0},
            {"editor_open",editor_is_open(selector_for(app))}};
    }
    if (type == "accept_terms") {
        const auto dialog = get_dialog(app,kTermsDialogId);
        if (!dialog) throw std::runtime_error("The Zombatar terms dialog is not open; acceptance was not sent.");
        invoke_terms_accept(app,dialog);
        return {{"accepted",true},{"terms_dialog_id",kTermsDialogId},
            {"profile_accepted",read<std::uint8_t>(add(profile,0x381),"terms acceptance flag")==1},
            {"editor_open",editor_is_open(selector_for(app))}};
    }

    const auto selector = selector_for(app);
    const auto editor = editor_for(selector);
    const auto child = child_for(editor);
    if (read<std::uint32_t>(add(child,kWidgetApp),"widget LawnApp") != app)
        throw std::runtime_error("Zombatar widget belongs to another LawnApp instance.");
    if (!editor_is_open(selector))
        throw std::runtime_error("The Zombatar editor is not the active selector page.");

    if (type == "new_avatar") {
        if (dialog_count(app) != 0) throw std::runtime_error("Dismiss dialogs before starting a new Zombatar.");
        const int mode = read<std::int32_t>(add(child,kWidgetMode),"editor mode");
        if (mode == 0)
            return {{"accepted",true},{"ready",true},{"mode",0},{"saved_count",avatar_count(profile)}};
        if (mode != 4) throw std::runtime_error("Start-new is available from mode 0 or the saved-avatar mode 4.");
        if (avatar_count(profile) >= kMaxAvatars)
            throw std::runtime_error("The profile already has the maximum 100 saved Zombatars.");
        // In mode 4, the ordinary MouseDown route maps the +0xD8 child control
        // (ID 0x3EB) to callback 0x3EC, which reinitializes the blank editor.
        require_enabled_control(child,0xd8,0x3eb);
        invoke_widget(child,0x3ec);
        const int after_mode = read<std::int32_t>(add(child,kWidgetMode),"editor mode after start-new");
        if (after_mode != 0) throw std::runtime_error("Target did not enter blank mode 0 for a new Zombatar.");
        return {{"accepted",true},{"ready",true},{"button_field","0xd8"},
            {"callback_id",0x3ec},{"mode_before",mode},{"mode_after",after_mode}};
    }

    if (type == "category") {
        if (dialog_count(app) != 0) throw std::runtime_error("Dismiss dialogs before changing Zombatar category.");
        const int category = request.at("index").get<int>();
        if (category < 0 || category > 7) throw std::runtime_error("Zombatar category index must be 0-7.");
        // Public index is zero-based; target callbacks are ordered by internal
        // category enum rather than by the physical child-button layout.
        static constexpr std::array<int,8> kCategoryButtonIds{{
            0x3f6,0x3f4,0x3f7,0x3f3,0x3f2,0x3f5,0x3f8,0x3f9
        }};
        const int id = kCategoryButtonIds[static_cast<std::size_t>(category)];
        require_enabled_button(child,id);
        invoke_widget(child,id);
        const int expected_enum = category + 1;
        const int selected_enum = read<std::int32_t>(add(child,kWidgetCategory),"selected category");
        if (selected_enum != expected_enum)
            throw std::runtime_error("The target did not select the requested Zombatar category.");
        return {{"accepted",true},{"button_id",id},{"category_index",category},
            {"category_enum",selected_enum},
            {"category_item_count",read<std::int32_t>(add(child,kWidgetItemCount),"current category item count")},
            {"page_count",read<std::int32_t>(add(child,kWidgetPageCount),"category page count")}};
    }
    if (type == "skin") {
        if (dialog_count(app) != 0) throw std::runtime_error("Dismiss dialogs before opening the Skin palette.");
        require_enabled_button(child,0x3f1);
        invoke_widget(child,0x3f1);
        const int selected=read<std::int32_t>(add(child,kWidgetCategory),"selected Skin category enum");
        const int items=read<std::int32_t>(add(child,kWidgetItemCount),"Skin category grid count");
        if(selected!=0 || items!=0) throw std::runtime_error("Target did not enter the verified enum-0 palette-only category.");
        return {{"accepted",true},{"category_enum",0},{"button_id",0x3f1},
            {"category_name","skin"},{"item_count",items},
            {"palette_mode",read<std::int32_t>(add(palette_for(child),0xd8),"Skin palette mode")}};
    }
    if (type == "page") {
        if (dialog_count(app) != 0) throw std::runtime_error("Dismiss dialogs before changing the Zombatar item page.");
        const auto direction = request.at("direction").get<std::string>();
        const int page = read<std::int32_t>(add(child,kWidgetPage),"item page");
        const int pages = read<std::int32_t>(add(child,kWidgetPageCount),"item page count");
        int id = 0;
        if (direction == "next") {
            if (page >= pages) throw std::runtime_error("There is no next Zombatar item page.");
            id = kButtonPageNext;
        } else if (direction == "previous") {
            if (page <= 1) throw std::runtime_error("There is no previous Zombatar item page.");
            id = kButtonPagePrevious;
        } else throw std::runtime_error("Page direction must be next or previous.");
        require_enabled_button(child,id);
        invoke_widget(child,id);
        return {{"accepted",true},{"button_id",id},
            {"page_before",page},{"page_after",read<std::int32_t>(add(child,kWidgetPage),"item page")}};
    }
    if (type == "saved_avatar") {
        if (dialog_count(app) != 0) throw std::runtime_error("Dismiss dialogs before changing the saved-Zombatar selection.");
        const auto direction = request.at("direction").get<std::string>();
        const auto count = avatar_count(profile);
        const int selected = read<std::int32_t>(add(child,kWidgetSelectedSaved),"selected saved avatar");
        int id = 0;
        if (direction == "next") {
            if (count == 0 || selected < 0 || static_cast<std::uint32_t>(selected + 1) >= count)
                throw std::runtime_error("There is no next saved Zombatar.");
            id = 0x3ed;
        } else if (direction == "previous") {
            if (count == 0 || selected <= 0)
                throw std::runtime_error("There is no previous saved Zombatar.");
            id = 0x3ee;
        } else throw std::runtime_error("Saved-avatar direction must be next or previous.");
        require_enabled_button(child,id);
        invoke_widget(child,id);
        return {{"accepted",true},{"button_id",id},{"selected_before",selected},
            {"selected_after",read<std::int32_t>(add(child,kWidgetSelectedSaved),"selected saved avatar")}};
    }
    if (type == "save_new") {
        if (dialog_count(app) != 0) throw std::runtime_error("Dismiss dialogs before saving a Zombatar.");
        if (g_pending_save.app || g_pending_delete.app)
            throw std::runtime_error("A Zombatar save or delete operation is already pending.");
        const int mode = read<std::int32_t>(add(child,kWidgetMode),"editor mode");
        const auto before = avatar_count(profile);
        if (before >= kMaxAvatars) throw std::runtime_error("The profile already has the maximum 100 saved Zombatars.");
        require_enabled_button(child,kButtonSaveNew);
        if (mode == 0) {
            // Preserve the target's own save transition: 3EB enters mode 1,
            // normal updates count down, and the widget then reaches mode 3.
            invoke_widget(child,kButtonSaveNew);
            if (read<std::int32_t>(add(child,kWidgetMode),"editor mode after begin-save") != 1)
                throw std::runtime_error("The target did not enter its normal save transition.");
            g_pending_save = PendingSaveNew{app,profile,selector,editor,child,before,GetTickCount64()};
            g_expected_jpg_exports[app] = ExpectedJpgExport{before,false};
            return {{"accepted",true},{"pending",true},{"stage","target_mode_transition"},
                {"mode_before",mode},{"mode_after",1},
                {"jpg_export",export_metadata(app,get_dialog(app,kSaveNoticeDialogId)!=0)}};
        }
        if (mode != 3)
            throw std::runtime_error("Save-new is only available from mode 0 or completed target mode 3.");
        invoke_widget(child,kButtonSaveNew);
        const auto after = avatar_count(profile);
        if (after != before + 1) throw std::runtime_error("Target save callback did not create exactly one new avatar.");
        const auto bytes = avatar_bytes(profile,before);
        guard.created.push_back(OwnedAvatar{before,bytes});
        g_expected_jpg_exports[app] = ExpectedJpgExport{before,false};
        return {{"accepted",true},{"created_index",before},{"saved_count",after},
            {"profile_save_path",0x470430},{"verified_new_entry",true},
            {"jpg_export",export_metadata(app,get_dialog(app,kSaveNoticeDialogId)!=0)}};
    }
    if (type == "back") {
        if (dialog_count(app) != 0) throw std::runtime_error("Dismiss dialogs before leaving the Zombatar editor.");
        require_enabled_button(child,kButtonBack);
        invoke_widget(child,kButtonBack);
        return {{"accepted",true},{"button_id",kButtonBack},{"transition_pending",true}};
    }
    if (type == "delete_new") {
        if (!request.value("confirm",false))
            throw std::runtime_error("Deleting a session-created avatar requires confirm=true.");
        if (g_pending_delete.app || g_pending_save.app) throw std::runtime_error("A Zombatar save or delete operation is already pending.");
        if (dialog_count(app) != 0) throw std::runtime_error("Dismiss dialogs before deleting a test-created Zombatar.");
        verify_baseline(profile,guard);
        const auto count = avatar_count(profile);
        if (guard.created.empty() || count != guard.baseline_count + guard.created.size())
            throw std::runtime_error("Only an avatar created and fingerprinted by this bridge session can be deleted.");
        const auto owned = guard.created.back();
        if (owned.index + 1 != count || avatar_bytes(profile,owned.index) != owned.bytes)
            throw std::runtime_error("Deletion is limited to the verified newest session-created avatar.");
        if (read<std::int32_t>(add(child,kWidgetSelectedSaved),"selected saved avatar") != static_cast<int>(owned.index))
            throw std::runtime_error("Select the newest session-created avatar in the normal list before deleting it.");
        g_pending_delete = PendingDelete{app,profile,selector,editor,child,owned.index,owned.bytes};
        return {{"accepted",true},{"pending",true},{"operation","delete_newest_session_created_avatar"},
            {"index",owned.index},{"confirmation",true}};
    }
    if (type == "color") {
        if (dialog_count(app) != 0) throw std::runtime_error("Dismiss dialogs before choosing a Zombatar color.");
        if (read<std::int32_t>(add(child,kWidgetMode),"editor mode") != 0)
            throw std::runtime_error("Color selection is available only in normal editor mode 0.");
        const int index=request.at("index").get<int>();
        const auto palette=palette_for(child);
        const auto local=palette_hit_point(palette,index);
        const auto point=palette_screen_point(palette,local.first,local.second);
        const int target_index=read<std::int32_t>(add(palette,0xcc),"first palette swatch")+index;
        const int previous=read<std::int32_t>(add(palette,0xd4),"previous selected palette swatch");
        invoke_palette_mouse_down(palette,point.first,point.second);
        const int selected=read<std::int32_t>(add(palette,0xd4),"selected palette swatch");
        if(selected!=target_index) throw std::runtime_error("Target palette MouseDown did not select the requested swatch.");
        return {{"accepted",true},{"selection_changed",selected!=previous},{"selection_kind","palette_color"},
            {"color_index",index},{"palette_index",selected},{"palette_mode",read<std::int32_t>(add(palette,0xd8),"palette mode")},
            {"hit_test_address",kPaletteHitTest},{"mouse_handler",kPaletteMouseDown},
            {"x",point.first},{"y",point.second}};
    }
    if (type == "item") {
        if (dialog_count(app) != 0) throw std::runtime_error("Dismiss dialogs before choosing a Zombatar item.");
        if (read<std::int32_t>(add(child,kWidgetMode),"editor mode") != 0)
            throw std::runtime_error("Zombatar item selection uses the target's mode-0 grid hit-test path.");
        const int category = read<std::int32_t>(add(child,kWidgetCategory),"selected category");
        if (category < 1 || category > 8) throw std::runtime_error("Select a Zombatar category before choosing an item.");
        const int item = request.at("index").get<int>();
        const int total = read<std::int32_t>(add(child,kWidgetItemCount),"current category item count");
        const int page = read<std::int32_t>(add(child,kWidgetPage),"item page");
        const int pages = read<std::int32_t>(add(child,kWidgetPageCount),"item page count");
        const int visible = read<std::int32_t>(add(child,kWidgetVisibleItemCount),"visible grid item count");
        if (total < 1 || total > 128 || page < 1 || page > pages || visible < 1 || visible > 18)
            throw std::runtime_error("The active Zombatar category has invalid target grid bounds.");
        if (item < 0 || item >= total) throw std::runtime_error("Zombatar item index is outside the current category.");
        const int page_offset = (page-1)*visible;
        if (item < page_offset || item >= page_offset+visible)
            throw std::runtime_error("Choose an item on the currently displayed Zombatar page.");
        const int previous = read<std::int32_t>(add(child,kWidgetSelectedItems+4*category),"previous selected Zombatar item");
        const auto center = find_item_hit_point(child,item,item-page_offset,visible);
        invoke_item_mouse_down(child,center.first,center.second);
        const int selected = read<std::int32_t>(add(child,kWidgetSelectedItems+4*category),"selected Zombatar item");
        if (selected != item)
            throw std::runtime_error("Target MouseDown did not select the requested item.");
        return {{"accepted",true},{"selection_changed",selected==item && selected!=previous},
            {"item_index",item},{"selection_kind","avatar_part"},
            {"category_enum",category},{"page",page},
            {"hit_test_address",kZombatarItemHitTest},{"mouse_handler",kZombatarMouseHandler},
            {"selected_item",selected}};
    }
    if (type == "export")
        throw std::runtime_error("A standalone export control is not mapped. The verified normal save-new flow generates the target Desktop JPG; no direct writer call was sent.");
    throw std::runtime_error("Unsupported Zombatar action type.");
}

void tick(std::uintptr_t app) {
    if (g_pending_save.app == app) {
        try {
            validate_app(app);
            const auto pending = g_pending_save;
            if (read<std::int32_t>(add(app,kAppScene),"scene during queued save") != kSceneMenu ||
                dialog_count(app) != 0)
                throw std::runtime_error("The menu/editor context changed or a dialog opened; queued save was canceled.");
            const auto profile = active_profile(app);
            if (profile != pending.profile) throw std::runtime_error("Active profile changed before queued Zombatar save.");
            const auto selector = selector_for(app);
            if (selector != pending.selector) throw std::runtime_error("GameSelector changed before queued Zombatar save.");
            const auto editor = editor_for(selector);
            const auto child = child_for(editor);
            if (editor != pending.editor || child != pending.child)
                throw std::runtime_error("Zombatar editor changed before queued save.");
            auto& guard = guard_for(profile);
            verify_baseline(profile,guard);
            if (avatar_count(profile) != pending.count_before)
                throw std::runtime_error("Saved-avatar count changed during queued save; operation canceled.");
            const int mode = read<std::int32_t>(add(child,kWidgetMode),"editor mode during queued save");
            if (mode == 1) {
                if (GetTickCount64()-g_pending_save.started_ms > 15000)
                    throw std::runtime_error("The target save transition did not reach mode 3 within 15 seconds.");
                return;
            }
            if (mode != 3) throw std::runtime_error("Target save transition left mode 1 before reaching mode 3.");
            require_enabled_button(child,kButtonSaveNew);
            invoke_widget(child,kButtonSaveNew);
            const auto after = avatar_count(profile);
            if (after != pending.count_before + 1)
                throw std::runtime_error("Target save callback did not create exactly one new avatar.");
            const auto bytes = avatar_bytes(profile,pending.count_before);
            guard.created.push_back(OwnedAvatar{pending.count_before,bytes});
            g_expected_jpg_exports[app] = ExpectedJpgExport{pending.count_before,false};
            g_last_result = json{{"created",true},{"created_index",pending.count_before},
                {"saved_count",after},{"profile_save_path",0x470430},{"verified_new_entry",true},
                {"jpg_export",export_metadata(app,get_dialog(app,kSaveNoticeDialogId)!=0)}};
            g_last_error.clear();
            g_pending_save = PendingSaveNew{};
        } catch (const std::exception& e) {
            g_last_error = e.what();
            g_last_result = json{{"created",false},{"error",g_last_error}};
            g_expected_jpg_exports.erase(app);
            g_pending_save = PendingSaveNew{};
        } catch (...) {
            g_last_error = "Unknown error while servicing queued Zombatar save.";
            g_last_result = json{{"created",false},{"error",g_last_error}};
            g_expected_jpg_exports.erase(app);
            g_pending_save = PendingSaveNew{};
        }
    }
    if (!g_pending_delete.app || app != g_pending_delete.app) return;
    try {
        validate_app(app);
        const auto pending = g_pending_delete;
        if (read<std::int32_t>(add(app,kAppScene),"scene during queued delete") != kSceneMenu ||
            dialog_count(app) != 0)
            throw std::runtime_error("The menu/editor context changed or a dialog opened; queued deletion was canceled.");
        const auto profile = active_profile(app);
        if (profile != pending.profile) throw std::runtime_error("Active profile changed before the queued Zombatar deletion.");
        const auto selector = selector_for(app);
        if (selector != pending.selector) throw std::runtime_error("GameSelector changed before the queued Zombatar deletion.");
        const auto editor = editor_for(selector);
        const auto child = child_for(editor);
        if (editor != pending.editor || child != pending.child)
            throw std::runtime_error("Zombatar editor changed before the queued deletion.");
        auto& guard = guard_for(profile);
        verify_baseline(profile,guard);
        const auto count = avatar_count(profile);
        if (guard.created.empty() || guard.created.back().index != pending.index ||
            count != guard.baseline_count + guard.created.size() || pending.index + 1 != count ||
            avatar_bytes(profile,pending.index) != pending.bytes || guard.created.back().bytes != pending.bytes)
            throw std::runtime_error("The newest session-created avatar changed; deletion was canceled.");
        if (read<std::int32_t>(add(child,kWidgetSelectedSaved),"selected saved avatar") != static_cast<int>(pending.index))
            throw std::runtime_error("The selected avatar changed; deletion was canceled.");

        // Deletion helper has a verified custom EDI ABI; it deletes the selected
        // record using 0x470AC0. Persist via the game's normal profile save.
        invoke_delete_selected(child);
        const auto after_delete = avatar_count(profile);
        if (after_delete != count - 1) throw std::runtime_error("Target delete callback did not remove exactly the verified session-created tail entry.");
        using Save = void(__stdcall*)(void*);
        reinterpret_cast<Save>(kSaveProfile)(reinterpret_cast<void*>(profile));
        guard.created.pop_back();
        g_last_result = json{{"deleted",true},{"index",pending.index},{"saved_count_before",count},
            {"saved_count_after",after_delete},{"profile_save_path",kSaveProfile}};
        g_last_error.clear();
        g_pending_delete = PendingDelete{};
    } catch (const std::exception& e) {
        g_last_error = e.what();
        g_last_result = json{{"deleted",false},{"error",g_last_error}};
        g_pending_delete = PendingDelete{};
    } catch (...) {
        g_last_error = "Unknown error while servicing queued Zombatar deletion.";
        g_last_result = json{{"deleted",false},{"error",g_last_error}};
        g_pending_delete = PendingDelete{};
    }
}

} // namespace pvz::zombatar
