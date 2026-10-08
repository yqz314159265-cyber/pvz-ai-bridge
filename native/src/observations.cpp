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
constexpr std::uint32_t kArrayHeaderBytes = 0x18;

constexpr std::uint32_t kPlantStride = 0x14C;
constexpr std::uint32_t kZombieStride = 0x168;
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
        out.push_back({{"id", id}, {"slot", i + 1}, {"plant_type", type}, {"type", type},
            {"row", value_at<std::int32_t>(p, kPlantStride, 0x1C, "plant.row") + 1},
            {"col", value_at<std::int32_t>(p, kPlantStride, 0x28, "plant.col") + 1},
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
        if ((id & 0xFFFF0000u) == 0 || value_at<std::uint8_t>(p, kZombieStride, 0xEC, "zombie.dead")) continue;
        const auto type = value_at<std::int32_t>(p, kZombieStride, 0x24, "zombie.type");
        const auto hp = value_at<std::int32_t>(p, kZombieStride, 0xC8, "zombie.hp");
        out.push_back({{"id", id}, {"slot", i + 1}, {"zombie_type", type}, {"type", type},
            {"row", value_at<std::int32_t>(p, kZombieStride, 0x1C, "zombie.row") + 1},
            {"status", value_at<std::int32_t>(p, kZombieStride, 0x28, "zombie.status")},
            {"x", finite_float(value_at<float>(p, kZombieStride, 0x2C, "zombie.x"))},
            {"y", finite_float(value_at<float>(p, kZombieStride, 0x30, "zombie.y"))},
            {"speed", finite_float(value_at<float>(p, kZombieStride, 0x34, "zombie.speed"))},
            {"health", hp}, {"hp", hp},
            {"max_hp", value_at<std::int32_t>(p, kZombieStride, 0xCC, "zombie.max_hp")},
            {"helmet_hp", value_at<std::int32_t>(p, kZombieStride, 0xD0, "zombie.helmet_hp")},
            {"shield_hp", value_at<std::int32_t>(p, kZombieStride, 0xDC, "zombie.shield_hp")}});
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
        const std::int64_t hit_x = static_cast<std::int64_t>(bank_x) +
            value_at<std::int32_t>(p, kSeedPacketStride, 0x0c, "seed.hit_x");
        const std::int64_t hit_y = static_cast<std::int64_t>(bank_y) +
            value_at<std::int32_t>(p, kSeedPacketStride, 0x08, "seed.hit_y") +
            value_at<std::int32_t>(p, kSeedPacketStride, 0x30, "seed.hit_y_offset");
        const auto hit_width = value_at<std::int32_t>(p, kSeedPacketStride, 0x14, "seed.hit_width");
        const auto hit_height = value_at<std::int32_t>(p, kSeedPacketStride, 0x10, "seed.hit_height");
        // 0x497634 jumps to the false return when this counter is positive.
        const auto hit_testable = value_at<std::int32_t>(p, kSeedPacketStride, 0x3c, "seed.hit_block_counter") <= 0 && type != -1;
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
                {"hit_testable", hit_testable}}}});
    }
    return out;
}
} // namespace

json snapshot(std::uintptr_t board) {
    if (!board) return {{"plants", json::array()}, {"zombies", json::array()},
        {"collectibles", json::array()}, {"grid_items", json::array()},
        {"seed_packets", json::array()}, {"observation_complete", false},
        {"observation_status", "no_board"}};
    // true means all expected arrays were read under the known layout and bounds.
    // It is not a claim of live validation against a running 1096 process.
    return {{"plants", plants(board)}, {"zombies", zombies(board)},
        {"collectibles", coins(board)}, {"grid_items", grid_items(board)},
        {"seed_packets", seed_packets(board)}, {"observation_complete", true},
        {"observation_status", "layout_complete_unverified_live"}};
}

} // namespace pvz::observations
