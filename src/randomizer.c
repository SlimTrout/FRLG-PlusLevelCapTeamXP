#include "global.h"
#include "random.h"
#include "item.h"
#include "event_data.h"
#include "fieldmap.h"
#include "constants/vars.h"
#include "randomizer.h"
#include "data.h"
#include "party_menu.h"
#include "string_util.h"
#include "constants/species.h"
#include "constants/items.h"

#include "data/randomizer_pools.h"

enum
{
    STAGE_NONE,     // never randomized (Castform, Eggs, Old Unown...)
    STAGE_0,
    STAGE_1,
    STAGE_2,
    STAGE_LEGENDARY,
};

static EWRAM_DATA u16 sGiftOriginalSpecies = SPECIES_NONE;
static EWRAM_DATA u16 sGiftRandomSpecies = SPECIES_NONE;

static u32 GetRandomizerSeed(void)
{
    return gSaveBlock2Ptr->playerTrainerId[0]
         | (gSaveBlock2Ptr->playerTrainerId[1] << 8)
         | (gSaveBlock2Ptr->playerTrainerId[2] << 16)
         | (gSaveBlock2Ptr->playerTrainerId[3] << 24);
}

// Same algorithm as Modern Emerald's RandomSeededModulo (without chaos mode).
u16 RandomizerSeededModulo(u32 value, u16 modulo)
{
    u32 otId = GetRandomizerSeed();
    u32 randMax = 0xFFFFFFFF - (0xFFFFFFFF % modulo);
    u32 result = 0;
    u8 i = 0;

    do
    {
        result = ISO_RANDOMIZE1(otId + value + result);
    }
    while ((result >= randMax) && (++i != 5));

    return result % modulo;
}

static u16 RandomizerSeeded(u16 value)
{
    return ISO_RANDOMIZE1(GetRandomizerSeed() + value) >> 16;
}

static bool8 IsInList(u16 species, const u16 *list, u16 count)
{
    u16 i;

    for (i = 0; i < count; i++)
    {
        if (list[i] == species)
            return TRUE;
    }
    return FALSE;
}

static u8 GetSpeciesStage(u16 species)
{
    if (IsInList(species, sRandomSpeciesEvo0, ARRAY_COUNT(sRandomSpeciesEvo0)))
        return STAGE_0;
    if (IsInList(species, sRandomSpeciesEvo1, ARRAY_COUNT(sRandomSpeciesEvo1)))
        return STAGE_1;
    if (IsInList(species, sRandomSpeciesEvo2, ARRAY_COUNT(sRandomSpeciesEvo2)))
        return STAGE_2;
    if (IsInList(species, sRandomSpeciesLegendary, ARRAY_COUNT(sRandomSpeciesLegendary)))
        return STAGE_LEGENDARY;
    return STAGE_NONE;
}

u16 GetRandomizedSpecies(u16 species, u8 kind)
{
    u32 offset = 0;
    u8 stage = GetSpeciesStage(species);

    if (stage == STAGE_NONE)
        return species;

    // Wild Pokemon map differently in each area, like Modern Emerald's "map based" setting.
    if (kind == RANDOMIZE_WILD)
        offset = gMapHeader.regionMapSectionId;

    switch (stage)
    {
    case STAGE_0:
        return sRandomSpeciesEvo0[RandomizerSeededModulo(species + offset, ARRAY_COUNT(sRandomSpeciesEvo0))];
    case STAGE_1:
        return sRandomSpeciesEvo1[RandomizerSeededModulo(species + offset, ARRAY_COUNT(sRandomSpeciesEvo1))];
    case STAGE_2:
        return sRandomSpeciesEvo2[RandomizerSeededModulo(species + offset, ARRAY_COUNT(sRandomSpeciesEvo2))];
    case STAGE_LEGENDARY:
        // Legendaries only change in static encounters, and only into other legendaries.
        if (kind == RANDOMIZE_STATIC)
            return sRandomSpeciesLegendary[RandomizerSeededModulo(species, ARRAY_COUNT(sRandomSpeciesLegendary))];
        return species;
    }
    return species;
}

// Modern Emerald's PickRandomStarter: shuffle the unevolved pool with a fixed seed and
// take entries 27 apart, so the three starters are always different.
u16 GetRandomizedStarter(u8 starterId)
{
    u16 list[ARRAY_COUNT(sRandomSpeciesEvo0)];
    u16 i, j, tmp;

    for (i = 0; i < ARRAY_COUNT(sRandomSpeciesEvo0); i++)
        list[i] = sRandomSpeciesEvo0[i];
    for (i = ARRAY_COUNT(list) - 1; i > 0; i--)
    {
        j = RandomizerSeeded(12289) % (i + 1);
        tmp = list[j];
        list[j] = list[i];
        list[i] = tmp;
    }
    return list[(starterId * 27) % ARRAY_COUNT(list)];
}

// TM shuffle: every new game shuffles TM01-TM50 among each other (a seeded
// permutation), and every TM source (ground, gifts, shops, Game Corner) goes
// through it. So each TM still exists exactly as often as before, just elsewhere.
// HMs are never shuffled.
#define NUM_SHUFFLED_TMS (ITEM_TM50 - ITEM_TM01 + 1)

u16 GetShuffledTM(u16 itemId)
{
    u8 perm[NUM_SHUFFLED_TMS];
    u8 i, j, tmp;

    if (itemId < ITEM_TM01 || itemId > ITEM_TM50)
        return itemId;
    for (i = 0; i < NUM_SHUFFLED_TMS; i++)
        perm[i] = i;
    for (i = NUM_SHUFFLED_TMS - 1; i > 0; i--)
    {
        j = RandomizerSeededModulo(0x7A00 + i, i + 1);
        tmp = perm[i];
        perm[i] = perm[j];
        perm[j] = tmp;
    }
    return ITEM_TM01 + perm[itemId - ITEM_TM01];
}

// special: VAR_TEMP_1 holds a Game Corner prize TM. Swaps in the shuffled TM and
// buffers its move as STR_VAR_2.
void ShufflePrizeTM(void)
{
    u16 tm = GetShuffledTM(VarGet(VAR_TEMP_1));

    VarSet(VAR_TEMP_1, tm);
    StringCopy(gStringVar2, gMoveNames[ItemIdToBattleMoveId(tm)]);
}

// Items some evolution needs stay where FRLG+ puts them, so those evolutions stay possible.
static const u16 sEvolutionItems[] =
{
    ITEM_FIRE_STONE, ITEM_WATER_STONE, ITEM_THUNDER_STONE, ITEM_LEAF_STONE, ITEM_MOON_STONE, ITEM_SUN_STONE,
    ITEM_METAL_COAT, ITEM_KINGS_ROCK, ITEM_DRAGON_SCALE, ITEM_UP_GRADE, ITEM_DEEP_SEA_TOOTH, ITEM_DEEP_SEA_SCALE,
    ITEM_HARD_STONE, ITEM_SPELL_TAG, ITEM_NEVER_MELT_ICE, ITEM_HEART_SCALE, ITEM_LUCKY_EGG,
};

static bool8 IsEvolutionItem(u16 itemId)
{
    return IsInList(itemId, sEvolutionItems, ARRAY_COUNT(sEvolutionItems));
}

// Modern Emerald's RandomItemId. Key items, HMs and Poke Balls are left alone
// (Oak's Poke Balls start FRLG+'s Nuzlocke rules); TMs become a TM the player
// doesn't have yet when possible.
u16 GetRandomizedItem(u16 itemId)
{
    u8 pocket;

    if (itemId == ITEM_NONE || itemId == ITEM_POKE_BALL || IsEvolutionItem(itemId))
        return itemId;

    pocket = ItemId_GetPocket(itemId);
    if (pocket == POCKET_KEY_ITEMS)
        return itemId;

    if (pocket == POCKET_TM_CASE)
        return GetShuffledTM(itemId);

    return sRandomItems[RandomizerSeededModulo(itemId + gMapHeader.regionMapSectionId, ARRAY_COUNT(sRandomItems))];
}

// ---------------------------------------------------------------- script specials

static void SetGiftSpeciesForDisplay(u16 original, u16 randomized)
{
    sGiftOriginalSpecies = original;
    sGiftRandomSpecies = randomized;
}

u16 GetGiftSpeciesForDisplay(u16 species)
{
    if (species != SPECIES_NONE && species == sGiftOriginalSpecies)
        return sGiftRandomSpecies;
    return species;
}

void ResetGiftSpeciesForDisplay(void)
{
    sGiftOriginalSpecies = SPECIES_NONE;
    sGiftRandomSpecies = SPECIES_NONE;
}

// Randomizes a gift and remembers it so the following text names the right Pokemon.
u16 RandomizeGiftSpecies(u16 species)
{
    u16 randomized = GetRandomizedSpecies(species, RANDOMIZE_GIFT);

    SetGiftSpeciesForDisplay(species, randomized);
    return randomized;
}

// callnative from Oak's Lab: replaces VAR_TEMP_2 (the player's starter species)
// with this ball's random starter. VAR_TEMP_1 holds which ball (0-2).
void SetRandomizedStarterVar(void)
{
    u16 starter = GetRandomizedStarter(VarGet(VAR_TEMP_1));

    VarSet(VAR_TEMP_2, starter);
}

// special: VAR_0x8000 holds the item an item ball or NPC is about to give.
void RandomizeObtainedItem(void)
{
    gSpecialVar_0x8000 = GetRandomizedItem(gSpecialVar_0x8000);
}

// special: VAR_0x8000 holds a TM someone is about to give. Randomizes it and buffers
// its name (STR_VAR_2) and move (STR_VAR_3) for the "received" text.
void RandomizeGiftTM(void)
{
    gSpecialVar_0x8000 = GetRandomizedItem(gSpecialVar_0x8000);
    CopyItemName(gSpecialVar_0x8000, gStringVar2);
    StringCopy(gStringVar3, gMoveNames[ItemIdToBattleMoveId(gSpecialVar_0x8000)]);
}

// special: re-buffers the TM in VAR_0x8006 as STR_VAR_2 / its move as STR_VAR_3
// (the "put away in the TM CASE" message overwrites STR_VAR_3).
void BufferGiftTMNames(void)
{
    CopyItemName(gSpecialVar_0x8006, gStringVar2);
    StringCopy(gStringVar3, gMoveNames[ItemIdToBattleMoveId(gSpecialVar_0x8006)]);
}

// special: VAR_0x8005 holds a hidden item's ID.
void RandomizeHiddenItem(void)
{
    gSpecialVar_0x8005 = GetRandomizedItem(gSpecialVar_0x8005);
}
