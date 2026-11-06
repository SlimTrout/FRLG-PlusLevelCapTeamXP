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
#include "constants/moves.h"
#include "constants/species.h"

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
    case COOP_CMD_STORY_PULL:
    case COOP_CMD_STORY_PULL_NO_WHITEOUT:
        // The partner started a story battle: go to them, link up (they lead) and join.
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
    if (sMovesPending)
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
        Split();
    }

    HandlePartnerCommand();
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
void Coop_RestoreParty(void)
{
    u8 k;

    if (sOwnParty == NULL)
        return;
    sMovesPending = 0;
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
