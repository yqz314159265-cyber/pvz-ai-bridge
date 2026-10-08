#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "garden.hpp"
#include "profiles.hpp"

namespace pvz::garden {
namespace {
using json = nlohmann::json;

// GOTY 1096 LawnApp fields from the verified pvztools profile. PlayerInfo and
// ZenGarden offsets are cross-checked against the exact live image; see
// docs/ENTITY_LAYOUT.md for the evidence and remaining limits.
constexpr std::uint32_t kAppBoard = 0x868;
constexpr std::uint32_t kAppMode = 0x918;
constexpr std::uint32_t kAppZenGarden = 0x93C;
constexpr std::uint32_t kAppPlayerInfo = 0x94C;
constexpr std::uint32_t kAppScene = 0x91C;
constexpr std::uint32_t kAppWidgetManager = 0x320;
constexpr std::uint32_t kGameModeZenGarden = 43;
constexpr std::uint32_t kGameModeTreeWisdom = 50;

constexpr std::uint32_t kZenBoard = 0x04;
constexpr std::uint32_t kZenGardenType = 0x08;
constexpr std::uint32_t kPlayerCoins = 0x54;
constexpr std::uint32_t kPlayerPurchases = 0x1EC;
constexpr std::uint32_t kPlayerPlantCount = 0x37C;
constexpr std::uint32_t kPlayerPlants = 0x388;
constexpr std::uint32_t kPottedPlantStride = 0x58;
constexpr std::uint32_t kMaxPottedPlants = 200;

constexpr std::uint32_t kBoardPlants = 0x0C4;
constexpr std::uint32_t kBoardGridItems = 0x134;
constexpr std::uint32_t kPlantStride = 0x14C;
constexpr std::uint32_t kGridItemStride = 0x0EC;
constexpr std::uint32_t kBoardPaused = 0x17C;
constexpr std::uint32_t kBoardCursor = 0x150;
constexpr std::uint32_t kBoardChallenge = 0x178;
constexpr std::uint32_t kCursorType = 0x30;
constexpr std::uint32_t kMaxPlantSlots = 4096;
constexpr std::size_t kMaxReadBytes = 4 * 1024 * 1024;

constexpr std::int32_t kCursorWateringCan = 9;
constexpr std::int32_t kCursorFertilizer = 10;
constexpr std::int32_t kCursorBugSpray = 11;
constexpr std::int32_t kCursorPhonograph = 12;
constexpr std::int32_t kCursorChocolate = 13;
constexpr std::int32_t kCursorMoneySign = 15;
constexpr std::int32_t kCursorWheelbarrow = 16;
constexpr std::int32_t kCursorTreeFood = 17;
constexpr std::int32_t kCursorGlove = 14;
constexpr std::int32_t kCursorPlantFromGlove = 3;
constexpr std::int32_t kObjectWateringCan = 6;
constexpr std::int32_t kObjectFertilizer = 7;
constexpr std::int32_t kObjectBugSpray = 8;
constexpr std::int32_t kObjectPhonograph = 9;
constexpr std::int32_t kObjectChocolate = 10;
constexpr std::int32_t kObjectMoneySign = 12;
constexpr std::int32_t kObjectWheelbarrow = 13;
constexpr std::int32_t kObjectTreeFood = 14;
constexpr std::int32_t kObjectNextGarden = 15;
constexpr std::int32_t kObjectGlove = 11;
constexpr std::int32_t kObjectTree = 21;
constexpr std::int32_t kObjectStinky = 20;
constexpr std::int32_t kNeedNone = 0;
constexpr std::int32_t kNeedWater = 1;
constexpr std::int32_t kNeedFertilizer = 2;
constexpr std::int32_t kNeedBugSpray = 3;
constexpr std::int32_t kNeedPhonograph = 4;
constexpr std::int32_t kGridItemStinky = 10;
constexpr std::int32_t kGridItemZenTool = 9;
constexpr std::int32_t kStinkySleeping = 23;
constexpr std::int32_t kStinkyFallingAsleep = 24;
constexpr std::int32_t kGardenMain = 0;
constexpr std::int32_t kGardenMushroom = 1;
constexpr std::int32_t kGardenWheelbarrow = 2;
constexpr std::int32_t kGardenAquarium = 3;
constexpr std::int32_t kPurchaseOffset = 1000;
constexpr std::uint32_t kPurchaseGoldWateringCan = 13;
constexpr std::uint32_t kPurchaseFertilizer = 14;
constexpr std::uint32_t kPurchaseBugSpray = 15;
constexpr std::uint32_t kPurchasePhonograph = 16;
constexpr std::uint32_t kPurchaseGlove = 17;
constexpr std::uint32_t kPurchaseWheelbarrow = 19;
constexpr std::uint32_t kPurchaseMushroomGarden = 18;
constexpr std::uint32_t kPurchaseAquarium = 25;
constexpr std::uint32_t kPurchaseTreeWisdom = 27;
constexpr std::uint32_t kPurchaseChocolate = 26;
constexpr std::uint32_t kPurchaseStinky = 20;
constexpr std::uint32_t kPurchaseTreeFood = 28;
constexpr std::uint32_t kBoardMouseDown1096 = 0x415430;
constexpr std::uint32_t kWidgetMouseMove1096 = 0x54FB90;
constexpr std::uint32_t kWidgetMouseDown1096 = 0x54F9D0;
constexpr std::uint32_t kWidgetMouseUp1096 = 0x54F8C0;
constexpr std::uint32_t kWidgetManagerVtable1096 = 0x6FC114;
constexpr std::uint32_t kDialogKeyDown1096 = 0x45D520;
constexpr std::uint32_t kDialogYes = 13;
constexpr std::uint32_t kDialogEscape = 27;
constexpr std::int32_t kDialogZenSell = 47;
constexpr ULONGLONG kCareTimeoutMs = 2000;

struct PendingCare {
    bool active = false;
    std::uintptr_t app = 0;
    std::uintptr_t board = 0;
    std::uintptr_t garden = 0;
    std::uintptr_t player = 0;
    std::uintptr_t challenge = 0;
    std::uint32_t plant_id = 0;
    bool snail_target = false;
    bool sell_target = false;
    bool wheelbarrow_target = false;
    bool tree_target = false;
    bool move_target = false;
    bool place_wheelbarrow_target = false;
    bool chocolate_plant_target = false;
    bool dispatching = false;
    std::int32_t inventory_index = -1;
    std::int32_t plant_count_before = -1;
    std::int32_t inventory_before = -1;
    std::int32_t target_row = -1;
    std::int32_t target_col = -1;
    std::int32_t source_row = -1;
    std::int32_t source_col = -1;
    std::int32_t garden_before = -1;
    std::int32_t sale_seed_type = -1;
    std::int32_t sale_garden_type = -1;
    std::int32_t sale_x = -1;
    std::int32_t sale_y = -1;
    bool sale_decision_sent = false;
    CareKind kind = CareKind::water;
    std::int32_t cursor_type = kCursorWateringCan;
    int x = 0;
    int y = 0;
    ULONGLONG started_ms = 0;
    std::uint32_t ticks = 0;
};

PendingCare g_pending_care{};
json g_last_care_result = nullptr;
struct PendingGardenTransition {
    bool active = false;
    std::uintptr_t app = 0;
    std::uintptr_t player = 0;
    std::int32_t target_id = -1;
    std::uint32_t advances = 0;
    std::uint32_t ticks = 0;
    ULONGLONG started_ms = 0;
};
PendingGardenTransition g_pending_garden_transition{};
json g_last_garden_transition = nullptr;

const char* care_name(CareKind kind);
std::int32_t cursor_type(std::uintptr_t board);
bool toolbar_object_available(std::uintptr_t board, std::int32_t object_type);
bool hit_test_object(std::uintptr_t board, int x, int y, std::int32_t expected);
const char* pending_action_name(const PendingCare& pending) {
    if (pending.snail_target) return "feed_snail";
    if (pending.sell_target) return "sell_plant";
    if (pending.wheelbarrow_target) return "put_in_wheelbarrow";
    if (pending.tree_target) return "feed_tree";
    if (pending.move_target) return "move_plant";
    if (pending.place_wheelbarrow_target) return "place_wheelbarrow_plant";
    if (pending.chocolate_plant_target) return "feed_plant_chocolate";
    return care_name(pending.kind);
}

std::uintptr_t add_address(std::uintptr_t base, std::size_t offset) {
    if (base == 0 || offset > std::numeric_limits<std::uintptr_t>::max() - base)
        throw std::runtime_error("Invalid garden memory address.");
    return base + offset;
}

bool readable(std::uintptr_t address, std::size_t bytes) {
    if (address < 0x10000 || bytes == 0 || bytes > kMaxReadBytes ||
        bytes > std::numeric_limits<std::uintptr_t>::max() - address) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info))) return false;
    const DWORD p = info.Protect & 0xFF;
    const bool can_read = p == PAGE_READONLY || p == PAGE_READWRITE || p == PAGE_WRITECOPY ||
        p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY;
    const auto begin = reinterpret_cast<std::uintptr_t>(info.BaseAddress);
    if (info.RegionSize > std::numeric_limits<std::uintptr_t>::max() - begin) return false;
    const auto end = begin + info.RegionSize;
    return info.State == MEM_COMMIT && !(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) &&
        can_read && address >= begin && address + bytes <= end;
}

std::vector<std::uint8_t> read_bytes(std::uintptr_t address, std::size_t bytes) {
    if (!readable(address, bytes)) throw std::runtime_error("Garden memory is no longer readable.");
    std::vector<std::uint8_t> data(bytes);
    std::memcpy(data.data(), reinterpret_cast<const void*>(address), bytes);
    return data;
}

template<class T> T get(const std::uint8_t* data, std::size_t size,
                        std::size_t offset, const char* field) {
    if (offset > size || sizeof(T) > size - offset)
        throw std::runtime_error(std::string("Invalid garden field: ") + field);
    T value{};
    std::memcpy(&value, data + offset, sizeof(T));
    return value;
}

template<class T> T read_at(std::uintptr_t base, std::size_t offset, const char* field) {
    const auto data = read_bytes(add_address(base, offset), sizeof(T));
    return get<T>(data.data(), data.size(), 0, field);
}

std::int32_t read_i32(std::uintptr_t base, std::size_t offset, const char* field) {
    return read_at<std::int32_t>(base, offset, field);
}

std::uintptr_t read_ptr(std::uintptr_t base, std::size_t offset, const char* field) {
    return read_at<std::uint32_t>(base, offset, field);
}

std::int32_t inventory_count(std::uintptr_t player, std::uint32_t item_index) {
    const auto index_offset = static_cast<std::size_t>(kPlayerPurchases) + item_index * 4;
    const auto raw = read_i32(player, index_offset, "PlayerInfo.mPurchases");
    const auto count = static_cast<std::int64_t>(raw) - kPurchaseOffset;
    return static_cast<std::int32_t>(std::clamp<std::int64_t>(
        count, 0, std::numeric_limits<std::int32_t>::max()));
}

int public_garden_id(std::int32_t game_type) {
    if (game_type == kGardenMain) return 0;
    if (game_type == kGardenMushroom) return 1;
    if (game_type == kGardenAquarium) return 2;
    return -1; // game type 2 is the wheelbarrow, not a garden selector value
}

const char* garden_name(std::int32_t type) {
    switch (type) {
    case kGardenMain: return "main";
    case kGardenMushroom: return "mushroom";
    case kGardenWheelbarrow: return "wheelbarrow";
    case kGardenAquarium: return "aquarium";
    default: return "unknown";
    }
}

const char* need_name(std::int32_t need) {
    switch (need) {
    case 0: return "none";
    case 1: return "water";
    case 2: return "fertilizer";
    case 3: return "bug_spray";
    case 4: return "phonograph";
    default: return "unknown";
    }
}

const char* care_name(CareKind kind) {
    switch (kind) {
    case CareKind::water: return "water";
    case CareKind::fertilize: return "fertilize";
    case CareKind::bug_spray: return "bug_spray";
    case CareKind::phonograph: return "phonograph";
    }
    return "unknown";
}

std::int32_t care_need(CareKind kind) {
    switch (kind) {
    case CareKind::water: return kNeedWater;
    case CareKind::fertilize: return kNeedFertilizer;
    case CareKind::bug_spray: return kNeedBugSpray;
    case CareKind::phonograph: return kNeedPhonograph;
    }
    throw std::runtime_error("Unsupported garden care kind.");
}

std::int32_t care_cursor(CareKind kind) {
    switch (kind) {
    case CareKind::water: return kCursorWateringCan;
    case CareKind::fertilize: return kCursorFertilizer;
    case CareKind::bug_spray: return kCursorBugSpray;
    case CareKind::phonograph: return kCursorPhonograph;
    }
    throw std::runtime_error("Unsupported garden care kind.");
}

std::int32_t care_object(CareKind kind) {
    switch (kind) {
    case CareKind::water: return kObjectWateringCan;
    case CareKind::fertilize: return kObjectFertilizer;
    case CareKind::bug_spray: return kObjectBugSpray;
    case CareKind::phonograph: return kObjectPhonograph;
    }
    throw std::runtime_error("Unsupported garden care kind.");
}

bool has_care_inventory(std::uintptr_t player, CareKind kind) {
    switch (kind) {
    case CareKind::water: return true;
    case CareKind::fertilize: return inventory_count(player, kPurchaseFertilizer) > 0;
    case CareKind::bug_spray: return inventory_count(player, kPurchaseBugSpray) > 0;
    case CareKind::phonograph:
        return read_i32(player, kPlayerPurchases + kPurchasePhonograph * 4,
                        "PlayerInfo.phonograph") != 0;
    }
    return false;
}

// Exact 1096 ZenGarden::GetPlantsNeed entry. It receives the PottedPlant
// pointer in ESI and returns PottedPlantNeed in EAX; disassembly is documented
// in docs/ENTITY_LAYOUT.md. The routine and its called helpers only read state.
__declspec(noinline) std::int32_t get_plants_need(std::uintptr_t potted_plant) {
    std::int32_t need = kNeedNone;
    __asm {
        push esi
        mov esi, potted_plant
        mov edx, 052D4A0h
        call edx
        mov need, eax
        pop esi
    }
    return need;
}

struct BoardPlants {
    std::vector<std::uint8_t> bytes;
    std::uint32_t count = 0;
};

BoardPlants read_board_plants(std::uintptr_t board) {
    const auto header = read_bytes(add_address(board, kBoardPlants), 0x0C);
    const auto* h = header.data();
    const auto block = get<std::uint32_t>(h, header.size(), 0, "Board.mPlants.mBlock");
    const auto count = get<std::uint32_t>(h, header.size(), 4, "Board.mPlants.mMaxUsedCount");
    const auto max_size = get<std::uint32_t>(h, header.size(), 8, "Board.mPlants.mMaxSize");
    if (count > max_size || count > kMaxPlantSlots || max_size > kMaxPlantSlots)
        throw std::runtime_error("Invalid garden Board plant-array bounds.");
    BoardPlants out;
    out.count = count;
    if (!count) return out;
    if (!block || count > kMaxReadBytes / kPlantStride)
        throw std::runtime_error("Invalid garden Board plant-array storage.");
    out.bytes = read_bytes(block, static_cast<std::size_t>(count) * kPlantStride);
    return out;
}

struct BoardGridItems {
    std::vector<std::uint8_t> bytes;
    std::uint32_t count = 0;
};

BoardGridItems read_board_grid_items(std::uintptr_t board) {
    const auto header = read_bytes(add_address(board, kBoardGridItems), 0x0C);
    const auto* h = header.data();
    const auto block = get<std::uint32_t>(h, header.size(), 0, "Board.mGridItems.mBlock");
    const auto count = get<std::uint32_t>(h, header.size(), 4, "Board.mGridItems.mMaxUsedCount");
    const auto max_size = get<std::uint32_t>(h, header.size(), 8, "Board.mGridItems.mMaxSize");
    if (count > max_size || count > kMaxPlantSlots || max_size > kMaxPlantSlots)
        throw std::runtime_error("Invalid garden Board grid-item array bounds.");
    BoardGridItems out;
    out.count = count;
    if (!count) return out;
    if (!block || count > kMaxReadBytes / kGridItemStride)
        throw std::runtime_error("Invalid garden Board grid-item array storage.");
    out.bytes = read_bytes(block, static_cast<std::size_t>(count) * kGridItemStride);
    return out;
}

struct Runtime {
    std::uintptr_t garden = 0;
    std::uintptr_t board = 0;
    std::uintptr_t player = 0;
    std::int32_t game_mode = 0;
    std::int32_t game_scene = 0;
    std::int32_t garden_type = -1;
    std::int32_t plant_count = 0;
    std::vector<std::uint8_t> potted;
    BoardPlants board_plants;
    BoardGridItems board_grid_items;
};

Runtime read_runtime(std::uintptr_t app, bool with_board_plants) {
    if (!app) throw std::runtime_error("LawnApp is unavailable.");
    Runtime r;
    r.game_mode = read_i32(app, kAppMode, "LawnApp.mGameMode");
    r.game_scene = read_i32(app, kAppScene, "LawnApp.mGameUi");
    r.board = read_ptr(app, kAppBoard, "LawnApp.mBoard");
    r.garden = read_ptr(app, kAppZenGarden, "LawnApp.mZenGarden");
    r.player = read_ptr(app, kAppPlayerInfo, "LawnApp.mPlayerInfo");
    if (r.garden && r.game_mode == kGameModeZenGarden) {
        const auto garden_board = read_ptr(r.garden, kZenBoard, "ZenGarden.mBoard");
        r.garden_type = read_i32(r.garden, kZenGardenType, "ZenGarden.mGardenType");
        if (garden_board && r.board && garden_board != r.board)
            throw std::runtime_error("ZenGarden and LawnApp refer to different Boards.");
    }
    if (r.player) {
        r.plant_count = read_i32(r.player, kPlayerPlantCount, "PlayerInfo.mNumPottedPlants");
        if (r.plant_count < 0 || r.plant_count > static_cast<std::int32_t>(kMaxPottedPlants))
            throw std::runtime_error("Invalid potted-plant inventory count.");
        if (r.plant_count) {
            const auto bytes = static_cast<std::size_t>(r.plant_count) * kPottedPlantStride;
            r.potted = read_bytes(add_address(r.player, kPlayerPlants), bytes);
        }
    }
    if (with_board_plants && r.board && r.game_mode == kGameModeZenGarden)
        r.board_plants = read_board_plants(r.board);
    if (with_board_plants && r.board &&
        (r.game_mode == kGameModeZenGarden || r.game_mode == kGameModeTreeWisdom))
        r.board_grid_items = read_board_grid_items(r.board);
    return r;
}

const std::uint8_t* find_live_grid_item(const Runtime& r, std::uint32_t id,
                                       std::int32_t required_type) {
    const std::uint8_t* found = nullptr;
    for (std::uint32_t i = 0; i < r.board_grid_items.count; ++i) {
        const auto* item = r.board_grid_items.bytes.data() +
            static_cast<std::size_t>(i) * kGridItemStride;
        if (get<std::uint32_t>(item, kGridItemStride, kGridItemStride - 4,
                               "DataArrayItem.mID") != id ||
            get<std::uint8_t>(item, kGridItemStride, 0x20, "GridItem.mDead") != 0 ||
            get<std::int32_t>(item, kGridItemStride, 0x08, "GridItem.mGridItemType") != required_type)
            continue;
        if (found) return nullptr;
        found = item;
    }
    return found;
}

json inventory_json(std::uintptr_t player) {
    if (!player) return json::object();
    const auto coins = static_cast<std::int64_t>(
        read_i32(player, kPlayerCoins, "PlayerInfo.mCoins")) * 10;
    const auto gold_can = read_i32(player, kPlayerPurchases + kPurchaseGoldWateringCan * 4,
                                   "PlayerInfo.gold_watering_can");
    const auto phonograph = read_i32(player, kPlayerPurchases + kPurchasePhonograph * 4,
                                     "PlayerInfo.phonograph");
    const auto glove = read_i32(player, kPlayerPurchases + kPurchaseGlove * 4,
                                "PlayerInfo.gardening_glove");
    const auto wheelbarrow = read_i32(player, kPlayerPurchases + kPurchaseWheelbarrow * 4,
                                      "PlayerInfo.wheelbarrow");
    const auto mushroom_garden = read_i32(player, kPlayerPurchases + kPurchaseMushroomGarden * 4,
                                          "PlayerInfo.mushroom_garden");
    const auto aquarium = read_i32(player, kPlayerPurchases + kPurchaseAquarium * 4,
                                   "PlayerInfo.aquarium");
    const auto tree_wisdom = read_i32(player, kPlayerPurchases + kPurchaseTreeWisdom * 4,
                                      "PlayerInfo.tree_wisdom");
    const auto stinky_wake_timestamp = read_i32(player, kPlayerPurchases + kPurchaseStinky * 4,
                                                "PlayerInfo.stinky_wake_timestamp");
    return {{"coins", coins},
        {"fertilizer", inventory_count(player, kPurchaseFertilizer)},
        {"bug_spray", inventory_count(player, kPurchaseBugSpray)},
        {"phonograph_owned", phonograph != 0},
        {"gold_watering_can_owned", gold_can != 0},
        {"gardening_glove_owned", glove != 0},
        {"wheelbarrow_owned", wheelbarrow != 0},
        {"mushroom_garden_owned", mushroom_garden != 0},
        {"aquarium_owned", aquarium != 0},
        {"tree_of_wisdom_owned", tree_wisdom != 0},
        {"stinky_wake_timestamp", stinky_wake_timestamp},
        {"chocolate", inventory_count(player, kPurchaseChocolate)},
        {"tree_food", inventory_count(player, kPurchaseTreeFood)}};
}

json stinky_snapshot(const Runtime& r, bool garden_active) {
    if (!garden_active || r.garden_type != kGardenMain)
        return {{"available", false}, {"ownership_known", false}, {"owned", nullptr},
            {"present", nullptr}, {"id", nullptr}, {"state_code", nullptr},
            {"is_sleeping", nullptr}, {"is_falling_asleep", nullptr}};
    const std::uint8_t* found = nullptr;
    for (std::uint32_t i = 0; i < r.board_grid_items.count; ++i) {
        const auto* item = r.board_grid_items.bytes.data() + static_cast<std::size_t>(i) * kGridItemStride;
        if (get<std::uint8_t>(item, kGridItemStride, 0x20, "GridItem.mDead") != 0 ||
            get<std::int32_t>(item, kGridItemStride, 0x08, "GridItem.mGridItemType") != kGridItemStinky)
            continue;
        if (found) throw std::runtime_error("Multiple active Stinky GridItems were found.");
        found = item;
    }
    if (!found)
        return {{"available", true}, {"ownership_known", false}, {"owned", nullptr},
            {"present", false}, {"id", nullptr}, {"state_code", nullptr},
            {"is_sleeping", nullptr}, {"is_falling_asleep", nullptr}};
    const auto state = get<std::int32_t>(found, kGridItemStride, 0x0C, "Stinky.mState");
    const auto id = get<std::uint32_t>(found, kGridItemStride, kGridItemStride - 4,
                                       "Stinky.DataArrayID");
    return {{"available", true}, {"ownership_known", true}, {"owned", true},
        {"present", true}, {"id", id}, {"state_code", state},
        {"is_sleeping", state == kStinkySleeping},
        {"is_falling_asleep", state == kStinkyFallingAsleep},
        {"ownership_evidence", "active_main_garden_grid_item"}};
}

std::uint32_t plant_id_for_inventory(const Runtime& r, std::int32_t inventory_index) {
    const auto* data = r.board_plants.bytes.data();
    std::uint32_t found = 0;
    for (std::uint32_t i = 0; i < r.board_plants.count; ++i) {
        const auto* p = data + static_cast<std::size_t>(i) * kPlantStride;
        if (get<std::uint8_t>(p, kPlantStride, 0x141, "Plant.mDead") != 0) continue;
        if (get<std::int32_t>(p, kPlantStride, 0x13C, "Plant.mPottedPlantIndex") != inventory_index) continue;
        const auto id = get<std::uint32_t>(p, kPlantStride, kPlantStride - 4, "DataArrayItem.mID");
        if ((id & 0xFFFF0000u) == 0) continue;
        if (found) return 0; // Ambiguous duplicate inventory linkage is not a stable ID.
        found = id;
    }
    return found;
}

json snapshot_from(Runtime&& r) {
    json plants = json::array();
    const bool active = r.game_mode == kGameModeZenGarden && r.game_scene == 3 &&
        r.garden != 0 && r.board != 0;
    const auto public_id = public_garden_id(r.garden_type);
    for (std::int32_t i = 0; i < r.plant_count; ++i) {
        const auto* p = r.potted.data() + static_cast<std::size_t>(i) * kPottedPlantStride;
        const auto raw_type = get<std::int32_t>(p, kPottedPlantStride, 0x04, "PottedPlant.mWhichZenGarden");
        const auto plant_seed = get<std::int32_t>(p, kPottedPlantStride, 0x00, "PottedPlant.mSeedType");
        const auto need = get_plants_need(add_address(r.player,
            static_cast<std::size_t>(kPlayerPlants) + static_cast<std::size_t>(i) * kPottedPlantStride));
        const auto id = active && raw_type == r.garden_type ? plant_id_for_inventory(r, i) : 0;
        const auto x = get<std::int32_t>(p, kPottedPlantStride, 0x08, "PottedPlant.mX");
        const auto y = get<std::int32_t>(p, kPottedPlantStride, 0x0C, "PottedPlant.mY");
        const auto id_json = id ? json(id) : json(nullptr);
        plants.push_back({{"id", id_json}, {"inventory_slot", i + 1},
            {"garden_id", public_garden_id(raw_type) >= 0 ? json(public_garden_id(raw_type)) : json(nullptr)},
            {"garden_type", garden_name(raw_type)}, {"plant_type", plant_seed},
            {"row", static_cast<std::int64_t>(y) + 1},
            {"col", static_cast<std::int64_t>(x) + 1},
            {"need", need_name(need)}, {"need_code", need},
            {"stored_need", need_name(get<std::int32_t>(p, kPottedPlantStride, 0x30, "PottedPlant.mPlantNeed"))},
            {"age", get<std::int32_t>(p, kPottedPlantStride, 0x24, "PottedPlant.mPlantAge")},
            {"times_fed", get<std::int32_t>(p, kPottedPlantStride, 0x28, "PottedPlant.mTimesFed")},
            {"feedings_per_grow", get<std::int32_t>(p, kPottedPlantStride, 0x2C, "PottedPlant.mFeedingsPerGrow")},
            {"last_watered_unix", get<__time64_t>(p, kPottedPlantStride, 0x18, "PottedPlant.mLastWateredTime")},
            {"last_need_fulfilled_unix", get<__time64_t>(p, kPottedPlantStride, 0x38, "PottedPlant.mLastNeedFulfilledTime")},
            {"last_fertilized_unix", get<__time64_t>(p, kPottedPlantStride, 0x40, "PottedPlant.mLastFertilizedTime")},
            {"on_active_board", id != 0}});
    }
    const auto cursor = active ? cursor_type(r.board) : 0;
    json pending_target = nullptr;
    if (g_pending_care.active) {
        pending_target = {{"action", pending_action_name(g_pending_care)},
            {"plant_id", g_pending_care.plant_id ? json(g_pending_care.plant_id) : json(nullptr)},
            {"inventory_slot", g_pending_care.inventory_index >= 0 ?
                json(g_pending_care.inventory_index + 1) : json(nullptr)},
            {"sale_dialog_id", g_pending_care.sell_target && g_pending_care.dispatching ?
                json(kDialogZenSell) : json(nullptr)},
            {"potted_seed_type", g_pending_care.sell_target ? json(g_pending_care.sale_seed_type) : json(nullptr)},
            {"potted_garden_type", g_pending_care.sell_target ? json(g_pending_care.sale_garden_type) : json(nullptr)},
            {"potted_row", g_pending_care.sell_target ? json(g_pending_care.sale_y + 1) : json(nullptr)},
            {"potted_col", g_pending_care.sell_target ? json(g_pending_care.sale_x + 1) : json(nullptr)}};
    }
    return {{"garden_active", active},
        {"garden_id", public_id >= 0 ? json(public_id) : json(nullptr)},
        {"garden_type", garden_name(r.garden_type)}, {"potted_plants", std::move(plants)},
        {"inventory", inventory_json(r.player)}, {"stinky", stinky_snapshot(r, active)},
        {"observation_complete", true},
        {"observation_status", "layout_live_1096_garden_verified"},
        {"cursor_type", active ? json(cursor) : json(nullptr)},
        {"money_sign_available", active ? json(toolbar_object_available(r.board, kObjectMoneySign)) : json(nullptr)},
        {"care_pending", g_pending_care.active}, {"pending_action", std::move(pending_target)},
        {"garden_transition_pending", g_pending_garden_transition.active},
        {"pending_garden_id", g_pending_garden_transition.active ?
            json(g_pending_garden_transition.target_id) : json(nullptr)},
        {"pending_garden_advances", g_pending_garden_transition.active ?
            json(g_pending_garden_transition.advances) : json(nullptr)},
        {"last_garden_transition", g_last_garden_transition},
        {"sale_confirmation_pending", g_pending_care.active && g_pending_care.sell_target && g_pending_care.dispatching},
        {"sale_target_plant_id", g_pending_care.active && g_pending_care.sell_target ?
            json(g_pending_care.plant_id) : json(nullptr)},
        {"sale_target_inventory_slot", g_pending_care.active && g_pending_care.sell_target &&
            g_pending_care.inventory_index >= 0 ? json(g_pending_care.inventory_index + 1) : json(nullptr)},
        {"last_care_action", g_last_care_result}};
}

json tree_snapshot_from(Runtime&& r) {
    if (r.game_mode != kGameModeTreeWisdom || r.game_scene != 3 || !r.board || !r.player)
        return {{"tree_active", false}, {"observation_complete", false},
            {"observation_status", "tree_mode_not_active"}};
    const auto challenge = read_ptr(r.board, kBoardChallenge, "Board.mChallenge");
    if (!challenge || !readable(challenge, 0xBC))
        throw std::runtime_error("Tree of Wisdom Challenge state is unavailable.");
    const auto height_offset = static_cast<std::size_t>(0x58) +
        static_cast<std::size_t>(kGameModeTreeWisdom) * sizeof(std::int32_t);
    const auto height = read_i32(r.player, height_offset, "PlayerInfo tree height record");
    if (height < 0 || height > 1000000)
        throw std::runtime_error("Tree of Wisdom height record is outside supported bounds.");
    const auto challenge_state = read_i32(challenge, 0x54, "Challenge.mChallengeState");
    const auto state_counter = read_i32(challenge, 0x58, "Challenge.mChallengeStateCounter");
    const auto talk_index = read_i32(challenge, 0xB8, "Challenge.mTreeOfWisdomTalkIndex");
    std::int32_t active_tree_food_items = 0;
    for (std::uint32_t i = 0; i < r.board_grid_items.count; ++i) {
        const auto* item = r.board_grid_items.bytes.data() + static_cast<std::size_t>(i) * kGridItemStride;
        if (get<std::uint8_t>(item, kGridItemStride, 0x20, "GridItem.mDead") == 0 &&
            get<std::int32_t>(item, kGridItemStride, 0x08, "GridItem.mGridItemType") == kGridItemZenTool)
            ++active_tree_food_items;
    }
    const auto food_count = inventory_count(r.player, kPurchaseTreeFood);
    const auto paused = read_at<std::uint8_t>(r.board, kBoardPaused, "Board.mPaused") != 0;
    const bool hint_visible = challenge_state == 12 || challenge_state == 14;
    return {{"tree_active", true}, {"height", height},
        {"tree_food", food_count}, {"hint_visible", hint_visible},
        {"hint_index", hint_visible && talk_index > 0 ? json(talk_index) : json(nullptr)},
        {"hint_key", hint_visible && talk_index > 0 ?
            json("TREE_OF_WISDOM_" + std::to_string(talk_index)) : json(nullptr)},
        {"challenge_state", challenge_state}, {"state_counter", state_counter},
        {"active_tree_food_items", active_tree_food_items}, {"paused", paused},
        {"can_feed", !paused && food_count > 0 && challenge_state != 11 && active_tree_food_items == 0},
        {"observation_complete", true},
        {"observation_status", "tree_layout_verified_from_1096_text_and_runtime_structure"},
        {"care_pending", g_pending_care.active}, {"last_care_action", g_last_care_result}};
}

using BoardMouseDown = void(__thiscall*)(void*, int, int, int);

void widget_mouse_move(std::uintptr_t app, int x, int y) {
    const auto manager = read_ptr(app, kAppWidgetManager, "LawnApp.mWidgetManager");
    if (!manager) throw std::runtime_error("WidgetManager is unavailable.");
    if (read_i32(manager, 0x110, "WidgetManager.mDownButtons") != 0)
        throw std::runtime_error("Pointer movement is unsafe while a mouse button is held.");
    using Fn = bool(__thiscall*)(void*, int, int);
    if (!reinterpret_cast<Fn>(kWidgetMouseMove1096)(reinterpret_cast<void*>(manager), x, y))
        throw std::runtime_error("WidgetManager rejected the requested pointer movement.");
}

void widget_mouse_click_board(std::uintptr_t app, std::uintptr_t board, int x, int y) {
    const auto manager = read_ptr(app, kAppWidgetManager, "LawnApp.mWidgetManager");
    if (!manager || read_ptr(manager, 0, "WidgetManager vtable") != kWidgetManagerVtable1096 ||
        read_ptr(manager, 0x20, "WidgetManager owner") != app ||
        read_at<std::uint32_t>(manager, 0xA8, "WidgetManager.mOverWidget") != board ||
        read_i32(manager, 0x110, "WidgetManager.mDownButtons") != 0)
        throw std::runtime_error("The normal Board widget is not the safe left-click target.");
    using MouseButton = bool(__thiscall*)(void*, int, int, int);
    const auto down = reinterpret_cast<MouseButton>(kWidgetMouseDown1096);
    const auto up = reinterpret_cast<MouseButton>(kWidgetMouseUp1096);
    (void)down(reinterpret_cast<void*>(manager), x, y, 0);
    bool accepted = false;
    try {
        accepted = (read_i32(manager, 0x110, "WidgetManager.mDownButtons after left down") & 1) != 0 &&
            read_ptr(manager, 0xA4, "WidgetManager.mLastDownWidget") == board;
    } catch (...) {
        (void)up(reinterpret_cast<void*>(manager), x, y, 0);
        throw;
    }
    (void)up(reinterpret_cast<void*>(manager), x, y, 0);
    if (!accepted) throw std::runtime_error("WidgetManager did not retain the intended Board click target.");
    if ((read_i32(manager, 0x110, "WidgetManager.mDownButtons after left up") & 1) != 0)
        throw std::runtime_error("WidgetManager did not release the normal left click.");
}

std::int32_t cursor_type(std::uintptr_t board) {
    const auto cursor = read_ptr(board, kBoardCursor, "Board.mCursor");
    if (!cursor) return 0;
    return read_i32(cursor, kCursorType, "CursorObject.mType");
}

struct Rect { int x, y, width, height; };

struct GardenCell { int col; int row; int x; int y; };
constexpr std::array<GardenCell, 32> kMainCells{{
    {0,0,73,73},{1,0,155,71},{2,0,239,68},{3,0,321,73},{4,0,406,71},{5,0,484,67},{6,0,566,70},{7,0,648,72},
    {0,1,67,168},{1,1,150,165},{2,1,232,170},{3,1,314,175},{4,1,416,173},{5,1,497,170},{6,1,578,164},{7,1,660,168},
    {0,2,41,268},{1,2,130,266},{2,2,219,260},{3,2,310,266},{4,2,416,267},{5,2,504,261},{6,2,594,265},{7,2,684,269},
    {0,3,37,371},{1,3,124,369},{2,3,211,368},{3,3,302,369},{4,3,425,375},{5,3,512,368},{6,3,602,365},{7,3,691,368}}};
constexpr std::array<GardenCell, 8> kMushroomCells{{
    {0,0,110,441},{1,0,237,360},{2,0,298,458},{3,0,355,296},{4,0,387,203},{5,0,460,385},{6,0,486,478},{7,0,552,283}}};
constexpr std::array<GardenCell, 8> kAquariumCells{{
    {0,0,113,185},{1,0,306,120},{2,0,356,270},{3,0,622,120},{4,0,669,270},{5,0,122,355},{6,0,365,458},{7,0,504,417}}};

GardenCell garden_cell(std::int32_t garden_type, std::int32_t row, std::int32_t col) {
    const int wanted_row = row - 1, wanted_col = col - 1;
    auto find = [&](const auto& cells) -> GardenCell {
        for (const auto& cell : cells)
            if (cell.row == wanted_row && cell.col == wanted_col) return cell;
        throw std::runtime_error("The requested row and column are outside this garden's valid cells.");
    };
    if (garden_type == kGardenMain) return find(kMainCells);
    if (garden_type == kGardenMushroom) return find(kMushroomCells);
    if (garden_type == kGardenAquarium) return find(kAquariumCells);
    throw std::runtime_error("The wheelbarrow is not a placement garden.");
}

bool inventory_cell_occupied(const Runtime& r, std::int32_t row, std::int32_t col,
                             std::int32_t except_index = -1) {
    for (std::int32_t i = 0; i < r.plant_count; ++i) {
        if (i == except_index) continue;
        const auto* pot = r.potted.data() + static_cast<std::size_t>(i) * kPottedPlantStride;
        if (get<std::int32_t>(pot, kPottedPlantStride, 0x04, "PottedPlant.mWhichZenGarden") == r.garden_type &&
            get<std::int32_t>(pot, kPottedPlantStride, 0x0C, "PottedPlant.mY") == row - 1 &&
            get<std::int32_t>(pot, kPottedPlantStride, 0x08, "PottedPlant.mX") == col - 1)
            return true;
    }
    return false;
}

Rect shovel_rect(std::uintptr_t board) {
    Rect r{};
    Rect* result_rect = &r;
    __asm {
        mov edx, board
        mov eax, result_rect
        mov ecx, 040E2D0h
        call ecx
    }
    return r;
}

Rect zen_button_rect(std::uintptr_t board, int object_type) {
    // This helper only adjusts the x coordinate. The common tool-bar Rect is
    // initialized by the preceding ShovelButtonRect call, exactly as in the
    // game's 1096 Board::MouseHitTest path.
    Rect r = shovel_rect(board);
    // Board::MouseHitTest at 0x414F90 special-cases object 15 and writes
    // x=0x234 (564), bypassing the dynamic toolbar-position helper.
    if (object_type == kObjectNextGarden) {
        r.x = 0x234;
        return r;
    }
    int* x_out = &r.x;
    __asm {
        push edi
        push object_type
        push board
        mov edi, x_out
        mov eax, 040E360h
        call eax
        pop edi
    }
    return r;
}

bool point_in_rect(const Rect& r, int x, int y) {
    return r.width > 0 && r.height > 0 && x >= r.x && y >= r.y &&
        x < r.x + r.width && y < r.y + r.height;
}

bool toolbar_object_available(std::uintptr_t board, std::int32_t object_type) {
    if (!board) return false;
    const Rect rect = zen_button_rect(board, object_type);
    const int x = rect.x + rect.width / 2;
    const int y = rect.y + rect.height / 2;
    return point_in_rect(rect, x, y) && hit_test_object(board, x, y, object_type);
}

bool hit_test_object(std::uintptr_t board, int x, int y, std::int32_t expected) {
    // 1096 Board::MouseHitTest uses the public HitResult layout: object type
    // begins at +4. Its callee removes four stack arguments (ret 0x10).
    std::uint8_t result[0x20]{};
    __asm {
        lea eax, result
        push eax
        push y
        push x
        push board
        mov ecx, 0414C40h
        call ecx
    }
    std::int32_t object_type = 0;
    std::memcpy(&object_type, result + 4, sizeof(object_type));
    return object_type == expected;
}

void select_tool(std::uintptr_t app, std::uintptr_t board, std::int32_t object_type,
                 std::int32_t cursor_expected) {
    const Rect rect = zen_button_rect(board, object_type);
    const int x = rect.x + rect.width / 2;
    const int y = rect.y + rect.height / 2;
    widget_mouse_move(app, x, y);
    if (!point_in_rect(rect, x, y) || !hit_test_object(board, x, y, object_type))
        throw std::runtime_error("The requested garden tool button is unavailable.");
    reinterpret_cast<BoardMouseDown>(kBoardMouseDown1096)(
        reinterpret_cast<void*>(board), x, y, 1);
    if (cursor_type(board) != cursor_expected)
        throw std::runtime_error("The normal toolbar path did not select the requested tool.");
}

__declspec(noinline) void invoke_garden_tool(std::uintptr_t garden, int x, int y,
                                              std::int32_t cursor_type) {
    __asm {
        mov ecx, garden
        mov eax, cursor_type
        push y
        push x
        mov edx, 052DFF0h
        call edx
    }
}

std::uintptr_t get_app_dialog(std::uintptr_t app, std::int32_t dialog_id) {
    const auto vtable = read_ptr(app, 0, "LawnApp vtable");
    const auto method = read_ptr(vtable, 0x124, "LawnApp.GetDialog vtable slot");
    if (method < 0x401000 || method >= 0x6D0000)
        throw std::runtime_error("Unexpected LawnApp.GetDialog target.");
    using GetDialog = void*(__thiscall*)(void*, int);
    return reinterpret_cast<std::uintptr_t>(
        reinterpret_cast<GetDialog>(method)(reinterpret_cast<void*>(app), dialog_id));
}

int app_dialog_count(std::uintptr_t app) {
    const auto vtable = read_ptr(app, 0, "LawnApp vtable");
    const auto method = read_ptr(vtable, 0x13C, "LawnApp.GetDialogCount vtable slot");
    if (method < 0x401000 || method >= 0x6D0000)
        throw std::runtime_error("Unexpected LawnApp.GetDialogCount target.");
    using GetDialogCount = int(__thiscall*)(void*);
    const auto result = reinterpret_cast<GetDialogCount>(method)(reinterpret_cast<void*>(app));
    if (result < 0 || result > 64) throw std::runtime_error("Invalid LawnApp dialog count.");
    return result;
}

std::uintptr_t verified_sale_dialog(std::uintptr_t app) {
    // The active Garden has no parent dialog; require this exact modal to be
    // the only application dialog before sending a key to it.
    if (app_dialog_count(app) != 1) return 0;
    const auto dialog = get_app_dialog(app, kDialogZenSell);
    if (!dialog || !readable(dialog, 0x170)) return 0;
    const auto vtable = read_ptr(dialog, 0, "Zen Sell dialog vtable");
    if (read_i32(dialog, 0x158, "Dialog.mDialogId") != kDialogZenSell ||
        read_ptr(dialog, 0x16C, "Dialog.mApp") != app ||
        read_ptr(vtable, 0xC0, "Zen Sell dialog key handler") != kDialogKeyDown1096)
        return 0;
    return dialog;
}

bool sale_target_intact(std::uintptr_t app, const PendingCare& pending) {
    if (app != pending.app) return false;
    Runtime r;
    try { r = read_runtime(app, true); }
    catch (...) { return false; }
    if (r.game_mode != kGameModeZenGarden || r.game_scene != 3 || r.board != pending.board ||
        r.garden != pending.garden || r.player != pending.player || r.plant_count != pending.plant_count_before)
        return false;
    const std::uint8_t* found = nullptr;
    for (std::uint32_t i = 0; i < r.board_plants.count; ++i) {
        const auto* p = r.board_plants.bytes.data() + static_cast<std::size_t>(i) * kPlantStride;
        if (get<std::uint32_t>(p, kPlantStride, kPlantStride - 4, "Plant.DataArrayID") == pending.plant_id &&
            get<std::uint8_t>(p, kPlantStride, 0x141, "Plant.mDead") == 0 &&
            get<std::int32_t>(p, kPlantStride, 0x13C, "Plant.mPottedPlantIndex") == pending.inventory_index) {
            if (found) return false;
            found = p;
        }
    }
    if (!found || pending.inventory_index < 0 || pending.inventory_index >= r.plant_count) return false;
    const auto* pot = r.potted.data() + static_cast<std::size_t>(pending.inventory_index) * kPottedPlantStride;
    return get<std::int32_t>(pot, kPottedPlantStride, 0x00, "PottedPlant.mSeedType") == pending.sale_seed_type &&
        get<std::int32_t>(pot, kPottedPlantStride, 0x04, "PottedPlant.mWhichZenGarden") == pending.sale_garden_type &&
        get<std::int32_t>(pot, kPottedPlantStride, 0x08, "PottedPlant.mX") == pending.sale_x &&
        get<std::int32_t>(pot, kPottedPlantStride, 0x0C, "PottedPlant.mY") == pending.sale_y;
}

void send_dialog_key(std::uintptr_t dialog, int key) {
    using KeyDown = void(__thiscall*)(void*, int);
    reinterpret_cast<KeyDown>(kDialogKeyDown1096)(reinterpret_cast<void*>(dialog), key);
}
} // namespace

json snapshot(std::uintptr_t app) {
    return snapshot_from(read_runtime(app, true));
}

json tree_snapshot(std::uintptr_t app) {
    return tree_snapshot_from(read_runtime(app, true));
}

json care(std::uintptr_t app, std::uint32_t plant_id, CareKind kind) {
    if (g_pending_garden_transition.active) throw std::runtime_error("A garden transition is already queued.");
    if (g_pending_care.active)
        throw std::runtime_error("A garden care action is already queued.");
    const auto expected_need = care_need(kind);
    if (!plant_id || (plant_id & 0xFFFF0000u) == 0)
        throw std::runtime_error("A current full DataArray plant ID is required.");
    auto r = read_runtime(app, true);
    if (r.game_mode != kGameModeZenGarden || r.game_scene != 3 || !r.garden || !r.board || !r.player)
        throw std::runtime_error("Care is available only on an active Zen Garden board.");
    if (r.garden_type < kGardenMain || r.garden_type > kGardenAquarium ||
        r.garden_type == kGardenWheelbarrow)
        throw std::runtime_error("The current garden is not a care garden.");
    if (read_at<std::uint8_t>(r.board, kBoardPaused, "Board.mPaused") != 0)
        throw std::runtime_error("Garden care is unavailable while the game is paused.");

    const auto count = static_cast<std::uint32_t>(r.plant_count);
    const auto* bytes = r.board_plants.bytes.data();
    const std::uint8_t* target = nullptr;
    bool duplicate_id = false;
    std::int32_t inventory_index = -1;
    for (std::uint32_t i = 0; i < r.board_plants.count; ++i) {
        const auto* p = bytes + static_cast<std::size_t>(i) * kPlantStride;
        const auto id = get<std::uint32_t>(p, kPlantStride, kPlantStride - 4, "DataArrayItem.mID");
        if (id != plant_id) continue;
        if (target) {
            duplicate_id = true;
            break;
        }
        if (get<std::uint8_t>(p, kPlantStride, 0x141, "Plant.mDead") != 0)
            throw std::runtime_error("The target plant is no longer active.");
        inventory_index = get<std::int32_t>(p, kPlantStride, 0x13C, "Plant.mPottedPlantIndex");
        target = p;
        break;
    }
    if (duplicate_id || !target || inventory_index < 0 || static_cast<std::uint32_t>(inventory_index) >= count)
        throw std::runtime_error("The plant ID is not present in the active garden.");
    const auto* pot = r.potted.data() + static_cast<std::size_t>(inventory_index) * kPottedPlantStride;
    if (get<std::int32_t>(pot, kPottedPlantStride, 0x04, "PottedPlant.mWhichZenGarden") != r.garden_type)
        throw std::runtime_error("The target plant belongs to a different garden.");
    const auto pot_address = add_address(r.player, static_cast<std::size_t>(kPlayerPlants) +
        static_cast<std::size_t>(inventory_index) * kPottedPlantStride);
    const auto need = get_plants_need(pot_address);
    if (need != expected_need)
        throw std::runtime_error("The target plant does not currently need that care tool.");
    if (!has_care_inventory(r.player, kind))
        throw std::runtime_error("The requested garden-care supply is unavailable.");

    const auto plant_x = get<std::int32_t>(target, kPlantStride, 0x08, "GameObject.mX");
    const auto plant_y = get<std::int32_t>(target, kPlantStride, 0x0C, "GameObject.mY");
    const auto plant_width = get<std::int32_t>(target, kPlantStride, 0x10, "GameObject.mWidth");
    const auto plant_height = get<std::int32_t>(target, kPlantStride, 0x14, "GameObject.mHeight");
    const auto plant_type = get<std::int32_t>(target, kPlantStride, 0x24, "Plant.mSeedType");
    if (plant_x < 0 || plant_x >= 800 || plant_y < 0 || plant_y >= 600 ||
        plant_width <= 0 || plant_width > 800 || plant_height <= 0 || plant_height > 600)
        throw std::runtime_error("Target plant hitbox is outside the supported game surface.");
    // Plant::GetPlantRect semantics: most plants trim 10 px from each side;
    // Pumpkin uses its full left edge and Tallnut its full width. Cob Cannon
    // has a wider fixed rectangle. The later highlight check is authoritative.
    int hit_x = plant_x + 10;
    int hit_width = plant_width - 20;
    if (plant_type == 30) hit_x = plant_x;                 // PUMPKINSHELL
    if (plant_type == 23) hit_width = plant_width;         // TALLNUT
    if (plant_type == 47) { hit_x = plant_x; hit_width = 140; } // COBCANNON
    const int hit_height = plant_type == 47 ? 80 : plant_height;
    if (hit_width <= 0 || hit_height <= 0)
        throw std::runtime_error("Target plant has an invalid game hitbox.");
    const auto cursor_type = care_cursor(kind);
    const int x = hit_x + hit_width / 2;
    const int y = plant_y + hit_height / 2;
    const auto object_type = care_object(kind);

    // Use the normal toolbar hit-test and Board::MouseDown route. This lets
    // the game enforce tool ownership and select its CursorObject normally.
    select_tool(app, r.board, object_type, cursor_type);
    widget_mouse_move(app, x, y);

    g_pending_care.active = true;
    g_pending_care.app = app;
    g_pending_care.board = r.board;
    g_pending_care.garden = r.garden;
    g_pending_care.player = r.player;
    g_pending_care.plant_id = plant_id;
    g_pending_care.snail_target = false;
    g_pending_care.inventory_index = inventory_index;
    g_pending_care.kind = kind;
    g_pending_care.cursor_type = cursor_type;
    g_pending_care.x = x;
    g_pending_care.y = y;
    g_pending_care.started_ms = GetTickCount64();
    g_pending_care.ticks = 0;
    g_last_care_result = {{"status", "queued"}, {"accepted", true},
        {"plant_id", plant_id}, {"care", care_name(kind)},
        {"verification_required", true}};
    return g_last_care_result;
}

json feed_snail(std::uintptr_t app, std::uint32_t stinky_id) {
    if (g_pending_garden_transition.active) throw std::runtime_error("A garden transition is already queued.");
    if (g_pending_care.active)
        throw std::runtime_error("A garden care action is already queued.");
    if (!stinky_id || (stinky_id & 0xFFFF0000u) == 0)
        throw std::runtime_error("A current full Stinky GridItem ID is required.");
    auto r = read_runtime(app, true);
    if (r.game_mode != kGameModeZenGarden || r.game_scene != 3 || !r.garden || !r.board || !r.player ||
        r.garden_type != kGardenMain)
        throw std::runtime_error("Stinky can only be fed in the active main Zen Garden.");
    if (read_at<std::uint8_t>(r.board, kBoardPaused, "Board.mPaused") != 0)
        throw std::runtime_error("Snail care is unavailable while the game is paused.");
    if (inventory_count(r.player, kPurchaseChocolate) <= 0)
        throw std::runtime_error("No chocolate is available in the garden inventory.");
    const auto* stinky = find_live_grid_item(r, stinky_id, kGridItemStinky);
    if (!stinky)
        throw std::runtime_error("The Stinky ID is not present in the active garden.");
    const auto stinky_state = get<std::int32_t>(stinky, kGridItemStride, 0x0C, "GridItem.mGridItemState");
    if (stinky_state != kStinkySleeping && stinky_state != kStinkyFallingAsleep)
        throw std::runtime_error("Stinky is not asleep and does not need chocolate to wake.");
    const auto px = get<float>(stinky, kGridItemStride, 0x24, "GridItem.mPosX");
    const auto py = get<float>(stinky, kGridItemStride, 0x28, "GridItem.mPosY");
    if (!std::isfinite(px) || !std::isfinite(py) || px < 6.0f || py < 10.0f ||
        px + 78.0f >= 800.0f || py + 80.0f >= 600.0f)
        throw std::runtime_error("Stinky hitbox is outside the supported game surface.");
    // Board::MouseHitTest uses Rect(mPosX-6,mPosY-10,84,90) for Stinky.
    const int x = static_cast<int>(std::lround(px + 36.0f));
    const int y = static_cast<int>(std::lround(py + 35.0f));
    select_tool(app, r.board, kObjectChocolate, kCursorChocolate);
    widget_mouse_move(app, x, y);
    if (!hit_test_object(r.board, x, y, kObjectStinky))
        throw std::runtime_error("The current Board hit-test did not resolve the Stinky target.");

    g_pending_care.active = true;
    g_pending_care.app = app;
    g_pending_care.board = r.board;
    g_pending_care.garden = r.garden;
    g_pending_care.player = r.player;
    g_pending_care.plant_id = stinky_id;
    g_pending_care.snail_target = true;
    g_pending_care.inventory_index = -1;
    g_pending_care.kind = CareKind::water; // the separate target flag selects chocolate dispatch
    g_pending_care.cursor_type = kCursorChocolate;
    g_pending_care.x = x;
    g_pending_care.y = y;
    g_pending_care.started_ms = GetTickCount64();
    g_pending_care.ticks = 0;
    g_last_care_result = {{"status", "queued"}, {"accepted", true},
        {"stinky_id", stinky_id}, {"care", "feed_snail"}, {"tool", "chocolate"},
        {"verification_required", true}};
    return g_last_care_result;
}

json wake_snail(std::uintptr_t app, std::uint32_t stinky_id) {
    if (g_pending_garden_transition.active) throw std::runtime_error("A garden transition is already queued.");
    if (g_pending_care.active) throw std::runtime_error("A garden action is already queued.");
    if (!stinky_id || (stinky_id & 0xFFFF0000u) == 0)
        throw std::runtime_error("A current full Stinky GridItem ID is required.");
    auto r = read_runtime(app, true);
    if (r.game_mode != kGameModeZenGarden || r.game_scene != 3 || !r.garden || !r.board || !r.player ||
        r.garden_type != kGardenMain)
        throw std::runtime_error("Stinky can only be woken in the active main Zen Garden.");
    if (read_at<std::uint8_t>(r.board, kBoardPaused, "Board.mPaused") != 0)
        throw std::runtime_error("Snail wake is unavailable while paused.");
    if (cursor_type(r.board) != 0)
        throw std::runtime_error("Clear the active tool cursor before waking Stinky.");
    const auto* stinky = find_live_grid_item(r, stinky_id, kGridItemStinky);
    if (!stinky) throw std::runtime_error("The Stinky ID is not present in the active garden.");
    const auto state = get<std::int32_t>(stinky, kGridItemStride, 0x0C, "GridItem.mGridItemState");
    if (state != kStinkySleeping)
        throw std::runtime_error("Stinky is not currently sleeping; wake_snail requires a snoozing snail.");
    const auto px = get<float>(stinky, kGridItemStride, 0x24, "GridItem.mPosX");
    const auto py = get<float>(stinky, kGridItemStride, 0x28, "GridItem.mPosY");
    if (!std::isfinite(px) || !std::isfinite(py) || px < 6.0f || py < 10.0f ||
        px + 78.0f >= 800.0f || py + 80.0f >= 600.0f)
        throw std::runtime_error("Stinky hitbox is outside the supported game surface.");
    const int x = static_cast<int>(std::lround(px + 36.0f));
    const int y = static_cast<int>(std::lround(py + 35.0f));
    widget_mouse_move(app, x, y);
    if (!hit_test_object(r.board, x, y, kObjectStinky))
        throw std::runtime_error("The normal Board hit-test did not resolve sleeping Stinky.");
    const auto timestamp_before = read_i32(r.player, kPlayerPurchases + kPurchaseStinky * 4,
                                            "PlayerInfo Stinky wake timestamp before click");
    widget_mouse_click_board(app, r.board, x, y);
    const auto timestamp_after = read_i32(r.player, kPlayerPurchases + kPurchaseStinky * 4,
                                           "PlayerInfo Stinky wake timestamp after click");
    return {{"status", "dispatched"}, {"accepted", true}, {"action", "wake_snail"},
        {"stinky_id", stinky_id}, {"chocolate_cost", 0},
        {"wake_timestamp_before", timestamp_before}, {"wake_timestamp_after", timestamp_after},
        {"normal_board_click", true}, {"normal_handler_invoked", true},
        {"wake_timestamp_changed", timestamp_after != timestamp_before}};
}

json cursor_cancel(std::uintptr_t app) {
    if (g_pending_garden_transition.active) throw std::runtime_error("A garden transition is already queued.");
    if (g_pending_care.active) throw std::runtime_error("Finish or cancel the pending garden action first.");
    auto r = read_runtime(app, false);
    if ((r.game_mode != kGameModeZenGarden && r.game_mode != kGameModeTreeWisdom) ||
        r.game_scene != 3 || !r.board || !r.player)
        throw std::runtime_error("Cursor cancellation is available only on an active garden board.");
    if (read_at<std::uint8_t>(r.board, kBoardPaused, "Board.mPaused") != 0)
        throw std::runtime_error("Cursor cancellation is unavailable while paused.");
    if (app_dialog_count(app) != 0) throw std::runtime_error("Close the active dialog before cancelling the cursor.");
    const auto selected = cursor_type(r.board);
    if (selected == 0) return {{"status", "already_clear"}, {"accepted", true}, {"cursor_type", 0}};
    const auto manager = read_ptr(app, kAppWidgetManager, "LawnApp.mWidgetManager");
    if (!manager || read_ptr(manager, 0, "WidgetManager vtable") != kWidgetManagerVtable1096 ||
        read_ptr(manager, 0x20, "WidgetManager owner") != app ||
        read_at<std::uint32_t>(manager, 0xA8, "WidgetManager.mOverWidget") != r.board ||
        read_at<std::uint8_t>(manager, 0x104, "WidgetManager.mMouseIn") == 0)
        throw std::runtime_error("The current pointer is not over the active Board surface.");
    if (read_i32(manager, 0x110, "WidgetManager.mDownButtons") != 0)
        throw std::runtime_error("Refusing to interfere with a held mouse button.");
    const auto x = read_i32(manager, 0x108, "WidgetManager.mLastMouseX");
    const auto y = read_i32(manager, 0x10C, "WidgetManager.mLastMouseY");
    if (x < 0 || x >= 800 || y < 0 || y >= 600)
        throw std::runtime_error("The current pointer is outside the supported game surface.");
    using MouseButton = bool(__thiscall*)(void*, int, int, int);
    const auto down = reinterpret_cast<MouseButton>(kWidgetMouseDown1096);
    const auto up = reinterpret_cast<MouseButton>(kWidgetMouseUp1096);
    (void)down(reinterpret_cast<void*>(manager), x, y, -1);
    bool accepted = false;
    try {
        accepted = (read_i32(manager, 0x110, "WidgetManager.mDownButtons after right down") & 2) != 0;
    } catch (...) {
        (void)up(reinterpret_cast<void*>(manager), x, y, -1);
        throw;
    }
    (void)up(reinterpret_cast<void*>(manager), x, y, -1);
    if (!accepted) throw std::runtime_error("WidgetManager did not accept the target's right-button code.");
    const auto cursor_after = cursor_type(r.board);
    const auto buttons_after_up = read_i32(manager, 0x110, "WidgetManager.mDownButtons after right up");
    if ((buttons_after_up & 2) != 0)
        throw std::runtime_error("WidgetManager did not release the target's right button.");
    return {{"status", cursor_after == 0 ? "cancelled" : "not_cancelled"},
        {"accepted", cursor_after == 0}, {"cursor_type_before", selected},
        {"cursor_type_after", cursor_after}, {"native_right_button", -1},
        {"normal_widget_manager_route", true}};
}

namespace {
json sale_decision(std::uintptr_t app, std::uint32_t plant_id, bool confirm) {
    if (!g_pending_care.active || !g_pending_care.sell_target || !g_pending_care.dispatching)
        throw std::runtime_error("No sale dialog is currently pending confirmation.");
    if (!plant_id || plant_id != g_pending_care.plant_id)
        throw std::runtime_error("The supplied full Plant ID does not match the pending sale target.");
    if (app != g_pending_care.app)
        throw std::runtime_error("The pending sale belongs to a different LawnApp.");
    if (g_pending_care.sale_decision_sent)
        throw std::runtime_error("A decision has already been sent to the pending sale dialog.");
    const auto dialog = verified_sale_dialog(app);
    if (!dialog) throw std::runtime_error("The exact Zen Sell dialog 47/key-handler pair is not active.");
    const bool intact = sale_target_intact(app, g_pending_care);
    g_pending_care.sale_decision_sent = true;
    if (!intact) {
        send_dialog_key(dialog, kDialogEscape);
        return {{"status", "target_drift_cancelled"}, {"accepted", false},
            {"action", "sell_plant"}, {"plant_id", plant_id},
            {"dialog_id", kDialogZenSell}, {"decision", "cancel"},
            {"reason", "The pending plant DataArray ID, potted slot, or potted identity changed; the exact dialog was cancelled."}};
    }
    send_dialog_key(dialog, confirm ? kDialogYes : kDialogEscape);
    return {{"status", "decision_dispatched"}, {"accepted", true},
        {"action", "sell_plant"}, {"plant_id", plant_id}, {"dialog_id", kDialogZenSell},
        {"decision", confirm ? "confirm" : "cancel"}, {"normal_dialog_key_handler", true},
        {"verification_pending", true}};
}
} // namespace

json confirm_sale(std::uintptr_t app, std::uint32_t plant_id) {
    return sale_decision(app, plant_id, true);
}

json cancel_sale(std::uintptr_t app, std::uint32_t plant_id) {
    return sale_decision(app, plant_id, false);
}

json sell_plant(std::uintptr_t app, std::uint32_t plant_id) {
    if (g_pending_garden_transition.active) throw std::runtime_error("A garden transition is already queued.");
    if (g_pending_care.active)
        throw std::runtime_error("A garden action is already queued.");
    if (!plant_id || (plant_id & 0xFFFF0000u) == 0)
        throw std::runtime_error("A current full DataArray plant ID is required.");
    const auto guarded_profile = read_ptr(app, kAppPlayerInfo, "LawnApp.mPlayerInfo");
    const auto profile_id = read_at<std::uint32_t>(guarded_profile, 0x20, "PlayerInfo.mId");
    profiles::authorize_test_profile(app, profile_id, "delete");
    auto r = read_runtime(app, true);
    if (r.game_mode != kGameModeZenGarden || r.game_scene != 3 || !r.garden || !r.board || !r.player ||
        r.garden_type < kGardenMain || r.garden_type > kGardenAquarium ||
        r.garden_type == kGardenWheelbarrow)
        throw std::runtime_error("Selling is available only on an active plant garden board.");
    if (read_at<std::uint8_t>(r.board, kBoardPaused, "Board.mPaused") != 0)
        throw std::runtime_error("Selling is unavailable while the game is paused.");
    if (!toolbar_object_available(r.board, kObjectMoneySign))
        throw std::runtime_error("The normal money-sign toolbar button is unavailable.");

    const std::uint8_t* target = nullptr;
    std::int32_t inventory_index = -1;
    for (std::uint32_t i = 0; i < r.board_plants.count; ++i) {
        const auto* p = r.board_plants.bytes.data() + static_cast<std::size_t>(i) * kPlantStride;
        if (get<std::uint32_t>(p, kPlantStride, kPlantStride - 4, "DataArrayItem.mID") != plant_id)
            continue;
        if (target) throw std::runtime_error("The plant ID is ambiguous in this garden.");
        if (get<std::uint8_t>(p, kPlantStride, 0x141, "Plant.mDead") != 0)
            throw std::runtime_error("The target plant is no longer active.");
        inventory_index = get<std::int32_t>(p, kPlantStride, 0x13C, "Plant.mPottedPlantIndex");
        target = p;
    }
    if (!target || inventory_index < 0 || inventory_index >= r.plant_count)
        throw std::runtime_error("The plant ID does not identify a potted garden plant.");
    const auto* pot = r.potted.data() + static_cast<std::size_t>(inventory_index) * kPottedPlantStride;
    if (get<std::int32_t>(pot, kPottedPlantStride, 0x04, "PottedPlant.mWhichZenGarden") != r.garden_type)
        throw std::runtime_error("The target plant belongs to a different garden.");

    const auto x0 = get<std::int32_t>(target, kPlantStride, 0x08, "GameObject.mX");
    const auto y0 = get<std::int32_t>(target, kPlantStride, 0x0C, "GameObject.mY");
    const auto width = get<std::int32_t>(target, kPlantStride, 0x10, "GameObject.mWidth");
    const auto height = get<std::int32_t>(target, kPlantStride, 0x14, "GameObject.mHeight");
    const auto plant_type = get<std::int32_t>(target, kPlantStride, 0x24, "Plant.mSeedType");
    if (x0 < 0 || x0 >= 800 || y0 < 0 || y0 >= 600 || width <= 0 || width > 800 || height <= 0 || height > 600)
        throw std::runtime_error("Target plant hitbox is outside the supported game surface.");
    int hit_x = x0 + 10;
    int hit_width = width - 20;
    if (plant_type == 30) hit_x = x0;
    if (plant_type == 23) hit_width = width;
    if (plant_type == 47) { hit_x = x0; hit_width = 140; }
    const int hit_height = plant_type == 47 ? 80 : height;
    if (hit_width <= 0 || hit_height <= 0)
        throw std::runtime_error("Target plant has an invalid game hitbox.");
    const int x = hit_x + hit_width / 2;
    const int y = y0 + hit_height / 2;

    // The 1096 normal money-sign handler asks the game to open Zen Sell
    // dialog 47. This only stages the exact target; a separate confirm_sale or
    // cancel_sale action must resolve that dialog through its ordinary key handler.
    select_tool(app, r.board, kObjectMoneySign, kCursorMoneySign);
    widget_mouse_move(app, x, y);
    g_pending_care.active = true;
    g_pending_care.app = app;
    g_pending_care.board = r.board;
    g_pending_care.garden = r.garden;
    g_pending_care.player = r.player;
    g_pending_care.plant_id = plant_id;
    g_pending_care.sell_target = true;
    g_pending_care.inventory_index = inventory_index;
    g_pending_care.plant_count_before = r.plant_count;
    g_pending_care.sale_seed_type = get<std::int32_t>(pot, kPottedPlantStride, 0x00, "PottedPlant.mSeedType");
    g_pending_care.sale_garden_type = get<std::int32_t>(pot, kPottedPlantStride, 0x04, "PottedPlant.mWhichZenGarden");
    g_pending_care.sale_x = get<std::int32_t>(pot, kPottedPlantStride, 0x08, "PottedPlant.mX");
    g_pending_care.sale_y = get<std::int32_t>(pot, kPottedPlantStride, 0x0C, "PottedPlant.mY");
    g_pending_care.cursor_type = kCursorMoneySign;
    g_pending_care.x = x;
    g_pending_care.y = y;
    g_pending_care.started_ms = GetTickCount64();
    g_pending_care.ticks = 0;
    g_last_care_result = {{"status", "queued"}, {"accepted", true},
        {"plant_id", plant_id}, {"care", "sell_plant"},
        {"confirmation", "game_yes_no"}, {"verification_required", true}};
    return g_last_care_result;
}

json put_in_wheelbarrow(std::uintptr_t app, std::uint32_t plant_id) {
    if (g_pending_garden_transition.active) throw std::runtime_error("A garden transition is already queued.");
    if (g_pending_care.active)
        throw std::runtime_error("A garden action is already queued.");
    if (!plant_id || (plant_id & 0xFFFF0000u) == 0)
        throw std::runtime_error("A current full DataArray plant ID is required.");
    auto r = read_runtime(app, true);
    if (r.game_mode != kGameModeZenGarden || r.game_scene != 3 || !r.garden || !r.board || !r.player ||
        r.garden_type < kGardenMain || r.garden_type > kGardenAquarium ||
        r.garden_type == kGardenWheelbarrow)
        throw std::runtime_error("The wheelbarrow tool requires an active plant garden board.");
    if (read_at<std::uint8_t>(r.board, kBoardPaused, "Board.mPaused") != 0)
        throw std::runtime_error("The wheelbarrow tool is unavailable while paused.");
    if (read_i32(r.player, kPlayerPurchases + kPurchaseWheelbarrow * 4,
                 "PlayerInfo.wheelbarrow") == 0)
        throw std::runtime_error("The wheelbarrow is not owned.");
    for (std::int32_t i = 0; i < r.plant_count; ++i) {
        const auto* pot = r.potted.data() + static_cast<std::size_t>(i) * kPottedPlantStride;
        if (get<std::int32_t>(pot, kPottedPlantStride, 0x04, "PottedPlant.mWhichZenGarden") == kGardenWheelbarrow)
            throw std::runtime_error("The wheelbarrow already contains a plant; placement routing is not enabled.");
    }

    const std::uint8_t* target = nullptr;
    std::int32_t inventory_index = -1;
    for (std::uint32_t i = 0; i < r.board_plants.count; ++i) {
        const auto* p = r.board_plants.bytes.data() + static_cast<std::size_t>(i) * kPlantStride;
        if (get<std::uint32_t>(p, kPlantStride, kPlantStride - 4, "DataArrayItem.mID") != plant_id)
            continue;
        if (target) throw std::runtime_error("The plant ID is ambiguous in this garden.");
        if (get<std::uint8_t>(p, kPlantStride, 0x141, "Plant.mDead") != 0)
            throw std::runtime_error("The target plant is no longer active.");
        inventory_index = get<std::int32_t>(p, kPlantStride, 0x13C, "Plant.mPottedPlantIndex");
        target = p;
    }
    if (!target || inventory_index < 0 || inventory_index >= r.plant_count)
        throw std::runtime_error("The plant ID does not identify a potted garden plant.");
    const auto* pot = r.potted.data() + static_cast<std::size_t>(inventory_index) * kPottedPlantStride;
    if (get<std::int32_t>(pot, kPottedPlantStride, 0x04, "PottedPlant.mWhichZenGarden") != r.garden_type)
        throw std::runtime_error("The target plant belongs to a different garden.");

    const auto x0 = get<std::int32_t>(target, kPlantStride, 0x08, "GameObject.mX");
    const auto y0 = get<std::int32_t>(target, kPlantStride, 0x0C, "GameObject.mY");
    const auto width = get<std::int32_t>(target, kPlantStride, 0x10, "GameObject.mWidth");
    const auto height = get<std::int32_t>(target, kPlantStride, 0x14, "GameObject.mHeight");
    const auto plant_type = get<std::int32_t>(target, kPlantStride, 0x24, "Plant.mSeedType");
    if (x0 < 0 || x0 >= 800 || y0 < 0 || y0 >= 600 || width <= 0 || width > 800 || height <= 0 || height > 600)
        throw std::runtime_error("Target plant hitbox is outside the supported game surface.");
    int hit_x = plant_type == 30 || plant_type == 47 ? x0 : x0 + 10;
    int hit_width = plant_type == 23 ? width : width - 20;
    if (plant_type == 47) hit_width = 140;
    const int hit_height = plant_type == 47 ? 80 : height;
    if (hit_width <= 0 || hit_height <= 0)
        throw std::runtime_error("Target plant has an invalid game hitbox.");
    const int x = hit_x + hit_width / 2;
    const int y = y0 + hit_height / 2;
    select_tool(app, r.board, kObjectWheelbarrow, kCursorWheelbarrow);
    widget_mouse_move(app, x, y);

    g_pending_care.active = true;
    g_pending_care.app = app;
    g_pending_care.board = r.board;
    g_pending_care.garden = r.garden;
    g_pending_care.player = r.player;
    g_pending_care.plant_id = plant_id;
    g_pending_care.wheelbarrow_target = true;
    g_pending_care.inventory_index = inventory_index;
    g_pending_care.cursor_type = kCursorWheelbarrow;
    g_pending_care.x = x;
    g_pending_care.y = y;
    g_pending_care.started_ms = GetTickCount64();
    g_pending_care.ticks = 0;
    g_last_care_result = {{"status", "queued"}, {"accepted", true},
        {"plant_id", plant_id}, {"care", "put_in_wheelbarrow"},
        {"verification_required", true}};
    return g_last_care_result;
}

json feed_tree(std::uintptr_t app) {
    if (g_pending_garden_transition.active) throw std::runtime_error("A garden transition is already queued.");
    if (g_pending_care.active)
        throw std::runtime_error("A garden or Tree of Wisdom action is already queued.");
    auto r = read_runtime(app, true);
    if (r.game_mode != kGameModeTreeWisdom || r.game_scene != 3 || !r.board || !r.player)
        throw std::runtime_error("Tree Food is available only in the active Tree of Wisdom mode.");
    if (read_at<std::uint8_t>(r.board, kBoardPaused, "Board.mPaused") != 0)
        throw std::runtime_error("Tree Food is unavailable while paused.");
    const auto challenge = read_ptr(r.board, kBoardChallenge, "Board.mChallenge");
    if (!challenge || !readable(challenge, 0xBC))
        throw std::runtime_error("Tree of Wisdom Challenge state is unavailable.");
    if (read_i32(challenge, 0x54, "Challenge.mChallengeState") == 11)
        throw std::runtime_error("Wait until the Tree of Wisdom finishes growing before feeding again.");
    const auto food_count = inventory_count(r.player, kPurchaseTreeFood);
    if (food_count <= 0)
        throw std::runtime_error("No Tree Food is available in the profile inventory.");
    for (std::uint32_t i = 0; i < r.board_grid_items.count; ++i) {
        const auto* item = r.board_grid_items.bytes.data() + static_cast<std::size_t>(i) * kGridItemStride;
        if (get<std::uint8_t>(item, kGridItemStride, 0x20, "GridItem.mDead") == 0 &&
            get<std::int32_t>(item, kGridItemStride, 0x08, "GridItem.mGridItemType") == kGridItemZenTool)
            throw std::runtime_error("A Tree Food animation is already active.");
    }
    const auto height = read_i32(r.player, 0x58 + kGameModeTreeWisdom * 4,
                                 "PlayerInfo tree height record");
    if (height < 0 || height > 1000000)
        throw std::runtime_error("Tree of Wisdom height record is outside supported bounds.");
    int x = 397, y = 362;
    if (height > 1 && height < 7) { x = 392; y = 352; }
    else if (height >= 7 && height < 12) { x = 392; y = 327; }
    else if (height >= 12) { x = 382; y = 307; }
    if (!hit_test_object(r.board, x, y, kObjectTree))
        throw std::runtime_error("The normal Board hit-test did not resolve the Tree of Wisdom.");

    // Object type 14 is the normal Tree Food button in mode 50. Selecting it
    // uses the same visible toolbar hit-test and Board::MouseDown path as UI.
    select_tool(app, r.board, kObjectTreeFood, kCursorTreeFood);
    widget_mouse_move(app, x, y);
    if (!hit_test_object(r.board, x, y, kObjectTree))
        throw std::runtime_error("The tree target changed while selecting Tree Food.");

    g_pending_care.active = true;
    g_pending_care.app = app;
    g_pending_care.board = r.board;
    g_pending_care.player = r.player;
    g_pending_care.challenge = challenge;
    g_pending_care.tree_target = true;
    g_pending_care.inventory_before = food_count;
    g_pending_care.cursor_type = kCursorTreeFood;
    g_pending_care.x = x;
    g_pending_care.y = y;
    g_pending_care.started_ms = GetTickCount64();
    g_pending_care.ticks = 0;
    g_last_care_result = {{"status", "queued"}, {"accepted", true},
        {"action", "feed_tree"}, {"tool", "tree_food"},
        {"height_before", height}, {"tree_food_before", food_count},
        {"verification_required", true}};
    return g_last_care_result;
}

json move_plant(std::uintptr_t app, std::uint32_t plant_id, std::int32_t row, std::int32_t col) {
    if (g_pending_garden_transition.active) throw std::runtime_error("A garden transition is already queued.");
    if (g_pending_care.active) throw std::runtime_error("A garden action is already queued.");
    if (!plant_id || (plant_id & 0xFFFF0000u) == 0)
        throw std::runtime_error("A current full DataArray plant ID is required.");
    auto r = read_runtime(app, true);
    if (r.game_mode != kGameModeZenGarden || r.game_scene != 3 || !r.garden || !r.board || !r.player ||
        r.garden_type < kGardenMain || r.garden_type > kGardenAquarium || r.garden_type == kGardenWheelbarrow)
        throw std::runtime_error("Plant movement requires an active plant garden.");
    if (read_at<std::uint8_t>(r.board, kBoardPaused, "Board.mPaused") != 0)
        throw std::runtime_error("Plant movement is unavailable while paused.");
    const auto destination = garden_cell(r.garden_type, row, col);
    const auto target_grid_row = destination.row + 1;
    const auto target_grid_col = destination.col + 1;
    const std::uint8_t* target = nullptr;
    std::int32_t inventory_index = -1;
    for (std::uint32_t i = 0; i < r.board_plants.count; ++i) {
        const auto* p = r.board_plants.bytes.data() + static_cast<std::size_t>(i) * kPlantStride;
        if (get<std::uint32_t>(p, kPlantStride, kPlantStride - 4, "Plant.DataArrayID") != plant_id) continue;
        if (target || get<std::uint8_t>(p, kPlantStride, 0x141, "Plant.mDead") != 0)
            throw std::runtime_error("Plant ID is ambiguous or no longer active.");
        inventory_index = get<std::int32_t>(p, kPlantStride, 0x13C, "Plant.mPottedPlantIndex");
        target = p;
    }
    if (!target || inventory_index < 0 || inventory_index >= r.plant_count)
        throw std::runtime_error("Plant ID is not an active potted plant in this garden.");
    const auto* pot = r.potted.data() + static_cast<std::size_t>(inventory_index) * kPottedPlantStride;
    if (get<std::int32_t>(pot, kPottedPlantStride, 0x04, "PottedPlant.mWhichZenGarden") != r.garden_type)
        throw std::runtime_error("Plant belongs to a different garden.");
    const auto old_row = get<std::int32_t>(pot, kPottedPlantStride, 0x0C, "PottedPlant.mY") + 1;
    const auto old_col = get<std::int32_t>(pot, kPottedPlantStride, 0x08, "PottedPlant.mX") + 1;
    if (old_row == target_grid_row && old_col == target_grid_col)
        throw std::runtime_error("Plant is already in the requested cell.");
    if (inventory_cell_occupied(r, target_grid_row, target_grid_col))
        throw std::runtime_error("The destination cell is occupied.");
    const auto x0 = get<std::int32_t>(target, kPlantStride, 0x08, "GameObject.mX");
    const auto y0 = get<std::int32_t>(target, kPlantStride, 0x0C, "GameObject.mY");
    const auto width = get<std::int32_t>(target, kPlantStride, 0x10, "GameObject.mWidth");
    const auto height = get<std::int32_t>(target, kPlantStride, 0x14, "GameObject.mHeight");
    if (x0 < 0 || x0 >= 800 || y0 < 0 || y0 >= 600 || width <= 0 || width > 800 || height <= 0 || height > 600)
        throw std::runtime_error("Plant hitbox is outside the supported game surface.");
    const auto plant_type = get<std::int32_t>(target, kPlantStride, 0x24, "Plant.mSeedType");
    int source_x = x0 + ((plant_type == 30 || plant_type == 47) ? 0 : 10);
    int source_width = plant_type == 23 ? width : width - 20;
    if (plant_type == 47) source_width = 140;
    const int source_height = plant_type == 47 ? 80 : height;
    if (source_width <= 0 || source_height <= 0) throw std::runtime_error("Plant hitbox is invalid.");
    source_x += source_width / 2;
    const int source_y = y0 + source_height / 2;

    // Select the owned glove normally, then pick up the requested plant with
    // the game's own 1096 ZenGarden handler. The next update performs the
    // destination click after revalidating the cursor's full Plant ID.
    select_tool(app, r.board, kObjectGlove, kCursorGlove);
    invoke_garden_tool(r.garden, source_x, source_y, kCursorGlove);
    const auto cursor = read_ptr(r.board, kBoardCursor, "Board.mCursor");
    if (cursor_type(r.board) != kCursorPlantFromGlove ||
        read_at<std::uint32_t>(cursor, 0x38, "CursorObject.mGlovePlantID") != plant_id)
        throw std::runtime_error("The normal glove handler did not pick up the requested plant.");
    const int target_x = destination.x + 40;
    const int target_y = destination.y + 40;
    widget_mouse_move(app, target_x, target_y);
    g_pending_care = {};
    g_pending_care.active = true;
    g_pending_care.app = app; g_pending_care.board = r.board; g_pending_care.garden = r.garden;
    g_pending_care.player = r.player; g_pending_care.plant_id = plant_id;
    g_pending_care.inventory_index = inventory_index; g_pending_care.move_target = true;
    g_pending_care.cursor_type = kCursorPlantFromGlove; g_pending_care.x = target_x; g_pending_care.y = target_y;
    g_pending_care.target_row = target_grid_row; g_pending_care.target_col = target_grid_col;
    g_pending_care.source_row = old_row; g_pending_care.source_col = old_col;
    g_pending_care.started_ms = GetTickCount64();
    g_last_care_result = {{"status", "queued"}, {"accepted", true}, {"plant_id", plant_id},
        {"action", "move_plant"}, {"from_row", old_row}, {"from_col", old_col},
        {"to_row", target_grid_row}, {"to_col", target_grid_col}, {"verification_required", true}};
    return g_last_care_result;
}

json place_wheelbarrow_plant(std::uintptr_t app, std::int32_t row, std::int32_t col) {
    if (g_pending_garden_transition.active) throw std::runtime_error("A garden transition is already queued.");
    if (g_pending_care.active) throw std::runtime_error("A garden action is already queued.");
    auto r = read_runtime(app, true);
    if (r.game_mode != kGameModeZenGarden || r.game_scene != 3 || !r.garden || !r.board || !r.player ||
        r.garden_type < kGardenMain || r.garden_type > kGardenAquarium || r.garden_type == kGardenWheelbarrow)
        throw std::runtime_error("Wheelbarrow placement requires an active plant garden.");
    if (read_at<std::uint8_t>(r.board, kBoardPaused, "Board.mPaused") != 0)
        throw std::runtime_error("Wheelbarrow placement is unavailable while paused.");
    if (!read_i32(r.player, kPlayerPurchases + kPurchaseWheelbarrow * 4, "PlayerInfo.wheelbarrow"))
        throw std::runtime_error("The wheelbarrow is not owned.");
    const auto destination = garden_cell(r.garden_type, row, col);
    const auto target_row = destination.row + 1, target_col = destination.col + 1;
    if (inventory_cell_occupied(r, target_row, target_col)) throw std::runtime_error("The destination cell is occupied.");
    std::int32_t index = -1, seed_type = -1;
    for (std::int32_t i = 0; i < r.plant_count; ++i) {
        const auto* pot = r.potted.data() + static_cast<std::size_t>(i) * kPottedPlantStride;
        if (get<std::int32_t>(pot, kPottedPlantStride, 0x04, "PottedPlant.mWhichZenGarden") == kGardenWheelbarrow) {
            if (index >= 0) throw std::runtime_error("Wheelbarrow inventory is ambiguous.");
            index = i; seed_type = get<std::int32_t>(pot, kPottedPlantStride, 0x00, "PottedPlant.mSeedType");
        }
    }
    if (index < 0) throw std::runtime_error("The wheelbarrow is empty.");
    if (r.garden_type == kGardenAquarium && seed_type != 16 && seed_type != 19 && seed_type != 24 && seed_type != 43)
        throw std::runtime_error("Only aquatic plants can be placed in the aquarium.");
    const int x = destination.x + 40, y = destination.y + 40;
    select_tool(app, r.board, kObjectWheelbarrow, kCursorWheelbarrow);
    widget_mouse_move(app, x, y);
    g_pending_care = {};
    g_pending_care.active = true;
    g_pending_care.app = app; g_pending_care.board = r.board; g_pending_care.garden = r.garden;
    g_pending_care.player = r.player; g_pending_care.inventory_index = index;
    g_pending_care.place_wheelbarrow_target = true; g_pending_care.cursor_type = kCursorWheelbarrow;
    g_pending_care.x = x; g_pending_care.y = y; g_pending_care.target_row = target_row;
    g_pending_care.target_col = target_col; g_pending_care.garden_before = r.garden_type;
    g_pending_care.started_ms = GetTickCount64();
    g_last_care_result = {{"status", "queued"}, {"accepted", true}, {"action", "place_wheelbarrow_plant"},
        {"row", target_row}, {"col", target_col}, {"plant_type", seed_type}, {"verification_required", true}};
    return g_last_care_result;
}

json next_garden(std::uintptr_t app) {
    if (g_pending_garden_transition.active)
        throw std::runtime_error("A garden transition is already queued.");
    if (g_pending_care.active) throw std::runtime_error("A garden action is already queued.");
    const auto mode = read_i32(app, kAppMode, "LawnApp.mGameMode");
    const auto scene = read_i32(app, kAppScene, "LawnApp.mGameUi");
    const auto board = read_ptr(app, kAppBoard, "LawnApp.mBoard");
    if ((mode != kGameModeZenGarden && mode != kGameModeTreeWisdom) || scene != 3 || !board)
        throw std::runtime_error("Next Garden is available only in an active Zen Garden or Tree of Wisdom mode.");
    if (read_at<std::uint8_t>(board, kBoardPaused, "Board.mPaused") != 0)
        throw std::runtime_error("Garden switching is unavailable while paused.");
    if (app_dialog_count(app) != 0)
        throw std::runtime_error("Close the active dialog before switching gardens.");
    if (cursor_type(board) != 0) throw std::runtime_error("Clear the current garden tool before switching gardens.");
    const Rect rect = zen_button_rect(board, kObjectNextGarden);
    const int x = rect.x + rect.width / 2, y = rect.y + rect.height / 2;
    widget_mouse_move(app, x, y);
    if (!hit_test_object(board, x, y, kObjectNextGarden))
        throw std::runtime_error("The normal Board hit-test reports Next Garden unavailable.");
    reinterpret_cast<BoardMouseDown>(kBoardMouseDown1096)(reinterpret_cast<void*>(board), x, y, 1);
    return {{"status", "dispatched"}, {"accepted", true}, {"action", "next_garden"},
        {"mode_before", mode}, {"mode_after", read_i32(app, kAppMode, "LawnApp.mGameMode")},
        {"normal_handler_invoked", true}};
}

json enter_garden(std::uintptr_t app, std::int32_t garden_id) {
    if (garden_id < 0 || garden_id > 2)
        throw std::runtime_error("garden_id must be 0 (main), 1 (mushroom), or 2 (aquarium).");
    if (g_pending_garden_transition.active || g_pending_care.active)
        throw std::runtime_error("Finish the pending garden action before switching gardens.");
    const auto r = read_runtime(app, false);
    if ((r.game_mode != kGameModeZenGarden && r.game_mode != kGameModeTreeWisdom) ||
        r.game_scene != 3 || !r.board || !r.player)
        throw std::runtime_error("Garden selection requires an active Zen Garden or Tree of Wisdom board.");
    if (r.game_mode == kGameModeZenGarden && (!r.garden || r.garden_type < kGardenMain ||
        r.garden_type > kGardenAquarium || r.garden_type == kGardenWheelbarrow))
        throw std::runtime_error("The current Zen Garden type is unavailable for garden selection.");
    if (read_at<std::uint8_t>(r.board, kBoardPaused, "Board.mPaused") != 0)
        throw std::runtime_error("Garden switching is unavailable while paused.");
    if (cursor_type(r.board) != 0)
        throw std::runtime_error("Clear the current garden tool before switching gardens.");
    if (app_dialog_count(app) != 0)
        throw std::runtime_error("Close the active dialog before switching gardens.");
    if (garden_id == 1 && read_i32(r.player,
            kPlayerPurchases + kPurchaseMushroomGarden * 4, "PlayerInfo mushroom garden ownership") == 0)
        throw std::runtime_error("The mushroom garden is not owned.");
    if (garden_id == 2 && read_i32(r.player,
            kPlayerPurchases + kPurchaseAquarium * 4, "PlayerInfo aquarium ownership") == 0)
        throw std::runtime_error("The aquarium is not owned.");
    if (r.game_mode == kGameModeZenGarden && public_garden_id(r.garden_type) == garden_id)
        return {{"status", "already_selected"}, {"accepted", true}, {"garden_id", garden_id},
            {"normal_handler_invoked", false}};

    // Validate that the normal, visible Next Garden hit target is present now;
    // the actual click is delayed to the update hook and revalidated there.
    const Rect rect = zen_button_rect(r.board, kObjectNextGarden);
    const int x = rect.x + rect.width / 2, y = rect.y + rect.height / 2;
    widget_mouse_move(app, x, y);
    if (!point_in_rect(rect, x, y) || !hit_test_object(r.board, x, y, kObjectNextGarden))
        throw std::runtime_error("The normal Next Garden button is unavailable.");

    g_pending_garden_transition = {};
    g_pending_garden_transition.active = true;
    g_pending_garden_transition.app = app;
    g_pending_garden_transition.player = r.player;
    g_pending_garden_transition.target_id = garden_id;
    g_pending_garden_transition.started_ms = GetTickCount64();
    g_last_garden_transition = {{"status", "queued"}, {"accepted", true},
        {"action", "enter_garden"}, {"garden_id", garden_id}, {"advances", 0},
        {"normal_handler_invoked", false}};
    return g_last_garden_transition;
}

json feed_plant_chocolate(std::uintptr_t app, std::uint32_t plant_id) {
    if (g_pending_garden_transition.active) throw std::runtime_error("A garden transition is already queued.");
    if (g_pending_care.active) throw std::runtime_error("A garden action is already queued.");
    if (!plant_id || (plant_id & 0xFFFF0000u) == 0) throw std::runtime_error("A current full plant ID is required.");
    auto r = read_runtime(app, true);
    if (r.game_mode != kGameModeZenGarden || r.game_scene != 3 || !r.garden || !r.board || !r.player ||
        r.garden_type < kGardenMain || r.garden_type > kGardenAquarium || r.garden_type == kGardenWheelbarrow)
        throw std::runtime_error("Chocolate feeding requires an active plant garden.");
    if (read_at<std::uint8_t>(r.board, kBoardPaused, "Board.mPaused") != 0)
        throw std::runtime_error("Chocolate feeding is unavailable while paused.");
    const auto chocolate = inventory_count(r.player, kPurchaseChocolate);
    if (chocolate < 1) throw std::runtime_error("No chocolate is available.");
    const std::uint8_t* target = nullptr;
    std::int32_t inventory_index = -1;
    for (std::uint32_t i = 0; i < r.board_plants.count; ++i) {
        const auto* p = r.board_plants.bytes.data() + static_cast<std::size_t>(i) * kPlantStride;
        if (get<std::uint32_t>(p, kPlantStride, kPlantStride - 4, "Plant.DataArrayID") != plant_id) continue;
        if (target || get<std::uint8_t>(p, kPlantStride, 0x141, "Plant.mDead"))
            throw std::runtime_error("Plant ID is ambiguous or no longer active.");
        target = p; inventory_index = get<std::int32_t>(p, kPlantStride, 0x13C, "Plant.mPottedPlantIndex");
    }
    if (!target || inventory_index < 0 || inventory_index >= r.plant_count)
        throw std::runtime_error("Plant ID is not an active potted plant in this garden.");
    const auto* pot = r.potted.data() + static_cast<std::size_t>(inventory_index) * kPottedPlantStride;
    if (get<std::int32_t>(pot, kPottedPlantStride, 0x04, "PottedPlant.mWhichZenGarden") != r.garden_type)
        throw std::runtime_error("Plant belongs to a different garden.");
    const int x0 = get<std::int32_t>(target, kPlantStride, 0x08, "GameObject.mX");
    const int y0 = get<std::int32_t>(target, kPlantStride, 0x0C, "GameObject.mY");
    const int width = get<std::int32_t>(target, kPlantStride, 0x10, "GameObject.mWidth");
    const int height = get<std::int32_t>(target, kPlantStride, 0x14, "GameObject.mHeight");
    const int seed = get<std::int32_t>(target, kPlantStride, 0x24, "Plant.mSeedType");
    if (x0 < 0 || x0 >= 800 || y0 < 0 || y0 >= 600 || width <= 20 || width > 800 || height <= 0 || height > 600)
        throw std::runtime_error("Plant hitbox is outside supported bounds.");
    int hit_x = x0 + ((seed == 30 || seed == 47) ? 0 : 10);
    int hit_width = seed == 23 ? width : width - 20;
    if (seed == 47) hit_width = 140;
    const int hit_height = seed == 47 ? 80 : height;
    hit_x += hit_width / 2;
    const int hit_y = y0 + hit_height / 2;
    select_tool(app, r.board, kObjectChocolate, kCursorChocolate);
    widget_mouse_move(app, hit_x, hit_y);
    g_pending_care = {};
    g_pending_care.active = true; g_pending_care.app = app; g_pending_care.board = r.board;
    g_pending_care.garden = r.garden; g_pending_care.player = r.player; g_pending_care.plant_id = plant_id;
    g_pending_care.inventory_index = inventory_index; g_pending_care.chocolate_plant_target = true;
    g_pending_care.inventory_before = chocolate; g_pending_care.cursor_type = kCursorChocolate;
    g_pending_care.x = hit_x; g_pending_care.y = hit_y; g_pending_care.started_ms = GetTickCount64();
    g_last_care_result = {{"status", "queued"}, {"accepted", true}, {"plant_id", plant_id},
        {"action", "feed_plant_chocolate"}, {"chocolate_before", chocolate}, {"verification_required", true}};
    return g_last_care_result;
}

void tick(std::uintptr_t app) try {
    if (g_pending_garden_transition.active) {
        auto finish_transition = [](const char* status, const char* reason) {
            g_last_garden_transition = {{"status", status}, {"accepted", false},
                {"action", "enter_garden"}, {"garden_id", g_pending_garden_transition.target_id},
                {"advances", g_pending_garden_transition.advances}, {"reason", reason},
                {"normal_handler_invoked", g_pending_garden_transition.advances != 0}};
            g_pending_garden_transition = {};
        };
        try {
            ++g_pending_garden_transition.ticks;
            if (app != g_pending_garden_transition.app) {
                finish_transition("cancelled", "The active LawnApp changed during garden switching.");
                return;
            }
            if (GetTickCount64() - g_pending_garden_transition.started_ms > kCareTimeoutMs) {
                finish_transition("timeout", "The requested owned garden did not become active in time.");
                return;
            }
            const auto player = read_ptr(app, kAppPlayerInfo, "LawnApp.mPlayerInfo");
            if (!player) return;
            if (player != g_pending_garden_transition.player) {
                finish_transition("cancelled", "The active player profile changed during garden switching.");
                return;
            }
            const auto scene = read_i32(app, kAppScene, "LawnApp.mGameUi");
            const auto mode = read_i32(app, kAppMode, "LawnApp.mGameMode");
            const auto board = read_ptr(app, kAppBoard, "LawnApp.mBoard");
            // Normal Board replacement passes through UI scene 2. Do not read
            // or click the old Board during that brief interstitial state.
            if (scene != 3 || !board) return;
            if (mode != kGameModeZenGarden && mode != kGameModeTreeWisdom) {
                finish_transition("cancelled", "The active garden board changed during switching.");
                return;
            }
            std::int32_t garden_type = -1;
            if (mode == kGameModeZenGarden) {
                const auto garden = read_ptr(app, kAppZenGarden, "LawnApp.mZenGarden");
                if (!garden) return;
                garden_type = read_i32(garden, kZenGardenType, "ZenGarden.mGardenType");
            }
            if (mode == kGameModeZenGarden && public_garden_id(garden_type) ==
                g_pending_garden_transition.target_id) {
                g_last_garden_transition = {{"status", "completed"}, {"accepted", true},
                    {"action", "enter_garden"}, {"garden_id", g_pending_garden_transition.target_id},
                    {"advances", g_pending_garden_transition.advances}, {"normal_handler_invoked", true}};
                g_pending_garden_transition = {};
                return;
            }
            if (g_pending_garden_transition.advances >= 4) {
                finish_transition("not_reached", "Four normal Next Garden advances did not reach the requested garden.");
                return;
            }
            if (read_at<std::uint8_t>(board, kBoardPaused, "Board.mPaused") != 0 ||
                cursor_type(board) != 0 || app_dialog_count(app) != 0) {
                finish_transition("cancelled", "Pause, cursor, or dialog state changed during garden switching.");
                return;
            }
            const Rect rect = zen_button_rect(board, kObjectNextGarden);
            const int x = rect.x + rect.width / 2, y = rect.y + rect.height / 2;
            widget_mouse_move(app, x, y);
            if (!point_in_rect(rect, x, y) || !hit_test_object(board, x, y, kObjectNextGarden)) {
                finish_transition("cancelled", "The normal Next Garden button is no longer available.");
                return;
            }
            reinterpret_cast<BoardMouseDown>(kBoardMouseDown1096)(reinterpret_cast<void*>(board), x, y, 1);
            ++g_pending_garden_transition.advances;
            g_last_garden_transition = {{"status", "queued"}, {"accepted", true},
                {"action", "enter_garden"}, {"garden_id", g_pending_garden_transition.target_id},
                {"advances", g_pending_garden_transition.advances}, {"normal_handler_invoked", true}};
        } catch (...) {
            try { finish_transition("cancelled", "Garden state could not be safely revalidated."); }
            catch (...) { g_pending_garden_transition = {}; }
        }
        return;
    }
    if (!g_pending_care.active || g_pending_care.dispatching) return;
    auto fail = [](const char* status, const char* reason) {
        g_last_care_result = {{"status", status}, {"accepted", false},
            {"reason", reason}, {"care", pending_action_name(g_pending_care)}};
        if (g_pending_care.snail_target) g_last_care_result["stinky_id"] = g_pending_care.plant_id;
        else if (g_pending_care.tree_target) g_last_care_result["action"] = "feed_tree";
        else if (g_pending_care.move_target || g_pending_care.place_wheelbarrow_target ||
                 g_pending_care.chocolate_plant_target) g_last_care_result["action"] = pending_action_name(g_pending_care);
        else g_last_care_result["plant_id"] = g_pending_care.plant_id;
        g_pending_care = {};
    };
    ++g_pending_care.ticks;
    if (GetTickCount64() - g_pending_care.started_ms > kCareTimeoutMs) {
        fail("timeout", "The target did not become a stable highlighted plant in time.");
        return;
    }
    if (app != g_pending_care.app) {
        fail("cancelled", "The active LawnApp changed while care was queued.");
        return;
    }
    Runtime r;
    try {
        r = read_runtime(app, true);
    } catch (...) {
        fail("cancelled", "Garden memory became unavailable while care was queued.");
        return;
    }
    const auto expected_mode = g_pending_care.tree_target ? kGameModeTreeWisdom : kGameModeZenGarden;
    if (r.game_mode != expected_mode || r.game_scene != 3 ||
        r.board != g_pending_care.board || (!g_pending_care.tree_target && r.garden != g_pending_care.garden) ||
        r.player != g_pending_care.player || (!g_pending_care.tree_target && r.garden_type < 0) ||
        read_at<std::uint8_t>(r.board, kBoardPaused, "Board.mPaused") != 0) {
        fail("cancelled", "The active garden or board state changed while care was queued.");
        return;
    }
    const auto manager = read_ptr(app, kAppWidgetManager, "LawnApp.mWidgetManager");
    if (!manager || read_i32(manager, 0x110, "WidgetManager.mDownButtons") != 0 ||
        read_i32(manager, 0x108, "WidgetManager.mLastMouseX") != g_pending_care.x ||
        read_i32(manager, 0x10C, "WidgetManager.mLastMouseY") != g_pending_care.y) {
        fail("cancelled", "Pointer position or button state changed while care was queued.");
        return;
    }
    if (cursor_type(r.board) != g_pending_care.cursor_type) {
        fail("cancelled", "The selected garden tool changed while care was queued.");
        return;
    }
    if (g_pending_care.tree_target) {
        const auto challenge = read_ptr(r.board, kBoardChallenge, "Board.mChallenge");
        if (!challenge || challenge != g_pending_care.challenge || !readable(challenge, 0xBC)) {
            fail("cancelled", "The Tree of Wisdom Challenge object changed while feeding was queued.");
            return;
        }
        if (read_i32(challenge, 0x54, "Challenge.mChallengeState") == 11) {
            fail("cancelled", "The Tree of Wisdom began growing while feeding was queued.");
            return;
        }
        if (inventory_count(r.player, kPurchaseTreeFood) != g_pending_care.inventory_before) {
            fail("cancelled", "Tree Food inventory changed while feeding was queued.");
            return;
        }
        for (std::uint32_t i = 0; i < r.board_grid_items.count; ++i) {
            const auto* item = r.board_grid_items.bytes.data() + static_cast<std::size_t>(i) * kGridItemStride;
            if (get<std::uint8_t>(item, kGridItemStride, 0x20, "GridItem.mDead") == 0 &&
                get<std::int32_t>(item, kGridItemStride, 0x08, "GridItem.mGridItemType") == kGridItemZenTool) {
                fail("cancelled", "A Tree Food animation became active before dispatch.");
                return;
            }
        }
        if (!hit_test_object(r.board, g_pending_care.x, g_pending_care.y, kObjectTree)) {
            fail("cancelled", "The Board no longer hit-tests the queued Tree of Wisdom target.");
            return;
        }
        const auto height_before = read_i32(r.player, 0x58 + kGameModeTreeWisdom * 4,
                                            "PlayerInfo tree height record");
        reinterpret_cast<BoardMouseDown>(kBoardMouseDown1096)(
            reinterpret_cast<void*>(r.board), g_pending_care.x, g_pending_care.y, 1);
        const auto after = read_runtime(app, true);
        std::int32_t active_tree_food_items = 0;
        for (std::uint32_t i = 0; i < after.board_grid_items.count; ++i) {
            const auto* item = after.board_grid_items.bytes.data() + static_cast<std::size_t>(i) * kGridItemStride;
            if (get<std::uint8_t>(item, kGridItemStride, 0x20, "GridItem.mDead after feed") == 0 &&
                get<std::int32_t>(item, kGridItemStride, 0x08, "GridItem.mGridItemType after feed") == kGridItemZenTool)
                ++active_tree_food_items;
        }
        const auto food_after = inventory_count(after.player, kPurchaseTreeFood);
        const auto height_after = read_i32(after.player, 0x58 + kGameModeTreeWisdom * 4,
                                           "PlayerInfo tree height after feed");
        const bool dispatched = food_after == g_pending_care.inventory_before - 1 && active_tree_food_items > 0;
        g_last_care_result = {{"status", dispatched ? "dispatched" : "not_dispatched"},
            {"accepted", dispatched}, {"action", "feed_tree"},
            {"tree_food_before", g_pending_care.inventory_before}, {"tree_food_after", food_after},
            {"height_before", height_before}, {"height_after", height_after},
            {"active_tree_food_items", active_tree_food_items},
            {"normal_handler_invoked", true}};
        g_pending_care = {};
        return;
    }
    if (g_pending_care.move_target) {
        if (g_pending_care.inventory_index < 0 || g_pending_care.inventory_index >= r.plant_count) {
            fail("cancelled", "Potted plant inventory changed before glove placement."); return;
        }
        const auto* pot = r.potted.data() + static_cast<std::size_t>(g_pending_care.inventory_index) * kPottedPlantStride;
        if (get<std::int32_t>(pot, kPottedPlantStride, 0x04, "PottedPlant.mWhichZenGarden") != r.garden_type ||
            get<std::int32_t>(pot, kPottedPlantStride, 0x0C, "PottedPlant.mY") + 1 != g_pending_care.source_row ||
            get<std::int32_t>(pot, kPottedPlantStride, 0x08, "PottedPlant.mX") + 1 != g_pending_care.source_col) {
            fail("cancelled", "The potted plant moved while glove placement was queued."); return;
        }
        const auto cursor = read_ptr(r.board, kBoardCursor, "Board.mCursor");
        if (read_at<std::uint32_t>(cursor, 0x38, "CursorObject.mGlovePlantID") != g_pending_care.plant_id) {
            fail("cancelled", "The glove cursor no longer identifies the requested plant."); return;
        }
        if (inventory_cell_occupied(r, g_pending_care.target_row, g_pending_care.target_col,
                                    g_pending_care.inventory_index)) {
            fail("cancelled", "The destination cell became occupied."); return;
        }
        bool live_id = false;
        for (std::uint32_t i = 0; i < r.board_plants.count; ++i) {
            const auto* p = r.board_plants.bytes.data() + static_cast<std::size_t>(i) * kPlantStride;
            if (get<std::uint32_t>(p, kPlantStride, kPlantStride - 4, "Plant.DataArrayID") == g_pending_care.plant_id &&
                get<std::uint8_t>(p, kPlantStride, 0x141, "Plant.mDead") == 0 &&
                get<std::int32_t>(p, kPlantStride, 0x13C, "Plant.mPottedPlantIndex") == g_pending_care.inventory_index)
                live_id = true;
        }
        if (!live_id) { fail("cancelled", "The target plant changed before glove placement."); return; }
        reinterpret_cast<BoardMouseDown>(kBoardMouseDown1096)(
            reinterpret_cast<void*>(r.board), g_pending_care.x, g_pending_care.y, 1);
        const auto after = read_runtime(app, false);
        bool moved = g_pending_care.inventory_index < after.plant_count;
        std::int32_t after_row = -1, after_col = -1;
        if (moved) {
            const auto* moved_pot = after.potted.data() + static_cast<std::size_t>(g_pending_care.inventory_index) * kPottedPlantStride;
            after_row = get<std::int32_t>(moved_pot, kPottedPlantStride, 0x0C, "PottedPlant.mY after move") + 1;
            after_col = get<std::int32_t>(moved_pot, kPottedPlantStride, 0x08, "PottedPlant.mX after move") + 1;
            moved = get<std::int32_t>(moved_pot, kPottedPlantStride, 0x04, "PottedPlant.mWhichZenGarden after move") == r.garden_type &&
                after_row == g_pending_care.target_row && after_col == g_pending_care.target_col;
        }
        g_last_care_result = {{"status", moved ? "dispatched" : "not_moved"}, {"accepted", moved},
            {"action", "move_plant"}, {"plant_id", g_pending_care.plant_id},
            {"row", after_row}, {"col", after_col}, {"normal_handler_invoked", true}};
        g_pending_care = {};
        return;
    }
    if (g_pending_care.place_wheelbarrow_target) {
        if (g_pending_care.inventory_index < 0 || g_pending_care.inventory_index >= r.plant_count) {
            fail("cancelled", "Potted plant inventory changed before wheelbarrow placement."); return;
        }
        const auto* pot = r.potted.data() + static_cast<std::size_t>(g_pending_care.inventory_index) * kPottedPlantStride;
        if (get<std::int32_t>(pot, kPottedPlantStride, 0x04, "PottedPlant.mWhichZenGarden") != kGardenWheelbarrow ||
            inventory_cell_occupied(r, g_pending_care.target_row, g_pending_care.target_col)) {
            fail("cancelled", "The wheelbarrow or destination changed before placement."); return;
        }
        reinterpret_cast<BoardMouseDown>(kBoardMouseDown1096)(
            reinterpret_cast<void*>(r.board), g_pending_care.x, g_pending_care.y, 1);
        const auto after = read_runtime(app, false);
        bool placed = g_pending_care.inventory_index < after.plant_count;
        if (placed) {
            const auto* placed_pot = after.potted.data() + static_cast<std::size_t>(g_pending_care.inventory_index) * kPottedPlantStride;
            placed = get<std::int32_t>(placed_pot, kPottedPlantStride, 0x04, "PottedPlant.mWhichZenGarden after place") == r.garden_type &&
                get<std::int32_t>(placed_pot, kPottedPlantStride, 0x0C, "PottedPlant.mY after place") + 1 == g_pending_care.target_row &&
                get<std::int32_t>(placed_pot, kPottedPlantStride, 0x08, "PottedPlant.mX after place") + 1 == g_pending_care.target_col;
        }
        g_last_care_result = {{"status", placed ? "dispatched" : "not_placed"}, {"accepted", placed},
            {"action", "place_wheelbarrow_plant"}, {"row", g_pending_care.target_row},
            {"col", g_pending_care.target_col}, {"normal_handler_invoked", true}};
        g_pending_care = {};
        return;
    }
    if (g_pending_care.chocolate_plant_target) {
        if (inventory_count(r.player, kPurchaseChocolate) != g_pending_care.inventory_before) {
            fail("cancelled", "Chocolate inventory changed while feeding was queued."); return;
        }
        std::uint32_t matching = 0, highlighted = 0;
        for (std::uint32_t i = 0; i < r.board_plants.count; ++i) {
            const auto* p = r.board_plants.bytes.data() + static_cast<std::size_t>(i) * kPlantStride;
            if (get<std::uint32_t>(p, kPlantStride, kPlantStride - 4, "Plant.DataArrayID") != g_pending_care.plant_id) continue;
            if (get<std::uint8_t>(p, kPlantStride, 0x141, "Plant.mDead") != 0 ||
                get<std::int32_t>(p, kPlantStride, 0x13C, "Plant.mPottedPlantIndex") != g_pending_care.inventory_index) break;
            ++matching;
            if (get<std::uint8_t>(p, kPlantStride, 0x145, "Plant.mHighlighted") != 0) ++highlighted;
        }
        if (matching != 1) { fail("cancelled", "The target potted plant changed while chocolate was queued."); return; }
        if (!hit_test_object(r.board, g_pending_care.x, g_pending_care.y, 1)) {
            fail("cancelled", "The normal Board hit-test no longer resolves the target plant."); return;
        }
        for (std::uint32_t i = 0; i < r.board_grid_items.count; ++i) {
            const auto* item = r.board_grid_items.bytes.data() + static_cast<std::size_t>(i) * kGridItemStride;
            if (get<std::int32_t>(item, kGridItemStride, 0x08, "GridItem.mGridItemType") == kGridItemStinky &&
                get<std::uint8_t>(item, kGridItemStride, 0x20, "GridItem.mDead") == 0 &&
                get<std::uint8_t>(item, kGridItemStride, 0x48, "GridItem.mHighlighted") != 0) {
                fail("cancelled", "Chocolate would also wake Stinky at this pointer position."); return;
            }
        }
        if (highlighted > 1) { fail("cancelled", "Chocolate would affect more than one potted plant."); return; }
        if (!highlighted) return;
        invoke_garden_tool(r.garden, g_pending_care.x, g_pending_care.y, kCursorChocolate);
        const auto chocolate_after = inventory_count(r.player, kPurchaseChocolate);
        const bool fed = chocolate_after == g_pending_care.inventory_before - 1;
        g_last_care_result = {{"status", fed ? "dispatched" : "not_dispatched"}, {"accepted", fed},
            {"action", "feed_plant_chocolate"}, {"plant_id", g_pending_care.plant_id},
            {"chocolate_before", g_pending_care.inventory_before}, {"chocolate_after", chocolate_after},
            {"normal_handler_invoked", true}};
        g_pending_care = {};
        return;
    }
    if (g_pending_care.snail_target) {
        if (inventory_count(r.player, kPurchaseChocolate) <= 0) {
            fail("cancelled", "Chocolate is no longer available.");
            return;
        }
        const auto* stinky = find_live_grid_item(r, g_pending_care.plant_id, kGridItemStinky);
        if (!stinky) {
            fail("cancelled", "The Stinky GridItem changed while care was queued.");
            return;
        }
        const auto state = get<std::int32_t>(stinky, kGridItemStride, 0x0C, "GridItem.mGridItemState");
        const auto px = get<float>(stinky, kGridItemStride, 0x24, "GridItem.mPosX");
        const auto py = get<float>(stinky, kGridItemStride, 0x28, "GridItem.mPosY");
        if ((state != kStinkySleeping && state != kStinkyFallingAsleep) ||
            !std::isfinite(px) || !std::isfinite(py) ||
            static_cast<int>(std::lround(px + 36.0f)) != g_pending_care.x ||
            static_cast<int>(std::lround(py + 35.0f)) != g_pending_care.y) {
            fail("cancelled", "Stinky's state or hitbox changed while care was queued.");
            return;
        }
        if (!hit_test_object(r.board, g_pending_care.x, g_pending_care.y, kObjectStinky)) {
            fail("cancelled", "The Board no longer hit-tests the queued Stinky target.");
            return;
        }
        std::uint32_t highlighted_plants = 0;
        for (std::uint32_t i = 0; i < r.board_plants.count; ++i) {
            const auto* p = r.board_plants.bytes.data() + static_cast<std::size_t>(i) * kPlantStride;
            if (get<std::uint8_t>(p, kPlantStride, 0x141, "Plant.mDead") == 0 &&
                get<std::int32_t>(p, kPlantStride, 0x13C, "Plant.mPottedPlantIndex") >= 0 &&
                get<std::uint8_t>(p, kPlantStride, 0x145, "Plant.mHighlighted") != 0)
                ++highlighted_plants;
        }
        if (highlighted_plants != 0) {
            fail("cancelled", "Chocolate could also hit a potted plant at this pointer location.");
            return;
        }
        if (get<std::uint8_t>(stinky, kGridItemStride, 0x48, "GridItem.mHighlighted") == 0)
            return; // Wait for the normal Board update to establish Stinky hover.
        reinterpret_cast<BoardMouseDown>(kBoardMouseDown1096)(
            reinterpret_cast<void*>(r.board), g_pending_care.x, g_pending_care.y, 1);
        g_last_care_result = {{"status", "dispatched"}, {"accepted", true},
            {"stinky_id", g_pending_care.plant_id}, {"care", pending_action_name(g_pending_care)},
            {"action", pending_action_name(g_pending_care)},
            {"tool", "chocolate"}, {"verification_required", true},
            {"normal_handler_invoked", true}};
        g_pending_care = {};
        return;
    }
    if (g_pending_care.wheelbarrow_target) {
        if (read_i32(r.player, kPlayerPurchases + kPurchaseWheelbarrow * 4,
                     "PlayerInfo.wheelbarrow") == 0) {
            fail("cancelled", "The wheelbarrow is no longer owned.");
            return;
        }
        for (std::int32_t i = 0; i < r.plant_count; ++i) {
            const auto* p = r.potted.data() + static_cast<std::size_t>(i) * kPottedPlantStride;
            if (get<std::int32_t>(p, kPottedPlantStride, 0x04, "PottedPlant.mWhichZenGarden") == kGardenWheelbarrow) {
                fail("cancelled", "The wheelbarrow became occupied; placement routing is not enabled.");
                return;
            }
        }
        std::uint32_t matching = 0;
        std::uint32_t highlighted = 0;
        for (std::uint32_t i = 0; i < r.board_plants.count; ++i) {
            const auto* p = r.board_plants.bytes.data() + static_cast<std::size_t>(i) * kPlantStride;
            if (get<std::uint32_t>(p, kPlantStride, kPlantStride - 4, "DataArrayItem.mID") != g_pending_care.plant_id)
                continue;
            if (get<std::uint8_t>(p, kPlantStride, 0x141, "Plant.mDead") != 0 ||
                get<std::int32_t>(p, kPlantStride, 0x13C, "Plant.mPottedPlantIndex") != g_pending_care.inventory_index)
                break;
            ++matching;
            if (get<std::uint8_t>(p, kPlantStride, 0x145, "Plant.mHighlighted") != 0) ++highlighted;
        }
        if (matching != 1) {
            fail("cancelled", "The target Plant ID or inventory link changed while wheelbarrow action was queued.");
            return;
        }
        if (highlighted != 1) return;
        const auto plant_id = g_pending_care.plant_id;
        const auto inventory_index = g_pending_care.inventory_index;
        invoke_garden_tool(r.garden, g_pending_care.x, g_pending_care.y, kCursorWheelbarrow);
        const auto after = read_runtime(app, false);
        const bool moved = inventory_index >= 0 && inventory_index < after.plant_count &&
            get<std::int32_t>(after.potted.data() + static_cast<std::size_t>(inventory_index) * kPottedPlantStride,
                              kPottedPlantStride, 0x04, "PottedPlant.mWhichZenGarden after wheelbarrow") ==
                kGardenWheelbarrow;
        g_last_care_result = {{"status", moved ? "dispatched" : "not_moved"}, {"accepted", moved},
            {"plant_id", plant_id}, {"care", "put_in_wheelbarrow"},
            {"normal_handler_invoked", true}};
        g_pending_care = {};
        return;
    }
    if (g_pending_care.sell_target) {
        if (!toolbar_object_available(r.board, kObjectMoneySign)) {
            fail("cancelled", "The normal money-sign toolbar button is no longer available.");
            return;
        }
        std::uint32_t matching = 0;
        std::uint32_t highlighted = 0;
        for (std::uint32_t i = 0; i < r.board_plants.count; ++i) {
            const auto* p = r.board_plants.bytes.data() + static_cast<std::size_t>(i) * kPlantStride;
            if (get<std::uint32_t>(p, kPlantStride, kPlantStride - 4, "DataArrayItem.mID") != g_pending_care.plant_id)
                continue;
            if (get<std::uint8_t>(p, kPlantStride, 0x141, "Plant.mDead") != 0 ||
                get<std::int32_t>(p, kPlantStride, 0x13C, "Plant.mPottedPlantIndex") != g_pending_care.inventory_index)
                break;
            ++matching;
            if (get<std::uint8_t>(p, kPlantStride, 0x145, "Plant.mHighlighted") != 0) ++highlighted;
        }
        if (matching != 1) {
            fail("cancelled", "The target Plant ID or inventory link changed while sale was queued.");
            return;
        }
        if (highlighted != 1) return;
        if (g_pending_care.inventory_index < 0 || g_pending_care.inventory_index >= r.plant_count) {
            fail("cancelled", "The target potted-plant slot changed while sale was queued.");
            return;
        }
        const auto* sale_pot = r.potted.data() + static_cast<std::size_t>(g_pending_care.inventory_index) * kPottedPlantStride;
        if (r.plant_count != g_pending_care.plant_count_before ||
            get<std::int32_t>(sale_pot, kPottedPlantStride, 0x00, "PottedPlant.mSeedType before sale") != g_pending_care.sale_seed_type ||
            get<std::int32_t>(sale_pot, kPottedPlantStride, 0x04, "PottedPlant.mWhichZenGarden before sale") != g_pending_care.sale_garden_type ||
            get<std::int32_t>(sale_pot, kPottedPlantStride, 0x08, "PottedPlant.mX before sale") != g_pending_care.sale_x ||
            get<std::int32_t>(sale_pot, kPottedPlantStride, 0x0C, "PottedPlant.mY before sale") != g_pending_care.sale_y) {
            fail("cancelled", "The pending potted-plant identity changed before the sale dialog opened.");
            return;
        }
        const auto plant_id = g_pending_care.plant_id;
        const auto plant_count_before = g_pending_care.plant_count_before;
        // The exact normal handler opens dialog 0x2F and waits for the player's
        // Yes/No choice. No synthetic confirmation is sent here.
        g_pending_care.dispatching = true;
        invoke_garden_tool(r.garden, g_pending_care.x, g_pending_care.y, kCursorMoneySign);
        const auto after = read_runtime(app, false);
        const bool sold = after.plant_count < plant_count_before;
        g_last_care_result = {{"status", sold ? "confirmed" : "declined"}, {"accepted", sold},
            {"plant_id", plant_id}, {"care", "sell_plant"},
            {"confirmation", "game_yes_no"}, {"normal_handler_invoked", true}};
        g_pending_care = {};
        return;
    }
    if (g_pending_care.inventory_index < 0 ||
        static_cast<std::size_t>(g_pending_care.inventory_index) >= r.potted.size() / kPottedPlantStride) {
        fail("cancelled", "The potted-plant inventory changed while care was queued.");
        return;
    }
    const auto* pot = r.potted.data() + static_cast<std::size_t>(g_pending_care.inventory_index) * kPottedPlantStride;
    if (get<std::int32_t>(pot, kPottedPlantStride, 0x04, "PottedPlant.mWhichZenGarden") != r.garden_type) {
        fail("cancelled", "The target plant changed gardens while care was queued.");
        return;
    }
    const auto pot_address = add_address(r.player, static_cast<std::size_t>(kPlayerPlants) +
        static_cast<std::size_t>(g_pending_care.inventory_index) * kPottedPlantStride);
    const auto expected_need = care_need(g_pending_care.kind);
    if (get_plants_need(pot_address) != expected_need) {
        fail("cancelled", "The target plant's need changed while care was queued.");
        return;
    }
    if (!has_care_inventory(r.player, g_pending_care.kind)) {
        fail("cancelled", "The requested garden-care supply is no longer available.");
        return;
    }
    std::uint32_t matching = 0;
    std::uint32_t highlighted = 0;
    for (std::uint32_t i = 0; i < r.board_plants.count; ++i) {
        const auto* p = r.board_plants.bytes.data() + static_cast<std::size_t>(i) * kPlantStride;
        if (get<std::uint32_t>(p, kPlantStride, kPlantStride - 4, "DataArrayItem.mID") != g_pending_care.plant_id)
            continue;
        if (get<std::uint8_t>(p, kPlantStride, 0x141, "Plant.mDead") != 0 ||
            get<std::int32_t>(p, kPlantStride, 0x13C, "Plant.mPottedPlantIndex") != g_pending_care.inventory_index)
            break;
        ++matching;
        if (get<std::uint8_t>(p, kPlantStride, 0x145, "Plant.mHighlighted") != 0) ++highlighted;
    }
    if (matching != 1) {
        fail("cancelled", "The target Plant ID or inventory link changed while care was queued.");
        return;
    }
    if (highlighted != 1) return; // Let the normal game update establish hover.

    invoke_garden_tool(r.garden, g_pending_care.x, g_pending_care.y, g_pending_care.cursor_type);
    g_last_care_result = {{"status", "dispatched"}, {"accepted", true},
        {"plant_id", g_pending_care.plant_id},
        {"care", care_name(g_pending_care.kind)},
        {"verification_required", true}, {"normal_handler_invoked", true}};
    g_pending_care = {};
} catch (...) {
    const auto plant_id = g_pending_care.plant_id;
    const auto kind = g_pending_care.kind;
    const auto snail_target = g_pending_care.snail_target;
    const auto tree_target = g_pending_care.tree_target;
    const auto move_target = g_pending_care.move_target;
    const auto place_target = g_pending_care.place_wheelbarrow_target;
    const auto chocolate_target = g_pending_care.chocolate_plant_target;
    g_pending_care = {};
    try {
        g_last_care_result = {{"status", "cancelled"}, {"accepted", false},
            {"reason", "Unexpected internal error while validating queued garden care."},
            {"care", snail_target ? "feed_snail" : tree_target ? "feed_tree" :
                move_target ? "move_plant" : place_target ? "place_wheelbarrow_plant" :
                chocolate_target ? "feed_plant_chocolate" : care_name(kind)}};
        if (snail_target) g_last_care_result["stinky_id"] = plant_id;
        else if (tree_target) g_last_care_result["action"] = "feed_tree";
        else if (move_target || place_target || chocolate_target)
            g_last_care_result["action"] = move_target ? "move_plant" :
                place_target ? "place_wheelbarrow_plant" : "feed_plant_chocolate";
        else g_last_care_result["plant_id"] = plant_id;
    } catch (...) {
        try { g_last_care_result = nullptr; } catch (...) {}
    }
}

} // namespace pvz::garden
