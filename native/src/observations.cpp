#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include "observations.hpp"

namespace pvz::observations {
namespace {
using json = nlohmann::json;

// Layouts are pinned to pvztools' verified GOTY Steam 1.2.0.1096 profile:
// https://github.com/BlueRoseMike/pvztools/blob/c60cb557b3260abfbc9cac1fec8eef0957670742/src/pvztools/addr/profiles/goty_1_2_0_1096_en.json
// The DataArray full-ID tail word follows the game's DataArrayItem { item, mID }:
// https://github.com/Electr0Gunner/PvZ-Reconstruction-BugFix/blob/da58098267fb4eda37282daaff207bbac2c9a1d3/Sexy.TodLib/DataArray.h
// The local code fingerprint and board offsets are also recorded in native/src/game.cpp.
constexpr std::uint32_t kPlants = 0x0C4;
constexpr std::uint32_t kZombies = 0x0A8;
constexpr std::uint32_t kCoins = 0x0FC;
constexpr std::uint32_t kGridItems = 0x134;
constexpr std::uint32_t kSeedBank = 0x15C;
// GOTY IsIceAt at 0x4114C0 indexes these six-row int32 arrays. Its
// PixelToGridX helper at 0x420070 uses lawn x=40, 80-pixel columns.
constexpr std::uint32_t kBoardIceMinX = 0x624;
constexpr std::uint32_t kBoardIceTimer = 0x63C;
constexpr std::uint32_t kArrayHeaderBytes = 0x18;

constexpr std::uint32_t kPlantStride = 0x14C;
constexpr std::uint32_t kZombieStride = 0x168;
// Verified in the supported 1.2.0.1096 text image: ApplyChill at 0x545430
// maxes [zombie + 0xAC] to 1000/2000; HitIceTrap at 0x546E90 reads that
// field and stores its 300-600 tick trap counter at [zombie + 0xB4].
constexpr std::uint32_t kZombieChilledCounter = 0x0AC;
constexpr std::uint32_t kZombieIceTrapCounter = 0x0B4;
constexpr std::int32_t kZombieBossType = 25;
constexpr std::uint32_t kZombieBossTargetRow = 0x130;
constexpr std::uint32_t kZombieBossProjectileReanimationId = 0x140;
// The exact Steam 1.2.0.1096 image writes the BossHeadSpit row at +0x14C
// and its fire/ice flag at +0x150; the pinned reconstruction header labels
// these two members four bytes earlier.
constexpr std::uint32_t kZombieBossProjectileRow = 0x14C;
constexpr std::uint32_t kZombieBossProjectileIsFire = 0x150;
constexpr std::uint32_t kCoinStride = 0x0D8;
constexpr std::uint32_t kGridItemStride = 0x0EC;
constexpr std::uint32_t kSeedPacketStride = 0x50;
constexpr std::uint32_t kMaxSeedSlots = 10;
constexpr std::uint32_t kMaxArraySlots = 4096;
constexpr std::size_t kMaxArrayBytes = 4 * 1024 * 1024;

std::uintptr_t add_address(std::uintptr_t base, std::size_t offset) {
    if (base == 0 || offset > std::numeric_limits<std::uintptr_t>::max() - base)
        throw std::runtime_error("Invalid game-memory address.");
    return base + offset;
}

bool readable(std::uintptr_t address, std::size_t bytes) {
    if (address < 0x10000 || bytes == 0 || address + bytes < address) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info))) return false;
    const DWORD p = info.Protect & 0xFF;
    const bool can_read = p == PAGE_READONLY || p == PAGE_READWRITE ||
        p == PAGE_WRITECOPY || p == PAGE_EXECUTE_READ ||
        p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
    const auto region_begin = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    const auto region_end = region_begin + info.RegionSize;
    return info.State == MEM_COMMIT && !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
        can_read && address >= region_begin && address + bytes <= region_end;
}

std::vector<std::uint8_t> read_bytes(std::uintptr_t address, std::size_t bytes) {
    if (!readable(address, bytes)) throw std::runtime_error("Entity memory is no longer readable.");
    std::vector<std::uint8_t> result(bytes);
    std::memcpy(result.data(), reinterpret_cast<const void*>(address), bytes);
    return result;
}

template<class T> T value_at(const std::uint8_t* bytes, std::size_t length,
                             std::size_t offset, const char* label) {
    if (offset > length || sizeof(T) > length - offset)
        throw std::runtime_error(std::string("Invalid 1.2.0.1096 entity field: ") + label);
    T value{};
    std::memcpy(&value, bytes + offset, sizeof(T));
    return value;
}

template<class T> T read_value(std::uintptr_t address, const char* label) {
    const auto bytes = read_bytes(address, sizeof(T));
    return value_at<T>(bytes.data(), bytes.size(), 0, label);
}

float finite_float(float value) {
    return std::isfinite(value) ? value : 0.0f;
}

struct ArrayView {
    std::uintptr_t data = 0;
    std::uint32_t count = 0;
    std::uint32_t max_size = 0;
};

struct PixelRect {
    std::int32_t x;
    std::int32_t y;
    std::int32_t width;
    std::int32_t height;
};

// Target 0x425724 calls 0x546B80 with EBX=zombie and EDI=&localRect.
// This helper uses that exact internal ABI and preserves the compiler's
// nonvolatile EBX/EDI state around the target call.
// Keep an explicit output pointer: returning an asm-written local struct lets
// MSVC's return-value optimization leave the caller's separate storage zeroed.
__declspec(noinline) void zombie_body_rect(std::uintptr_t zombie, PixelRect* output) {
    __asm {
        push ebx
        push edi
        mov ebx, zombie
        mov edi, output
        mov eax, 0546B80h
        call eax
        pop edi
        pop ebx
    }
}

bool bounded_rect(const PixelRect& rect) {
    constexpr std::int32_t kCoordinateLimit = 8192;
    constexpr std::int32_t kDimensionLimit = 4096;
    return rect.x >= -kCoordinateLimit && rect.x <= kCoordinateLimit &&
        rect.y >= -kCoordinateLimit && rect.y <= kCoordinateLimit &&
        rect.width > 0 && rect.width <= kDimensionLimit &&
        rect.height > 0 && rect.height <= kDimensionLimit;
}

const char* boss_phase_name(std::int32_t phase) {
    switch (phase) {
    case 78: return "PHASE_BOSS_ENTER";
    case 79: return "PHASE_BOSS_IDLE";
    case 80: return "PHASE_BOSS_SPAWNING";
    case 81: return "PHASE_BOSS_STOMPING";
    case 82: return "PHASE_BOSS_BUNGEES_ENTER";
    case 83: return "PHASE_BOSS_BUNGEES_DROP";
    case 84: return "PHASE_BOSS_BUNGEES_LEAVE";
    case 85: return "PHASE_BOSS_DROP_RV";
    case 86: return "PHASE_BOSS_HEAD_ENTER";
    case 87: return "PHASE_BOSS_HEAD_IDLE_BEFORE_SPIT";
    case 88: return "PHASE_BOSS_HEAD_IDLE_AFTER_SPIT";
    case 89: return "PHASE_BOSS_HEAD_SPIT";
    case 90: return "PHASE_BOSS_HEAD_LEAVE";
    default: return nullptr;
    }
}

ArrayView array_view(std::uintptr_t board, std::uint32_t offset, const char* name,
                     std::uint32_t stride) {
    const auto header = read_bytes(add_address(board, offset), kArrayHeaderBytes);
    ArrayView out;
    const auto* p = header.data();
    out.data = value_at<std::uint32_t>(p, header.size(), 0, "DataArray.mBlock");
    out.count = value_at<std::uint32_t>(p, header.size(), 4, "DataArray.mMaxUsedCount");
    out.max_size = value_at<std::uint32_t>(p, header.size(), 8, "DataArray.mMaxSize");
    if (out.count > out.max_size || out.max_size > kMaxArraySlots || out.count > kMaxArraySlots)
        throw std::runtime_error(std::string("Invalid ") + name + " DataArray bounds.");
    if (out.count == 0) return out;
    if (out.data == 0 || stride == 0 || out.count > kMaxArrayBytes / stride)
        throw std::runtime_error(std::string("Invalid ") + name + " DataArray storage.");
    const auto byte_count = static_cast<std::size_t>(out.count) * stride;
    if (byte_count > kMaxArrayBytes || byte_count > std::numeric_limits<std::uintptr_t>::max() - out.data)
        throw std::runtime_error(std::string("Oversized ") + name + " DataArray.");
    if (!readable(out.data, byte_count))
        throw std::runtime_error(std::string("Unreadable ") + name + " DataArray storage.");
    return out;
}

json plants(std::uintptr_t board) {
    const auto a = array_view(board, kPlants, "plants", kPlantStride);
    json out = json::array();
    if (!a.count) return out;
    const auto data = read_bytes(a.data, static_cast<std::size_t>(a.count) * kPlantStride);
    for (std::uint32_t i = 0; i < a.count; ++i) {
        const auto* p = data.data() + static_cast<std::size_t>(i) * kPlantStride;
        const auto id = value_at<std::uint32_t>(p, kPlantStride, kPlantStride - 4, "plant.full_id");
        if ((id & 0xFFFF0000u) == 0 || value_at<std::uint8_t>(p, kPlantStride, 0x141, "plant.dead")) continue;
        const auto type = value_at<std::int32_t>(p, kPlantStride, 0x24, "plant.type");
        // GameObject mX/mY/mWidth/mHeight are +08/+0C/+10/+14. For
        // Plant::GetPlantRect, ordinary plants trim 10 px from each side;
        // Pumpkin, Tallnut, and Cob Cannon have their documented exceptions.
        const auto x = value_at<std::int32_t>(p, kPlantStride, 0x08, "plant.x");
        const auto y = value_at<std::int32_t>(p, kPlantStride, 0x0C, "plant.y");
        const auto width = value_at<std::int32_t>(p, kPlantStride, 0x10, "plant.width");
        const auto height = value_at<std::int32_t>(p, kPlantStride, 0x14, "plant.height");
        const auto hit_x = type == 30 ? x : type == 47 ? x : x + 10;
        const auto hit_width = type == 23 ? width : type == 47 ? 140 : width - 20;
        const auto hit_height = type == 47 ? 80 : height;
        out.push_back({{"id", id}, {"slot", i + 1}, {"plant_type", type}, {"type", type},
            {"row", value_at<std::int32_t>(p, kPlantStride, 0x1C, "plant.row") + 1},
            {"col", value_at<std::int32_t>(p, kPlantStride, 0x28, "plant.col") + 1},
            {"x", x}, {"y", y}, {"width", width}, {"height", height},
            {"plant_hit_rect", {{"x", hit_x}, {"y", y}, {"width", hit_width}, {"height", hit_height}}},
            {"state", value_at<std::int32_t>(p, kPlantStride, 0x3C, "plant.state")},
            {"health", value_at<std::int32_t>(p, kPlantStride, 0x40, "plant.hp")},
            {"hp", value_at<std::int32_t>(p, kPlantStride, 0x40, "plant.hp")},
            {"max_hp", value_at<std::int32_t>(p, kPlantStride, 0x44, "plant.max_hp")},
            {"squished", value_at<std::uint8_t>(p, kPlantStride, 0x142, "plant.squished") != 0},
            {"asleep", value_at<std::uint8_t>(p, kPlantStride, 0x143, "plant.asleep") != 0}});
    }
    return out;
}

json zombies(std::uintptr_t board) {
    const auto a = array_view(board, kZombies, "zombies", kZombieStride);
    json out = json::array();
    if (!a.count) return out;
    const auto data = read_bytes(a.data, static_cast<std::size_t>(a.count) * kZombieStride);
    for (std::uint32_t i = 0; i < a.count; ++i) {
        const auto* p = data.data() + static_cast<std::size_t>(i) * kZombieStride;
        const auto id = value_at<std::uint32_t>(p, kZombieStride, kZombieStride - 4, "zombie.full_id");
        // The low ID word is the zero-based DataArray slot. Reject free or
        // stale-generation records before passing any object to game code.
        if ((id & 0xFFFF0000u) == 0 || (id & 0xFFFFu) != i ||
            value_at<std::uint8_t>(p, kZombieStride, 0xEC, "zombie.dead")) continue;
        const auto type = value_at<std::int32_t>(p, kZombieStride, 0x24, "zombie.type");
        const auto hp = value_at<std::int32_t>(p, kZombieStride, 0xC8, "zombie.hp");
        const auto status = value_at<std::int32_t>(p, kZombieStride, 0x28, "zombie.status");
        PixelRect body{};
        zombie_body_rect(a.data + static_cast<std::size_t>(i) * kZombieStride, &body);
        const bool body_valid = bounded_rect(body);
        PixelRect hammer = body;
        if (body_valid) hammer.height -= 50; // 0x42572F shortens the body rect before hammer overlap.
        const bool hammer_valid = body_valid && bounded_rect(hammer);
        const auto rect_json = [](const PixelRect& rect) {
            return json{{"x", rect.x}, {"y", rect.y},
                {"width", rect.width}, {"height", rect.height}};
        };
        json record = {{"id", id}, {"slot", i + 1}, {"zombie_type", type}, {"type", type},
            {"row", value_at<std::int32_t>(p, kZombieStride, 0x1C, "zombie.row") + 1},
            {"status", status},
            {"x", finite_float(value_at<float>(p, kZombieStride, 0x2C, "zombie.x"))},
            {"y", finite_float(value_at<float>(p, kZombieStride, 0x30, "zombie.y"))},
            {"speed", finite_float(value_at<float>(p, kZombieStride, 0x34, "zombie.speed"))},
            {"chilled_counter", value_at<std::int32_t>(p, kZombieStride, kZombieChilledCounter, "zombie.chilled_counter")},
            {"ice_trap_counter", value_at<std::int32_t>(p, kZombieStride, kZombieIceTrapCounter, "zombie.ice_trap_counter")},
            {"render_order", value_at<std::int32_t>(p, kZombieStride, 0x20, "zombie.render_order")},
            {"mind_controlled", value_at<std::uint8_t>(p, kZombieStride, 0xB8, "zombie.mind_controlled") != 0},
            {"helmet_type", value_at<std::int32_t>(p, kZombieStride, 0xC4, "zombie.helmet_type")},
            {"body_rect", body_valid ? rect_json(body) : json(nullptr)},
            {"hammer_hit_rect", hammer_valid ? rect_json(hammer) : json(nullptr)},
            {"hammer_hit_shape", {{"type", "circle"}, {"radius", 45}, {"center_y_offset", -20}}},
            {"health", hp}, {"hp", hp},
            {"max_hp", value_at<std::int32_t>(p, kZombieStride, 0xCC, "zombie.max_hp")},
            {"helmet_hp", value_at<std::int32_t>(p, kZombieStride, 0xD0, "zombie.helmet_hp")},
            {"shield_hp", value_at<std::int32_t>(p, kZombieStride, 0xDC, "zombie.shield_hp")}};
        if (type == kZombieBossType) {
            const auto target_row = value_at<std::int32_t>(p, kZombieStride,
                kZombieBossTargetRow, "zombie.boss_target_row");
            const auto projectile_row = value_at<std::int32_t>(p, kZombieStride,
                kZombieBossProjectileRow, "zombie.boss_projectile_row");
            const auto phase_name = boss_phase_name(status);
            record["phase"] = status;
            record["phase_name"] = phase_name ? json(phase_name) : json(nullptr);
            record["boss_target_row"] = target_row >= 0 && target_row < 6 ?
                json(target_row + 1) : json(nullptr);
            record["boss_projectile_reanimation_id"] = value_at<std::uint32_t>(p,
                kZombieStride, kZombieBossProjectileReanimationId,
                "zombie.boss_projectile_reanimation_id");
            record["boss_projectile_row"] = projectile_row >= 0 && projectile_row < 6 ?
                json(projectile_row + 1) : json(nullptr);
            record["boss_projectile_is_fire"] = value_at<std::uint8_t>(p,
                kZombieStride, kZombieBossProjectileIsFire,
                "zombie.boss_projectile_is_fire") != 0;
        }
        out.push_back(record);
    }
    return out;
}

json coins(std::uintptr_t board) {
    const auto a = array_view(board, kCoins, "coins", kCoinStride);
    json out = json::array();
    if (!a.count) return out;
    const auto data = read_bytes(a.data, static_cast<std::size_t>(a.count) * kCoinStride);
    for (std::uint32_t i = 0; i < a.count; ++i) {
        const auto* p = data.data() + static_cast<std::size_t>(i) * kCoinStride;
        // Coin is 8-byte aligned: full ID at 0xd0, followed by alignment padding.
        // Verified against four live slots and their DataArray free-list IDs.
        const auto id = value_at<std::uint32_t>(p, kCoinStride, 0xD0, "coin.full_id");
        if ((id & 0xFFFF0000u) == 0 || value_at<std::uint8_t>(p, kCoinStride, 0x38, "coin.dead")) continue;
        const auto type = value_at<std::int32_t>(p, kCoinStride, 0x58, "coin.type");
        const auto amount = type == 4 ? 25 : type == 5 ? 15 : type == 6 ? 50 : type==1?10:type==2?50:type==3?1000:0;
        const char* kind = type == 4 ? "sun" : type == 5 ? "small_sun" : type == 6 ? "large_sun" : (type>=1&&type<=3)?"coin":"other";
        const bool collecting = value_at<std::uint8_t>(p, kCoinStride, 0x50, "coin.collecting") != 0;
        out.push_back({{"id", id}, {"slot", i + 1}, {"kind", kind}, {"amount", amount},
            {"type", value_at<std::int32_t>(p, kCoinStride, 0x58, "coin.type")},
            {"x", finite_float(value_at<float>(p, kCoinStride, 0x24, "coin.x"))},
            {"y", finite_float(value_at<float>(p, kCoinStride, 0x28, "coin.y"))},
            {"being_collected", collecting}, {"collecting", collecting},
            {"age", value_at<std::int32_t>(p, kCoinStride, 0x4C, "coin.age")},
            {"motion", value_at<std::int32_t>(p, kCoinStride, 0x5C, "coin.motion")},
            {"usable_seed_type", value_at<std::int32_t>(p, kCoinStride, 0x68, "coin.seed_type")}});
    }
    return out;
}

json ice_surface(std::uintptr_t board) {
    const auto mins = read_bytes(add_address(board, kBoardIceMinX), 6 * sizeof(std::int32_t));
    const auto timers = read_bytes(add_address(board, kBoardIceTimer), 6 * sizeof(std::int32_t));
    const auto background = read_value<std::int32_t>(add_address(board, 0x5564), "board.background");
    const int rows = (background == 2 || background == 3) ? 6 : 5;
    json row_records = json::array(), cells = json::array();
    for (int row = 0; row < rows; ++row) {
        const auto min_x = value_at<std::int32_t>(mins.data(), mins.size(), row * 4, "ice.min_x");
        const auto timer = value_at<std::int32_t>(timers.data(), timers.size(), row * 4, "ice.timer");
        const bool active = timer != 0 && min_x <= 750 && background >= 0 && background <= 5;
        // Keep arithmetic wide; the helper clamps an off-lawn point to col 0.
        const auto zero_col = std::clamp<std::int64_t>(
            (static_cast<std::int64_t>(min_x) + 12 - 40) / 80, 0, 8);
        row_records.push_back({{"row", row + 1}, {"timer", timer}, {"min_x", min_x},
            {"active", active}, {"first_blocked_col", active ? json(zero_col + 1) : json(nullptr)}});
        if (active) for (int col = static_cast<int>(zero_col); col < 9; ++col)
            cells.push_back({{"row", row + 1}, {"col", col + 1}});
    }
    return {{"ice_rows", row_records}, {"ice_cells", cells}};
}

json grid_items(std::uintptr_t board) {
    const auto a = array_view(board, kGridItems, "grid items", kGridItemStride);
    json out = json::array();
    if (!a.count) return out;
    const auto data = read_bytes(a.data, static_cast<std::size_t>(a.count) * kGridItemStride);
    for (std::uint32_t i = 0; i < a.count; ++i) {
        const auto* p = data.data() + static_cast<std::size_t>(i) * kGridItemStride;
        const auto id = value_at<std::uint32_t>(p, kGridItemStride, kGridItemStride - 4, "grid_item.full_id");
        if ((id & 0xFFFF0000u) == 0 || value_at<std::uint8_t>(p, kGridItemStride, 0x20, "grid_item.dead")) continue;
        // Keep the public record to the visible object's identity, type and
        // location. GridItem payload fields can encode vase contents or other
        // concealed/future outcomes, so do not serialize state, counters, or
        // the zombie/seed/pot/sun payload fields here.
        out.push_back({{"id", id}, {"slot", i + 1},
            {"type", value_at<std::int32_t>(p, kGridItemStride, 0x08, "grid_item.type")},
            {"col", value_at<std::int32_t>(p, kGridItemStride, 0x10, "grid_item.col") + 1},
            {"row", value_at<std::int32_t>(p, kGridItemStride, 0x14, "grid_item.row") + 1}});
    }
    return out;
}

bool seed_point_hits(std::uintptr_t board, std::uintptr_t packet, int x, int y) {
    // Verified 1096 Board::MouseHitTest ABI, also used by garden.cpp.
    // This is a query only: no pointer movement, button callback or mutation.
    if (x < 0 || x >= 800 || y < 0 || y >= 600) return false;
    std::uint32_t result[8]{};
    __asm {
        lea eax, result
        push eax
        push y
        push x
        push board
        mov ecx, 0414C40h
        call ecx
    }
    return result[0] == packet && result[1] == 4;
}

json seed_packets(std::uintptr_t board) {
    const auto bank = read_value<std::uint32_t>(add_address(board, kSeedBank), "board.seed_bank");
    if (!bank) return json::array();
    constexpr std::size_t kBankHeaderBytes = 0x28;
    const auto header = read_bytes(bank, kBankHeaderBytes);
    const auto count = value_at<std::int32_t>(header.data(), header.size(), 0x24, "seed_bank.count");
    if (count < 0 || count > static_cast<std::int32_t>(kMaxSeedSlots))
        throw std::runtime_error("Invalid seed bank packet count.");
    if (!count) return json::array();
    const auto bank_x = read_value<std::int32_t>(add_address(bank, 0x08), "seed_bank.x");
    const auto bank_y = read_value<std::int32_t>(add_address(bank, 0x0c), "seed_bank.y");
    // Board::MouseHitTest (0x414C40) checks the seed bank before the shovel,
    // then lets the visible shovel claim overlapping points. 0x40E2D0 builds
    // this exact 70x72 shovel rectangle from the packet count and game mode.
    const auto app = read_value<std::uint32_t>(add_address(board, 0x0a4), "board.app");
    const auto mode = read_value<std::int32_t>(add_address(app, 0x918), "app.game_mode");
    const bool shovel_shown = read_value<std::uint8_t>(add_address(board, 0x5609), "board.shovel_shown") != 0;
    const auto shovel_x = (mode == 18 || mode == 49) ? 600 :
        456 + (count <= 6 ? 0 : count == 7 ? 60 : count == 8 ? 76 : count == 9 ? 112 : 153);
    const auto packets = read_bytes(add_address(bank, 0x28), static_cast<std::size_t>(count) * kSeedPacketStride);
    json out = json::array();
    for (std::int32_t i = 0; i < count; ++i) {
        const auto* p = packets.data() + static_cast<std::size_t>(i) * kSeedPacketStride;
        const auto type = value_at<std::int32_t>(p, kSeedPacketStride, 0x34, "seed.type");
        const auto elapsed = value_at<std::int32_t>(p, kSeedPacketStride, 0x24, "seed.cd_elapsed");
        const auto total = value_at<std::int32_t>(p, kSeedPacketStride, 0x28, "seed.cd_total");
        const bool usable=value_at<std::uint8_t>(p,kSeedPacketStride,0x48,"seed.usable")!=0;
        // Board::MouseHitTest (0x414C40) calls SeedBank::MouseHitTest
        // (0x498010), which subtracts bank x/y and tests each packet through
        // 0x497630. Preserve that exact half-open rectangle in board pixels.
        // 1096 helper 0x497630 compares ESI (bank-relative x) with
        // packet +08/+30/+10, and EBX (relative y) with +0C/+14.
        // The +30 conveyor displacement belongs to x, not y.
        const std::int64_t hit_x = static_cast<std::int64_t>(bank_x) +
            value_at<std::int32_t>(p, kSeedPacketStride, 0x08, "seed.hit_x") +
            value_at<std::int32_t>(p, kSeedPacketStride, 0x30, "seed.hit_x_offset");
        const std::int64_t hit_y = static_cast<std::int64_t>(bank_y) +
            value_at<std::int32_t>(p, kSeedPacketStride, 0x0c, "seed.hit_y");
        const auto hit_width = value_at<std::int32_t>(p, kSeedPacketStride, 0x10, "seed.hit_width");
        const auto hit_height = value_at<std::int32_t>(p, kSeedPacketStride, 0x14, "seed.hit_height");
        // 0x497634 jumps to the false return when this counter is positive.
        const bool packet_rect_valid = hit_width > 0 && hit_height > 0 && hit_width <= 800 && hit_height <= 600;
        const auto hit_testable = value_at<std::int32_t>(p, kSeedPacketStride, 0x3c, "seed.hit_block_counter") <= 0 &&
            type != -1 && packet_rect_valid;
        const auto shovel_left = std::max<std::int64_t>(hit_x, shovel_x);
        const auto shovel_top = std::max<std::int64_t>(hit_y, 0);
        const auto shovel_right = std::min<std::int64_t>(hit_x + hit_width, shovel_x + 70);
        const auto shovel_bottom = std::min<std::int64_t>(hit_y + hit_height, 72);
        const bool overlaps_shovel = shovel_shown && shovel_left < shovel_right && shovel_top < shovel_bottom;
        json board_hit_regions = json::array();
        if (hit_testable) {
            if (!overlaps_shovel) {
                board_hit_regions.push_back({{"x", hit_x}, {"y", hit_y},
                    {"width", hit_width}, {"height", hit_height}});
            } else {
                // Subtract the shovel overlap from the half-open packet rect;
                // the remaining regions are candidate Board hit points.
                if (shovel_top > hit_y) board_hit_regions.push_back({{"x", hit_x}, {"y", hit_y},
                    {"width", hit_width}, {"height", shovel_top - hit_y}});
                if (shovel_bottom < hit_y + hit_height) board_hit_regions.push_back({{"x", hit_x}, {"y", shovel_bottom},
                    {"width", hit_width}, {"height", hit_y + hit_height - shovel_bottom}});
                if (shovel_left > hit_x) board_hit_regions.push_back({{"x", hit_x}, {"y", shovel_top},
                    {"width", shovel_left - hit_x}, {"height", shovel_bottom - shovel_top}});
                if (shovel_right < hit_x + hit_width) board_hit_regions.push_back({{"x", shovel_right}, {"y", shovel_top},
                    {"width", hit_x + hit_width - shovel_right}, {"height", shovel_bottom - shovel_top}});
            }
        }
        json verified_point = nullptr;
        // Verify against the real game's hit test as well as rectangle math;
        // a geometrically eligible packet can be covered by another tool.
        if (read_value<std::int32_t>(add_address(app, 0x91c), "app.scene") == 3) {
            for (const auto& region : board_hit_regions) {
                if (region["width"].get<int>() < 8 || region["height"].get<int>() < 8) continue;
                const int x = region["x"].get<int>() + region["width"].get<int>() / 2;
                const int y = region["y"].get<int>() + region["height"].get<int>() / 2;
                if (seed_point_hits(board, bank + 0x28 + i * kSeedPacketStride, x, y)) {
                    verified_point = {{"x", x}, {"y", y}};
                    break;
                }
            }
        }
        const auto remaining = usable ? 0 : static_cast<std::int32_t>(std::max<std::int64_t>(
            static_cast<std::int64_t>(total) - static_cast<std::int64_t>(elapsed), 0));
        out.push_back({{"slot", i + 1}, {"plant_type", type}, {"type", type},
            {"imitater_type", value_at<std::int32_t>(p, kSeedPacketStride, 0x38, "seed.imitater_type")},
            {"cooldown_remaining", remaining}, {"cooldown", remaining},
            {"cooldown_elapsed", elapsed}, {"cooldown_total", total},
            {"usable", usable},
            {"input_hit_rect", {{"x", hit_x}, {"y", hit_y},
                {"width", hit_width}, {"height", hit_height},
                {"center_x", hit_x + hit_width / 2},
                {"center_y", hit_y + hit_height / 2},
                {"hit_testable", hit_testable},
                {"packet_rect_hit_testable", hit_testable},
                {"board_hit_tested_point", verified_point},
                {"overlaps_shovel", overlaps_shovel},
                {"normal_board_hit_regions", board_hit_regions}}}});
    }
    return out;
}
} // namespace

json snapshot(std::uintptr_t board) {
    if (!board) return {{"plants", json::array()}, {"zombies", json::array()},
        {"collectibles", json::array()}, {"grid_items", json::array()},
        {"ice_rows", json::array()}, {"ice_cells", json::array()},
        {"seed_packets", json::array()}, {"observation_complete", false},
        {"observation_status", "no_board"}};
    // true means all expected arrays were read under the known layout and bounds.
    // It is not a claim of live validation against a running 1096 process.
    json result = {{"plants", plants(board)}, {"zombies", zombies(board)},
        {"collectibles", coins(board)}, {"grid_items", grid_items(board)},
        {"seed_packets", seed_packets(board)}, {"observation_complete", true},
        {"observation_status", "layout_complete_unverified_live"}};
    result.update(ice_surface(board));
    return result;
}

} // namespace pvz::observations
