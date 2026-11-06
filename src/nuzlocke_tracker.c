#include "global.h"
#include "gflib.h"
#include "blit.h"
#include "data.h"
#include "decompress.h"
#include "event_data.h"
#include "field_specials.h"
#include "help_system.h"
#include "malloc.h"
#include "pokemon.h"
#include "pokemon_storage_system.h"
#include "region_map.h"
#include "string_util.h"
#include "text.h"
#include "nuzlocke_tracker.h"
#include "constants/flags.h"
#include "constants/maps.h"
#include "constants/region_map_sections.h"
#include "constants/songs.h"
#include "constants/species.h"

// Pressing L opens this instead of the Help System. It reuses the Help System's
// overlay (it pauses whatever is running and draws on BG0), so it works on the
// overworld, in menus and in battle.
//
// Data: gSaveBlock1Ptr->nuzlockeTracker[areaId] holds the species of the first
// Pokémon met in that Nuzlocke area, plus NUZLOCKE_TRACKER_CAUGHT if it was caught.
// Area IDs are the ones from NuzlockeLUT (battle_setup.c); 0 is the starter, and
// 0x4F-0x53 are the extra Safari Zone areas, which have their own Nuzlocke flags.
// Whether a caught Pokémon is alive or dead is worked out live from the party and PC.

extern const u8 NuzlockeLUT[];

#define AREA_SAFARI_CENTER  0x27
#define AREA_SAFARI_EAST    0x4F
#define AREA_SAFARI_NORTH   0x50
#define AREA_SAFARI_WEST    0x51
#define AREA_SAFARI_NW      0x52
#define AREA_SAFARI_NE      0x53

#define TRACKER_SPECIES_MASK 0x7FFF

enum
{
    STATUS_NONE,     // not visited / nothing met yet
    STATUS_UNKNOWN,  // met before the tracker existed; species unknown
    STATUS_MISSED,   // first encounter wasn't caught
    STATUS_ALIVE,
    STATUS_DEAD,
    STATUS_GONE,     // caught, but no longer in the party or PC (released/traded)
};

#define ROW_HEIGHT    16
#define VISIBLE_ROWS  8
#define PANEL_WIDTH   26 // tiles
#define PANEL_HEIGHT  16 // tiles

#define NAME_X     4
#define SPECIES_X  104
#define STATUS_RIGHT 204

// Help System palette indices
#define COLOR_WHITE       1
#define COLOR_DARK_GRAY   2
#define COLOR_LIGHT_GRAY  3
#define COLOR_RED         4
#define COLOR_GREEN       6
#define COLOR_LIGHT_GREEN 7
#define COLOR_BLUE        8
#define COLOR_LIGHT_BLUE  9

struct TrackerArea
{
    u8 id;
    u8 mapsec;          // for the name; MAPSEC_NONE uses customName
    const u8 *customName;
};

static const u8 sText_Starter[] = _("STARTER");
static const u8 sText_SafariEast[] = _("SAFARI ZONE E.");
static const u8 sText_SafariNorth[] = _("SAFARI ZONE N.");
static const u8 sText_SafariWest[] = _("SAFARI ZONE W.");
static const u8 sText_SafariNW[] = _("SAFARI ZONE NW");
static const u8 sText_SafariNE[] = _("SAFARI ZONE NE");
static const u8 sText_Alive[] = _("ALIVE");
static const u8 sText_Dead[] = _("DEAD");
static const u8 sText_Missed[] = _("MISSED");
static const u8 sText_Gone[] = _("GONE");
static const u8 sText_Seen[] = _("SEEN");
static const u8 sText_Unknown[] = _("???");
static const u8 sText_Title[] = _("NUZLOCKE");
static const u8 sText_AliveCount[] = _("ALIVE ");
static const u8 sText_DeadCount[] = _("  DEAD ");

#define AREA(areaId, sec) { .id = areaId, .mapsec = sec, .customName = NULL }
#define AREA_NAMED(areaId, name) { .id = areaId, .mapsec = MAPSEC_NONE, .customName = name }

// In the order a normal playthrough reaches them.
static const struct TrackerArea sTrackerAreas[] =
{
    AREA_NAMED(NUZLOCKE_TRACKER_STARTER, sText_Starter),
    AREA(0x08, MAPSEC_ROUTE_1),
    AREA(0x02, MAPSEC_VIRIDIAN_CITY),
    AREA(0x1D, MAPSEC_ROUTE_22),
    AREA(0x09, MAPSEC_ROUTE_2),
    AREA(0x21, MAPSEC_VIRIDIAN_FOREST),
    AREA(0x0A, MAPSEC_ROUTE_3),
    AREA(0x22, MAPSEC_MT_MOON),
    AREA(0x0B, MAPSEC_ROUTE_4),
    AREA(0x03, MAPSEC_CERULEAN_CITY),
    AREA(0x1F, MAPSEC_ROUTE_24),
    AREA(0x20, MAPSEC_ROUTE_25),
    AREA(0x0C, MAPSEC_ROUTE_5),
    AREA(0x0D, MAPSEC_ROUTE_6),
    AREA(0x04, MAPSEC_VERMILION_CITY),
    AREA(0x23, MAPSEC_S_S_ANNE),
    AREA(0x12, MAPSEC_ROUTE_11),
    AREA(0x24, MAPSEC_DIGLETTS_CAVE),
    AREA(0x10, MAPSEC_ROUTE_9),
    AREA(0x11, MAPSEC_ROUTE_10),
    AREA(0x28, MAPSEC_ROCK_TUNNEL),
    AREA(0x0F, MAPSEC_ROUTE_8),
    AREA(0x0E, MAPSEC_ROUTE_7),
    AREA(0x05, MAPSEC_CELADON_CITY),
    AREA(0x2A, MAPSEC_POKEMON_TOWER),
    AREA(0x13, MAPSEC_ROUTE_12),
    AREA(0x14, MAPSEC_ROUTE_13),
    AREA(0x15, MAPSEC_ROUTE_14),
    AREA(0x16, MAPSEC_ROUTE_15),
    AREA(0x17, MAPSEC_ROUTE_16),
    AREA(0x18, MAPSEC_ROUTE_17),
    AREA(0x19, MAPSEC_ROUTE_18),
    AREA(0x06, MAPSEC_FUCHSIA_CITY),
    AREA(AREA_SAFARI_CENTER, MAPSEC_KANTO_SAFARI_ZONE),
    AREA_NAMED(AREA_SAFARI_EAST, sText_SafariEast),
    AREA_NAMED(AREA_SAFARI_NORTH, sText_SafariNorth),
    AREA_NAMED(AREA_SAFARI_WEST, sText_SafariWest),
    AREA(0x2C, MAPSEC_POWER_PLANT),
    AREA(0x1A, MAPSEC_ROUTE_19),
    AREA(0x1B, MAPSEC_ROUTE_20),
    AREA(0x29, MAPSEC_SEAFOAM_ISLANDS),
    AREA(0x07, MAPSEC_CINNABAR_ISLAND),
    AREA(0x26, MAPSEC_POKEMON_MANSION),
    AREA(0x1C, MAPSEC_ROUTE_21),
    AREA(0x01, MAPSEC_PALLET_TOWN),
    AREA(0x2D, MAPSEC_ONE_ISLAND),
    AREA(0x30, MAPSEC_KINDLE_ROAD),
    AREA(0x31, MAPSEC_TREASURE_BEACH),
    AREA(0x42, MAPSEC_MT_EMBER),
    AREA(0x32, MAPSEC_CAPE_BRINK),
    AREA(0x34, MAPSEC_THREE_ISLE_PORT),
    AREA(0x33, MAPSEC_BOND_BRIDGE),
    AREA(0x43, MAPSEC_BERRY_FOREST),
    AREA(0x1E, MAPSEC_ROUTE_23),
    AREA(0x25, MAPSEC_KANTO_VICTORY_ROAD),
    AREA(0x2E, MAPSEC_FOUR_ISLAND),
    AREA(0x44, MAPSEC_ICEFALL_CAVE),
    AREA(0x2F, MAPSEC_FIVE_ISLAND),
    AREA(0x37, MAPSEC_FIVE_ISLE_MEADOW),
    AREA(0x38, MAPSEC_MEMORIAL_PILLAR),
    AREA(0x36, MAPSEC_WATER_LABYRINTH),
    AREA(0x35, MAPSEC_RESORT_GORGEOUS),
    AREA(0x45, MAPSEC_LOST_CAVE),
    AREA(0x39, MAPSEC_OUTCAST_ISLAND),
    AREA(0x3A, MAPSEC_GREEN_PATH),
    AREA(0x3B, MAPSEC_WATER_PATH),
    AREA(0x3C, MAPSEC_RUIN_VALLEY),
    AREA(0x46, MAPSEC_PATTERN_BUSH),
    AREA(0x47, MAPSEC_ALTERING_CAVE),
    AREA(0x3D, MAPSEC_TRAINER_TOWER),
    AREA(0x3E, MAPSEC_CANYON_ENTRANCE),
    AREA(0x3F, MAPSEC_SEVAULT_CANYON),
    AREA(0x40, MAPSEC_TANOBY_RUINS),
    AREA(0x48, MAPSEC_MONEAN_CHAMBER),
    AREA(0x49, MAPSEC_LIPTOO_CHAMBER),
    AREA(0x4A, MAPSEC_WEEPTH_CHAMBER),
    AREA(0x4B, MAPSEC_DILFORD_CHAMBER),
    AREA(0x4C, MAPSEC_SCUFIB_CHAMBER),
    AREA(0x4D, MAPSEC_RIXY_CHAMBER),
    AREA(0x4E, MAPSEC_VIAPOIS_CHAMBER),
    AREA(0x2B, MAPSEC_CERULEAN_CAVE),
    AREA(0x41, MAPSEC_NAVEL_ROCK),
    AREA_NAMED(AREA_SAFARI_NW, sText_SafariNW),
    AREA_NAMED(AREA_SAFARI_NE, sText_SafariNE),
    AREA(0x54, MAPSEC_ARTISAN_CAVE),
    AREA(0x55, MAPSEC_SOUTHERN_ISLAND),
    AREA(0x56, MAPSEC_BATTLE_FRONTIER),
};

#define NUM_TRACKER_AREAS ARRAY_COUNT(sTrackerAreas)
#define MAX_SCROLL (NUM_TRACKER_AREAS - VISIBLE_ROWS)

#define MAX_TRACKED_MONS (PARTY_SIZE + TOTAL_BOXES_COUNT * IN_BOX_COUNT)

struct OwnedMon
{
    u16 species;
    u8 metLocation;
    u8 alive:1;
    u8 assigned:1;
};

static EWRAM_DATA u8 sStatus[NUZLOCKE_TRACKER_SLOTS] = {0};
static EWRAM_DATA u16 sShownSpecies[NUZLOCKE_TRACKER_SLOTS] = {0};
static EWRAM_DATA u8 sScroll = 0;
static EWRAM_DATA u8 sAliveCount = 0;
static EWRAM_DATA u8 sDeadCount = 0;

// ---------------------------------------------------------------- recording

static u8 GetAreaIdForCurrentMap(u8 mapsec)
{
    u8 id;

    if (mapsec >= MAPSEC_COUNT)
        return 0;
    id = NuzlockeLUT[mapsec];
    if (id == AREA_SAFARI_CENTER) // same split as NuzlockeFlagSet
    {
        switch (gSaveBlock1Ptr->location.mapNum)
        {
        case MAP_NUM(SAFARI_ZONE_EAST):       return AREA_SAFARI_EAST;
        case MAP_NUM(SAFARI_ZONE_NORTH):      return AREA_SAFARI_NORTH;
        case MAP_NUM(SAFARI_ZONE_WEST):       return AREA_SAFARI_WEST;
        case MAP_NUM(SAFARI_ZONE_NORTH_WEST): return AREA_SAFARI_NW;
        case MAP_NUM(SAFARI_ZONE_NORTH_EAST): return AREA_SAFARI_NE;
        }
    }
    return id;
}

static bool8 IsAreaEncountered(u8 id)
{
    if (gSaveBlock1Ptr->nuzlockeTracker[id] != 0)
        return TRUE;
    if (id == NUZLOCKE_TRACKER_STARTER)
        return FALSE;
    return FlagGet(NUZLOCKE_FLAGS_START + id - 1);
}

// Call before NuzlockeFlagSet: only the first encounter in an area is recorded.
void NuzlockeTracker_RecordEncounter(u8 mapsec, u16 species, bool8 caught)
{
    u8 id;

    if (!gSaveBlock1Ptr->keyFlags.nuzlocke || !FlagGet(FLAG_SYS_POKEDEX_GET))
        return;
    if (species == SPECIES_NONE || species >= NUM_SPECIES)
        return;
    id = GetAreaIdForCurrentMap(mapsec);
    if (id == 0 || id >= NUZLOCKE_TRACKER_SLOTS || IsAreaEncountered(id))
        return;
    gSaveBlock1Ptr->nuzlockeTracker[id] = species | (caught ? NUZLOCKE_TRACKER_CAUGHT : 0);
}

void NuzlockeTracker_RecordStarter(u16 species)
{
    if (species != SPECIES_NONE && species < NUM_SPECIES)
        gSaveBlock1Ptr->nuzlockeTracker[NUZLOCKE_TRACKER_STARTER] = species | NUZLOCKE_TRACKER_CAUGHT;
}

// ---------------------------------------------------------------- status

static bool8 IsSameEvolutionLine(u16 species, u16 recorded)
{
    u32 i;

    if (species == recorded)
        return TRUE;
    for (i = 0; i < EVOS_PER_LINE; i++)
    {
        if (gEvolutionLines[species][i] == recorded)
            return TRUE;
    }
    return FALSE;
}

// Which tracker areas a Pokémon met at this location could belong to.
static u8 GetCandidateAreas(u8 metLocation, u8 *out)
{
    u8 n = 0;

    if (metLocation == MAPSEC_PALLET_TOWN)
        out[n++] = NUZLOCKE_TRACKER_STARTER;
    if (metLocation == MAPSEC_KANTO_SAFARI_ZONE)
    {
        out[n++] = AREA_SAFARI_CENTER;
        out[n++] = AREA_SAFARI_EAST;
        out[n++] = AREA_SAFARI_NORTH;
        out[n++] = AREA_SAFARI_WEST;
    }
    else if (metLocation == MAPSEC_SAFARI_ZONE)
    {
        out[n++] = AREA_SAFARI_NW;
        out[n++] = AREA_SAFARI_NE;
    }
    else if (metLocation < MAPSEC_COUNT && NuzlockeLUT[metLocation] != 0)
    {
        out[n++] = NuzlockeLUT[metLocation];
    }
    return n;
}

static bool8 TryAssignMon(struct OwnedMon *mon, u8 id)
{
    u16 entry = gSaveBlock1Ptr->nuzlockeTracker[id];

    if (sStatus[id] != STATUS_GONE) // only caught entries still looking for their Pokémon
        return FALSE;
    if (!IsSameEvolutionLine(mon->species, entry & TRACKER_SPECIES_MASK))
        return FALSE;
    sStatus[id] = mon->alive ? STATUS_ALIVE : STATUS_DEAD;
    sShownSpecies[id] = mon->species;
    mon->assigned = TRUE;
    return TRUE;
}

static void ComputeStatuses(void)
{
    struct OwnedMon *mons;
    u32 otId = GetPlayerTrainerId();
    u16 count = 0;
    u16 i, j;
    u8 candidates[4];
    u8 numCandidates;

    for (i = 0; i < NUZLOCKE_TRACKER_SLOTS; i++)
    {
        u16 entry = gSaveBlock1Ptr->nuzlockeTracker[i];

        sShownSpecies[i] = entry & TRACKER_SPECIES_MASK;
        if (!IsAreaEncountered(i))
            sStatus[i] = STATUS_NONE;
        else if (entry == 0)
            sStatus[i] = STATUS_UNKNOWN;
        else if (entry & NUZLOCKE_TRACKER_CAUGHT)
            sStatus[i] = STATUS_GONE; // until its Pokémon is found below
        else
            sStatus[i] = STATUS_MISSED;
    }

    mons = AllocZeroed(sizeof(struct OwnedMon) * MAX_TRACKED_MONS);
    if (mons != NULL)
    {
        for (i = 0; i < PARTY_SIZE; i++)
        {
            struct Pokemon *mon = &gPlayerParty[i];

            if (!GetMonData(mon, MON_DATA_SANITY_HAS_SPECIES) || GetMonData(mon, MON_DATA_IS_EGG))
                continue;
            if (GetMonData(mon, MON_DATA_OT_ID) != otId)
                continue;
            mons[count].species = GetMonData(mon, MON_DATA_SPECIES);
            mons[count].metLocation = GetMonData(mon, MON_DATA_MET_LOCATION);
            mons[count].alive = GetMonData(mon, MON_DATA_HP) != 0;
            count++;
        }
        for (i = 0; i < TOTAL_BOXES_COUNT; i++)
        {
            for (j = 0; j < IN_BOX_COUNT; j++)
            {
                struct BoxPokemon *boxMon = &gPokemonStoragePtr->boxes[i][j];

                if (!GetBoxMonData(boxMon, MON_DATA_SANITY_HAS_SPECIES) || GetBoxMonData(boxMon, MON_DATA_IS_EGG))
                    continue;
                if (GetBoxMonData(boxMon, MON_DATA_OT_ID) != otId)
                    continue;
                mons[count].species = GetBoxMonData(boxMon, MON_DATA_SPECIES);
                mons[count].metLocation = GetBoxMonData(boxMon, MON_DATA_MET_LOCATION);
                mons[count].alive = GetBoxMonData(boxMon, MON_DATA_BOX_HP) != 0;
                count++;
            }
        }

        // Pass 1: match by where the Pokémon was met and its evolution line.
        for (i = 0; i < count; i++)
        {
            numCandidates = GetCandidateAreas(mons[i].metLocation, candidates);
            for (j = 0; j < numCandidates; j++)
            {
                if (TryAssignMon(&mons[i], candidates[j]))
                    break;
            }
        }
        // Pass 2: Eggs hatch somewhere else, so match the rest by evolution line only.
        for (j = 0; j < NUZLOCKE_TRACKER_SLOTS; j++)
        {
            if (sStatus[j] != STATUS_GONE)
                continue;
            for (i = 0; i < count; i++)
            {
                if (!mons[i].assigned && TryAssignMon(&mons[i], j))
                    break;
            }
        }
        Free(mons);
    }

    sAliveCount = 0;
    sDeadCount = 0;
    for (i = 0; i < NUM_TRACKER_AREAS; i++)
    {
        u8 status = sStatus[sTrackerAreas[i].id];

        if (status == STATUS_ALIVE)
            sAliveCount++;
        else if (status == STATUS_DEAD)
            sDeadCount++;
    }
}

// ---------------------------------------------------------------- drawing

static void PrintInPanel(const u8 *str, u8 fontId, u8 x, u8 y, u8 fg, u8 bg, u8 shadow)
{
    GenerateFontHalfRowLookupTable(fg, bg, shadow);
    HelpSystemRenderText(fontId, gDecompressionBuffer, str, x, y, PANEL_WIDTH, PANEL_HEIGHT);
}

static void DrawRow(u8 row)
{
    struct Bitmap panel;
    const struct TrackerArea *area = &sTrackerAreas[sScroll + row];
    u8 status = sStatus[area->id];
    u8 y = row * ROW_HEIGHT;
    u8 bg = COLOR_WHITE;
    u8 shadow = COLOR_LIGHT_GRAY;
    const u8 *statusText;
    u8 statusColor, statusShadow;
    u8 nameBuf[32];

    panel.pixels = gDecompressionBuffer;
    panel.width = PANEL_WIDTH * 8;
    panel.height = PANEL_HEIGHT * 8;
    FillBitmapRect4Bit(&panel, 0, y, PANEL_WIDTH * 8, ROW_HEIGHT, bg);
    // thin divider under each row
    FillBitmapRect4Bit(&panel, 2, y + ROW_HEIGHT - 1, PANEL_WIDTH * 8 - 4, 1, COLOR_LIGHT_GRAY);

    // Area name: dimmed until something has been met there.
    if (area->customName != NULL)
        StringCopy(nameBuf, area->customName);
    else
        GetMapName(nameBuf, area->mapsec, 0);
    if (status == STATUS_NONE)
        PrintInPanel(nameBuf, FONT_NORMAL, NAME_X, y + 1, COLOR_LIGHT_GRAY, bg, bg);
    else
        PrintInPanel(nameBuf, FONT_NORMAL, NAME_X, y + 1, COLOR_DARK_GRAY, bg, shadow);

    if (status == STATUS_NONE)
        return; // nothing met here yet

    if (status == STATUS_UNKNOWN)
        PrintInPanel(sText_Unknown, FONT_NORMAL, SPECIES_X, y + 1, COLOR_BLUE, bg, shadow);
    else
        PrintInPanel(gSpeciesNames[sShownSpecies[area->id]], FONT_NORMAL, SPECIES_X, y + 1, COLOR_BLUE, bg, shadow);

    switch (status)
    {
    case STATUS_ALIVE:
        statusText = sText_Alive;
        statusColor = COLOR_GREEN;
        statusShadow = COLOR_LIGHT_GREEN;
        break;
    case STATUS_DEAD:
        statusText = sText_Dead;
        statusColor = COLOR_RED;
        statusShadow = shadow;
        break;
    case STATUS_GONE:
        statusText = sText_Gone;
        statusColor = COLOR_DARK_GRAY;
        statusShadow = shadow;
        break;
    case STATUS_UNKNOWN:
        statusText = sText_Seen;
        statusColor = COLOR_DARK_GRAY;
        statusShadow = shadow;
        break;
    default:
        statusText = sText_Missed;
        statusColor = COLOR_DARK_GRAY;
        statusShadow = shadow;
        break;
    }
    PrintInPanel(statusText, FONT_NORMAL, STATUS_RIGHT - GetStringWidth(FONT_NORMAL, statusText, 0), y + 1, statusColor, bg, statusShadow);
}

static void DrawList(void)
{
    u8 i;

    for (i = 0; i < VISIBLE_ROWS; i++)
        DrawRow(i);
    HS_ShowOrHideScrollArrows(0, 0);
    if (sScroll > 0)
        HS_ShowOrHideScrollArrows(0, 1);
    if (sScroll < MAX_SCROLL)
        HS_ShowOrHideScrollArrows(1, 1);
    CommitTilemap();
}

static void DrawHeader(void)
{
    u8 buf[40];
    u8 *str;

    HelpSystem_FillPanel3();
    HelpSystem_PrintTextInTopLeftCorner(sText_Title);

    str = StringCopy(buf, sText_AliveCount);
    str = ConvertIntToDecimalStringN(str, sAliveCount, STR_CONV_MODE_LEFT_ALIGN, 3);
    str = StringCopy(str, sText_DeadCount);
    ConvertIntToDecimalStringN(str, sDeadCount, STR_CONV_MODE_LEFT_ALIGN, 3);
    HelpSystem_FillPanel2();
    HelpSystem_PrintTextRightAlign_Row52(buf);
}

// Start the list at the most recent area with an encounter, so the current
// part of the run is on screen.
static u8 GetInitialScroll(void)
{
    s32 i;
    s32 last = 0;

    for (i = 0; i < (s32)NUM_TRACKER_AREAS; i++)
    {
        if (sStatus[sTrackerAreas[i].id] != STATUS_NONE)
            last = i;
    }
    last -= VISIBLE_ROWS - 2;
    if (last < 0)
        last = 0;
    if (last > (s32)MAX_SCROLL)
        last = MAX_SCROLL;
    return last;
}

void NuzlockeTracker_Open(void)
{
    ComputeStatuses();
    sScroll = GetInitialScroll();

    HS_BufferFillMapWithTile1FF();
    DrawHeader();
    HS_ShowOrHideWordHELPinTopLeft(1);
    HS_ShowOrHideControlsGuideInTopRight(1);
    HS_ShowOrHideMainWindowText(1);
    HS_ShowOrHideHeaderAndFooterLines_Lighter(1);
    HS_ShowOrHideVerticalBlackBarsAlongSides(1);
    DrawList();
}

// Returns FALSE when the tracker should close.
bool8 NuzlockeTracker_HandleInput(void)
{
    s32 newScroll = sScroll;

    if (JOY_NEW(B_BUTTON | L_BUTTON | START_BUTTON))
        return FALSE;

    if (JOY_REPT(DPAD_UP))
        newScroll--;
    else if (JOY_REPT(DPAD_DOWN))
        newScroll++;
    else if (JOY_REPT(DPAD_LEFT))
        newScroll -= VISIBLE_ROWS;
    else if (JOY_REPT(DPAD_RIGHT))
        newScroll += VISIBLE_ROWS;

    if (newScroll < 0)
        newScroll = 0;
    if (newScroll > (s32)MAX_SCROLL)
        newScroll = MAX_SCROLL;
    if (newScroll != sScroll)
    {
        sScroll = newScroll;
        PlaySE(SE_SELECT);
        DrawList();
    }
    return TRUE;
}
