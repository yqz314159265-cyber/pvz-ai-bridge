#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "profiles.hpp"

namespace pvz::profiles {
namespace {
constexpr std::uintptr_t kImageBase = 0x400000;
constexpr std::uintptr_t kGlobalApp = 0x731c50;
constexpr std::uintptr_t kExpectedAppVtable = 0x6f19e0;
constexpr std::uintptr_t kExpectedManagerVtable = 0x6f2e84;
constexpr std::uintptr_t kExpectedUserDialogVtable = 0x6fa120;
constexpr std::uintptr_t kExpectedListListenerVtable = 0x6fa274;
constexpr std::uintptr_t kExpectedListClicked = 0x52b780;
constexpr std::uintptr_t kExpectedGetSelectedName = 0x52b650;
constexpr std::uintptr_t kExpectedNewUserDialogVtable = 0x6f2510;
constexpr std::uintptr_t kExpectedEditWidgetVtable = 0x6f1bb0;
constexpr std::uintptr_t kExpectedEditSetText = 0x556290;
constexpr std::uintptr_t kExpectedAppButtonDepress = 0x458590;
constexpr std::uintptr_t kTargetStringConstruct = 0x405dd0;
constexpr std::uintptr_t kTargetStringDestroy = 0x401ae0;
constexpr std::uintptr_t kDoUserDialog = 0x4558f0;
constexpr std::uintptr_t kDoCreateUserDialog = 0x455a80;
constexpr std::uintptr_t kDoConfirmDeleteUserDialog = 0x455e90;
constexpr std::uintptr_t kDoRenameUserDialog = 0x456250;
constexpr std::size_t kAppProfileManager = 0x948;
constexpr std::size_t kAppActiveProfile = 0x94c;
constexpr std::size_t kAppScene = 0x91c;
constexpr std::size_t kAppGetDialogSlot = 0x124;
constexpr std::size_t kAppGetDialogCountSlot = 0x13c;
constexpr std::size_t kDialogId = 0x158;
constexpr std::size_t kDialogOwner = 0x16c;
constexpr std::size_t kUserDialogList = 0x194;
constexpr std::size_t kUserDialogListListener = 0x18c;
constexpr int kUserDialogId = 29;
constexpr int kCreateDialogId = 30;
constexpr int kDeleteConfirmDialogId = 31;
constexpr int kRenameDialogId = 32;
constexpr int kCreateErrorDialogId = 33;
constexpr int kRenameErrorDialogId = 34;
constexpr int kSceneMainMenu = 1;
constexpr std::size_t kManagerTreeSentinel = 0x1c;
constexpr std::size_t kManagerTreeSize = 0x20;
constexpr std::size_t kTreeLeft = 0x00;
constexpr std::size_t kTreeParent = 0x04;
constexpr std::size_t kTreeRight = 0x08;
constexpr std::size_t kTreeNodeBytes = 0x38;
constexpr std::size_t kTreeValue = 0x30;
constexpr std::size_t kPlayerNameInline = 0x04;
constexpr std::size_t kPlayerNameLength = 0x14;
constexpr std::size_t kPlayerNameCapacity = 0x18;
constexpr std::size_t kPlayerUseSequence = 0x1c;
constexpr std::size_t kPlayerId = 0x20;
constexpr std::uint32_t kMaximumUsers = 8;
constexpr std::uint32_t kMaximumNameLength = 12;

struct ProfileRecord {
    std::uint32_t id = 0;
    std::uint32_t use_sequence = 0;
    std::uintptr_t address = 0;
    std::string name;
    bool active = false;
};

struct GuardState {
    std::uintptr_t app = 0;
    bool initialized = false;
    std::unordered_map<std::uint32_t, std::string> baseline;
    std::unordered_set<std::uint32_t> protected_ids{1, 2};
    std::unordered_set<std::string> protected_names;
    std::unordered_map<std::uint32_t, std::string> created;
};

GuardState g_guard;

enum class JobKind { open, select, create, rename, erase, close };
enum class JobPhase { open_user, wait_user, wait_editor, wait_delete, commit, verify };
struct PendingJob {
    std::uint64_t id = 0;
    std::uintptr_t app = 0;
    JobKind kind = JobKind::open;
    JobPhase phase = JobPhase::open_user;
    std::string state;
    std::string error;
    std::string target_name;
    std::string confirmation_name;
    std::string new_name;
    std::uint32_t target_id = 0;
    int target_dialog_id = 0;
    std::size_t target_index = 0;
    json result = json::object();
};

PendingJob g_job;
std::uint64_t g_next_job_id = 1;
bool g_ticking = false;

std::uintptr_t checked_add(std::uintptr_t base, std::size_t offset) {
    if (base < 0x10000 || offset > std::numeric_limits<std::uintptr_t>::max() - base)
        throw std::runtime_error("Invalid profile-memory address.");
    return base + offset;
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

template<class T> T read_value(std::uintptr_t address, const char* field) {
    if (!readable(address, sizeof(T)))
        throw std::runtime_error(std::string("Unreadable profile field: ") + field);
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
}

void validate_app(std::uintptr_t app) {
    if (reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) != kImageBase ||
        app != read_value<std::uint32_t>(kGlobalApp, "global app") ||
        !readable(app, 0x950) ||
        read_value<std::uint32_t>(app, "app vtable") != kExpectedAppVtable)
        throw std::runtime_error("The active app does not match GOTY 1.2.0.1096.");
}

std::string player_name(std::uintptr_t player) {
    const auto length = read_value<std::uint32_t>(checked_add(player, kPlayerNameLength), "name length");
    const auto capacity = read_value<std::uint32_t>(checked_add(player, kPlayerNameCapacity), "name capacity");
    if (length == 0 || length > kMaximumNameLength || capacity < length || capacity > 4096)
        throw std::runtime_error("Invalid 1.2.0.1096 profile name bounds.");

    const auto storage = capacity < 16
        ? checked_add(player, kPlayerNameInline)
        : read_value<std::uint32_t>(checked_add(player, kPlayerNameInline), "name storage pointer");
    if (!readable(storage, static_cast<std::size_t>(length) + 1))
        throw std::runtime_error("Profile name storage is unreadable.");
    std::string name(reinterpret_cast<const char*>(storage), length);
    if (name.find('\0') != std::string::npos ||
        read_value<char>(storage + length, "name terminator") != '\0')
        throw std::runtime_error("Profile name is not a valid terminated string.");
    return name;
}

std::uintptr_t tree_successor(std::uintptr_t node, std::uintptr_t sentinel) {
    auto right = read_value<std::uint32_t>(checked_add(node, kTreeRight), "tree right link");
    if (right != sentinel) {
        if (!readable(right, kTreeNodeBytes)) throw std::runtime_error("Invalid profile-tree right link.");
        node = right;
        auto left = read_value<std::uint32_t>(checked_add(node, kTreeLeft), "tree left link");
        for (unsigned depth = 0; left != sentinel && depth < kMaximumUsers; ++depth) {
            if (!readable(left, kTreeNodeBytes)) throw std::runtime_error("Invalid profile-tree left link.");
            node = left;
            left = read_value<std::uint32_t>(checked_add(node, kTreeLeft), "tree left link");
        }
        if (left != sentinel) throw std::runtime_error("Profile-tree traversal exceeded its bound.");
        return node;
    }

    auto parent = read_value<std::uint32_t>(checked_add(node, kTreeParent), "tree parent link");
    for (unsigned depth = 0; parent != sentinel && depth < kMaximumUsers; ++depth) {
        if (!readable(parent, kTreeNodeBytes)) throw std::runtime_error("Invalid profile-tree parent link.");
        if (node != read_value<std::uint32_t>(checked_add(parent, kTreeRight), "tree right link"))
            return parent;
        node = parent;
        parent = read_value<std::uint32_t>(checked_add(node, kTreeParent), "tree parent link");
    }
    if (parent != sentinel) throw std::runtime_error("Profile-tree traversal exceeded its bound.");
    return sentinel;
}

std::vector<ProfileRecord> read_roster(std::uintptr_t app) {
    validate_app(app);
    const auto manager = read_value<std::uint32_t>(checked_add(app, kAppProfileManager), "manager pointer");
    if (!manager || !readable(manager, 0x24) ||
        read_value<std::uint32_t>(manager, "manager vtable") != kExpectedManagerVtable)
        throw std::runtime_error("ProfileMgr does not match the verified GOTY 1.2.0.1096 layout.");

    const auto expected_count = read_value<std::uint32_t>(checked_add(manager, kManagerTreeSize), "tree size");
    const auto sentinel = read_value<std::uint32_t>(checked_add(manager, kManagerTreeSentinel), "tree sentinel");
    if (expected_count > kMaximumUsers || !sentinel || !readable(sentinel, kTreeNodeBytes))
        throw std::runtime_error("ProfileMgr tree bounds are invalid.");

    std::vector<ProfileRecord> roster;
    roster.reserve(expected_count);
    auto node = read_value<std::uint32_t>(checked_add(sentinel, kTreeLeft), "tree begin node");
    const auto active = read_value<std::uint32_t>(checked_add(app, kAppActiveProfile), "active profile");
    std::unordered_set<std::uintptr_t> seen_nodes;
    std::unordered_set<std::uint32_t> seen_ids;
    std::set<std::string> seen_names;
    while (node != sentinel) {
        if (roster.size() >= kMaximumUsers || !seen_nodes.insert(node).second ||
            !readable(node, kTreeNodeBytes))
            throw std::runtime_error("ProfileMgr tree is cyclic, oversized, or unreadable.");
        const auto player = checked_add(node, kTreeValue);
        ProfileRecord record;
        record.address = player;
        record.id = read_value<std::uint32_t>(checked_add(player, kPlayerId), "stable profile ID");
        record.use_sequence = read_value<std::uint32_t>(checked_add(player, kPlayerUseSequence), "profile use sequence");
        record.name = player_name(player);
        record.active = player == active;
        if (!record.id || !seen_ids.insert(record.id).second || !seen_names.insert(record.name).second)
            throw std::runtime_error("ProfileMgr contains duplicate or invalid profile identity.");
        roster.push_back(std::move(record));
        node = tree_successor(node, sentinel);
    }
    if (roster.size() != expected_count || (active && std::none_of(roster.begin(), roster.end(),
            [active](const ProfileRecord& profile) { return profile.active; })))
        throw std::runtime_error("ProfileMgr roster and active-profile pointer do not agree.");
    return roster;
}

json roster_json(const std::vector<ProfileRecord>& roster, std::uintptr_t app) {
    json profiles = json::array();
    for (const auto& record : roster) {
        profiles.push_back({{"id", record.id}, {"name", record.name},
            {"use_sequence", record.use_sequence}, {"active", record.active}});
    }
    return {{"profiles", std::move(profiles)}, {"count", roster.size()},
        {"active_profile", read_value<std::uint32_t>(checked_add(app, kAppActiveProfile), "active profile")}};
}

const ProfileRecord& find_id(const std::vector<ProfileRecord>& roster, std::uint32_t id) {
    const auto found = std::find_if(roster.begin(), roster.end(),
        [id](const ProfileRecord& item) { return item.id == id; });
    if (found == roster.end()) throw std::runtime_error("Profile ID is not present in the current roster.");
    return *found;
}

bool valid_game_name(const std::string& name) {
    if (name.empty() || name.size() > kMaximumNameLength || name.front() == ' ' || name.back() == ' ')
        return false;
    bool previous_space = false;
    for (unsigned char ch : name) {
        const bool space = ch == ' ';
        if (space && previous_space) return false;
        if (!(space || (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z')))
            return false;
        previous_space = space;
    }
    return true;
}

std::string normalized_game_name(const std::string& name) {
    std::string normalized;
    normalized.reserve(name.size());
    bool pending_space = false;
    for (char ch : name) {
        if (ch == ' ') {
            pending_space = !normalized.empty();
        } else {
            if (pending_space) normalized.push_back(' ');
            normalized.push_back(ch);
            pending_space = false;
        }
    }
    return normalized;
}

struct TargetString {
    std::uint32_t reserved = 0;
    char inline_buffer[16]{};
    std::uint32_t length = 0;
    std::uint32_t capacity = 15;
};
static_assert(sizeof(TargetString) == 0x1c, "Target SexyString layout must remain 28 bytes.");

void construct_target_string(TargetString& target, const std::string& value) {
#if defined(_MSC_VER) && defined(_M_IX86)
    std::memset(&target, 0, sizeof(target));
    target.capacity = 15;
    auto* destination = &target;
    const auto* source = value.c_str();
    const auto count = static_cast<std::uint32_t>(value.size());
    const std::uintptr_t function = kTargetStringConstruct;
    __asm {
        mov ecx, destination
        push count
        push source
        call function
    }
#else
    (void)target;
    (void)value;
    throw std::runtime_error("Profile dialog text requires the verified MSVC x86 string ABI.");
#endif
}

void destroy_target_string(TargetString& target) {
#if defined(_MSC_VER) && defined(_M_IX86)
    auto* destination = &target;
    const std::uintptr_t function = kTargetStringDestroy;
    __asm {
        mov ecx, destination
        call function
    }
#else
    (void)target;
#endif
}

void call_edi_noarg(std::uintptr_t function, std::uintptr_t app) {
#if defined(_MSC_VER) && defined(_M_IX86)
    __asm {
        mov edi, app
        call function
    }
#else
    (void)function; (void)app;
    throw std::runtime_error("Profile UI callbacks require the verified MSVC x86 ABI.");
#endif
}

void call_edi_string(std::uintptr_t function, std::uintptr_t app, TargetString& value) {
#if defined(_MSC_VER) && defined(_M_IX86)
    auto* string = &value;
    __asm {
        mov edi, app
        push string
        call function
    }
#else
    (void)function; (void)app; (void)value;
    throw std::runtime_error("Profile UI callbacks require the verified MSVC x86 ABI.");
#endif
}

void call_this_string(std::uintptr_t function, std::uintptr_t app, TargetString& value) {
#if defined(_MSC_VER) && defined(_M_IX86)
    auto* string = &value;
    __asm {
        mov ecx, app
        push string
        call function
    }
#else
    (void)function; (void)app; (void)value;
    throw std::runtime_error("Profile UI callbacks require the verified MSVC x86 ABI.");
#endif
}

void invoke_app_button(std::uintptr_t app, int callback_id) {
    const auto vtable = read_value<std::uint32_t>(app, "app vtable");
    const auto callback = read_value<std::uint32_t>(checked_add(vtable, 0x08), "app ButtonDepress callback");
    if (callback != kExpectedAppButtonDepress)
        throw std::runtime_error("App ButtonDepress callback differs from the verified target.");
    using ButtonDepress = void(__thiscall*)(void*, int);
    reinterpret_cast<ButtonDepress>(callback)(reinterpret_cast<void*>(app), callback_id);
}

void invoke_list_clicked(std::uintptr_t dialog, int index, int click_count) {
#if defined(_MSC_VER) && defined(_M_IX86)
    const auto listener = checked_add(dialog, kUserDialogListListener);
    const auto vtable = read_value<std::uint32_t>(listener, "UserDialog list-listener vtable");
    if (vtable != kExpectedListListenerVtable ||
        read_value<std::uint32_t>(vtable, "ListClicked callback") != kExpectedListClicked)
        throw std::runtime_error("UserDialog ListClicked callback differs from the verified target.");
    const int widget_id = 0;
    const std::uintptr_t function = kExpectedListClicked;
    __asm {
        mov ecx, listener
        push click_count
        push index
        push widget_id
        call function
    }
#else
    (void)dialog; (void)index; (void)click_count;
    throw std::runtime_error("Profile list callbacks require the verified MSVC x86 ABI.");
#endif
}

std::string target_string_value(const TargetString& value, std::uint32_t maximum_length) {
    const auto length = value.length;
    const auto capacity = value.capacity;
    if (length > maximum_length || capacity < length || capacity > 4096)
        throw std::runtime_error("Selected profile text has invalid target-string bounds.");
    const auto object = reinterpret_cast<std::uintptr_t>(&value);
    const auto storage = capacity < 16
        ? checked_add(object, offsetof(TargetString, inline_buffer))
        : read_value<std::uint32_t>(checked_add(object, offsetof(TargetString, inline_buffer)), "target string storage");
    if (!readable(storage, static_cast<std::size_t>(length) + 1) ||
        read_value<char>(storage + length, "target string terminator") != '\0')
        throw std::runtime_error("Selected profile text is unreadable or unterminated.");
    std::string result(reinterpret_cast<const char*>(storage), length);
    if (result.find('\0') != std::string::npos)
        throw std::runtime_error("Selected profile text contains an embedded terminator.");
    return result;
}

std::string selected_user_dialog_name(std::uintptr_t dialog) {
#if defined(_MSC_VER) && defined(_M_IX86)
    TargetString selected{};
    selected.capacity = 15;
    selected.inline_buffer[0] = '\0';
    auto* output = &selected;
    auto* user_dialog = reinterpret_cast<void*>(dialog);
    const std::uintptr_t function = kExpectedGetSelectedName;
    __asm {
        mov ecx, user_dialog
        mov esi, output
        call function
    }
    try {
        const auto name = target_string_value(selected, kMaximumNameLength);
        destroy_target_string(selected);
        return name;
    } catch (...) {
        destroy_target_string(selected);
        throw;
    }
#else
    (void)dialog;
    throw std::runtime_error("Profile selection verification requires the verified MSVC x86 ABI.");
#endif
}

std::uintptr_t read_dialog(std::uintptr_t app, int id, std::uintptr_t expected_vtable);

void set_edit_text(std::uintptr_t app, int dialog_id, const std::string& value) {
    const auto dialog = read_dialog(app, dialog_id, kExpectedNewUserDialogVtable);
    const auto widget = read_value<std::uint32_t>(checked_add(dialog, kUserDialogList), "NewUserDialog edit widget");
    if (!widget || !readable(widget, 0x128) ||
        read_value<std::uint32_t>(widget, "EditWidget vtable") != kExpectedEditWidgetVtable)
        throw std::runtime_error("NewUserDialog edit widget does not match the verified target.");
    const auto vtable = read_value<std::uint32_t>(widget, "EditWidget vtable");
    const auto setter = read_value<std::uint32_t>(checked_add(vtable, 0x124), "EditWidget::SetText");
    if (setter != kExpectedEditSetText)
        throw std::runtime_error("EditWidget::SetText target differs from the verified executable.");
    TargetString text;
    construct_target_string(text, value);
    try {
        using SetText = void(__thiscall*)(void*, const TargetString*, bool);
        reinterpret_cast<SetText>(setter)(reinterpret_cast<void*>(widget), &text, true);
    } catch (...) {
        destroy_target_string(text);
        throw;
    }
    destroy_target_string(text);
}

std::uintptr_t app_dialog(std::uintptr_t app, int id) {
    const auto vtable = read_value<std::uint32_t>(app, "app vtable");
    const auto callback = read_value<std::uint32_t>(checked_add(vtable, kAppGetDialogSlot), "GetDialog callback");
    if (callback < 0x401000 || callback >= 0x6d0000)
        throw std::runtime_error("GetDialog callback is outside the verified executable.");
    using GetDialog = void*(__thiscall*)(void*, int);
    return reinterpret_cast<std::uintptr_t>(reinterpret_cast<GetDialog>(callback)(reinterpret_cast<void*>(app), id));
}

int app_dialog_count(std::uintptr_t app) {
    const auto vtable = read_value<std::uint32_t>(app, "app vtable");
    const auto callback = read_value<std::uint32_t>(checked_add(vtable, kAppGetDialogCountSlot), "dialog count callback");
    if (callback < 0x401000 || callback >= 0x6d0000)
        throw std::runtime_error("Dialog-count callback is outside the verified executable.");
    using GetCount = int(__thiscall*)(void*);
    const int count = reinterpret_cast<GetCount>(callback)(reinterpret_cast<void*>(app));
    if (count < 0 || count > 64) throw std::runtime_error("App dialog count is invalid.");
    return count;
}

std::uintptr_t read_dialog(std::uintptr_t app, int id, std::uintptr_t expected_vtable) {
    const auto dialog = app_dialog(app, id);
    if (!dialog || !readable(dialog, kDialogOwner + sizeof(std::uint32_t)))
        throw std::runtime_error("Profile dialog identity or owner does not match the verified target.");
    const auto vtable = read_value<std::uint32_t>(dialog, "dialog vtable");
    if ((expected_vtable && vtable != expected_vtable) || vtable < 0x6d5000 || vtable >= 0x730000 ||
        read_value<std::int32_t>(checked_add(dialog, kDialogId), "dialog ID") != id ||
        read_value<std::uint32_t>(checked_add(dialog, kDialogOwner), "dialog owner") != app)
        throw std::runtime_error("Profile dialog identity or owner does not match the verified target.");
    return dialog;
}

void require_main_menu_idle(std::uintptr_t app) {
    validate_app(app);
    if (read_value<std::int32_t>(checked_add(app, kAppScene), "game scene") != kSceneMainMenu ||
        app_dialog_count(app) != 0)
        throw std::runtime_error("Profile management requires an idle main menu with no dialogs open.");
}

const ProfileRecord& find_name(const std::vector<ProfileRecord>& roster, const std::string& name) {
    const auto found = std::find_if(roster.begin(), roster.end(),
        [&name](const ProfileRecord& item) { return item.name == name; });
    if (found == roster.end()) throw std::runtime_error("Profile name is not present in the current roster.");
    return *found;
}

void check_original_identities(const std::vector<ProfileRecord>& roster, std::uint32_t allowed_new_id) {
    for (const auto& baseline : g_guard.baseline) {
        const auto& current = find_id(roster, baseline.first);
        if (current.name != baseline.second)
            throw std::runtime_error("An original profile name changed; refusing the operation.");
    }
    for (const auto& profile : roster) {
        if (g_guard.baseline.count(profile.id) || g_guard.created.count(profile.id) ||
            profile.id == allowed_new_id) continue;
        if (profile.id == 1 || profile.id == 2)
            throw std::runtime_error("A protected original profile appeared outside the captured baseline.");
        throw std::runtime_error("An untracked profile appeared outside the captured baseline.");
    }
}

json job_json() {
    json result{{"job_id", g_job.id}, {"operation", g_job.kind == JobKind::open ? "profile_open" :
        g_job.kind == JobKind::select ? "profile_select" : g_job.kind == JobKind::create ? "profile_create" :
        g_job.kind == JobKind::rename ? "profile_rename" : g_job.kind == JobKind::erase ? "profile_delete" : "profile_close"},
        {"state", g_job.state}};
    if (!g_job.error.empty()) result["error"] = g_job.error;
    if (g_job.state == "complete" || g_job.state == "awaiting_confirmation")
        result["result"] = g_job.result;
    return result;
}

std::uint64_t checked_job_id(const json& value, const char* field) {
    if (value.is_number_unsigned()) return value.get<std::uint64_t>();
    if (!value.is_number_integer())
        throw std::runtime_error(std::string(field) + " must be a positive integer.");
    const auto signed_value = value.get<std::int64_t>();
    if (signed_value <= 0)
        throw std::runtime_error(std::string(field) + " must be a positive integer.");
    return static_cast<std::uint64_t>(signed_value);
}

void complete_job(json result = json::object()) {
    g_job.state = "complete";
    g_job.result = std::move(result);
}

void fail_job(const char* error) {
    g_job.state = "failed";
    g_job.error = error;
}

void require_initialized(std::uintptr_t app) {
    if (!g_guard.initialized || g_guard.app != app)
        throw std::runtime_error("Profile safety baseline is unavailable; refusing profile mutation.");
}

void check_original_identities(const std::vector<ProfileRecord>& roster,
                               std::uint32_t allowed_new_id = 0);

} // namespace

json snapshot(std::uintptr_t app) {
    const auto roster = read_roster(app);
    auto result = roster_json(roster, app);
    result["safe_mutations_initialized"] = g_guard.initialized && g_guard.app == app;
    result["created_test_ids"] = json::array();
    for (const auto& entry : g_guard.created)
        result["created_test_ids"].push_back({{"id", entry.first}, {"name", entry.second}});
    result["protected_ids"] = json::array();
    for (auto id : g_guard.protected_ids) result["protected_ids"].push_back(id);
    return result;
}

void initialize_guard(std::uintptr_t app, const std::vector<std::string>& protected_file_names) {
    const auto roster = read_roster(app);
    if (g_guard.initialized) {
        if (g_guard.app != app) throw std::runtime_error("Profile safety baseline belongs to another app instance.");
        for (const auto& name : protected_file_names) {
            if (valid_game_name(name)) g_guard.protected_names.insert(normalized_game_name(name));
        }
        verify_baseline(app);
        return;
    }

    g_guard.app = app;
    g_guard.baseline.clear();
    g_guard.protected_names.clear();
    g_guard.created.clear();
    g_guard.protected_ids = {1, 2};
    for (const auto& profile : roster) {
        g_guard.baseline.emplace(profile.id, profile.name);
        g_guard.protected_ids.insert(profile.id);
        g_guard.protected_names.insert(profile.name);
    }
    for (const auto& name : protected_file_names) {
        if (valid_game_name(name)) g_guard.protected_names.insert(normalized_game_name(name));
    }
    g_guard.initialized = true;
}

void verify_baseline(std::uintptr_t app) {
    require_initialized(app);
    const auto roster = read_roster(app);
    check_original_identities(roster);
}

void authorize_create(std::uintptr_t app, const std::string& name) {
    require_initialized(app);
    verify_baseline(app);
    if (!valid_game_name(name)) throw std::runtime_error("Profile name must be 1-12 letters, digits, or single spaces.");
    const auto canonical = normalized_game_name(name);
    const auto roster = read_roster(app);
    if (roster.size() >= kMaximumUsers)
        throw std::runtime_error("Profile creation is disabled at the eight-profile limit to prevent game eviction.");
    if (g_guard.protected_names.count(canonical))
        throw std::runtime_error("A protected original profile name cannot be reused.");
    if (std::any_of(roster.begin(), roster.end(), [&canonical](const ProfileRecord& p) { return p.name == canonical; }))
        throw std::runtime_error("A profile with that name already exists.");
}

void record_created(std::uintptr_t app, std::uint32_t id) {
    require_initialized(app);
    const auto roster = read_roster(app);
    check_original_identities(roster, id);
    const auto& profile = find_id(roster, id);
    if (g_guard.protected_ids.count(id) || g_guard.protected_names.count(profile.name) ||
        g_guard.baseline.count(id) || g_guard.created.count(id))
        throw std::runtime_error("New profile identity collides with a protected or previously tracked profile.");
    g_guard.created.emplace(id, profile.name);
}

void authorize_test_profile(std::uintptr_t app, std::uint32_t id, const std::string& operation) {
    require_initialized(app);
    verify_baseline(app);
    if (operation != "rename" && operation != "delete")
        throw std::runtime_error("Unsupported profile mutation.");
    if (!g_guard.created.count(id) || g_guard.protected_ids.count(id))
        throw std::runtime_error("Only a test profile created during this bridge session can be changed.");
    const auto roster = read_roster(app);
    const auto& profile = find_id(roster, id);
    if (profile.name != g_guard.created.at(id))
        throw std::runtime_error("Tracked test profile name changed outside the guarded profile API.");
}

void verify_rename(std::uintptr_t app, std::uint32_t id, const std::string& new_name) {
    require_initialized(app);
    verify_baseline(app);
    if (!g_guard.created.count(id) || g_guard.protected_ids.count(id))
        throw std::runtime_error("Only a test profile created during this bridge session can be renamed.");
    if (!valid_game_name(new_name)) throw std::runtime_error("Profile name must be 1-12 letters, digits, or single spaces.");
    const auto canonical = normalized_game_name(new_name);
    const auto roster = read_roster(app);
    const auto& renamed = find_id(roster, id);
    if (renamed.name != canonical)
        throw std::runtime_error("Normal rename completion did not preserve the requested profile identity.");
    if (g_guard.protected_names.count(canonical))
        throw std::runtime_error("A test profile cannot be renamed to an original profile name.");
    if (std::any_of(roster.begin(), roster.end(), [id, &canonical](const ProfileRecord& profile) {
            return profile.id != id && profile.name == canonical;
        }))
        throw std::runtime_error("Renamed profile name is not unique.");
    g_guard.created[id] = canonical;
}

void verify_delete(std::uintptr_t app, std::uint32_t id) {
    require_initialized(app);
    verify_baseline(app);
    if (!g_guard.created.count(id) || g_guard.protected_ids.count(id))
        throw std::runtime_error("Only a test profile created during this bridge session can be deleted.");
    const auto roster = read_roster(app);
    if (std::any_of(roster.begin(), roster.end(), [id](const ProfileRecord& profile) { return profile.id == id; }))
        throw std::runtime_error("Normal delete completion left the test profile in the roster.");
    for (const auto& baseline : g_guard.baseline) {
        const auto& original = find_id(roster, baseline.first);
        if (original.name != baseline.second)
            throw std::runtime_error("An original profile changed during test-profile deletion.");
    }
    g_guard.created.erase(id);
}

namespace {

void validate_user_dialog(std::uintptr_t app, std::uintptr_t& dialog,
                          const std::vector<ProfileRecord>& roster) {
    dialog = read_dialog(app, kUserDialogId, kExpectedUserDialogVtable);
    if (!readable(checked_add(dialog, kUserDialogList), sizeof(std::uint32_t)) ||
        !readable(checked_add(dialog, kUserDialogListListener), sizeof(std::uint32_t)))
        throw std::runtime_error("UserDialog list widgets are unreadable.");
    const auto list = read_value<std::uint32_t>(checked_add(dialog, kUserDialogList), "UserDialog list widget");
    const auto listener_vtable = read_value<std::uint32_t>(checked_add(dialog, kUserDialogListListener), "UserDialog list listener");
    const auto user_count = read_value<std::int32_t>(checked_add(dialog, 0x1a0), "UserDialog user count");
    if (!list || !readable(list, 0x10) || listener_vtable != kExpectedListListenerVtable ||
        user_count < 0 || static_cast<std::size_t>(user_count) != roster.size())
        throw std::runtime_error("UserDialog roster/list state differs from the verified profile tree.");
}

std::size_t select_verified_user_row(std::uintptr_t app, std::uintptr_t dialog,
                                     const std::vector<ProfileRecord>& roster,
                                     std::uint32_t target_id, const std::string& target_name) {
    const auto& target = find_id(roster, target_id);
    if (target.name != target_name)
        throw std::runtime_error("Profile identity changed before resolving its UserDialog row.");
    for (std::size_t index = 0; index < roster.size(); ++index) {
        invoke_list_clicked(dialog, static_cast<int>(index), 1);
        const auto selected_name = selected_user_dialog_name(dialog);
        const auto& selected = find_name(roster, selected_name);
        if (selected.name == target_name && selected.id == target_id) {
            const auto verify_name = selected_user_dialog_name(dialog);
            const auto& verified = find_name(roster, verify_name);
            if (verified.id != target_id || verified.name != target_name)
                throw std::runtime_error("UserDialog selection changed during stable-ID verification.");
            return index;
        }
    }
    throw std::runtime_error("Could not resolve the requested stable profile ID to an actual UserDialog row.");
}

void verify_actual_delete_target(std::uintptr_t app, std::uint32_t target_id,
                                 const std::string& target_name,
                                 const std::string& confirmation_name) {
    authorize_test_profile(app, target_id, "delete");
    std::uintptr_t dialog = 0;
    const auto roster = read_roster(app);
    validate_user_dialog(app, dialog, roster);
    const auto selected_name = selected_user_dialog_name(dialog);
    const auto& selected = find_name(roster, selected_name);
    const auto& target = find_id(roster, target_id);
    if (selected.id != target_id || selected.name != target_name ||
        target.name != target_name || confirmation_name != selected_name)
        throw std::runtime_error("Delete confirmation target does not match the actual selected stable profile ID and name.");
    verify_baseline(app);
    authorize_test_profile(app, target_id, "delete");
}

void verify_actual_rename_target(std::uintptr_t app, std::uint32_t target_id,
                                 const std::string& target_name) {
    authorize_test_profile(app, target_id, "rename");
    std::uintptr_t dialog = 0;
    const auto roster = read_roster(app);
    validate_user_dialog(app, dialog, roster);
    const auto selected_name = selected_user_dialog_name(dialog);
    const auto& selected = find_name(roster, selected_name);
    if (selected.id != target_id || selected.name != target_name)
        throw std::runtime_error("Rename dialog target does not match the actual selected stable profile ID and name.");
    verify_baseline(app);
    authorize_test_profile(app, target_id, "rename");
}

void schedule_job(std::uintptr_t app, JobKind kind, const json& args) {
    validate_app(app);
    const auto scene = read_value<std::int32_t>(checked_add(app, kAppScene), "game scene");
    if (scene != kSceneMainMenu)
        throw std::runtime_error("Profile management is available only from the main menu.");

    const auto roster = read_roster(app);
    PendingJob job;
    job.id = g_next_job_id++;
    if (!job.id) job.id = g_next_job_id++;
    job.app = app;
    job.kind = kind;
    job.phase = JobPhase::open_user;
    job.state = "pending";

    if (kind == JobKind::select) {
        require_initialized(app);
        verify_baseline(app);
        if (!args.contains("name") || !args["name"].is_string())
            throw std::runtime_error("profile_select requires a profile name.");
        const auto requested = args["name"].get<std::string>();
        if (!valid_game_name(requested)) throw std::runtime_error("Invalid profile name.");
        job.target_name = normalized_game_name(requested);
        const auto& profile = find_name(roster, job.target_name);
        job.target_id = profile.id;
    } else if (kind == JobKind::create) {
        if (!args.contains("name") || !args["name"].is_string())
            throw std::runtime_error("profile_create requires a profile name.");
        job.new_name = args["name"].get<std::string>();
        authorize_create(app, job.new_name);
        job.target_name = normalized_game_name(job.new_name);
    } else if (kind == JobKind::rename) {
        if (!args.contains("id") || (!args["id"].is_number_unsigned() && !args["id"].is_number_integer()) ||
            !args.contains("name") || !args["name"].is_string())
            throw std::runtime_error("profile_rename requires an integer id and string name.");
        const auto signed_id = args["id"].get<std::int64_t>();
        if (signed_id <= 0 || signed_id > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("Invalid profile ID.");
        job.target_id = static_cast<std::uint32_t>(signed_id);
        job.new_name = args["name"].get<std::string>();
        authorize_test_profile(app, job.target_id, "rename");
        if (!valid_game_name(job.new_name))
            throw std::runtime_error("Profile name must be 1-12 letters, digits, or spaces.");
        job.target_name = g_guard.created.at(job.target_id);
        (void)find_id(roster, job.target_id);
        const auto canonical = normalized_game_name(job.new_name);
        if (g_guard.protected_names.count(canonical) ||
            std::any_of(roster.begin(), roster.end(), [&canonical, id = job.target_id](const ProfileRecord& p) {
                return p.id != id && p.name == canonical;
            }))
            throw std::runtime_error("A profile with that name already exists or is protected.");
    } else if (kind == JobKind::erase) {
        if (!args.contains("id") || (!args["id"].is_number_unsigned() && !args["id"].is_number_integer()))
            throw std::runtime_error("profile_delete requires an integer profile id.");
        const auto signed_id = args["id"].get<std::int64_t>();
        if (signed_id <= 0 || signed_id > std::numeric_limits<std::uint32_t>::max())
            throw std::runtime_error("Invalid profile ID.");
        job.target_id = static_cast<std::uint32_t>(signed_id);
        authorize_test_profile(app, job.target_id, "delete");
        const auto& profile = find_id(roster, job.target_id);
        job.target_name = profile.name;
    }

    const int dialogs = app_dialog_count(app);
    if (dialogs == 1 && app_dialog(app, kUserDialogId)) {
        std::uintptr_t existing = 0;
        validate_user_dialog(app, existing, roster);
        job.phase = JobPhase::wait_user;
    } else if (dialogs != 0) {
        throw std::runtime_error("Profile management requires no dialogs, or the existing UserDialog only.");
    }
    if (kind == JobKind::open && job.phase == JobPhase::wait_user) {
        job.state = "complete";
        job.result = {{"dialog_id", kUserDialogId}, {"opened", true}};
    }
    g_job = std::move(job);
}

void schedule_close(std::uintptr_t app, const json& args) {
    validate_app(app);
    if (!args.contains("dialog_id") || !args["dialog_id"].is_number_integer())
        throw std::runtime_error("profile_close requires dialog_id 29, 30, 31, or 32.");
    const int id = args["dialog_id"].get<int>();
    if (id < kUserDialogId || id > kRenameDialogId)
        throw std::runtime_error("Only profile dialogs 29 through 32 can be closed through this action.");
    const auto dialog = read_dialog(app, id, id == kUserDialogId ? kExpectedUserDialogVtable :
        (id == kCreateDialogId || id == kRenameDialogId) ? kExpectedNewUserDialogVtable : 0);
    (void)dialog;
    if (g_job.state == "pending") throw std::runtime_error("A profile dialog job is already pending.");
    PendingJob job;
    job.id = g_next_job_id++;
    if (!job.id) job.id = g_next_job_id++;
    job.app = app;
    job.kind = JobKind::close;
    job.phase = JobPhase::commit;
    job.state = "pending";
    job.target_dialog_id = id;
    g_job = std::move(job);
}

void advance_job(std::uintptr_t app) {
    if (g_job.state != "pending") return;
    if (app != g_job.app) throw std::runtime_error("Pending profile job belongs to a different app instance.");
    validate_app(app);

    switch (g_job.phase) {
    case JobPhase::open_user: {
        require_main_menu_idle(app);
        g_job.phase = JobPhase::wait_user;
        call_edi_noarg(kDoUserDialog, app);
        return;
    }
    case JobPhase::wait_user: {
        const auto roster = read_roster(app);
        std::uintptr_t dialog = 0;
        validate_user_dialog(app, dialog, roster);
        if (g_job.kind == JobKind::open) {
            complete_job({{"dialog_id", kUserDialogId}, {"opened", true}});
            return;
        }
        if (g_job.kind == JobKind::select) {
            const auto& target = find_id(roster, g_job.target_id);
            if (target.name != g_job.target_name)
                throw std::runtime_error("Selected profile identity changed while the dialog was opening.");
            g_job.target_index = select_verified_user_row(app, dialog, roster,
                                                          g_job.target_id, g_job.target_name);
            g_job.phase = JobPhase::verify;
            invoke_list_clicked(dialog, static_cast<int>(g_job.target_index), 2);
            return;
        }
        if (g_job.kind == JobKind::create) {
            authorize_create(app, g_job.new_name);
            g_job.phase = JobPhase::wait_editor;
            invoke_list_clicked(dialog, static_cast<int>(roster.size()), 1);
            return;
        }
        if (g_job.kind == JobKind::rename || g_job.kind == JobKind::erase) {
            const char* action = g_job.kind == JobKind::rename ? "rename" : "delete";
            authorize_test_profile(app, g_job.target_id, action);
            const auto& target = find_id(roster, g_job.target_id);
            if (target.name != g_job.target_name)
                throw std::runtime_error("Target profile identity changed while the dialog was opening.");
            g_job.target_index = select_verified_user_row(app, dialog, roster,
                                                          g_job.target_id, g_job.target_name);
            const auto selected_name = selected_user_dialog_name(dialog);
            const auto& actual = find_name(roster, selected_name);
            if (actual.id != g_job.target_id || actual.name != g_job.target_name)
                throw std::runtime_error("UserDialog row did not resolve to the requested stable profile identity.");
            if (g_job.kind == JobKind::erase) g_job.confirmation_name = selected_name;
            g_job.phase = g_job.kind == JobKind::rename ? JobPhase::wait_editor : JobPhase::wait_delete;
            TargetString name;
            construct_target_string(name, selected_name);
            try {
                invoke_list_clicked(dialog, static_cast<int>(g_job.target_index), 1);
                const auto after_click = selected_user_dialog_name(dialog);
                const auto& clicked = find_name(roster, after_click);
                if (clicked.id != g_job.target_id || clicked.name != g_job.target_name)
                    throw std::runtime_error("UserDialog selection changed before opening the mutation dialog.");
                if (g_job.kind == JobKind::rename)
                    call_edi_string(kDoRenameUserDialog, app, name);
                else
                    call_this_string(kDoConfirmDeleteUserDialog, app, name);
            } catch (...) {
                destroy_target_string(name);
                throw;
            }
            destroy_target_string(name);
            return;
        }
        throw std::runtime_error("Unknown profile job stage.");
    }
    case JobPhase::wait_editor: {
        const int dialog_id = g_job.kind == JobKind::create ? kCreateDialogId : kRenameDialogId;
        read_dialog(app, dialog_id, kExpectedNewUserDialogVtable);
        if (g_job.kind == JobKind::rename)
            authorize_test_profile(app, g_job.target_id, "rename");
        set_edit_text(app, dialog_id, g_job.new_name);
        g_job.phase = JobPhase::commit;
        return;
    }
    case JobPhase::wait_delete:
        read_dialog(app, kDeleteConfirmDialogId, 0);
        verify_actual_delete_target(app, g_job.target_id, g_job.target_name, g_job.confirmation_name);
        g_job.state = "awaiting_confirmation";
        g_job.result = {{"target_id", g_job.target_id}, {"target_name", g_job.target_name},
            {"confirmation_name", g_job.confirmation_name}, {"confirm_operation", "profile_delete_confirm"},
            {"originals_protected", true}};
        return;
    case JobPhase::commit:
        if (g_job.kind == JobKind::close) {
            read_dialog(app, g_job.target_dialog_id,
                g_job.target_dialog_id == kUserDialogId ? kExpectedUserDialogVtable :
                (g_job.target_dialog_id == kCreateDialogId || g_job.target_dialog_id == kRenameDialogId) ? kExpectedNewUserDialogVtable : 0);
            g_job.phase = JobPhase::verify;
            invoke_app_button(app, 3000 + g_job.target_dialog_id);
        } else if (g_job.kind == JobKind::create) {
            authorize_create(app, g_job.new_name);
            read_dialog(app, kCreateDialogId, kExpectedNewUserDialogVtable);
            g_job.phase = JobPhase::verify;
            invoke_app_button(app, 2000 + kCreateDialogId);
        } else if (g_job.kind == JobKind::rename) {
            authorize_test_profile(app, g_job.target_id, "rename");
            verify_actual_rename_target(app, g_job.target_id, g_job.target_name);
            read_dialog(app, kRenameDialogId, kExpectedNewUserDialogVtable);
            g_job.phase = JobPhase::verify;
            invoke_app_button(app, 2000 + kRenameDialogId);
        } else if (g_job.kind == JobKind::erase) {
            read_dialog(app, kDeleteConfirmDialogId, 0);
            verify_actual_delete_target(app, g_job.target_id, g_job.target_name, g_job.confirmation_name);
            g_job.phase = JobPhase::verify;
            invoke_app_button(app, 2000 + kDeleteConfirmDialogId);
        } else {
            throw std::runtime_error("Unexpected profile completion stage.");
        }
        return;
    case JobPhase::verify:
        if (g_job.kind == JobKind::close) {
            if (app_dialog(app, g_job.target_dialog_id))
                throw std::runtime_error("Normal profile-dialog cancel callback did not close the dialog.");
            complete_job({{"dialog_id", g_job.target_dialog_id}, {"closed", true}});
            return;
        }
        if (g_job.kind == JobKind::create && app_dialog(app, kCreateErrorDialogId)) {
            invoke_app_button(app, 2000 + kCreateErrorDialogId);
            invoke_app_button(app, 3000 + kCreateDialogId);
            fail_job("The normal create dialog rejected the profile name.");
            return;
        }
        if (g_job.kind == JobKind::rename && app_dialog(app, kRenameErrorDialogId)) {
            invoke_app_button(app, 2000 + kRenameErrorDialogId);
            invoke_app_button(app, 3000 + kRenameDialogId);
            fail_job("The normal rename dialog rejected the profile name.");
            return;
        }
        if (g_job.kind == JobKind::select) {
            verify_baseline(app);
            const auto active = read_value<std::uint32_t>(checked_add(app, kAppActiveProfile), "active profile");
            if (!active || active != find_id(read_roster(app), g_job.target_id).address)
                throw std::runtime_error("Normal selection callback did not activate the requested profile.");
            complete_job({{"selected_id", g_job.target_id}, {"name", g_job.target_name}, {"active", true}});
        } else if (g_job.kind == JobKind::create) {
            const auto roster = read_roster(app);
            const auto& created = find_name(roster, normalized_game_name(g_job.new_name));
            record_created(app, created.id);
            complete_job({{"created_id", created.id}, {"name", created.name}, {"active", created.active}});
        } else if (g_job.kind == JobKind::rename) {
            verify_rename(app, g_job.target_id, g_job.new_name);
            complete_job({{"renamed_id", g_job.target_id},
                {"name", g_guard.created.at(g_job.target_id)}});
        } else if (g_job.kind == JobKind::erase) {
            verify_delete(app, g_job.target_id);
            complete_job({{"deleted_id", g_job.target_id}});
        } else {
            throw std::runtime_error("Unexpected profile verification stage.");
        }
        return;
    }
}

} // namespace

json dispatch(std::uintptr_t app, const std::string& operation, const json& args) {
    if (operation == "profile_list") return snapshot(app);
    if (operation == "profile_job") {
        if (g_job.id == 0 || (args.contains("job_id") && checked_job_id(args["job_id"], "job_id") != g_job.id))
            throw std::runtime_error("Profile job ID is not available.");
        return job_json();
    }
    if (operation == "profile_delete_confirm") {
        if (g_job.kind != JobKind::erase || g_job.state != "awaiting_confirmation")
            throw std::runtime_error("No profile deletion is awaiting confirmation.");
        if (!args.contains("job_id") || checked_job_id(args["job_id"], "job_id") != g_job.id)
            throw std::runtime_error("Delete confirmation job ID does not match the verified target.");
        if (!args.contains("confirm") || !args["confirm"].is_boolean() || !args["confirm"].get<bool>())
            throw std::runtime_error("Deletion confirmation must be explicitly true; use profile_close to cancel.");
        g_job.state = "pending";
        g_job.phase = JobPhase::commit;
        return job_json();
    }
    if (g_job.state == "awaiting_confirmation" && operation != "profile_close")
        throw std::runtime_error("A profile deletion is awaiting explicit confirmation or cancellation.");
    if (g_job.state == "pending") throw std::runtime_error("A profile dialog job is already pending.");

    if (operation == "profile_open") schedule_job(app, JobKind::open, args);
    else if (operation == "profile_select") schedule_job(app, JobKind::select, args);
    else if (operation == "profile_create") schedule_job(app, JobKind::create, args);
    else if (operation == "profile_rename") schedule_job(app, JobKind::rename, args);
    else if (operation == "profile_delete") schedule_job(app, JobKind::erase, args);
    else if (operation == "profile_close") schedule_close(app, args);
    else throw std::runtime_error("Unknown profile operation.");
    return job_json();
}

void tick(std::uintptr_t app) {
    if (g_job.state != "pending" || g_ticking) return;
    g_ticking = true;
    try {
        advance_job(app);
    } catch (const std::exception& error) {
        g_job.state = "failed";
        g_job.error = error.what();
    } catch (...) {
        g_job.state = "failed";
        g_job.error = "Unknown failure during the normal profile dialog flow.";
    }
    g_ticking = false;
}

} // namespace pvz::profiles
