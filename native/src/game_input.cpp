#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>

#include "game_input.hpp"

namespace pvz::game_input {
namespace {
using json = nlohmann::json;

constexpr std::uintptr_t kImageBase = 0x400000;
constexpr std::uintptr_t kGlobalApp = 0x731c50;
constexpr std::uintptr_t kAppVtable = 0x6f19e0;
constexpr std::uintptr_t kAppGetDialogCountSlot = 0x13c;
constexpr std::uintptr_t kGetDialogCount = 0x5ce590;
constexpr std::uintptr_t kBoardOffset = 0x868;
constexpr std::uintptr_t kChooserOffset = 0x874;
constexpr std::uintptr_t kSceneOffset = 0x91c;
constexpr std::uintptr_t kModeOffset = 0x918;
constexpr std::uintptr_t kManagerOffset = 0x320;
constexpr std::uintptr_t kWidgetManagerVtable = 0x6fc114;

constexpr std::uintptr_t kMouseUp = 0x54f8c0;
constexpr std::uintptr_t kMouseDown = 0x54f9d0;
constexpr std::uintptr_t kMouseMove = 0x54fb90;
// Target evidence: WidgetManager vtable 0x6FC114, verified from the live 1096
// object and docs/research-widget-pointer.md. MouseDown/Up are __thiscall
// (ECX=manager, stack x/y/button, bool AL, ret 0xC); MouseMove is __thiscall
// (ECX=manager, stack x/y, bool AL, ret 8). Prologues at 0x54F9D0,
// 0x54F8C0, and 0x54FB90 use those exact stack shapes. At MouseDown's
// 0x54F9E8..0x54FA03 and 0x54FAB0..0x54FAD8, signed button<0 sets right
// button bit 0x2; MouseUp at 0x54F8D8..0x54F914 clears that same bit.
// Semantic confirmation: pinned Electr0Gunner/PvZ-Reconstruction-BugFix
// commit da58098267fb4eda37282daaff207bbac2c9a1d3,
// SexyAppFramework/WidgetManager.cpp:570-600,605-668 names negative
// clickCount as right mouse input. Lawn/Board.cpp:3683-3691 routes the
// negative plant click through RefreshSeedPacketFromCursor; lines 4095-4101
// clear the tool cursor and play its drop sound for the negative tool click.
constexpr std::size_t kWidgetManagerHover = 0xa8;
constexpr std::size_t kWidgetManagerDownWidget = 0xa4;
constexpr std::size_t kWidgetManagerMouseX = 0x108;
constexpr std::size_t kWidgetManagerMouseY = 0x10c;
constexpr std::size_t kWidgetManagerDownButtons = 0x110;
constexpr std::size_t kWidgetManagerOwner = 0x20;
constexpr std::size_t kWidgetParent = 0x24;
constexpr std::size_t kBoardCursor = 0x150;
constexpr std::size_t kCursorType = 0x30;
constexpr int kSceneLevelIntro = 2;
constexpr int kScenePlaying = 3;
constexpr int kModeZenGarden = 43;
constexpr int kModeTreeOfWisdom = 50;
constexpr int kMaxScreenX = 800;
constexpr int kMaxScreenY = 600;
constexpr unsigned kMaxParentDepth = 32;

enum class ButtonKind { left, right };

struct Context {
    std::uintptr_t manager = 0;
    std::uintptr_t board = 0;
    std::uintptr_t root = 0;
    int scene = -1;
    int mode = -1;
};

struct PendingJob {
    std::string id;
    std::string scope;
    std::string surface;
    int x = 0;
    int y = 0;
    int initial_pointer_x = 0;
    int initial_pointer_y = 0;
    ButtonKind button = ButtonKind::left;
    std::uintptr_t board = 0;
    std::uintptr_t root = 0;
    std::uintptr_t manager = 0;
};

std::mutex g_mutex;
std::optional<PendingJob> g_pending;
bool g_running = false;
json g_last_result = nullptr;
std::atomic<std::uint64_t> g_next_job_id{1};

std::uintptr_t add(std::uintptr_t base, std::size_t offset) {
    if (base < 0x10000 || offset > UINTPTR_MAX - base)
        throw std::runtime_error("Invalid game-input address.");
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
        throw std::runtime_error(std::string("Unreadable game-input field: ") + field);
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
}

void require_main_thread(std::uint32_t thread_id) {
    if (!thread_id || GetCurrentThreadId() != thread_id)
        throw std::runtime_error("Game input is only available on the verified update thread.");
}

void validate_app(std::uintptr_t app) {
    if (reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) != kImageBase ||
        app != read<std::uint32_t>(kGlobalApp, "global LawnApp") ||
        !readable(app, 0x960) || read<std::uint32_t>(app, "LawnApp vtable") != kAppVtable)
        throw std::runtime_error("The active app does not match Steam GOTY 1.2.0.1096.");
}

int dialog_count(std::uintptr_t app) {
    if (read<std::uint32_t>(app, "LawnApp vtable") != kAppVtable ||
        read<std::uint32_t>(add(kAppVtable, kAppGetDialogCountSlot), "GetDialogCount slot") != kGetDialogCount)
        throw std::runtime_error("LawnApp GetDialogCount does not match the verified 1096 target.");
    using GetDialogCount = int(__thiscall*)(void*);
    const int count = reinterpret_cast<GetDialogCount>(kGetDialogCount)(reinterpret_cast<void*>(app));
    if (count < 0 || count > 64) throw std::runtime_error("Dialog count is outside the verified bounds.");
    return count;
}

Context validate_context(std::uintptr_t app, const std::string& scope,
                         const std::string& surface) {
    if (scope.empty() || scope.size() > 128)
        throw std::runtime_error("A current board-generation scope is required.");
    validate_app(app);
    if (dialog_count(app) != 0)
        throw std::runtime_error("Board input is blocked while any game dialog is open.");
    Context context{};
    context.scene = read<std::int32_t>(add(app, kSceneOffset), "game scene");
    context.mode = read<std::int32_t>(add(app, kModeOffset), "game mode");
    if (context.mode == kModeZenGarden || context.mode == kModeTreeOfWisdom)
        throw std::runtime_error("Generic board input is blocked in Zen Garden and Tree of Wisdom.");
    context.board = read<std::uint32_t>(add(app, kBoardOffset), "LawnApp Board");
    if (!context.board || !readable(context.board, 0x200))
        throw std::runtime_error("An active readable Board is required.");
    if (context.scene != kSceneLevelIntro && context.scene != kScenePlaying)
        throw std::runtime_error("Generic board input requires scene 2 or 3.");
    if (surface == "board") {
        if (context.scene != kScenePlaying)
            throw std::runtime_error("The board surface is available only in scene 3.");
        if (read<std::uint8_t>(add(context.board, 0x17c), "Board paused"))
            throw std::runtime_error("Resume the game before sending board input.");
        context.root = context.board;
    } else if (surface == "seed_chooser") {
        if (context.scene != kSceneLevelIntro)
            throw std::runtime_error("The seed chooser surface is available only in scene 2.");
        context.root = read<std::uint32_t>(add(app, kChooserOffset), "LawnApp SeedChooser");
        if (!context.root || !readable(context.root, 0x100))
            throw std::runtime_error("An active readable SeedChooser is required.");
    } else {
        throw std::runtime_error("Surface must be 'board' or 'seed_chooser'.");
    }
    context.manager = read<std::uint32_t>(add(app, kManagerOffset), "LawnApp WidgetManager");
    if (!context.manager || !readable(context.manager, 0x220) ||
        read<std::uint32_t>(context.manager, "WidgetManager vtable") != kWidgetManagerVtable)
        throw std::runtime_error("WidgetManager does not match the verified 1096 object.");
    if (read<std::uint32_t>(add(context.root, kWidgetManagerOwner), "surface WidgetManager") != context.manager)
        throw std::runtime_error("Selected surface is not attached to the active WidgetManager.");
    return context;
}

bool target_belongs_to_surface(std::uintptr_t target, std::uintptr_t root,
                               std::uintptr_t manager) {
    std::unordered_set<std::uintptr_t> visited;
    auto current = target;
    for (unsigned depth = 0; current && depth <= kMaxParentDepth; ++depth) {
        if (current == root) return true;
        if (!visited.insert(current).second || !readable(current, 0x28)) return false;
        if (read<std::uint32_t>(add(current, kWidgetManagerOwner), "target WidgetManager") != manager)
            return false;
        current = read<std::uint32_t>(add(current, kWidgetParent), "target parent Widget");
    }
    return false;
}

int button_argument(ButtonKind button) {
    // Normal framework convention: left=0; right=-1 (verified by the pinned
    // SexyAppFramework WidgetManager handler and target 1096 branch behavior).
    return button == ButtonKind::left ? 0 : -1;
}

std::uint32_t button_mask(ButtonKind button) {
    return button == ButtonKind::left ? 1u : 2u;
}

bool move_pointer(std::uintptr_t manager, int x, int y) {
    using MouseMove = bool(__thiscall*)(void*, int, int);
    return reinterpret_cast<MouseMove>(kMouseMove)(reinterpret_cast<void*>(manager), x, y);
}

void mouse_down(std::uintptr_t manager, int x, int y, int button) {
    using MouseButton = bool(__thiscall*)(void*, int, int, int);
    reinterpret_cast<MouseButton>(kMouseDown)(reinterpret_cast<void*>(manager), x, y, button);
}

void mouse_up(std::uintptr_t manager, int x, int y, int button) noexcept {
    using MouseButton = bool(__thiscall*)(void*, int, int, int);
    reinterpret_cast<MouseButton>(kMouseUp)(reinterpret_cast<void*>(manager), x, y, button);
}

void release_if_held(std::uintptr_t manager, int x, int y, int button) noexcept {
    try {
        if (!manager) return;
        const auto held = read<std::uint32_t>(add(manager, kWidgetManagerDownButtons), "held mouse buttons");
        if (held & (button < 0 ? 2u : 1u)) mouse_up(manager, x, y, button);
    } catch (...) {
        // Keep cleanup faults contained; the queued tick reports the primary error.
    }
}

struct ReleaseOnExit {
    std::uintptr_t manager;
    int x;
    int y;
    int button;
    bool armed = false;
    ~ReleaseOnExit() { if (armed) release_if_held(manager, x, y, button); }
};

void revalidate_queued_context(std::uintptr_t app, const PendingJob& job,
                               const std::string& scope) {
    if (scope != job.scope) throw std::runtime_error("Board-generation scope changed before input.");
    const auto context = validate_context(app, scope, job.surface);
    if (context.board != job.board || context.root != job.root || context.manager != job.manager)
        throw std::runtime_error("The active Board, surface, or WidgetManager changed before input.");
}

void perform_click(std::uintptr_t app, const PendingJob& job, const std::string& scope) {
    revalidate_queued_context(app, job, scope);
    const auto context = validate_context(app, scope, job.surface);
    const auto held = read<std::uint32_t>(add(context.manager, kWidgetManagerDownButtons), "held mouse buttons");
    if (held != 0) throw std::runtime_error("Refusing to interfere with a held mouse button.");
    const int old_x = read<std::int32_t>(add(context.manager, kWidgetManagerMouseX), "current mouse x");
    const int old_y = read<std::int32_t>(add(context.manager, kWidgetManagerMouseY), "current mouse y");
    if (old_x != job.initial_pointer_x || old_y != job.initial_pointer_y)
        throw std::runtime_error("Pointer position changed while the board action was queued.");

    if (!move_pointer(context.manager, job.x, job.y))
        throw std::runtime_error("WidgetManager rejected the requested pointer movement.");
    revalidate_queued_context(app, job, scope);
    if (read<std::uint32_t>(add(context.manager, kWidgetManagerDownButtons), "held mouse buttons") != 0)
        throw std::runtime_error("A mouse button became held during pointer movement.");
    const auto hover = read<std::uint32_t>(add(context.manager, kWidgetManagerHover), "hover widget");
    if (!hover || !target_belongs_to_surface(hover, context.root, context.manager))
        throw std::runtime_error("Normal hit testing did not select the requested Board/Chooser surface.");

    const int button = button_argument(job.button);
    ReleaseOnExit release{context.manager, job.x, job.y, button, true};
    mouse_down(context.manager, job.x, job.y, button);
    const auto down_buttons = read<std::uint32_t>(add(context.manager, kWidgetManagerDownButtons), "down button state");
    const auto down_widget = read<std::uint32_t>(add(context.manager, kWidgetManagerDownWidget), "down widget");
    if ((down_buttons & button_mask(job.button)) == 0 || down_widget != hover ||
        !target_belongs_to_surface(down_widget, context.root, context.manager))
        throw std::runtime_error("WidgetManager did not retain the verified surface hit target.");

    // MouseUp is unconditional once MouseDown may have set a bit. Its normal
    // handler path lets Board, seed packets, and child widgets apply semantics.
    mouse_up(context.manager, job.x, job.y, button);
    release.armed = false;
    if (read<std::uint32_t>(add(context.manager, kWidgetManagerDownButtons), "released button state") != 0)
        throw std::runtime_error("WidgetManager did not release the requested mouse button.");
}

json fail(const std::string& detail) {
    return {{"ok", false}, {"accepted", false}, {"detail", detail}};
}

json last_result() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_last_result;
}

int current_cursor_type(std::uintptr_t board) {
    const auto cursor = read<std::uint32_t>(add(board, kBoardCursor), "Board cursor object");
    if (!cursor || !readable(cursor, kCursorType + sizeof(std::int32_t)))
        throw std::runtime_error("Cursor object is unavailable.");
    const auto type = read<std::int32_t>(add(cursor, kCursorType), "cursor type");
    if (type < 0 || type > 17) throw std::runtime_error("Cursor type is outside the verified enum range.");
    return type;
}

} // namespace

json snapshot(std::uintptr_t app, const std::string& board_scope,
              std::uint32_t main_thread_id) {
    try {
        (void)board_scope; // Scope is an internal stale-job guard, never metadata.
        require_main_thread(main_thread_id);
        validate_app(app);
        const int scene = read<std::int32_t>(add(app, kSceneOffset), "game scene");
        const int mode = read<std::int32_t>(add(app, kModeOffset), "game mode");
        const auto board = read<std::uint32_t>(add(app, kBoardOffset), "LawnApp Board");
        json result{{"ok", true}, {"scene", scene}, {"mode", mode},
            {"board_available", board && readable(board, 0x200)},
            {"seed_chooser_available", false},
            {"last_result", last_result()}};
        const auto chooser = read<std::uint32_t>(add(app, kChooserOffset), "LawnApp SeedChooser");
        result["seed_chooser_available"] = scene == kSceneLevelIntro && chooser && readable(chooser, 0x100);
        if (board && readable(board, 0x200)) {
            try { result["cursor_type"] = current_cursor_type(board); }
            catch (...) { result["cursor_type"] = nullptr; }
            result["paused"] = read<std::uint8_t>(add(board,0x17c), "Board paused") != 0;
            const auto bank = read<std::uint32_t>(add(board,0x15c), "Board seed bank");
            if (bank && readable(bank,0x28)) {
                const auto count = read<std::int32_t>(add(bank,0x24), "seed bank capacity");
                if (count >= 0 && count <= 10) {
                    // Exact 0x40E2D0 rectangle used by Board::MouseHitTest.
                    const int left = (mode==18 || mode==49) ? 600 :
                        456 + (count<=6 ? 0 : count==7 ? 60 : count==8 ? 76 : count==9 ? 112 : 153);
                    result["shovel_hit_rect"] = {{"x",left},{"y",0},{"width",70},{"height",72},
                        {"center_x",left+35},{"center_y",36},
                        {"shown",read<std::uint8_t>(add(board,0x5609), "shovel shown") != 0}};
                }
            }
        } else result["cursor_type"] = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            result["pending"] = g_pending.has_value() || g_running;
            result["job_id"] = g_pending ? json(g_pending->id) : json(nullptr);
        }
        return result;
    } catch (const std::exception& error) {
        return {{"ok", false}, {"detail", error.what()}, {"last_result", last_result()}};
    } catch (...) {
        return {{"ok", false}, {"detail", "Unknown game-input snapshot failure."},
            {"last_result", last_result()}};
    }
}

json action(std::uintptr_t app, const json& args, const std::string& board_scope,
            std::uint32_t main_thread_id) {
    try {
        require_main_thread(main_thread_id);
        if (!args.is_object()) throw std::runtime_error("Game-input action must be an object.");
        for (auto it = args.begin(); it != args.end(); ++it) {
            if (it.key() != "surface" && it.key() != "x" && it.key() != "y" && it.key() != "button")
                throw std::runtime_error("Game-input action accepts only surface, x, y, and button.");
        }
        if (!args.contains("surface") || !args["surface"].is_string() ||
            !args.contains("x") || !args["x"].is_number_integer() || args["x"].is_boolean() ||
            !args.contains("y") || !args["y"].is_number_integer() || args["y"].is_boolean() ||
            !args.contains("button") || !args["button"].is_string())
            throw std::runtime_error("Game-input action requires surface, integer x/y, and button.");
        const auto surface = args["surface"].get<std::string>();
        const auto x = args["x"].get<int>();
        const auto y = args["y"].get<int>();
        if (x < 0 || x >= kMaxScreenX || y < 0 || y >= kMaxScreenY)
            throw std::runtime_error("Coordinates must be inside the 800 by 600 game surface.");
        const auto button_text = args["button"].get<std::string>();
        if (button_text != "left" && button_text != "right")
            throw std::runtime_error("Button must be 'left' or 'right'.");
        if (board_scope.empty() || board_scope.size() > 128)
            throw std::runtime_error("A current board-generation scope is required.");
        const auto context = validate_context(app, board_scope, surface);
        const auto held = read<std::uint32_t>(add(context.manager, kWidgetManagerDownButtons), "held mouse buttons");
        if (held != 0) throw std::runtime_error("Refusing to queue while a mouse button is held.");
        PendingJob job{};
        job.id = "game-input-" + std::to_string(g_next_job_id.fetch_add(1, std::memory_order_relaxed));
        job.scope = board_scope;
        job.surface = surface;
        job.x = x;
        job.y = y;
        job.initial_pointer_x = read<std::int32_t>(add(context.manager, kWidgetManagerMouseX), "current mouse x");
        job.initial_pointer_y = read<std::int32_t>(add(context.manager, kWidgetManagerMouseY), "current mouse y");
        job.button = button_text == "left" ? ButtonKind::left : ButtonKind::right;
        job.board = context.board;
        job.root = context.root;
        job.manager = context.manager;
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            if (g_pending || g_running) throw std::runtime_error("A game-input job is already pending or running.");
            g_pending = job;
            g_last_result = {{"job_id", job.id}, {"status", "pending"},
                {"surface", surface}, {"button", button_text}, {"x", x}, {"y", y}};
        }
        return {{"ok", true}, {"accepted", true}, {"transition_pending", true},
            {"job_id", job.id}, {"surface", surface}, {"button", button_text}};
    } catch (const std::exception& error) {
        return fail(error.what());
    } catch (...) {
        return fail("Unknown game-input queue failure.");
    }
}

json tick(std::uintptr_t app, const std::string& board_scope,
          std::uint32_t main_thread_id) {
    PendingJob job{};
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_running || !g_pending) return {{"ok", true}, {"status", "idle"}};
        job = std::move(*g_pending);
        g_pending.reset();
        g_running = true;
        g_last_result["status"] = "running";
    }
    json result{{"ok", false}, {"job_id", job.id}};
    try {
        require_main_thread(main_thread_id);
        result["cursor_before"] = current_cursor_type(job.board);
        perform_click(app, job, board_scope);
        result["ok"] = true;
        result["status"] = "dispatched";
        result["detail"] = "Normal WidgetManager MouseDown/MouseUp was dispatched.";
        if (read<std::uint32_t>(add(app,kBoardOffset), "Board after input") == job.board)
            result["cursor_after"] = current_cursor_type(job.board);
    } catch (const std::exception& error) {
        result["status"] = "failed";
        result["detail"] = error.what();
    } catch (...) {
        result["status"] = "failed";
        result["detail"] = "Unknown game-input dispatch failure.";
    }
    result["surface"] = job.surface;
    result["button"] = job.button == ButtonKind::left ? "left" : "right";
    result["x"] = job.x;
    result["y"] = job.y;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_last_result = result;
        g_running = false;
    }
    return result;
}

} // namespace pvz::game_input
