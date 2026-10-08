#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

#include "continue_dialog.hpp"

namespace pvz::continue_dialog {
namespace {
using json = nlohmann::json;

constexpr std::uintptr_t kGlobalApp = 0x731c50;
constexpr std::uintptr_t kContinueDialogVtable = 0x6edd78;
constexpr std::uintptr_t kContinueListenerVtable = 0x6edeb0;
constexpr std::uintptr_t kContinueButtonDepress = 0x437460;
constexpr std::size_t kDialogMapHeadPointer = 0x33c;
constexpr std::size_t kDialogListener = 0xa0;
constexpr std::size_t kButtonDepressSlot = 0x08;
constexpr std::size_t kDialogIdField = 0x158;
constexpr std::size_t kDialogResultField = 0x160;
constexpr std::size_t kDialogOwnerField = 0x16c;
constexpr int kContinueDialogId = 37;
constexpr int kContinueButtonId = 0;

std::uintptr_t add(std::uintptr_t p, std::size_t n) {
    if (p < 0x10000 || n > std::numeric_limits<std::uintptr_t>::max() - p)
        throw std::runtime_error("Invalid app or ContinueDialog address.");
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
        throw std::runtime_error(std::string("Unreadable ContinueDialog field: ") + field);
    T value{};
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(T));
    return value;
}

void validate_app(std::uintptr_t app) {
    if (app != read<std::uint32_t>(kGlobalApp, "global app") || !readable(app, 0x960))
        throw std::runtime_error("Unexpected LawnApp pointer/layout.");
}

std::uintptr_t find_continue_dialog(std::uintptr_t app) {
    // The map's header pointer is stored at app+0x33c. Each node is the
    // version-matched tree layout: left, parent, right, key, value.
    const auto head = read<std::uint32_t>(add(app, kDialogMapHeadPointer), "dialog map head");
    if (!head || !readable(head, 0x14))
        throw std::runtime_error("ContinueDialog map header is invalid.");
    auto node = read<std::uint32_t>(add(head, 0x04), "dialog map root");
    for (unsigned depth = 0; node && node != head && depth < 128; ++depth) {
        if (!readable(node, 0x14))
            throw std::runtime_error("ContinueDialog map node is invalid.");
        const auto key = read<std::int32_t>(add(node, 0x0c), "dialog map key");
        if (key == kContinueDialogId) {
            const auto dialog = read<std::uint32_t>(add(node, 0x10), "ContinueDialog pointer");
            if (!dialog || !readable(dialog, kDialogOwnerField + sizeof(std::uint32_t)))
                throw std::runtime_error("ContinueDialog pointer is invalid.");
            return dialog;
        }
        node = read<std::uint32_t>(add(node, key < kContinueDialogId ? 0x08 : 0x00),
            "dialog map branch");
    }
    return 0;
}

} // namespace

bool is_open(std::uintptr_t app) {
    validate_app(app);
    const auto dialog=find_continue_dialog(app);
    return dialog&&read<std::uint32_t>(dialog,"ContinueDialog vtable")==kContinueDialogVtable;
}

json continue_saved_game(std::uintptr_t app) {
    validate_app(app);
    const auto dialog = find_continue_dialog(app);
    if(!dialog)throw std::runtime_error("Continue Saved Game dialog (ID 37) is not open.");
    if (read<std::uint32_t>(dialog, "ContinueDialog vtable") != kContinueDialogVtable)
        throw std::runtime_error("Dialog ID 37 is not the verified 1096 ContinueDialog class.");
    if (read<std::int32_t>(add(dialog, kDialogIdField), "ContinueDialog ID") != kContinueDialogId)
        throw std::runtime_error("ContinueDialog's stored ID does not match ID 37.");
    if (read<std::uint32_t>(add(dialog, kDialogOwnerField), "ContinueDialog owner") != app)
        throw std::runtime_error("ContinueDialog is not owned by the active LawnApp.");

    const auto listener = add(dialog, kDialogListener);
    const auto listener_vtable = read<std::uint32_t>(listener, "ContinueDialog listener vtable");
    if (listener_vtable != kContinueListenerVtable)
        throw std::runtime_error("ContinueDialog listener does not match the verified 1096 layout.");
    const auto callback = read<std::uint32_t>(add(listener_vtable, kButtonDepressSlot),
        "ContinueDialog ButtonDepress slot");
    if (callback != kContinueButtonDepress)
        throw std::runtime_error("ContinueDialog ButtonDepress target does not match the verified 1096 callback.");
    const auto result_before = read<std::int32_t>(add(dialog, kDialogResultField),
        "ContinueDialog result");

#if defined(_MSC_VER) && defined(_M_IX86)
    using ButtonDepress = void(__thiscall*)(void*, int);
    reinterpret_cast<ButtonDepress>(callback)(reinterpret_cast<void*>(listener), kContinueButtonId);
#else
    throw std::runtime_error("Continue Saved Game requires the verified MSVC x86 ABI.");
#endif

    return json{{"accepted", true}, {"route", "normal_continue_saved_game"},
        {"dialog_id", kContinueDialogId}, {"button_id", kContinueButtonId},
        {"result_before_callback", result_before},
        {"scene", read<std::int32_t>(add(app, 0x91c), "game scene")},
        {"mode", read<std::int32_t>(add(app, 0x918), "game mode")}};
}

} // namespace pvz::continue_dialog
