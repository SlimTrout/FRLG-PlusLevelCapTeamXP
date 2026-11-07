#include "global.h"
#include "coop.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "event_scripts.h"
#include "field_fadetransition.h"
#include "field_message_box.h"
#include "field_specials.h"
#include "save.h"
#include "task.h"
#include "link.h"
#include "malloc.h"
#include "pokemon.h"
#include "item.h"
#include "script_pokemon_util.h"
#include "pokedex.h"
#include "money.h"
#include "battle_setup.h"
#include "battle.h"
#include "wild_encounter.h"
#include "constants/global.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "main.h"
#include "overworld.h"
#include "script.h"
#include "string_util.h"
#include "characters.h"
#include "constants/event_objects.h"
#include "constants/event_object_movement.h"
#include "constants/flags.h"
#include "constants/maps.h"
#include "constants/layouts.h"
#include "constants/vars.h"
#include "constants/moves.h"
#include "constants/species.h"
#include "constants/items.h"

// Brainy Blue Co-op.
//
// Partner on the map: the partner is shown as a walking character, driven by the
// state the app copies into gCoopMailbox.partner every frame.
//
// Linked play: the follower has no character of its own. Its (invisible) player
// stands on the leader's tile and walks the leader's steps, so its camera follows
// the leader. The leader records every step it takes in a small ring; the follower
// moves its player straight along them, with no collision and nothing on the map
// reacting to it. If it ever loses track (the leader went through a door, flew, or
// fell through a hole), it warps to the leader.
//
// Dialogue: whenever the leader has a field message on screen, the follower shows
// the same text and turns its pages when the leader does, and it closes when the
// leader's closes.

extern const u8 Coop_EventScript_TalkToPartner[];
extern const u8 Coop_EventScript_LinkRequest[];
extern const u8 Coop_EventScript_ControlRequest[];
extern const u8 Coop_EventScript_Notice[];
extern const u8 Coop_EventScript_FollowerMenu[];
extern const u8 Coop_EventScript_SaveWithHost[];
extern const u8 Coop_EventScript_LearnMoves[];

extern const u8 Coop_Text_PartnerLinkedYouLead[];
extern const u8 Coop_Text_PartnerDeclinedLink[];
extern const u8 Coop_Text_YouHaveControl[];
extern const u8 Coop_Text_PartnerKeptControl[];
extern const u8 Coop_Text_PartnerSplitOff[];
extern const u8 Coop_Text_PartnerDisconnected[];
extern const u8 Coop_Text_SplitForStarters[];
extern const u8 Coop_Text_PulledIntoStoryBattle[];
extern const u8 Coop_EventScript_StoryGetReady[];
extern const u8 Coop_EventScript_WhiteOut[];
extern const u8 Coop_EventScript_NicknameCaught[];
extern const u8 Coop_EventScript_TradeRequest[];
extern const u8 Coop_Text_SoloBattle[];
extern const u8 Coop_Text_SplitForSafari[];

EWRAM_DATA struct CoopMailbox gCoopMailbox = {0};
EWRAM_DATA u8 gCoopTextPages = 0; // not plain "u8 gCoopTextPages;": that lands past the end of IWRAM, on top of the GPU register buffer

static u8 sPartnerObjId;
static u8 sPartnerMapGroup;
static u8 sPartnerMapNum;
static bool8 sPartnerObjValid;

// link state
static u8 sLastPartnerCmdSeq;
static bool8 sAwaitingLinkAnswer;
static bool8 sAwaitingControlAnswer;
static const u8 *sPendingScript;
static const u8 *sPendingNotice;
static bool8 sWasActive;
static EWRAM_DATA bool8 sCoopScriptRunning = FALSE; // its messages aren't mirrored
static bool8 sFollowerMenuRequested;
static bool8 sSaveWithHost;
static u8 sItemParty;              // co-op battle: battle party slot the bag item was used on
static u8 sItemMove;               // ...and the move (PP items)
static bool8 sApplyingItem;        // host: using the partner's item on its copy
static u8 sItemMonByBattler[4];    // host: the item each battler used this turn went on this slot
static u8 sCaughtBy;               // co-op battle: player index of who caught the wild Pokemon
static struct Pokemon *sCaughtMon; // (on the heap while a co-op battle is on)
static bool8 sStoryPartnerReady;   // story battle starter: the partner said they're ready
static bool8 sStoryReadySent;      // pulled player: said ready, so no "joining in" notice
static bool8 sWhiteOutPending;     // all own Pokemon fainted in a co-op battle the partner won
static bool8 sNoShare;             // the next item/Pokemon isn't shared (item balls, gifts received)
static u16 sLastPartnerGiftSeq;    // partner's gift counter last seen
static bool8 sGiftSeqKnown;
static u16 sConnectedFrames;       // overworld frames since connecting (the guest gets the host's world first)
static u16 sNickSpecies;           // co-op catch to offer a nickname for
static u8 sNickWhere;              // MON_GIVEN_TO_PARTY / MON_GIVEN_TO_PC
static u8 sTradeAnswer;            // trade asker: 0 waiting, 1 yes, 2 no
static bool8 sTrading;             // a co-op trade's scene is on
static u8 sLeveledUp;             // own Pokemon that leveled up in the last co-op battle
static u8 sCoopBattlePending;    // COOP_CMD_* of a battle the partner started, waiting to join
static bool8 sStoryPullWarp;     // pulled into a story battle: warp to the partner first
static bool8 sWarpArrived;       // that warp is done
static u8 sMovesPending; // own party slots with level-up moves they couldn't learn in a co-op battle

// step recording (always recorded)
static s16 sLastX;
static s16 sLastY;
static u8 sLastMapGroup;
static u8 sLastMapNum;
static u32 sLastFrame;

// following
static EWRAM_DATA u8 sConsumed = 0;     // leader steps already walked
static u8 sIdleFrames;
static bool8 sWarping;
static bool8 sResync;
static u16 sWarpFrames;
static u32 sFollowFrame;

// dialogue mirroring
static EWRAM_DATA bool8 sMsgShown = FALSE;     // leader: a field message is up
static EWRAM_DATA u8 sMsgPageBase = 0;         // leader: gCoopTextPages when it started
static EWRAM_DATA bool8 sMirroring = FALSE;    // follower: showing the leader's message
static bool8 sShowingMirror;
static u8 sMirrorSeq;
static u8 sMirrorPageBase;

// ---------------------------------------------------------------- helpers

static struct ObjectEvent *GetPlayerObject(void)
{
    return &gObjectEvents[gPlayerAvatar.objectEventId];
}

static bool8 OnSameMapAsPartner(void)
{
    return gCoopMailbox.partner.mapGroup == gSaveBlock1Ptr->location.mapGroup
        && gCoopMailbox.partner.mapNum == gSaveBlock1Ptr->location.mapNum;
}

static u8 GetDirectionTo(s16 dx, s16 dy)
{
    if (dx > 0)
        return DIR_EAST;
    if (dx < 0)
        return DIR_WEST;
    if (dy > 0)
        return DIR_SOUTH;
    return DIR_NORTH;
}

static void SendCommand(u8 cmd)
{
    gCoopMailbox.local.cmd = cmd;
    gCoopMailbox.local.cmdSeq++;
}

static void ShowNotice(const u8 *text)
{
    sPendingNotice = text;
}

static void StopMirroring(void)
{
    if (sMirroring)
        HideFieldMessageBox();
    sMirroring = FALSE;
}

static void ShowOwnPlayer(void)
{
    GetPlayerObject()->invisible = FALSE;
}

static void StartFollowing(void)
{
    gCoopMailbox.local.mode = COOP_MODE_FOLLOW;
    sConsumed = gCoopMailbox.partner.stepCount;
    sIdleFrames = 0;
    sWarping = FALSE;
    sResync = FALSE;
}

static void StartLeading(void)
{
    gCoopMailbox.local.mode = COOP_MODE_LEAD;
    StopMirroring();
    ShowOwnPlayer();
}

static void Split(void)
{
    gCoopMailbox.local.mode = COOP_MODE_SPLIT;
    StopMirroring();
    ShowOwnPlayer();
}

bool8 Coop_IsLinked(void)
{
    return gCoopMailbox.active && gCoopMailbox.local.mode != COOP_MODE_SPLIT;
}

bool8 Coop_IsFollowing(void)
{
    return gCoopMailbox.active && gCoopMailbox.local.mode == COOP_MODE_FOLLOW;
}

bool8 Coop_IsPartnerObject(struct ObjectEvent *objectEvent)
{
    return gCoopMailbox.active && objectEvent->localId == COOP_PARTNER_LOCALID;
}

const u8 *Coop_GetPartnerInteractionScript(void)
{
    sCoopScriptRunning = TRUE;
    return Coop_EventScript_TalkToPartner;
}

// R while linked: the leader's or follower's co-op menu.
const u8 *Coop_GetMenuScript(void)
{
    sCoopScriptRunning = TRUE;
    if (Coop_IsFollowing())
        return Coop_EventScript_FollowerMenu;
    return Coop_EventScript_TalkToPartner;
}

// ---------------------------------------------------------------- local state

static void RecordStep(u8 dir)
{
    struct CoopPlayerState *s = &gCoopMailbox.local;
    u8 slot = s->stepCount % COOP_STEP_RING;
    u8 shift = (slot % 4) * 2;

    s->steps[slot / 4] = (s->steps[slot / 4] & ~(3 << shift)) | (((dir - 1) & 3) << shift);
    s->stepCount++;
}

static u8 GetPartnerStep(u8 index)
{
    u8 slot = index % COOP_STEP_RING;

    return ((gCoopMailbox.partner.steps[slot / 4] >> ((slot % 4) * 2)) & 3) + 1;
}

// Every tile the player moves on the overworld is a step, whether by input, a
// script or a slide (a ledge jump is two). A gap in frames means a warp or a
// screen in between, and a map change is crossing into a connected map; neither
// is a step.
static void TrackSteps(void)
{
    struct ObjectEvent *player = GetPlayerObject();
    u32 frame = gMain.vblankCounter2;
    s16 dx = player->currentCoords.x - sLastX;
    s16 dy = player->currentCoords.y - sLastY;

    if ((dx != 0 || dy != 0) && frame - sLastFrame <= 2
     && gSaveBlock1Ptr->location.mapNum == sLastMapNum && gSaveBlock1Ptr->location.mapGroup == sLastMapGroup
     && (dx == 0 || dy == 0) && dx >= -2 && dx <= 2 && dy >= -2 && dy <= 2)
    {
        u8 dir = GetDirectionTo(dx, dy);
        u8 n = (dx != 0) ? (dx > 0 ? dx : -dx) : (dy > 0 ? dy : -dy);

        while (n--)
            RecordStep(dir);
    }
    sLastX = player->currentCoords.x;
    sLastY = player->currentCoords.y;
    sLastMapNum = gSaveBlock1Ptr->location.mapNum;
    sLastMapGroup = gSaveBlock1Ptr->location.mapGroup;
    sLastFrame = frame;
}

static void WriteLocalState(void)
{
    struct CoopPlayerState *s = &gCoopMailbox.local;
    struct ObjectEvent *player = GetPlayerObject();

    s->mapGroup = gSaveBlock1Ptr->location.mapGroup;
    s->mapNum = gSaveBlock1Ptr->location.mapNum;
    s->x = player->currentCoords.x;
    s->y = player->currentCoords.y;
    s->facing = player->facingDirection;
    s->elevation = player->currentElevation;
    s->gender = gSaveBlock2Ptr->playerGender;
    s->running = (gPlayerAvatar.flags & PLAYER_AVATAR_FLAG_DASH) != 0;
    s->biking = (gPlayerAvatar.flags & (PLAYER_AVATAR_FLAG_MACH_BIKE | PLAYER_AVATAR_FLAG_ACRO_BIKE)) != 0;
    s->inOverworld = TRUE;
    s->msgActive = sMsgShown;
    s->msgPage = gCoopTextPages - sMsgPageBase;
    s->keySettings = *(u16 *)&gSaveBlock1Ptr->keyFlags;
    s->hasPokemon = CalculatePlayerPartyCount() != 0;
    StringCopyN(s->name, gSaveBlock2Ptr->playerName, PLAYER_NAME_LENGTH + 1);
    s->seq++;
}

// ---------------------------------------------------------------- partner on the map

static struct ObjectEvent *GetPartnerObject(void)
{
    struct ObjectEvent *obj;

    if (!sPartnerObjValid)
        return NULL;
    obj = &gObjectEvents[sPartnerObjId];
    if (!obj->active || obj->localId != COOP_PARTNER_LOCALID
     || sPartnerMapGroup != gSaveBlock1Ptr->location.mapGroup || sPartnerMapNum != gSaveBlock1Ptr->location.mapNum)
    {
        if (obj->active && obj->localId == COOP_PARTNER_LOCALID)
            RemoveObjectEvent(obj);
        sPartnerObjValid = FALSE;
        return NULL;
    }
    return obj;
}

static void RemovePartner(void)
{
    struct ObjectEvent *obj = GetPartnerObject();

    if (obj != NULL)
        RemoveObjectEvent(obj);
    sPartnerObjValid = FALSE;
}

// Same rectangle the game uses to remove off-screen objects, so the partner is
// never spawned just to be removed again.
static bool8 IsInView(s16 x, s16 y)
{
    s16 left = gSaveBlock1Ptr->pos.x - 2;
    s16 right = gSaveBlock1Ptr->pos.x + MAP_OFFSET_W + 2;
    s16 top = gSaveBlock1Ptr->pos.y;
    s16 bottom = gSaveBlock1Ptr->pos.y + MAP_OFFSET_H + 2;

    return x >= left && x <= right && y >= top && y <= bottom;
}

static void UpdatePartnerSprite(void)
{
    struct CoopPlayerState *p = &gCoopMailbox.partner;
    struct ObjectEvent *obj = GetPartnerObject();
    s16 dx, dy;

    // A follower has no character of its own: the leader sees nobody.
    if (!OnSameMapAsPartner() || p->mode == COOP_MODE_FOLLOW)
    {
        RemovePartner();
        return;
    }

    if (obj == NULL)
    {
        int id;

        if (!IsInView(p->x, p->y))
            return;
        id = SpawnSpecialObjectEventParameterized(p->gender ? OBJ_EVENT_GFX_GREEN_NORMAL : OBJ_EVENT_GFX_RED_NORMAL,
                                                  MOVEMENT_TYPE_NONE, COOP_PARTNER_LOCALID, p->x, p->y, p->elevation);
        if (id >= OBJECT_EVENTS_COUNT)
            return;
        sPartnerObjId = id;
        sPartnerObjValid = TRUE;
        sPartnerMapGroup = gSaveBlock1Ptr->location.mapGroup;
        sPartnerMapNum = gSaveBlock1Ptr->location.mapNum;
        obj = &gObjectEvents[id];
        SetObjectEventDirection(obj, p->facing);
        return;
    }

    // Let a step that's already under way finish first.
    if (obj->heldMovementActive && !ObjectEventClearHeldMovementIfFinished(obj))
        return;

    obj->currentElevation = p->elevation;
    obj->previousElevation = p->elevation;
    dx = p->x - obj->currentCoords.x;
    dy = p->y - obj->currentCoords.y;
    if (dx == 0 && dy == 0)
    {
        if (obj->facingDirection != p->facing)
            ObjectEventSetHeldMovement(obj, GetFaceDirectionMovementAction(p->facing));
    }
    else if ((dx == 0 || dy == 0) && (dx * dx + dy * dy) == 1)
    {
        u8 dir = GetDirectionTo(dx, dy);
        u8 action;

        if (p->biking)
            action = GetWalkFasterMovementAction(dir);
        else if (p->running)
            action = GetWalkFastMovementAction(dir);
        else
            action = GetWalkNormalMovementAction(dir);
        ObjectEventSetHeldMovement(obj, action);
    }
    else
    {
        MoveObjectEventToMapCoords(obj, p->x, p->y);
        SetObjectEventDirection(obj, p->facing);
    }
}

// ---------------------------------------------------------------- commands from the partner

static void HandlePartnerCommand(void)
{
    struct CoopPlayerState *p = &gCoopMailbox.partner;

    if (p->cmdSeq == sLastPartnerCmdSeq)
        return;
    sLastPartnerCmdSeq = p->cmdSeq;

    switch (p->cmd)
    {
    case COOP_CMD_LINK_ASK:
        if (gCoopMailbox.local.mode == COOP_MODE_SPLIT)
            sPendingScript = Coop_EventScript_LinkRequest;
        break;
    case COOP_CMD_LINK_ACCEPT:
        if (sAwaitingLinkAnswer)
        {
            sAwaitingLinkAnswer = FALSE;
            StartLeading();
            ShowNotice(Coop_Text_PartnerLinkedYouLead);
        }
        break;
    case COOP_CMD_LINK_DECLINE:
        if (sAwaitingLinkAnswer)
        {
            sAwaitingLinkAnswer = FALSE;
            ShowNotice(Coop_Text_PartnerDeclinedLink);
        }
        break;
    case COOP_CMD_HANDOVER:
        sAwaitingControlAnswer = FALSE;
        StartLeading();
        ShowNotice(Coop_Text_YouHaveControl);
        break;
    case COOP_CMD_CONTROL_ASK:
        if (gCoopMailbox.local.mode == COOP_MODE_LEAD)
            sPendingScript = Coop_EventScript_ControlRequest;
        break;
    case COOP_CMD_CONTROL_DECLINE:
        if (sAwaitingControlAnswer)
        {
            sAwaitingControlAnswer = FALSE;
            ShowNotice(Coop_Text_PartnerKeptControl);
        }
        break;
    case COOP_CMD_SPLIT:
        if (gCoopMailbox.local.mode != COOP_MODE_SPLIT)
        {
            Split();
            ShowNotice(Coop_Text_PartnerSplitOff);
        }
        break;
    case COOP_CMD_WILD_BATTLE:
        if (Coop_IsFollowing())
            sCoopBattlePending = COOP_CMD_WILD_BATTLE;
        break;
    case COOP_CMD_TRAINER_BATTLE:
    case COOP_CMD_TRAINER_BATTLE_NO_WHITEOUT:
        if (Coop_IsFollowing())
            sCoopBattlePending = p->cmd;
        break;
    case COOP_CMD_STORY_ASK:
        // The partner is about to start a story battle: get ready (heal, PC) first.
        if (gCoopMailbox.local.mode == COOP_MODE_SPLIT)
            sPendingScript = Coop_EventScript_StoryGetReady;
        break;
    case COOP_CMD_STORY_READY:
        sStoryPartnerReady = TRUE;
        break;
    case COOP_CMD_HEAL:
        if (Coop_IsFollowing())
            HealPlayerParty();
        break;
    case COOP_CMD_TRADE_ASK:
        if (gCoopMailbox.local.mode == COOP_MODE_SPLIT)
            sPendingScript = Coop_EventScript_TradeRequest;
        else
            SendCommand(COOP_CMD_TRADE_DECLINE);
        break;
    case COOP_CMD_TRADE_ACCEPT:
        sTradeAnswer = 1;
        break;
    case COOP_CMD_TRADE_DECLINE:
        sTradeAnswer = 2;
        if (sPendingScript == Coop_EventScript_TradeRequest)
            sPendingScript = NULL; // the asker gave up
        break;
    case COOP_CMD_STORY_PULL:
    case COOP_CMD_STORY_PULL_NO_WHITEOUT:
        // The partner started a story battle: go to them, link up (they lead) and join.
        if (sStoryReadySent)
            sStoryReadySent = FALSE;
        else
            sPendingNotice = Coop_Text_PulledIntoStoryBattle;
        StopMirroring();
        StartFollowing();
        sStoryPullWarp = TRUE;
        sCoopBattlePending = (p->cmd == COOP_CMD_STORY_PULL) ? COOP_CMD_TRAINER_BATTLE : COOP_CMD_TRAINER_BATTLE_NO_WHITEOUT;
        break;
    case COOP_CMD_SAVED:
        if (Coop_IsGuest())
            sSaveWithHost = TRUE;
        break;
    }
}

static bool8 CanRunCoopScript(void)
{
    return Coop_IsInOverworld() && !ArePlayerFieldControlsLocked() && !ScriptContext_IsEnabled() && !sMirroring;
}

static void RunCoopScript(const u8 *script)
{
    sCoopScriptRunning = TRUE;
    ScriptContext_SetupScript(script);
}

static void RunPendingScripts(void)
{
    if (!CanRunCoopScript())
        return;
    if (sWhiteOutPending)
    {
        sWhiteOutPending = FALSE;
        RunCoopScript(Coop_EventScript_WhiteOut);
    }
    else if (sMovesPending)
    {
        u8 slot = 0;

        while (!(sMovesPending & (1 << slot)))
            slot++;
        sMovesPending &= ~(1 << slot);
        gSpecialVar_0x8004 = slot;
        gSpecialVar_0x8005 = GetNumberOfRelearnableMoves(&gPlayerParty[slot]);
        if (gSpecialVar_0x8005 != 0)
            RunCoopScript(Coop_EventScript_LearnMoves);
    }
    else if (sSaveWithHost)
    {
        sSaveWithHost = FALSE;
        RunCoopScript(Coop_EventScript_SaveWithHost);
    }
    else if (sPendingScript != NULL)
    {
        RunCoopScript(sPendingScript);
        sPendingScript = NULL;
    }
    else if (sPendingNotice != NULL)
    {
        Coop_BufferPartnerName();
        StringExpandPlaceholders(gStringVar4, sPendingNotice);
        sPendingNotice = NULL;
        RunCoopScript(Coop_EventScript_Notice);
    }
    else if (sFollowerMenuRequested)
    {
        sFollowerMenuRequested = FALSE;
        if (Coop_IsFollowing())
            RunCoopScript(Coop_EventScript_FollowerMenu);
    }
}

// ---------------------------------------------------------------- following

static void WarpToLeader(void)
{
    struct CoopPlayerState *p = &gCoopMailbox.partner;

    SetWarpDestination(p->mapGroup, p->mapNum, -1, p->x - MAP_OFFSET, p->y - MAP_OFFSET);
    DoWarp();
    sWarping = TRUE;
    sWarpFrames = 0;
    sFollowFrame = gMain.vblankCounter2; // (may be stale if following just started)
    sIdleFrames = 0;
}

// Walk the invisible player one tile, ignoring collision and tile effects.
static void FollowerStep(u8 dir, u8 pending)
{
    struct CoopPlayerState *p = &gCoopMailbox.partner;
    struct ObjectEvent *player = GetPlayerObject();
    u8 action;

    if (pending >= 4 || p->biking)
        action = GetWalkFasterMovementAction(dir);
    else if (pending >= 2 || p->running)
        action = GetWalkFastMovementAction(dir);
    else
        action = GetWalkNormalMovementAction(dir);
    player->currentElevation = p->elevation;
    player->previousElevation = p->elevation;
    ObjectEventClearHeldMovementIfActive(player);
    ObjectEventSetHeldMovement(player, action);
}

static void UpdateFollowing(void)
{
    struct CoopPlayerState *p = &gCoopMailbox.partner;
    struct ObjectEvent *player = GetPlayerObject();
    u32 frame = gMain.vblankCounter2;
    u8 pending;
    s16 dx, dy;

    if (!Coop_IsInOverworld())
        return;
    player->invisible = TRUE;

    // A warp is done once the overworld has been reloaded (a gap in frames).
    if (sWarping)
    {
        if (frame - sFollowFrame > 2 || ++sWarpFrames > 300)
        {
            sWarping = FALSE;
            sResync = TRUE;
            sWarpArrived = TRUE;
        }
        sFollowFrame = frame;
        return;
    }
    sFollowFrame = frame;

    // The follower's own menus and warp transitions pause it.
    if (ArePlayerFieldControlsLocked())
        return;
    if (sResync)
    {
        // Steps taken during the warp are made up by walking straight there.
        sConsumed = p->stepCount;
        sResync = FALSE;
    }
    if (ObjectEventCheckHeldMovementStatus(player) == 0)
        return; // a step is under way
    ObjectEventClearHeldMovementIfFinished(player);

    pending = p->stepCount - sConsumed;
    if (pending > COOP_STEP_RING - 2)
    {
        WarpToLeader();
        return;
    }
    if (pending > 0)
    {
        FollowerStep(GetPartnerStep(sConsumed), pending);
        sConsumed++;
        sIdleFrames = 0;
        return;
    }

    // Caught up. Went through a door or warp, flew, or fell: go there.
    if (!OnSameMapAsPartner())
    {
        if (++sIdleFrames > 4)
            WarpToLeader();
        return;
    }
    dx = p->x - player->currentCoords.x;
    dy = p->y - player->currentCoords.y;
    if (dx == 0 && dy == 0)
    {
        sIdleFrames = 0;
        if (player->facingDirection != p->facing)
            SetObjectEventDirection(player, p->facing);
        return;
    }
    // Off by a little (just linked, or after a warp): walk straight there.
    if (dx > 12 || dx < -12 || dy > 12 || dy < -12)
    {
        if (++sIdleFrames > 4)
            WarpToLeader();
        return;
    }
    if (++sIdleFrames > 2)
        FollowerStep(GetDirectionTo(dx, dx != 0 ? 0 : dy), 2);
}

// Called from CB1_Overworld before the game reads input. The follower's buttons
// do nothing but open the Start menu (and A or R, the co-op menu).
void Coop_FilterInput(u16 *newKeys, u16 *heldKeys)
{
    if (!Coop_IsFollowing())
        return;

    // While joining the partner's battle, only closing messages is left to do.
    if (sCoopBattlePending)
    {
        *newKeys &= (ScriptContext_IsEnabled() && !sMirroring) ? (A_BUTTON | B_BUTTON) : 0;
        *heldKeys = 0;
        return;
    }
    if ((*newKeys & (A_BUTTON | R_BUTTON)) && !sMirroring && !ArePlayerFieldControlsLocked() && !ScriptContext_IsEnabled())
        sFollowerMenuRequested = TRUE;
    *newKeys &= sMirroring ? 0 : START_BUTTON;
    *heldKeys = 0;
}

// ---------------------------------------------------------------- dialogue mirroring

// Field message boxes call these (field_message_box.c).
void Coop_OnFieldMessage(const u8 *text)
{
    u16 i;

    if (sShowingMirror)
        return;
    if (sCoopScriptRunning)
    {
        // The co-op menus and notices are this player's own business.
        sMsgShown = FALSE;
        return;
    }
    for (i = 0; i < COOP_TEXT_SIZE - 1 && text[i] != EOS; i++)
        gCoopMailbox.localText[i] = text[i];
    gCoopMailbox.localText[i] = EOS;
    gCoopMailbox.local.msgColor = ContextNpcGetTextColor();
    gCoopMailbox.local.msgSignpost = IsMsgSignpost();
    gCoopMailbox.local.msgSeq++;
    sMsgPageBase = gCoopTextPages;
    sMsgShown = TRUE;
}

void Coop_OnFieldMessageHidden(void)
{
    if (!sShowingMirror)
        sMsgShown = FALSE;
}

bool8 Coop_IsMirroringText(void)
{
    return sMirroring && !gMain.inBattle;
}

// The follower's text printer asks this instead of checking A/B.
bool8 Coop_ShouldAdvanceText(void)
{
    return (s8)(gCoopMailbox.partner.msgPage - (u8)(gCoopTextPages - sMirrorPageBase)) > 0;
}

static void ShowMirror(void)
{
    u16 color = gSpecialVar_TextColor;

    gCoopMailbox.partnerText[COOP_TEXT_SIZE - 1] = EOS;
    sMirrorSeq = gCoopMailbox.partner.msgSeq;
    sMirrorPageBase = gCoopTextPages;
    sShowingMirror = TRUE;
    gSpecialVar_TextColor = gCoopMailbox.partner.msgColor;
    if (gCoopMailbox.partner.msgSignpost)
        MsgSetSignpost();
    else
        MsgSetNotSignpost();
    ForceShowFieldMessage(gCoopMailbox.partnerText); // copied into gStringVar4
    gSpecialVar_TextColor = color;
    sShowingMirror = FALSE;
    sMirroring = TRUE;
}

static void UpdateMirroredText(void)
{
    struct CoopPlayerState *p = &gCoopMailbox.partner;
    bool8 leaderTalking = Coop_IsFollowing() && p->mode == COOP_MODE_LEAD && p->msgActive;

    if (sMirroring)
    {
        if (!leaderTalking)
        {
            sShowingMirror = TRUE;
            StopMirroring();
            sShowingMirror = FALSE;
        }
        else if (p->msgSeq != sMirrorSeq)
        {
            ShowMirror();
        }
    }
    else if (leaderTalking && Coop_IsInOverworld() && !ScriptContext_IsEnabled() && !ArePlayerFieldControlsLocked())
    {
        ShowMirror();
    }
}

static void TryStartPartnersWildBattle(void);
static void FollowHostRules(void);
static void CheckPartnerGift(void);
static bool8 IsSafariZoneMap(void);
bool8 BattleSetup_IsStarting(void);

// ---------------------------------------------------------------- main update

// Called every overworld frame.
void Coop_Update(void)
{
    gCoopMailbox.magic[0] = COOP_MAGIC_0;
    gCoopMailbox.magic[1] = COOP_MAGIC_1;
    // A co-op script is over once it has released the player (it may wait on a
    // yes/no box in between, when the script context isn't running).
    if (sCoopScriptRunning && !ScriptContext_IsEnabled() && !ArePlayerFieldControlsLocked())
        sCoopScriptRunning = FALSE;
    // A trainer battle is over once the player is back and free to move.
    if (gCoopMailbox.local.battleTrainer && Coop_IsInOverworld() && !ScriptContext_IsEnabled() && !ArePlayerFieldControlsLocked()
     && !BattleSetup_IsStarting())
        gCoopMailbox.local.battleTrainer = 0;
    TrackSteps();
    WriteLocalState();
    gCoopMailbox.flagsPtr = (u32)gSaveBlock1Ptr->flags;
    gCoopMailbox.varsPtr = (u32)gSaveBlock1Ptr->vars;
    if (gCoopMailbox.worldChanged)
    {
        gCoopMailbox.worldChanged = FALSE;
        Coop_RemoveObjectsHiddenByFlags();
    }

    if (!gCoopMailbox.active)
    {
        if (sWasActive && gCoopMailbox.local.mode != COOP_MODE_SPLIT)
            Split();
        sWasActive = FALSE;
        RemovePartner();
        return;
    }
    if (!sWasActive)
    {
        // Fresh connection: start split and ignore commands sent before it.
        sWasActive = TRUE;
        sLastPartnerCmdSeq = gCoopMailbox.partner.cmdSeq;
        sGiftSeqKnown = FALSE;
        sConnectedFrames = 0;
        Split();
    }

    HandlePartnerCommand();
    FollowHostRules();
    CheckPartnerGift();
    // Picking starters in Oak's lab: each player picks their own, so a linked pair
    // splits up there as long as this player has no Pokemon yet.
    if (Coop_IsLinked() && CalculatePlayerPartyCount() == 0
     && gSaveBlock1Ptr->location.mapGroup == MAP_GROUP(PALLET_TOWN_PROFESSOR_OAKS_LAB)
     && gSaveBlock1Ptr->location.mapNum == MAP_NUM(PALLET_TOWN_PROFESSOR_OAKS_LAB))
    {
        SendCommand(COOP_CMD_SPLIT);
        Split();
        ShowNotice(Coop_Text_SplitForStarters);
    }
    // The Safari Zone: each player goes in (and pays) on their own.
    if (Coop_IsLinked() && IsSafariZoneMap())
    {
        SendCommand(COOP_CMD_SPLIT);
        Split();
        ShowNotice(Coop_Text_SplitForSafari);
    }
    TryStartPartnersWildBattle();
    if (Coop_IsFollowing())
        UpdateFollowing();
    UpdateMirroredText();
    UpdatePartnerSprite();
    RunPendingScripts();
}

// ---------------------------------------------------------------- specials

void Coop_BufferPartnerName(void)
{
    StringCopyN(gStringVar1, gCoopMailbox.partner.name, PLAYER_NAME_LENGTH);
    gStringVar1[PLAYER_NAME_LENGTH] = EOS;
}

u16 Coop_GetLinkMode(void)
{
    return gCoopMailbox.local.mode;
}

void Coop_SendLinkRequest(void)
{
    sAwaitingLinkAnswer = TRUE;
    SendCommand(COOP_CMD_LINK_ASK);
}

// VAR_0x8004 = the yes/no answer
void Coop_AnswerLinkRequest(void)
{
    if (gSpecialVar_0x8004 == 1)
    {
        StartFollowing();
        SendCommand(COOP_CMD_LINK_ACCEPT);
    }
    else
    {
        SendCommand(COOP_CMD_LINK_DECLINE);
    }
}

void Coop_AnswerControlRequest(void)
{
    if (gSpecialVar_0x8004 == 1)
        Coop_GiveControl();
    else
        SendCommand(COOP_CMD_CONTROL_DECLINE);
}

void Coop_GiveControl(void)
{
    SendCommand(COOP_CMD_HANDOVER);
    StartFollowing();
}

void Coop_AskForControl(void)
{
    sAwaitingControlAnswer = TRUE;
    SendCommand(COOP_CMD_CONTROL_ASK);
}

void Coop_SplitOff(void)
{
    SendCommand(COOP_CMD_SPLIT);
    Split();
}

// ---------------------------------------------------------------- Pokemon Center: healing

// special (nurse): a linked leader's partner gets healed too
void Coop_ShareHeal(void)
{
    if (Coop_IsLinked() && !Coop_IsFollowing())
        SendCommand(COOP_CMD_HEAL);
}

// ---------------------------------------------------------------- trading
//
// Upstairs in a Pokemon Center, a player talking to the (split) partner can ask to
// trade. Both pick a Pokemon, see what they'd get, confirm, and the game's own trade
// scene plays (as for in-game trades) with the partner's Pokemon.

// specialvar: in a Pokemon Center's upstairs (where trades are offered)
bool8 Coop_IsTradeRoom(void)
{
    return gMapHeader.mapLayoutId == LAYOUT_POKEMON_CENTER_2F;
}

void Coop_SendTradeRequest(void)
{
    sTradeAnswer = 0;
    SendCommand(COOP_CMD_TRADE_ASK);
}

// special: VAR_0x8004 = yes/no
void Coop_AnswerTradeRequest(void)
{
    SendCommand(gSpecialVar_0x8004 == 1 ? COOP_CMD_TRADE_ACCEPT : COOP_CMD_TRADE_DECLINE);
}

// The asker waits for the answer (B gives up). VAR_RESULT = TRUE if accepted.
static void Task_WaitTradeAnswer(u8 taskId)
{
    if (sTradeAnswer == 0 && gCoopMailbox.active && !JOY_NEW(B_BUTTON))
        return;
    if (sTradeAnswer == 0)
        SendCommand(COOP_CMD_TRADE_DECLINE);
    gSpecialVar_Result = (sTradeAnswer == 1);
    DestroyTask(taskId);
    ScriptContext_Enable();
}

void Coop_WaitTradeAnswer(void)
{
    CreateTask(Task_WaitTradeAnswer, 80);
}

struct CoopTradeBlock
{
    u8 cancel;
    u8 answer;
    u8 padding[2];
    struct Pokemon mon;
};

// Both games send their choice and wait for the other's (B gives up).
// tStep 0: the chosen Pokemon (VAR_0x8005, or none); then the yes/no (VAR_0x8006).
#define tStep  data[0]
#define tState data[1]
static void Task_TradeExchange(u8 taskId)
{
    s16 *data = gTasks[taskId].data;
    u8 partner = gCoopMailbox.playerIndex ^ 1;
    struct CoopTradeBlock *block = (struct CoopTradeBlock *)gBlockSendBuffer;
    struct CoopTradeBlock out;

    if (!gCoopMailbox.active || (tState == 1 && JOY_NEW(B_BUTTON)))
    {
        // the partner left (or this player gave up waiting)
        Coop_CloseLink();
        gSpecialVar_Result = FALSE;
        DestroyTask(taskId);
        ScriptContext_Enable();
        return;
    }
    switch (tState)
    {
    case 0:
        if (!IsLinkTaskFinished())
            break;
        memset(&out, 0, sizeof(out));
        if (tStep == 0)
        {
            out.cancel = (gSpecialVar_0x8005 >= PARTY_SIZE);
            if (!out.cancel)
                out.mon = gPlayerParty[gSpecialVar_0x8005];
        }
        else
        {
            out.answer = (gSpecialVar_0x8006 == 1);
        }
        if (SendBlock(0, &out, sizeof(out)))
            tState++;
        break;
    case 1:
        if (!(GetBlockReceivedStatus() & (1 << partner)))
            break;
        block = (struct CoopTradeBlock *)gBlockRecvBuffer[partner];
        if (tStep == 0)
        {
            gSpecialVar_Result = !block->cancel && gSpecialVar_0x8005 < PARTY_SIZE;
            if (gSpecialVar_Result)
            {
                u8 noMail = MAIL_NONE;

                gEnemyParty[0] = block->mon;
                SetMonData(&gEnemyParty[0], MON_DATA_MAIL, &noMail); // (their mail stays in their game)
                Coop_BufferPartnerName();
                GetMonData(&gPlayerParty[gSpecialVar_0x8005], MON_DATA_NICKNAME, gStringVar2);
                StringGet_Nickname(gStringVar2);
                GetMonData(&gEnemyParty[0], MON_DATA_NICKNAME, gStringVar3);
                StringGet_Nickname(gStringVar3);
            }
        }
        else
        {
            gSpecialVar_Result = block->answer && gSpecialVar_0x8006 == 1;
        }
        ResetBlockReceivedFlags();
        if (tStep != 0 || !gSpecialVar_Result)
            Coop_CloseLink();
        DestroyTask(taskId);
        ScriptContext_Enable();
        break;
    }
}
#undef tStep
#undef tState

// special: send this player's choice (VAR_0x8005) and get the partner's.
// VAR_RESULT = TRUE if both picked one: STR_VAR_1 partner, STR_VAR_2 / STR_VAR_3 the Pokemon.
void Coop_TradeExchangeMons(void)
{
    u8 taskId = CreateTask(Task_TradeExchange, 80);

    gTasks[taskId].data[0] = 0;
    Coop_OpenLink();
}

// special: send this player's yes/no (VAR_0x8006) and get the partner's.
// VAR_RESULT = TRUE if both said yes.
void Coop_TradeConfirm(void)
{
    u8 taskId = CreateTask(Task_TradeExchange, 80);

    gTasks[taskId].data[0] = 1;
}

// special: the trade scene is starting
void Coop_BeginTrade(void)
{
    sTrading = TRUE;
}

bool8 Coop_IsTrading(void)
{
    return sTrading;
}

void Coop_EndTrade(void)
{
    sTrading = FALSE;
}

// ---------------------------------------------------------------- one-player battles

// Safari Zone, ghosts, the old man's demonstration, legendary and other set-piece
// Pokemon: one player only. A linked pair splits up for it.
void Coop_ForceSoloBattle(void)
{
    if (Coop_IsLinked())
    {
        Coop_SplitOff();
        ShowNotice(Coop_Text_SoloBattle);
    }
}

static bool8 IsSafariZoneMap(void)
{
    u16 map = (gSaveBlock1Ptr->location.mapGroup << 8) | gSaveBlock1Ptr->location.mapNum;

    if (map == MAP_FUCHSIA_CITY_SAFARI_ZONE_ENTRANCE)
        return TRUE;
    return gSaveBlock1Ptr->location.mapGroup == MAP_GROUP(SAFARI_ZONE_CENTER)
        && gSaveBlock1Ptr->location.mapNum >= MAP_NUM(SAFARI_ZONE_CENTER)
        && gSaveBlock1Ptr->location.mapNum <= MAP_NUM(SAFARI_ZONE_NORTH_EAST);
}

// ---------------------------------------------------------------- shared one-time rewards
//
// Items and Pokemon given by scripts (NPC gifts, prizes for story events, key items,
// TMs) go to both players: when one gets something, the same goes straight into the
// other's bag (or PC). They're passed through the shared variables: each player has
// ten (VAR_0x40D0 + 10 * index): a counter, then three gifts (the last three sent, so
// a few given at once all arrive), each the kind, the item or species, and the amount
// or level.

#define GIFT_VARS(idx) (VAR_0x40D0 + 10 * (idx))
#define GIFT_SLOTS 3
#define GIFT_SLOT(base, seq) ((base) + 1 + ((seq) % GIFT_SLOTS) * 3)
enum { GIFT_ITEM = 1, GIFT_MON };

static bool8 IsGiftSharedHere(void)
{
    u16 map = (gSaveBlock1Ptr->location.mapGroup << 8) | gSaveBlock1Ptr->location.mapNum;

    // things bought there can be had again: each player buys their own
    return map != MAP_CELADON_CITY_GAME_CORNER_PRIZE_ROOM && map != MAP_CELADON_CITY_DEPARTMENT_STORE_ROOF;
}

static void SendGift(u8 kind, u16 id, u16 amount)
{
    u16 base = GIFT_VARS(gCoopMailbox.playerIndex);
    u16 seq = VarGet(base) + 1;
    u16 slot = GIFT_SLOT(base, seq);

    VarSet(slot, kind);
    VarSet(slot + 1, id);
    VarSet(slot + 2, amount);
    VarSet(base, seq);
}

void Coop_ShareItemGift(u16 item, u16 count)
{
    if (sNoShare)
    {
        sNoShare = FALSE;
        return;
    }
    if (count != 0 && gCoopMailbox.active && IsGiftSharedHere())
        SendGift(GIFT_ITEM, item, count);
}

void Coop_ShareMonGift(u16 species, u8 level, u16 item)
{
    u16 map = (gSaveBlock1Ptr->location.mapGroup << 8) | gSaveBlock1Ptr->location.mapNum;

    if (sNoShare)
    {
        sNoShare = FALSE;
        return;
    }
    // (starters: each player picks their own)
    if (gCoopMailbox.active && IsGiftSharedHere() && map != MAP_PALLET_TOWN_PROFESSOR_OAKS_LAB)
        SendGift(GIFT_MON, species, level);
}

// special: the next item added isn't shared (an item ball or hidden item)
void Coop_DontShareNextItem(void)
{
    sNoShare = TRUE;
}

// The partner got a gift: this player gets the same, once free.
static void CheckPartnerGift(void)
{
    u16 base = GIFT_VARS(gCoopMailbox.playerIndex ^ 1);
    u16 seq = VarGet(base);

    // Right after connecting the shared variables are still being brought in line
    // (the guest takes the host's): only changes after that are gifts.
    if (!sGiftSeqKnown || sConnectedFrames < 600)
    {
        if (sConnectedFrames < 600)
            sConnectedFrames++;
        sGiftSeqKnown = TRUE;
        sLastPartnerGiftSeq = seq;
        return;
    }
    // (more than three behind: the oldest are gone)
    if ((u16)(seq - sLastPartnerGiftSeq) > GIFT_SLOTS)
        sLastPartnerGiftSeq = seq - GIFT_SLOTS;
    while (sLastPartnerGiftSeq != seq)
    {
        u16 slot = GIFT_SLOT(base, ++sLastPartnerGiftSeq);
        u16 id = VarGet(slot + 1);
        u16 amount = VarGet(slot + 2);

        // It goes straight into the bag (or the PC), without a word.
        if (VarGet(slot) == GIFT_ITEM)
        {
            if (!AddBagItem(id, amount))
                AddPCItem(id, amount);
        }
        else if (VarGet(slot) == GIFT_MON)
        {
            ScriptGiveMon(id, amount, ITEM_NONE, 0, 0, 0);
        }
    }
}

// special: VAR_TEMP_1 = the caught Pokemon's species, VAR_RESULT = 0 in the team, 1 in the PC
void Coop_PrepareCaughtNickname(void)
{
    VarSet(VAR_TEMP_1, sNickSpecies);
    gSpecialVar_Result = (sNickWhere == MON_GIVEN_TO_PARTY) ? 0 : 1;
}

// ---------------------------------------------------------------- story battles: getting ready

// The player who ran into a story battle asks the partner to get ready.
void Coop_AskPartnerToGetReady(void)
{
    sStoryPartnerReady = FALSE;
    SendCommand(COOP_CMD_STORY_ASK);
}

bool8 Coop_TakeStoryPartnerReady(void)
{
    bool8 ready = sStoryPartnerReady;

    sStoryPartnerReady = FALSE;
    return ready;
}

// special: the pulled player is ready to be brought over
void Coop_StorySendReady(void)
{
    sStoryReadySent = TRUE;
    SendCommand(COOP_CMD_STORY_READY);
}

// special: VAR_RESULT = whether healing is allowed (not with the No Pokemon Center rule)
void Coop_CanHealForStory(void)
{
    gSpecialVar_Result = !gSaveBlock1Ptr->keyFlags.noPMC;
}

// ---------------------------------------------------------------- whiting out after a co-op battle

// This player's Pokemon all fainted but the partner kept going and the battle ended
// without a loss: they white out once they're back on the map.
void Coop_SetWhiteOutPending(void)
{
    sWhiteOutPending = TRUE;
}

// special: whiting out on your own leaves the partner behind
void Coop_DoWhiteOut(void)
{
    if (gCoopMailbox.active && gCoopMailbox.local.mode != COOP_MODE_SPLIT)
        Coop_SplitOff();
    SetMainCallback2(CB2_WhiteOut);
}

// ---------------------------------------------------------------- the guest plays by the host's rules

// The host's Key System settings (difficulty, Nuzlocke, exp, level caps and so on)
// are the guest's too while connected.
static void FollowHostRules(void)
{
    struct KeySystemFlags host;
    struct KeySystemFlags *own = &gSaveBlock1Ptr->keyFlags;
    bool8 recalc;
    u8 i;

    if (!Coop_IsGuest() || gCoopMailbox.partner.seq == 0)
        return;
    *(u16 *)&host = gCoopMailbox.partner.keySettings;
    recalc = (own->ivCalcMode != host.ivCalcMode || own->evCalcMode != host.evCalcMode);
    own->difficulty = host.difficulty;
    own->version = host.version;
    own->nuzlocke = host.nuzlocke;
    own->ivCalcMode = host.ivCalcMode;
    own->evCalcMode = host.evCalcMode;
    own->noPMC = host.noPMC;
    own->expMod = host.expMod;
    own->levelCap = host.levelCap;
    own->teamExp = host.teamExp;
    if (recalc)
    {
        for (i = 0; i < gPlayerPartyCount; i++)
            CalculateMonStats(&gPlayerParty[i], FALSE);
    }
}

bool8 Coop_IsGuest(void)
{
    return gCoopMailbox.active && gCoopMailbox.playerIndex == 1;
}

// Every successful save goes through here (save.c).
void Coop_OnGameSaved(void)
{
    if (gCoopMailbox.active && gCoopMailbox.playerIndex == 0)
        SendCommand(COOP_CMD_SAVED);
}

// The guest's half of a save: its own team, bag and the shared world as it is now.
// Runs in a task while the script waits, the way the game's own saves do.
static void Task_SaveWithHost(u8 taskId)
{
    SaveMapView();
    TrySavingData(SAVE_NORMAL);
    DestroyTask(taskId);
    ScriptContext_Enable();
}

void Coop_SaveWithHost(void)
{
    CreateTask(Task_SaveWithHost, 80);
}

// ---------------------------------------------------------------- trainers

// A trainer battle starts (the player talked to a trainer or was spotted). Until it
// is over and the player is free again, the partner's game treats this trainer as
// beaten, so the two of them can't end up fighting the same trainer.
void Coop_OnTrainerBattle(u16 trainerId)
{
    gCoopMailbox.local.battleTrainer = trainerId + 1;
}

bool8 Coop_IsTrainerTakenByPartner(u16 trainerId)
{
    return gCoopMailbox.active && gCoopMailbox.partner.battleTrainer == trainerId + 1;
}

// ---------------------------------------------------------------- co-op link

// While a co-op battle runs, the game's link layer (SendBlock, received blocks,
// player count, master) is carried by the app over the network instead of the
// link cable. The battle code then works exactly as in a link battle.
void Coop_OpenLink(void)
{
    u8 i;

    gCoopMailbox.blockSendBuf = (u32)gBlockSendBuffer;
    gCoopMailbox.blockRecvBuf = (u32)gBlockRecvBuffer;
    gCoopMailbox.blockRecvStatus = (u32)gBlockReceivedStatus;
    gCoopMailbox.blockSendPending = FALSE;
    ResetBlockReceivedFlags();
    gLinkStatus = (2 << LINK_STAT_PLAYER_COUNT_SHIFT) | (gCoopMailbox.playerIndex == 0 ? LINK_STAT_MASTER : 0);
    for (i = 0; i < 2; i++)
    {
        struct LinkPlayer *player = &gLinkPlayers[i];
        bool8 self = (i == gCoopMailbox.playerIndex);

        memset(player, 0, sizeof(*player));
        player->version = VERSION_FIRE_RED;
        player->linkType = LINKTYPE_BATTLE;
        player->id = i == 0 ? 0 : 2; // battler: host on the left, guest on the right
        player->language = GAME_LANGUAGE;
        player->gender = self ? gSaveBlock2Ptr->playerGender : gCoopMailbox.partner.gender;
        StringCopyN(player->name, self ? gSaveBlock2Ptr->playerName : gCoopMailbox.partner.name, PLAYER_NAME_LENGTH);
        player->name[PLAYER_NAME_LENGTH] = EOS;
    }
    gReceivedRemoteLinkPlayers = TRUE;
    gCoopMailbox.linkActive = TRUE;
}

void Coop_CloseLink(void)
{
    gCoopMailbox.linkActive = FALSE;
    gReceivedRemoteLinkPlayers = FALSE;
    gLinkStatus = 0;
    ResetBlockReceivedFlags();
}

bool8 Coop_IsLinkOpen(void)
{
    return gCoopMailbox.linkActive;
}

bool8 Coop_SendBlock(const void *src, u16 size)
{
    u8 self = gCoopMailbox.playerIndex;

    if (gCoopMailbox.blockSendPending || size > BLOCK_BUFFER_SIZE)
        return FALSE;
    memcpy(gBlockSendBuffer, src, size);
    gCoopMailbox.blockSendSize = size;
    gCoopMailbox.blockSendSeq++;
    gCoopMailbox.blockSendPending = TRUE;
    // A link block also arrives at its sender.
    memcpy(gBlockRecvBuffer[self], src, size);
    gBlockReceivedStatus[self] = TRUE;
    return TRUE;
}

bool8 Coop_IsLinkTaskFinished(void)
{
    return !gCoopMailbox.blockSendPending;
}

// ---------------------------------------------------------------- co-op battles

// During a co-op battle the battle party holds both players' first three Pokemon
// (host's in slots 0-2, guest's in 3-5). The player's own party is kept aside and
// gets its Pokemon back from the battle party afterwards.
static struct Pokemon *sOwnParty;       // the real party, PARTY_SIZE
static struct Pokemon *sBattleMons;     // [player][4]: three party Pokemon and a wild one

bool8 Coop_IsHost(void)
{
    return gCoopMailbox.active && gCoopMailbox.playerIndex == 0;
}

void Coop_BackUpParty(void)
{
    sOwnParty = AllocZeroed(sizeof(struct Pokemon) * PARTY_SIZE);
    sBattleMons = AllocZeroed(sizeof(struct Pokemon) * 8);
    sCaughtMon = AllocZeroed(sizeof(struct Pokemon));
    sCaughtBy = 0xFF;
    memcpy(sOwnParty, gPlayerParty, sizeof(struct Pokemon) * PARTY_SIZE);
    // This player's own wild Pokemon, rolled from their own encounter table.
    memcpy(&sBattleMons[gCoopMailbox.playerIndex * 4 + 3], &gEnemyParty[0], sizeof(struct Pokemon));
}

// What this game sends in each of the two start blocks.
void *Coop_GetBattleSendMons(u8 part)
{
    static struct Pokemon *const unused = NULL;
    struct Pokemon *mine = &sBattleMons[gCoopMailbox.playerIndex * 4];

    (void)unused;
    memcpy(&mine[0], &sOwnParty[0], sizeof(struct Pokemon) * 3);
    return &mine[part * 2];
}

void Coop_ReceiveBattleMons(u8 part, struct Pokemon *fromHost, struct Pokemon *fromGuest)
{
    memcpy(&sBattleMons[0 * 4 + part * 2], fromHost, sizeof(struct Pokemon) * 2);
    memcpy(&sBattleMons[1 * 4 + part * 2], fromGuest, sizeof(struct Pokemon) * 2);
}

void Coop_BuildBattleParties(void)
{
    memcpy(&gPlayerParty[0], &sBattleMons[0], sizeof(struct Pokemon) * 3);
    memcpy(&gPlayerParty[3], &sBattleMons[4], sizeof(struct Pokemon) * 3);
    ZeroEnemyPartyMons();
    memcpy(&gEnemyParty[0], &sBattleMons[3], sizeof(struct Pokemon));
    memcpy(&gEnemyParty[1], &sBattleMons[7], sizeof(struct Pokemon));
    CalculatePlayerPartyCount();
    CalculateEnemyPartyCount();
}


// A Pokemon with four moves that leveled up past moves it doesn't know.
static bool8 MissedLevelUpMoves(struct Pokemon *mon, u8 oldLevel)
{
    const u16 *learnset;
    u16 species = GetMonData(mon, MON_DATA_SPECIES);
    u8 newLevel = GetMonData(mon, MON_DATA_LEVEL);
    u8 i, j;

    if (species == SPECIES_NONE || newLevel <= oldLevel || GetMonData(mon, MON_DATA_MOVE4) == MOVE_NONE)
        return FALSE;
    learnset = gLevelUpLearnsets[species];
    for (i = 0; learnset[i] != LEVEL_UP_END; i++)
    {
        u8 level = (learnset[i] & LEVEL_UP_MOVE_LV) >> 9;
        u16 move = learnset[i] & LEVEL_UP_MOVE_ID;

        if (level <= oldLevel || level > newLevel)
            continue;
        for (j = 0; j < MAX_MON_MOVES; j++)
        {
            if (GetMonData(mon, MON_DATA_MOVE1 + j) == move)
                break;
        }
        if (j == MAX_MON_MOVES)
            return TRUE;
    }
    return FALSE;
}

// After the battle: this player's three Pokemon come back from the battle party.
// Which of this player's own Pokemon (party slots 0-2) went up a level in the last
// co-op battle; they may evolve now.
u8 Coop_TakeLeveledUpMons(void)
{
    u8 bits = sLeveledUp;

    sLeveledUp = 0;
    return bits;
}

void Coop_RestoreParty(void)
{
    u8 k;

    if (sOwnParty == NULL)
        return;
    sMovesPending = 0;
    sLeveledUp = 0;
    for (k = 0; k < MULTI_PARTY_SIZE; k++)
    {
        if (GetMonData(&gPlayerParty[gCoopMailbox.playerIndex * 3 + k], MON_DATA_LEVEL) > GetMonData(&sOwnParty[k], MON_DATA_LEVEL))
            sLeveledUp |= 1 << k;
    }
    if (gCoopMailbox.playerIndex != 0)
    {
        for (k = 0; k < MULTI_PARTY_SIZE; k++)
        {
            if (MissedLevelUpMoves(&gPlayerParty[gCoopMailbox.playerIndex * 3 + k], GetMonData(&sOwnParty[k], MON_DATA_LEVEL)))
                sMovesPending |= 1 << k;
        }
    }
    memcpy(&sOwnParty[0], &gPlayerParty[gCoopMailbox.playerIndex * 3], sizeof(struct Pokemon) * 3);
    memcpy(gPlayerParty, sOwnParty, sizeof(struct Pokemon) * PARTY_SIZE);
    CalculatePlayerPartyCount();
    Free(sOwnParty);
    Free(sBattleMons);
    sOwnParty = NULL;
    sBattleMons = NULL;
    // (the caught Pokemon, if any, is kept until Coop_GiveCaughtMon)
}

// This player caught the wild Pokemon in the last co-op battle.
bool8 Coop_CaughtByMe(void)
{
    return sCaughtMon != NULL && sCaughtBy == gCoopMailbox.playerIndex
        && GetMonData(sCaughtMon, MON_DATA_SPECIES) != SPECIES_NONE;
}

// The wild Pokemon this player met in the last co-op wild battle (rolled from their own
// encounter table); valid until Coop_RestoreParty.
struct Pokemon *Coop_GetOwnWildMon(void)
{
    return sBattleMons != NULL ? &sBattleMons[gCoopMailbox.playerIndex * 4 + 3] : NULL;
}

// The caught Pokemon joins this player's team (or goes to their PC).
void Coop_GiveCaughtMon(void)
{
    if (Coop_CaughtByMe())
    {
        u16 dexNum = SpeciesToNationalPokedexNum(GetMonData(sCaughtMon, MON_DATA_SPECIES));

        u8 where = GiveMonToPlayer(sCaughtMon);

        GetSetPokedexFlag(dexNum, FLAG_SET_SEEN);
        GetSetPokedexFlag(dexNum, FLAG_SET_CAUGHT);
        // Offer a nickname back on the map (Nuzlocke catches were already named).
        if (where != MON_CANT_GIVE && gSaveBlock1Ptr->keyFlags.nuzlocke != 1)
        {
            sNickSpecies = GetMonData(sCaughtMon, MON_DATA_SPECIES);
            sNickWhere = where;
            sPendingScript = Coop_EventScript_NicknameCaught;
        }
    }
    sCaughtBy = 0xFF;
    if (sCaughtMon != NULL)
        Free(sCaughtMon);
    sCaughtMon = NULL;
}

// ---------------------------------------------------------------- bag in co-op battles
//
// Each player uses their own bag. Medicine and the like work on that player's game
// right away (as normal); the host's game, which runs the battle, then uses the same
// item on its copy of the Pokemon, and when the item's turn comes both games' copies
// are brought in line (BattleScript_CoopUseItem).

void Coop_ResetItemUse(void)
{
    sItemParty = 0xFF;
    sItemMove = 0;
}

void Coop_NoteItemUsed(u8 partyIndex, u8 moveIndex)
{
    if (sApplyingItem)
        return;
    sItemParty = partyIndex;
    sItemMove = moveIndex;
}

u8 Coop_GetItemParty(void)
{
    return sItemParty;
}

u8 Coop_GetItemMove(void)
{
    return sItemMove;
}

// Host: a player's item choice arrived (battler: who used it).
void Coop_OnItemChosen(u8 battler, u16 item, u8 partyId, u8 moveIndex)
{
    sItemMonByBattler[battler] = partyId;
    // The host's own items were already used in its own bag; the partner's still need
    // using on the host's copy.
    if (partyId < PARTY_SIZE && gBattlerPositions[battler] == B_POSITION_PLAYER_RIGHT)
    {
        u8 savedActive = gActiveBattler;
        u8 savedInMenu = gBattlerInMenuId;

        gBattlerInMenuId = battler;
        sApplyingItem = TRUE;
        ExecuteTableBasedItemEffect(&gPlayerParty[partyId], item, partyId, moveIndex);
        sApplyingItem = FALSE;
        gBattlerInMenuId = savedInMenu;
        gActiveBattler = savedActive;
    }
}

u8 Coop_GetItemMonForBattler(u8 battler)
{
    return sItemMonByBattler[battler];
}

// ---------------------------------------------------------------- catching in co-op battles

// Host: a ball thrown by this battler worked.
void Coop_OnMonCaught(u8 battler, struct Pokemon *mon)
{
    if (sCaughtMon == NULL)
        return;
    sCaughtBy = (gBattlerPositions[battler] == B_POSITION_PLAYER_LEFT) ? 0 : 1;
    memcpy(sCaughtMon, mon, sizeof(struct Pokemon));
}

u8 Coop_GetCaughtBy(void)
{
    return sCaughtBy;
}

void Coop_SetCaughtBy(u8 playerIndex)
{
    sCaughtBy = playerIndex;
}

struct Pokemon *Coop_GetCaughtMon(void)
{
    return sCaughtMon;
}

// The leader ran into a wild Pokemon while linked: both games go into a co-op battle.
void Coop_StartLinkedWildBattle(void)
{
    SendCommand(COOP_CMD_WILD_BATTLE);
    BattleSetup_StartCoopWildBattle();
}

void Coop_StartLinkedTrainerBattle(bool8 noWhiteOut, bool8 pullPartner)
{
    if (pullPartner)
    {
        // A story battle while split: the partner comes over and the two link up.
        gCoopMailbox.local.mode = COOP_MODE_LEAD;
        SendCommand(noWhiteOut ? COOP_CMD_STORY_PULL_NO_WHITEOUT : COOP_CMD_STORY_PULL);
    }
    else
    {
        SendCommand(noWhiteOut ? COOP_CMD_TRAINER_BATTLE_NO_WHITEOUT : COOP_CMD_TRAINER_BATTLE);
    }
}

// Join the battle the partner started, once this game is free (and has arrived, when
// it was pulled over for a story battle).
static void TryStartPartnersWildBattle(void)
{
    u8 kind = sCoopBattlePending;

    if (kind == 0 || !Coop_IsInOverworld() || sPendingNotice != NULL || sPendingScript != NULL)
        return;
    if (sStoryPullWarp)
    {
        if (ScriptContext_IsEnabled() || ArePlayerFieldControlsLocked())
            return;
        sStoryPullWarp = FALSE;
        WarpToLeader();
        return;
    }
    if (ScriptContext_IsEnabled() || sWarping || (ArePlayerFieldControlsLocked() && !sWarpArrived))
        return;
    sCoopBattlePending = 0;
    sWarpArrived = FALSE;
    // The partner's message before the battle (a trainer's challenge) is over now.
    StopMirroring();
    sShowingMirror = FALSE;
    if (kind == COOP_CMD_WILD_BATTLE)
    {
        if (Coop_GenerateWildMonHere())
            BattleSetup_StartCoopWildBattle();
    }
    else if (gCoopMailbox.partner.battleTrainer != 0)
    {
        gTrainerBattleOpponent_A = gCoopMailbox.partner.battleTrainer - 1;
        BattleSetup_StartCoopTrainerBattle(FALSE, kind == COOP_CMD_TRAINER_BATTLE_NO_WHITEOUT);
    }
}

// Exp in a co-op battle follows each Pokemon's owner's own settings (exp multiplier,
// team exp, level cap). The host's game runs the battle, so while it gives exp to
// one of the guest's Pokemon (battle party slots 3-5) it uses the guest's settings.
u16 Coop_UseExpOwnerSettings(u8 partyId)
{
    u16 saved = *(u16 *)&gSaveBlock1Ptr->keyFlags;

    if (Coop_IsLinkOpen() && partyId >= MULTI_PARTY_SIZE && partyId < PARTY_SIZE)
        *(u16 *)&gSaveBlock1Ptr->keyFlags = gCoopMailbox.partner.keySettings;
    return saved;
}

void Coop_RestoreExpSettings(u16 saved)
{
    *(u16 *)&gSaveBlock1Ptr->keyFlags = saved;
}

// Oak's lab: the rival battle waits until the partner has picked their starter too.
u16 Coop_PartnerNeedsStarter(void)
{
    return gCoopMailbox.active && gCoopMailbox.partner.inOverworld && !gCoopMailbox.partner.hasPokemon;
}
