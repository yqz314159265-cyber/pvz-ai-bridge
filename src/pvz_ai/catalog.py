"""Static Plants vs. Zombies GOTY mode metadata (not a support/unlock list).

IDs and enum labels follow the original game's ``GameMode`` enumeration.
The catalog intentionally separates enum presence from save unlocks and from
the capabilities exposed by a particular native bridge build.
"""
from __future__ import annotations

from typing import TypedDict


class ModeMetadata(TypedDict):
    id: int
    enum_name: str
    name: str
    category: str
    hidden: bool
    action_profile: str
    native_support: str
    unlocked: None


_MODE_ROWS: tuple[tuple[int, str, str, str, bool, str], ...] = (
    (0, "Adventure", "Adventure", "adventure", False, "plant_defense"),
    (1, "SurvivalDay", "Survival: Day", "survival", False, "plant_defense"),
    (2, "SurvivalNight", "Survival: Night", "survival", False, "plant_defense"),
    (3, "SurvivalPool", "Survival: Pool", "survival", False, "plant_defense"),
    (4, "SurvivalFog", "Survival: Fog", "survival", False, "plant_defense"),
    (5, "SurvivalRoof", "Survival: Roof", "survival", False, "plant_defense"),
    (6, "SurvivalHardDay", "Survival: Day (Hard)", "survival", False, "plant_defense"),
    (7, "SurvivalHardNight", "Survival: Night (Hard)", "survival", False, "plant_defense"),
    (8, "SurvivalHardPool", "Survival: Pool (Hard)", "survival", False, "plant_defense"),
    (9, "SurvivalHardFog", "Survival: Fog (Hard)", "survival", False, "plant_defense"),
    (10, "SurvivalHardRoof", "Survival: Roof (Hard)", "survival", False, "plant_defense"),
    (11, "SurvivalEndless1", "Survival: Day (Endless)", "survival", True, "plant_defense"),
    (12, "SurvivalEndless2", "Survival: Night (Endless)", "survival", True, "plant_defense"),
    (13, "SurvivalEndless3", "Survival: Pool (Endless)", "survival", False, "plant_defense"),
    (14, "SurvivalEndless4", "Survival: Fog (Endless)", "survival", True, "plant_defense"),
    (15, "SurvivalEndless5", "Survival: Roof (Endless)", "survival", True, "plant_defense"),
    (16, "ZomBotany", "ZomBotany", "minigame", False, "minigame"),
    (17, "WallnutBowling", "Wall-nut Bowling", "minigame", False, "minigame"),
    (18, "SlotMachine", "Slot Machine", "minigame", False, "minigame"),
    (19, "ItsRainingSeeds", "It's Raining Seeds", "minigame", False, "minigame"),
    (20, "Beghouled", "Beghouled", "minigame", False, "minigame"),
    (21, "Invisighoul", "Invisi-ghoul", "minigame", False, "minigame"),
    (22, "SeeingStars", "Seeing Stars", "minigame", False, "minigame"),
    (23, "Zombiquarium", "Zombiquarium", "minigame", False, "minigame"),
    (24, "BeghouledTwist", "Beghouled Twist", "minigame", False, "minigame"),
    (25, "BigTroubleLittleZombie", "Big Trouble Little Zombie", "minigame", False, "minigame"),
    (26, "PortalCombat", "Portal Combat", "minigame", False, "minigame"),
    (27, "ColumnLikeYouSeeEm", "Column Like You See 'Em", "minigame", False, "minigame"),
    (28, "BobsledBonanza", "Bobsled Bonanza", "minigame", False, "minigame"),
    (29, "ZombieNimbleZombieQuick", "Zombie Nimble Zombie Quick", "minigame", False, "minigame"),
    (30, "WhackAZombie", "Whack a Zombie", "minigame", False, "minigame"),
    (31, "LastStand", "Last Stand", "minigame", False, "minigame"),
    (32, "ZomBotany2", "ZomBotany 2", "minigame", False, "minigame"),
    (33, "WallnutBowling2", "Wall-nut Bowling 2", "minigame", False, "minigame"),
    (34, "PogoParty", "Pogo Party", "minigame", False, "minigame"),
    (35, "DrZombossRevenge", "Dr. Zomboss's Revenge", "minigame", False, "minigame"),
    (36, "LimboArtChallengeWallnut", "Art Challenge: Wall-nut", "limbo", True, "none"),
    (37, "LimboSunnyDay", "Sunny Day (internal)", "limbo", True, "none"),
    (38, "LimboUnsodded", "Unsodded (internal)", "limbo", True, "none"),
    (39, "LimboBigTime", "Big Time (internal)", "limbo", True, "none"),
    (40, "LimboArtChallengeSunflower", "Art Challenge: Sunflower", "limbo", True, "none"),
    (41, "LimboAirRaid", "Air Raid (internal)", "limbo", True, "none"),
    (42, "LimboIceLevel", "Ice Level (internal)", "limbo", True, "none"),
    (43, "ZenGarden", "Zen Garden", "garden", False, "garden"),
    (44, "LimboHighGravity", "High Gravity (internal)", "limbo", True, "none"),
    (45, "LimboGraveDanger", "Grave Danger (internal)", "limbo", True, "none"),
    (46, "LimboCanYouDigIt", "Can You Dig It? (internal)", "limbo", True, "none"),
    (47, "LimboDarkStormyNight", "Dark Stormy Night (internal)", "limbo", True, "none"),
    (48, "LimboBungeeBlitz", "Bungee Blitz (internal)", "limbo", True, "none"),
    (49, "LimboIntro", "Intro (internal)", "limbo", True, "none"),
    (50, "TreeOfWisdom", "Tree of Wisdom", "garden", False, "tree"),
    (51, "VaseBreaker1", "Vasebreaker", "vasebreaker", False, "vasebreaker"),
    (52, "VaseBreaker2", "To the Left", "vasebreaker", False, "vasebreaker"),
    (53, "VaseBreaker3", "Third Vase", "vasebreaker", False, "vasebreaker"),
    (54, "VaseBreaker4", "Chain Reaction", "vasebreaker", False, "vasebreaker"),
    (55, "VaseBreaker5", "M is for Metal", "vasebreaker", False, "vasebreaker"),
    (56, "VaseBreaker6", "Scary Potter", "vasebreaker", False, "vasebreaker"),
    (57, "VaseBreaker7", "Hokey Pokey", "vasebreaker", False, "vasebreaker"),
    (58, "VaseBreaker8", "Another Chain Reaction", "vasebreaker", False, "vasebreaker"),
    (59, "VaseBreaker9", "Ace of Vase", "vasebreaker", False, "vasebreaker"),
    (60, "VaseBreakerEndless", "Vasebreaker Endless", "vasebreaker", False, "vasebreaker"),
    (61, "IZombie1", "I, Zombie", "izombie", False, "izombie"),
    (62, "IZombie2", "I, Zombie 2", "izombie", False, "izombie"),
    (63, "IZombie3", "I, Zombie 3", "izombie", False, "izombie"),
    (64, "IZombie4", "I, Zombie 4", "izombie", False, "izombie"),
    (65, "IZombie5", "I, Zombie 5", "izombie", False, "izombie"),
    (66, "IZombie6", "I, Zombie 6", "izombie", False, "izombie"),
    (67, "IZombie7", "I, Zombie 7", "izombie", False, "izombie"),
    (68, "IZombie8", "I, Zombie 8", "izombie", False, "izombie"),
    (69, "IZombie9", "I, Zombie 9", "izombie", False, "izombie"),
    (70, "IZombieEndless", "I, Zombie Endless", "izombie", False, "izombie"),
    (71, "UpsellTest", "Upsell Test (internal)", "internal", True, "none"),
    (72, "Intro", "Intro (internal)", "internal", True, "none"),
)

MODE_CATALOG: tuple[ModeMetadata, ...] = tuple(
    {
        "id": mode_id,
        "enum_name": enum_name,
        "name": name,
        "category": category,
        "hidden": hidden,
        "action_profile": profile,
        "native_support": "unknown",
        "unlocked": None,
    }
    for mode_id, enum_name, name, category, hidden, profile in _MODE_ROWS
)

MODE_BY_ID = {mode["id"]: mode for mode in MODE_CATALOG}
