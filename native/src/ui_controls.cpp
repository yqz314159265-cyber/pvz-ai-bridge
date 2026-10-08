#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "ui_controls.hpp"

namespace pvz::ui_controls {
namespace {
using json = nlohmann::json;

constexpr std::uintptr_t kGlobalApp = 0x731c50;
constexpr std::uintptr_t kAppVtable = 0x6f19e0;
constexpr std::uintptr_t kGetDialogSlot = 0x124;
constexpr std::uintptr_t kGetDialog = 0x5ce270;
constexpr std::uintptr_t kManagerOffset = 0x320;
constexpr std::uintptr_t kModeOffset = 0x918;
constexpr std::uintptr_t kSelectorOffset = 0x870;
constexpr std::uintptr_t kSelectorSlideTarget = 0x160;
constexpr std::uintptr_t kWidgetManagerVtable = 0x6fc114;

constexpr std::uintptr_t kGameButtonVtable = 0x6ee7a8;
constexpr std::uintptr_t kLawnButtonVtable = 0x6ee680;
constexpr std::uintptr_t kGenericHyperlinkVtable = 0x6fcb2c;
constexpr std::uintptr_t kChallengeButtonVtable = 0x6fc38c;
constexpr std::uintptr_t kSliderVtable = 0x6fc8a4;
constexpr std::uintptr_t kCheckboxVtable = 0x6fcd8c;
constexpr std::uintptr_t kGameSelectorVtable = 0x6eec88;

// Fixed, local 1096 WidgetManager input entry points. These are never read
// from request data or selected through a caller-controlled address.
constexpr std::uintptr_t kMouseUp = 0x54f8c0;
constexpr std::uintptr_t kMouseDown = 0x54f9d0;
constexpr std::uintptr_t kMouseMove = 0x54fb90;
constexpr std::size_t kMouseOverWidget = 0xa8;
constexpr std::size_t kMouseDownWidget = 0xa4;
constexpr std::size_t kDownButtons = 0x110;
constexpr std::size_t kWidgetX = 0x40;
constexpr std::size_t kWidgetY = 0x44;
constexpr std::size_t kWidgetWidth = 0x48;
constexpr std::size_t kWidgetHeight = 0x4c;
constexpr std::size_t kWidgetChildHead = 0x18;
constexpr std::size_t kWidgetChildCount = 0x1c;
constexpr std::size_t kWidgetManagerOwner = 0x20;
constexpr std::size_t kWidgetParentOwner = 0x24;
constexpr std::size_t kWidgetVisible = 0x65;
constexpr std::size_t kWidgetDisabled = 0x66;
constexpr std::size_t kButtonId = 0xa0;
constexpr std::size_t kButtonLabel = 0xa8;
constexpr std::size_t kButtonLabelLength = 0xb8;
constexpr std::size_t kButtonLabelCapacity = 0xbc;
constexpr std::size_t kControlValue = 0xa8;
constexpr std::size_t kSliderKnobImage = 0xb8;
constexpr std::size_t kSliderHorizontal = 0xc8;
constexpr std::size_t kImageWidth = 0x24;
constexpr std::size_t kImageHeight = 0x28;
constexpr std::size_t kSliderPressed = 0xbc;
constexpr std::size_t kSliderDragOffsetX = 0xc0;
constexpr std::size_t kSliderDragOffsetY = 0xc4;
constexpr std::size_t kListNodeNext = 0;
constexpr std::size_t kListNodePrevious = 4;
constexpr std::size_t kListNodeWidget = 8;
constexpr std::size_t kMaxRoots = 1024;
constexpr std::size_t kMaxChildrenPerWidget = 512;
constexpr std::size_t kMaxWidgets = 4096;
constexpr std::size_t kMaxDepth = 24;

struct WidgetInfo {
    std::uintptr_t address = 0;
    std::uintptr_t vtable = 0;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int screen_x = 0;
    int screen_y = 0;
    int clip_left = 0;
    int clip_top = 0;
    int clip_right = 0;
    int clip_bottom = 0;
    bool visible = false;
    bool enabled = false;
    int button_id = -1;
    double value = 0.0;
    bool has_value = false;
    std::string kind;
    std::string label;
    std::string id;
};

struct WalkState {
    std::uintptr_t manager = 0;
    std::string scope;
    std::vector<WidgetInfo> controls;
    std::unordered_set<std::uintptr_t> visited;
    std::size_t widget_count = 0;
    std::size_t skipped = 0;
    bool truncated = false;
    std::vector<std::string> errors;
};

struct PendingJob {
    std::string job_id;
    std::string control_id;
    std::string action;
    std::string scope;
    double value = 0.0;
};

std::mutex g_job_mutex;
std::optional<PendingJob> g_pending_job;
bool g_job_running = false;
json g_last_job = nullptr;
std::atomic<std::uint64_t> g_next_job{1};

json last_job_snapshot() {
    std::lock_guard<std::mutex> lock(g_job_mutex);
    return g_last_job;
}

std::uintptr_t add(std::uintptr_t pointer, std::size_t offset) {
    if (pointer < 0x10000 || offset > std::numeric_limits<std::uintptr_t>::max() - pointer)
        throw std::runtime_error("Invalid widget address.");
    return pointer + offset;
}

bool readable(std::uintptr_t address, std::size_t size) {
    if (address < 0x10000 || size == 0 ||
        size > std::numeric_limits<std::uintptr_t>::max() - address) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info))) return false;
    const DWORD protection = info.Protect & 0xff;
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
        throw std::runtime_error(std::string("Unreadable UI field: ") + field);
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
}

std::string opaque_id(const std::string& scope, std::uintptr_t widget,
                      std::uintptr_t vtable, std::uint32_t control_code) {
    // FNV-1a is used only to avoid returning process pointers. Authorization
    // comes from resolving the token again in the current generation.
    std::uint64_t hash = 1469598103934665603ull;
    const auto mix = [&hash](const void* data, std::size_t size) {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < size; ++i) {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
    };
    mix(scope.data(), scope.size());
    mix(&widget, sizeof(widget));
    mix(&vtable, sizeof(vtable));
    mix(&control_code, sizeof(control_code));
    std::ostringstream out;
    out << "ui-" << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

std::string widget_kind(std::uintptr_t vtable) {
    if (vtable == kGameButtonVtable) return "button";
    if (vtable == kLawnButtonVtable) return "lawn_button";
    if (vtable == kGenericHyperlinkVtable) return "hyperlink_button";
    if (vtable == kChallengeButtonVtable) return "challenge_button";
    if (vtable == kSliderVtable) return "slider";
    if (vtable == kCheckboxVtable) return "checkbox";
    return {};
}

std::string read_button_label(std::uintptr_t widget) {
    const auto length = read<std::uint32_t>(add(widget, kButtonLabelLength), "button label length");
    const auto capacity = read<std::uint32_t>(add(widget, kButtonLabelCapacity), "button label capacity");
    if (!length) return {};
    if (length > 512 || capacity < length || capacity > 4096)
        throw std::runtime_error("Button label string bounds are invalid.");
    const auto storage = capacity < 16
        ? add(widget, kButtonLabel)
        : static_cast<std::uintptr_t>(read<std::uint32_t>(add(widget, kButtonLabel), "button label storage"));
    if (!storage || !readable(storage, length))
        throw std::runtime_error("Button label storage is unreadable.");
    std::string value(length, '\0');
    std::memcpy(value.data(), reinterpret_cast<const void*>(storage), length);
    if (value.find('\0') != std::string::npos) return {};
    // JSON strings must be valid UTF-8. Skip labels with malformed byte runs
    // instead of letting a localized string break the entire snapshot.
    for (std::size_t i = 0; i < value.size();) {
        const auto c = static_cast<unsigned char>(value[i]);
        if (c < 0x80) {
            if (c < 0x20 && c != '\t') return {};
            ++i;
            continue;
        }
        std::size_t count = c >= 0xc2 && c <= 0xdf ? 2 :
            c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
        if (!count || i + count > value.size()) return {};
        for (std::size_t j = 1; j < count; ++j)
            if ((static_cast<unsigned char>(value[i + j]) & 0xc0) != 0x80) return {};
        const auto second = static_cast<unsigned char>(value[i + 1]);
        if ((c == 0xe0 && second < 0xa0) || (c == 0xed && second >= 0xa0) ||
            (c == 0xf0 && second < 0x90) || (c == 0xf4 && second >= 0x90)) return {};
        i += count;
    }
    return value;
}

std::uintptr_t get_dialog(std::uintptr_t app, int id) {
    if (read<std::uint32_t>(app, "app vtable") != kAppVtable ||
        read<std::uint32_t>(add(kAppVtable, kGetDialogSlot), "GetDialog slot") != kGetDialog)
        throw std::runtime_error("LawnApp dialog lookup does not match the verified 1096 target.");
    using GetDialog = void*(__thiscall*)(void*, int);
    return reinterpret_cast<std::uintptr_t>(
        reinterpret_cast<GetDialog>(kGetDialog)(reinterpret_cast<void*>(app), id));
}

std::string blocked_context(std::uintptr_t app) {
    // Profile dialogs can contain destructive normal callbacks. Hide their
    // controls from this generic surface and refuse all generic actions.
    for (int id = 29; id <= 34; ++id) {
        if (get_dialog(app, id)) return "protected_profile_dialog";
    }
    // Restart confirmation (23) and the Zombatar delete confirmation (54)
    // require dedicated, intent-specific handlers.
    if (get_dialog(app, 23)) return "protected_restart_confirmation";
    if (get_dialog(app, 54)) return "protected_zombatar_delete_confirmation";
    const auto mode = read<std::int32_t>(add(app, kModeOffset), "game mode");
    const auto scene = read<std::int32_t>(add(app, 0x91c), "game scene");
    if (scene == 3 && mode == 43) return "garden_requires_semantic_actions";
    if (scene == 3 && mode == 50) return "tree_requires_semantic_actions";
    // The selector/editor objects are allocated in the main menu even while
    // closed. Block only when the menu scene's editor slide target is open.
    const auto selector = read<std::uint32_t>(add(app, kSelectorOffset), "selector");
    if (scene == 1 && selector && readable(selector, 0x16c) &&
        read<std::uint32_t>(selector, "selector vtable") == kGameSelectorVtable) {
        const bool editor_open = read<std::int32_t>(add(selector, kSelectorSlideTarget),
            "selector slide target") == -1600;
        // Dialog 55 only acknowledges the game's normal JPG save notice.
        // Its modal hit testing still prevents activating underlying controls.
        if (editor_open && !get_dialog(app, 55)) return "zombatar_controls_require_semantic_actions";
    }
    return {};
}

void add_error(WalkState& state, const std::string& detail) {
    ++state.skipped;
    if (state.errors.size() < 8) state.errors.push_back(detail);
}

void walk_widget(WalkState& state, std::uintptr_t widget, std::size_t depth,
                 int parent_x, int parent_y, bool parent_visible, bool parent_enabled,
                 int clip_left, int clip_top, int clip_right, int clip_bottom) {
    if (!widget || depth > kMaxDepth || state.widget_count >= kMaxWidgets) {
        state.truncated = true;
        return;
    }
    if (!state.visited.insert(widget).second) return;
    ++state.widget_count;
    try {
        if (!readable(widget, 0x20)) throw std::runtime_error("Widget header is unreadable.");
        const auto vtable = read<std::uint32_t>(widget, "widget vtable");
        const auto owner = read<std::uint32_t>(add(widget, kWidgetManagerOwner), "widget manager");
        if (owner != state.manager) throw std::runtime_error("Widget belongs to a different manager.");
        const int x = read<std::int32_t>(add(widget, kWidgetX), "widget x");
        const int y = read<std::int32_t>(add(widget, kWidgetY), "widget y");
        const int width = read<std::int32_t>(add(widget, kWidgetWidth), "widget width");
        const int height = read<std::int32_t>(add(widget, kWidgetHeight), "widget height");
        // The stock GOTY achievements container is 800 x 16150.
        if (width < 0 || height < 0 || width > 32768 || height > 32768 ||
            x < -32768 || x > 32768 || y < -32768 || y > 32768)
            throw std::runtime_error("Widget geometry is outside safe bounds.");
        const auto visible_byte = read<std::uint8_t>(add(widget, kWidgetVisible), "widget visibility");
        const auto disabled_byte = read<std::uint8_t>(add(widget, kWidgetDisabled), "widget disabled");
        if (visible_byte > 1 || disabled_byte > 1)
            throw std::runtime_error("Widget state bytes are invalid.");
        const bool visible = parent_visible && visible_byte != 0;
        const bool enabled = parent_enabled && visible && disabled_byte == 0;
        const int screen_x = parent_x + x;
        const int screen_y = parent_y + y;
        if (screen_x < -32768 || screen_x > 32768 ||
            screen_y < -32768 || screen_y > 32768)
            throw std::runtime_error("Accumulated widget position is outside safe bounds.");
        const int right = screen_x + width;
        const int bottom = screen_y + height;
        const int child_clip_left = std::max(clip_left, screen_x);
        const int child_clip_top = std::max(clip_top, screen_y);
        const int child_clip_right = std::min(clip_right, right);
        const int child_clip_bottom = std::min(clip_bottom, bottom);

        const auto kind = widget_kind(vtable);
        if (!kind.empty()) {
            WidgetInfo info{};
            info.address = widget;
            info.vtable = vtable;
            info.x = x;
            info.y = y;
            info.width = width;
            info.height = height;
            info.screen_x = screen_x;
            info.screen_y = screen_y;
            info.clip_left = child_clip_left;
            info.clip_top = child_clip_top;
            info.clip_right = child_clip_right;
            info.clip_bottom = child_clip_bottom;
            const bool on_screen = child_clip_left < child_clip_right && child_clip_top < child_clip_bottom;
            info.visible = visible && on_screen;
            info.enabled = enabled && on_screen;
            info.kind = kind;
            if (kind == "button" || kind == "lawn_button" ||
                kind == "hyperlink_button" || kind == "challenge_button") {
                info.button_id = static_cast<int>(read<std::uint32_t>(add(widget, kButtonId), "button ID"));
                info.label = read_button_label(widget);
            } else if (kind == "slider") {
                info.value = read<double>(add(widget, kControlValue), "slider value");
                info.has_value = std::isfinite(info.value) && info.value >= 0.0 && info.value <= 1.0;
                if (!info.has_value) throw std::runtime_error("Slider value is invalid.");
            } else if (kind == "checkbox") {
                const auto checked = read<std::uint8_t>(add(widget, kControlValue), "checkbox value");
                if (checked > 1) throw std::runtime_error("Checkbox value is invalid.");
                info.value = checked != 0 ? 1.0 : 0.0;
                info.has_value = true;
            }
            const auto code = info.button_id < 0 ? 0u : static_cast<std::uint32_t>(info.button_id);
            info.id = opaque_id(state.scope, widget, vtable, code);
            state.controls.push_back(std::move(info));
        }

        // WidgetContainer is embedded at widget+4. Its sentinel head is at
        // +0x18 and the target-maintained child count at +0x1C. Nodes are a
        // circular doubly-linked list: next +0, prev +4, widget +8.
        const auto child_count = read<std::uint32_t>(add(widget, kWidgetChildCount), "child count");
        if (child_count > kMaxChildrenPerWidget) throw std::runtime_error("Widget child count exceeds limit.");
        if (child_count != 0) {
            const auto head = read<std::uint32_t>(add(widget, kWidgetChildHead), "child list head");
            if (!head || !readable(head, 8)) throw std::runtime_error("Widget child-list sentinel is unreadable.");
            const auto expected_tail = read<std::uint32_t>(add(head, 4), "child-list tail");
            auto node = read<std::uint32_t>(head, "child-list first node");
            std::uintptr_t previous = head;
            for (std::uint32_t i = 0; i < child_count; ++i) {
                if (!node || node == head || !readable(node, 12))
                    throw std::runtime_error("Widget child list ended before its recorded count.");
                if (read<std::uint32_t>(add(node, kListNodePrevious), "child-list previous") != previous)
                    throw std::runtime_error("Widget child-list links are inconsistent.");
                const auto child = read<std::uint32_t>(add(node, kListNodeWidget), "child widget");
                const auto next = read<std::uint32_t>(add(node, kListNodeNext), "child-list next");
                if (!child || !next) throw std::runtime_error("Widget child-list node is incomplete.");
                walk_widget(state, child, depth + 1, screen_x, screen_y, visible, enabled,
                    child_clip_left, child_clip_top, child_clip_right, child_clip_bottom);
                previous = node;
                node = next;
            }
            if (node != head || previous != expected_tail)
                throw std::runtime_error("Widget child count does not match its sentinel links.");
        }
    } catch (const std::exception& error) {
        add_error(state, error.what());
    } catch (...) {
        add_error(state, "Unknown widget metadata error.");
    }
}

WalkState enumerate(std::uintptr_t app, const std::string& scope) {
    if (scope.empty() || scope.size() > 128) throw std::runtime_error("UI scope token is missing or too long.");
    if (!app || app != read<std::uint32_t>(kGlobalApp, "global app") || !readable(app, 0x960))
        throw std::runtime_error("Unexpected LawnApp pointer/layout.");
    WalkState state{};
    state.scope = scope;
    state.manager = read<std::uint32_t>(add(app, kManagerOffset), "WidgetManager pointer");
    if (!state.manager || !readable(state.manager, 0x120) ||
        read<std::uint32_t>(state.manager, "WidgetManager vtable") != kWidgetManagerVtable)
        throw std::runtime_error("WidgetManager does not match the verified 1096 object.");

    // WidgetManager derives from WidgetContainer. Its actual root hierarchy is
    // the embedded base container at manager+4: sentinel pointer +0x18, count
    // +0x1C. The separate vector at manager+0x80 is used by 0x54E790 as an
    // ordered/modal list; it is not the hit-test hierarchy used as tree roots.
    const auto root_count = read<std::uint32_t>(add(state.manager, kWidgetChildCount), "manager child count");
    if (root_count > kMaxRoots) throw std::runtime_error("WidgetManager root count exceeds limit.");
    if (root_count) {
        const auto head = read<std::uint32_t>(add(state.manager, kWidgetChildHead), "manager child-list head");
        if (!head || !readable(head, 8)) throw std::runtime_error("WidgetManager child-list sentinel is unreadable.");
        const auto expected_tail = read<std::uint32_t>(add(head, 4), "manager child-list tail");
        auto node = read<std::uint32_t>(head, "manager child-list first node");
        std::uintptr_t previous = head;
        for (std::uint32_t i = 0; i < root_count; ++i) {
            if (!node || node == head || !readable(node, 12))
                throw std::runtime_error("WidgetManager child list ended before its recorded count.");
            if (read<std::uint32_t>(add(node, kListNodePrevious), "manager child-list previous") != previous)
                throw std::runtime_error("WidgetManager child-list links are inconsistent.");
            const auto root = read<std::uint32_t>(add(node, kListNodeWidget), "manager root widget");
            const auto next = read<std::uint32_t>(add(node, kListNodeNext), "manager child-list next");
            if (!root || !next) throw std::runtime_error("WidgetManager child-list node is incomplete.");
            walk_widget(state, root, 0, 0, 0, true, true, 0, 0, 800, 600);
            previous = node;
            node = next;
        }
        if (node != head || previous != expected_tail)
            throw std::runtime_error("WidgetManager root count does not match its sentinel links.");
    }
    return state;
}

json public_control(const WidgetInfo& info) {
    json control{{"id", info.id}, {"kind", info.kind},
        {"bounds", {{"x", info.screen_x}, {"y", info.screen_y},
            {"width", info.width}, {"height", info.height}}},
        {"visible", info.visible}, {"enabled", info.enabled}};
    if (info.button_id >= 0) control["control_code"] = info.button_id;
    if (!info.label.empty()) control["label"] = info.label;
    if (info.has_value) control["value"] = info.value;
    return control;
}

void require_main_thread(std::uint32_t main_thread_id) {
    if (!main_thread_id || GetCurrentThreadId() != main_thread_id)
        throw std::runtime_error("Widget operation refused outside the verified game-update thread.");
}

void move_pointer(std::uintptr_t manager, int x, int y) {
    using MouseMove = bool(__thiscall*)(void*, int, int);
    if (!reinterpret_cast<MouseMove>(kMouseMove)(reinterpret_cast<void*>(manager), x, y))
        throw std::runtime_error("WidgetManager rejected pointer movement.");
}

void mouse_down(std::uintptr_t manager, int x, int y) {
    using MouseButton = bool(__thiscall*)(void*, int, int, int);
    reinterpret_cast<MouseButton>(kMouseDown)(reinterpret_cast<void*>(manager), x, y, 0);
}

void mouse_up(std::uintptr_t manager, int x, int y) noexcept {
    using MouseButton = bool(__thiscall*)(void*, int, int, int);
    reinterpret_cast<MouseButton>(kMouseUp)(reinterpret_cast<void*>(manager), x, y, 0);
}

void release_button(std::uintptr_t manager, int x, int y) noexcept {
    // This transaction can only press button index 0. Do not infer mappings
    // for other manager bits while cleaning up an interrupted action.
    try {
        if (!manager) return;
        const auto down = read<std::uint32_t>(add(manager, kDownButtons), "down buttons");
        if (down & 1u) mouse_up(manager, x, y);
    } catch (...) {
        // Never allow cleanup faults to escape a transaction failure.
    }
}

struct ReleaseOnExit {
    std::uintptr_t manager;
    int x;
    int y;
    bool armed = false;
    ~ReleaseOnExit() { if (armed) release_button(manager, x, y); }
};

void pointer_click(std::uintptr_t manager, const WidgetInfo& info,
                   int down_x, int down_y, int up_x, int up_y) {
    if (read<std::uint32_t>(add(manager, kDownButtons), "mouse button state") != 0)
        throw std::runtime_error("WidgetManager already has a held mouse button.");
    if (!info.enabled || !info.visible || info.width <= 0 || info.height <= 0)
        throw std::runtime_error("Widget is not currently visible and enabled.");
    const auto inside_clip = [&info](int x, int y) {
        return x >= info.clip_left && x < info.clip_right &&
            y >= info.clip_top && y < info.clip_bottom;
    };
    if (!inside_clip(down_x, down_y) || !inside_clip(up_x, up_y))
        throw std::runtime_error("Requested pointer path leaves the visible widget bounds.");
    move_pointer(manager, down_x, down_y);
    if (read<std::uint32_t>(add(manager, kMouseOverWidget), "hover widget") != info.address)
        throw std::runtime_error("Normal hit testing did not select the requested widget.");
    ReleaseOnExit release{manager, down_x, down_y, true};
    mouse_down(manager, down_x, down_y);
    if ((read<std::uint32_t>(add(manager, kDownButtons), "mouse button state") & 1u) == 0 ||
        read<std::uint32_t>(add(manager, kMouseDownWidget), "down widget") != info.address)
        throw std::runtime_error("WidgetManager did not retain the requested normal mouse-down target.");
    if (up_x != down_x || up_y != down_y) {
        move_pointer(manager, up_x, up_y);
        release.x = up_x;
        release.y = up_y;
        if ((read<std::uint32_t>(add(manager, kDownButtons), "mouse button state") & 1u) == 0)
            throw std::runtime_error("WidgetManager lost the held button during drag.");
    }
    mouse_up(manager, up_x, up_y);
    release.armed = false;
    if (read<std::uint32_t>(add(manager, kDownButtons), "mouse button state") != 0)
        throw std::runtime_error("WidgetManager did not release the mouse button.");
}

void pointer_slider_drag(std::uintptr_t manager, const WidgetInfo& info,
                         bool horizontal, int start_axis, int target_knob_left,
                         int knob_extent, int travel) {
    if (read<std::uint32_t>(add(manager, kDownButtons), "mouse button state") != 0)
        throw std::runtime_error("WidgetManager already has a held mouse button.");
    if (!info.enabled || !info.visible || info.width <= 0 || info.height <= 0)
        throw std::runtime_error("Slider is not currently visible and enabled.");
    const auto inside_clip = [&info](int x, int y) {
        return x >= info.clip_left && x < info.clip_right &&
            y >= info.clip_top && y < info.clip_bottom;
    };
    const int center_other = horizontal ? info.height / 2 : info.width / 2;
    const int down_x = info.screen_x + (horizontal ? start_axis : center_other);
    const int down_y = info.screen_y + (horizontal ? center_other : start_axis);
    if (!inside_clip(down_x, down_y))
        throw std::runtime_error("Slider knob center is outside its visible bounds.");
    move_pointer(manager, down_x, down_y);
    if (read<std::uint32_t>(add(manager, kMouseOverWidget), "slider hover widget") != info.address)
        throw std::runtime_error("Normal hit testing did not select the requested Slider.");
    ReleaseOnExit release{manager, down_x, down_y, true};
    mouse_down(manager, down_x, down_y);
    if ((read<std::uint32_t>(add(manager, kDownButtons), "slider mouse-down state") & 1u) == 0 ||
        read<std::uint32_t>(add(manager, kMouseDownWidget), "slider down widget") != info.address)
        throw std::runtime_error("WidgetManager did not retain the requested Slider mouse-down target.");
    if (read<std::uint8_t>(add(info.address, kSliderPressed), "Slider knob pressed state") != 1)
        throw std::runtime_error("The normal Slider handler did not capture its knob.");

    // MouseDown stores the exact in-knob offset here. Reuse that target-derived
    // offset so draw rounding and even/odd knob sizes cannot shift the value.
    const auto offset = read<std::int32_t>(add(info.address,
        horizontal ? kSliderDragOffsetX : kSliderDragOffsetY), "Slider knob grab offset");
    if (offset < 0 || offset >= knob_extent)
        throw std::runtime_error("Slider knob grab offset is outside its image.");
    const int target_axis = target_knob_left + offset;
    const int up_x = info.screen_x + (horizontal ? target_axis : center_other);
    const int up_y = info.screen_y + (horizontal ? center_other : target_axis);
    if (!inside_clip(up_x, up_y))
        throw std::runtime_error("Requested Slider value maps outside its visible bounds.");
    move_pointer(manager, up_x, up_y);
    if ((read<std::uint32_t>(add(manager, kDownButtons), "slider drag state") & 1u) == 0)
        throw std::runtime_error("WidgetManager lost the held button during Slider drag.");
    mouse_up(manager, up_x, up_y);
    release.armed = false;
    if (read<std::uint32_t>(add(manager, kDownButtons), "slider mouse-up state") != 0)
        throw std::runtime_error("WidgetManager did not release the Slider mouse button.");
    const double expected = static_cast<double>(target_knob_left) / travel;
    const double actual = read<double>(add(info.address, kControlValue), "Slider value after drag");
    // The game's division may round through a float before storing a double.
    if (!std::isfinite(actual) || std::abs(actual - expected) > 1e-6)
        throw std::runtime_error("Slider did not reach the normal pixel-quantized value.");
}

void execute_job(std::uintptr_t app, const PendingJob& job, const std::string& scope) {
    if (job.scope != scope) throw std::runtime_error("UI job scope changed before execution.");
    const auto blocked = blocked_context(app);
    if (!blocked.empty()) throw std::runtime_error("Generic UI action refused in " + blocked + ".");
    auto state = enumerate(app, scope);
    if (state.skipped || state.truncated)
        throw std::runtime_error("UI tree metadata is incomplete; action refused.");
    const auto found = std::find_if(state.controls.begin(), state.controls.end(),
        [&job](const WidgetInfo& info) { return info.id == job.control_id; });
    if (found == state.controls.end()) throw std::runtime_error("The control_id is stale or unknown.");
    if (!found->enabled || !found->visible)
        throw std::runtime_error("The requested control is not visible and enabled.");
    if (job.action == "activate") {
        if (found->kind != "button" && found->kind != "lawn_button" &&
            found->kind != "hyperlink_button" && found->kind != "challenge_button" &&
            found->kind != "checkbox")
            throw std::runtime_error("Activate is supported only for verified button and checkbox classes.");
        const int x = found->screen_x + found->width / 2;
        const int y = found->screen_y + found->height / 2;
        pointer_click(state.manager, *found, x, y, x, y);
        return;
    }
    if (job.action == "drag") {
        if (found->kind != "slider" || !found->has_value)
            throw std::runtime_error("Drag is supported only for verified Slider controls.");
        const auto horizontal_flag = read<std::uint8_t>(add(found->address, kSliderHorizontal),
            "slider orientation");
        if (horizontal_flag > 1) throw std::runtime_error("Slider orientation flag is invalid.");
        const bool horizontal = horizontal_flag != 0;
        const auto image = read<std::uint32_t>(add(found->address, kSliderKnobImage),
            "slider knob image");
        const int knob_extent = read<std::int32_t>(add(image,
            horizontal ? kImageWidth : kImageHeight), "slider knob image extent");
        const int widget_extent = horizontal ? found->width : found->height;
        const int travel = widget_extent - knob_extent;
        if (knob_extent < 4 || knob_extent > widget_extent || travel <= 0)
            throw std::runtime_error("Slider knob image does not fit its verified travel axis.");

        // The target places the knob at the integer conversion of
        // (widget extent - image extent) * value. Centering gives enough margin
        // for the target's configured rounding mode; MouseDown then stores the
        // exact local grab offset and MouseDrag divides by this same travel.
        const int start_left = static_cast<int>(std::trunc(found->value * travel));
        const int grab_offset = knob_extent / 2;
        const int target_left = static_cast<int>(std::lround(job.value * travel));
        const int start_axis = start_left + grab_offset;
        pointer_slider_drag(state.manager, *found, horizontal, start_axis,
            target_left, knob_extent, travel);
        return;
    }
    throw std::runtime_error("Unsupported UI semantic action.");
}

json snapshot_result(std::uintptr_t app, const std::string& scope,
                    std::uint32_t main_thread_id) {
    try {
        require_main_thread(main_thread_id);
        const auto blocked = blocked_context(app);
        if (!blocked.empty()) return {{"ok", false}, {"controls", json::array()},
            {"blocked", true}, {"reason", blocked}, {"last_job", last_job_snapshot()}};
        auto state = enumerate(app, scope);
        json controls = json::array();
        for (const auto& info : state.controls) controls.push_back(public_control(info));
        json result{{"ok", true}, {"controls", std::move(controls)},
            {"widget_count", state.widget_count}, {"skipped_widgets", state.skipped},
            {"truncated", state.truncated}};
        if (!state.errors.empty()) result["metadata_errors"] = state.errors;
        result["last_job"] = last_job_snapshot();
        return result;
    } catch (const std::exception& error) {
        return {{"ok", false}, {"controls", json::array()}, {"detail", error.what()},
            {"last_job", last_job_snapshot()}};
    } catch (...) {
        return {{"ok", false}, {"controls", json::array()},
            {"detail", "Unknown widget metadata failure."}, {"last_job", last_job_snapshot()}};
    }
}

} // namespace

json snapshot(std::uintptr_t app, const std::string& scope,
              std::uint32_t main_thread_id) {
    return snapshot_result(app, scope, main_thread_id);
}

json dispatch(std::uintptr_t app, const json& args, const std::string& scope,
              std::uint32_t main_thread_id) {
    try {
        (void)app;
        (void)main_thread_id;
        if (!args.is_object()) throw std::runtime_error("UI action arguments must be an object.");
        for (auto it = args.begin(); it != args.end(); ++it) {
            if (it.key() != "control_id" && it.key() != "action" && it.key() != "value")
                throw std::runtime_error("UI action accepts only control_id, action, and optional value.");
        }
        if (scope.empty() || scope.size() > 128)
            throw std::runtime_error("UI scope token is missing or too long.");
        const auto control_id = args.value("control_id", std::string{});
        const auto action = args.value("action", std::string{});
        if (control_id.empty() || control_id.size() > 40)
            throw std::runtime_error("A current opaque control_id is required.");
        PendingJob job{};
        job.control_id = control_id;
        job.action = action;
        job.scope = scope;
        if (action == "activate") {
            // No extra action arguments are needed.
        }
        if (action == "drag") {
            if (!args.contains("value") || !args["value"].is_number())
                throw std::runtime_error("Slider drag requires a numeric value between zero and one.");
            const double value = args["value"].get<double>();
            if (!std::isfinite(value) || value < 0.0 || value > 1.0)
                throw std::runtime_error("Slider drag value must be between zero and one.");
            job.value = value;
        } else if (action != "activate") {
            throw std::runtime_error("Unsupported UI semantic action.");
        }
        const auto sequence = g_next_job.fetch_add(1, std::memory_order_relaxed);
        job.job_id = "ui-job-" + std::to_string(sequence);
        {
            std::lock_guard<std::mutex> lock(g_job_mutex);
            if (g_pending_job || g_job_running)
                throw std::runtime_error("A generic UI action is already pending or running.");
            g_pending_job = job;
            g_last_job = {{"job_id", job.job_id}, {"status", "pending"},
                {"action", action}, {"control_id", control_id}};
            if (action == "drag") g_last_job["requested_value"] = job.value;
        }
        json result{{"ok", true}, {"accepted", true}, {"transition_pending", true},
            {"job_id", job.job_id}, {"action", action}, {"control_id", control_id}};
        if (action == "drag") result["requested_value"] = job.value;
        return result;
    } catch (const std::exception& error) {
        return {{"ok", false}, {"accepted", false}, {"detail", error.what()}};
    } catch (...) {
        return {{"ok", false}, {"accepted", false},
            {"detail", "Unknown UI action failure."}};
    }
}

json tick(std::uintptr_t app, const std::string& scope,
          std::uint32_t main_thread_id) {
    PendingJob job{};
    {
        std::lock_guard<std::mutex> lock(g_job_mutex);
        if (!g_pending_job) return {{"ok", true}, {"status", "idle"}};
        job = std::move(*g_pending_job);
        g_pending_job.reset();
        g_job_running = true;
        g_last_job["status"] = "running";
    }

    json result{{"ok", false}, {"job_id", job.job_id}};
    try {
        require_main_thread(main_thread_id);
        execute_job(app, job, scope);
        result["ok"] = true;
        result["status"] = "dispatched";
        result["detail"] = "The normal WidgetManager input transaction was issued.";
    } catch (const std::exception& error) {
        result["status"] = "failed";
        result["detail"] = error.what();
    } catch (...) {
        result["status"] = "failed";
        result["detail"] = "Unknown queued UI action failure.";
    }
    {
        std::lock_guard<std::mutex> lock(g_job_mutex);
        result["action"] = job.action;
        result["control_id"] = job.control_id;
        if (job.action == "drag") result["requested_value"] = job.value;
        g_last_job = result;
        g_job_running = false;
    }
    return result;
}

} // namespace pvz::ui_controls
