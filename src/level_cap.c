#include "global.h"
#include "event_data.h"
#include "pokemon.h"
#include "level_cap.h"
#include "constants/flags.h"

// Level caps track the ace of the next boss trainer. Each entry holds the
// Normal Mode ace level plus the level scaling that Challenge Mode adds (and
// Easy Mode subtracts) for that fight, so the cap always matches what the
// player will actually face on the current difficulty.
struct LevelCapStage
{
    u8 level;
    u8 scaling;
};

// Indexed by badge count (0-7).
static const struct LevelCapStage sGymLevelCaps[NUM_BADGES] =
{
    { 14, 1 }, // Brock
    { 21, 1 }, // Misty
    { 24, 2 }, // Lt. Surge
    { 29, 2 }, // Erika
    { 43, 3 }, // Koga (Sabrina's ace is the same level, so order doesn't matter)
    { 43, 3 }, // Sabrina
    { 47, 4 }, // Blaine
    { 50, 4 }, // Giovanni
};

enum
{
    E4_LORELEI,
    E4_BRUNO,
    E4_AGATHA,
    E4_LANCE,
    E4_CHAMPION,
    E4_COUNT
};

// First run through the Pokémon League.
static const struct LevelCapStage sLeagueLevelCaps[E4_COUNT] =
{
    [E4_LORELEI]  = { 54, 4 },
    [E4_BRUNO]    = { 56, 4 },
    [E4_AGATHA]   = { 58, 4 },
    [E4_LANCE]    = { 60, 4 },
    [E4_CHAMPION] = { 63, 4 },
};

// League rematches. Rematch trainers get an extra 5 levels of scaling on
// Challenge/Easy Mode (see CreateNPCTrainerParty in battle_main.c).
static const struct LevelCapStage sRematchLevelCap = { 75, 4 + 5 }; // Champion rematch ace

static const u16 sLeagueFlags[E4_COUNT] =
{
    FLAG_DEFEATED_LORELEI,
    FLAG_DEFEATED_BRUNO,
    FLAG_DEFEATED_AGATHA,
    FLAG_DEFEATED_LANCE,
    FLAG_DEFEATED_CHAMP,
};

static u8 GetStageCap(const struct LevelCapStage *stage)
{
    s32 cap = stage->level;

    if (gSaveBlock1Ptr->keyFlags.difficulty == DIFFICULTY_CHALLENGE)
        cap += stage->scaling;
    else if (gSaveBlock1Ptr->keyFlags.difficulty == DIFFICULTY_EASY)
        cap -= stage->scaling;

    if (cap < 1)
        cap = 1;
    if (cap > MAX_LEVEL)
        cap = MAX_LEVEL;
    return cap;
}

static u32 CountBadges(void)
{
    u32 i, count = 0;

    for (i = FLAG_BADGE01_GET; i <= FLAG_BADGE08_GET; i++)
    {
        if (FlagGet(i))
            count++;
    }
    return count;
}

u8 GetCurrentLevelCap(void)
{
    u32 i, badges;

    if (!gSaveBlock1Ptr->keyFlags.levelCap)
        return MAX_LEVEL;

    // League rematches are unlocked after the Sevii Islands network is restored.
    if (FlagGet(FLAG_SYS_CAN_LINK_WITH_RS))
        return GetStageCap(&sRematchLevelCap);

    // Beat the Champion once; the E4 flags are cleared afterwards, so check this first.
    if (FlagGet(FLAG_SYS_GAME_CLEAR))
        return GetStageCap(&sLeagueLevelCaps[E4_CHAMPION]);

    badges = CountBadges();
    if (badges < NUM_BADGES)
        return GetStageCap(&sGymLevelCaps[badges]);

    // All eight badges: step through the League as each member falls.
    for (i = 0; i < E4_COUNT - 1; i++)
    {
        if (!FlagGet(sLeagueFlags[i]))
            break;
    }
    return GetStageCap(&sLeagueLevelCaps[i]);
}

bool8 IsMonAtLevelCap(struct Pokemon *mon)
{
    u8 level = GetMonData(mon, MON_DATA_LEVEL);

    return (level < MAX_LEVEL && level >= GetCurrentLevelCap());
}

u32 GetCappedExpGain(struct Pokemon *mon, u32 expGain)
{
    u8 cap = GetCurrentLevelCap();
    u8 level = GetMonData(mon, MON_DATA_LEVEL);
    u16 species;
    u32 currentExp, capExp;

    if (cap >= MAX_LEVEL || level >= MAX_LEVEL)
        return expGain;
    if (level >= cap)
        return 0;

    species = GetMonData(mon, MON_DATA_SPECIES);
    currentExp = GetMonData(mon, MON_DATA_EXP);
    capExp = gExperienceTables[gSpeciesInfo[species].growthRate][cap];

    if (currentExp >= capExp)
        return 0;
    if (currentExp + expGain > capExp)
        return capExp - currentExp;
    return expGain;
}
