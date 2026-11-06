#include "global.h"
#include "coop.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "event_scripts.h"
#include "field_fadetransition.h"
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

// Brainy Blue Co-op.
//
// Partner on the map: the partner is shown as a walking character, driven by the
// state the app copies into gCoopMailbox.partner every frame.
//
// Linked play: the leader records every step it takes by input (its direction) in
// a small ring. The follower replays those directions as if its own player had
// pressed them, one step behind, so doors, stairs, ledges and map edges all work
// exactly as they do for the leader. If the follower ever loses track (the leader
// flew, fell through a hole, or a script moved them), it warps to the leader.

extern const u8 Coop_EventScript_TalkToPartner[];
extern const u8 Coop_EventScript_LinkRequest[];
extern const u8 Coop_EventScript_ControlRequest[];
extern const u8 Coop_EventScript_Notice[];
extern const u8 Coop_EventScript_FollowerMenu[];

extern const u8 Coop_Text_PartnerLinkedYouLead[];
extern const u8 Coop_Text_PartnerDeclinedLink[];
extern const u8 Coop_Text_YouHaveControl[];
extern const u8 Coop_Text_PartnerKeptControl[];
extern const u8 Coop_Text_PartnerSplitOff[];
extern const u8 Coop_Text_PartnerDisconnected[];

EWRAM_DATA struct CoopMailbox gCoopMailbox = {0};

static EWRAM_DATA u8 sPartnerObjId = 0;
static EWRAM_DATA u8 sPartnerMapGroup = 0;
static EWRAM_DATA u8 sPartnerMapNum = 0;
static EWRAM_DATA bool8 sPartnerObjValid = FALSE;

// link state
static EWRAM_DATA u8 sLastPartnerCmdSeq = 0;
static EWRAM_DATA bool8 sAwaitingLinkAnswer = FALSE;
static EWRAM_DATA bool8 sAwaitingControlAnswer = FALSE;
static EWRAM_DATA const u8 *sPendingScript = NULL;
static EWRAM_DATA const u8 *sPendingNotice = NULL;
static EWRAM_DATA bool8 sWasActive = FALSE;

// step recording (leader side; always recorded)
static EWRAM_DATA s16 sLastX = 0;
static EWRAM_DATA s16 sLastY = 0;
static EWRAM_DATA u8 sLastMapGroup = 0;
static EWRAM_DATA u8 sLastMapNum = 0;
static EWRAM_DATA u32 sLastFrame = 0;

// following
static EWRAM_DATA u8 sConsumed = 0;     // leader steps already replayed
static EWRAM_DATA u8 sInitialDir = 0;   // first step: into the leader's tile
static EWRAM_DATA u8 sInjectDir = 0;    // direction being "pressed" right now
static EWRAM_DATA bool8 sInjectFresh = FALSE;
static EWRAM_DATA s16 sInjectFromX = 0;
static EWRAM_DATA s16 sInjectFromY = 0;
static EWRAM_DATA u8 sInjectFromMapNum = 0;
static EWRAM_DATA u8 sInjectFromMapGroup = 0;
static EWRAM_DATA u16 sLostFrames = 0;
static EWRAM_DATA bool8 sFollowerMenuRequested = FALSE;

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

// Direction from the player to the partner if they stand side by side, else 0.
static u8 GetAdjacentDirToPartner(void)
{
    struct ObjectEvent *player = GetPlayerObject();
    s16 dx = gCoopMailbox.partner.x - player->currentCoords.x;
    s16 dy = gCoopMailbox.partner.y - player->currentCoords.y;

    if (!OnSameMapAsPartner())
        return 0;
    if ((dx == 0 && (dy == 1 || dy == -1)) || (dy == 0 && (dx == 1 || dx == -1)))
        return GetDirectionTo(dx, dy);
    return 0;
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

static void StartFollowing(void)
{
    gCoopMailbox.local.mode = COOP_MODE_FOLLOW;
    sConsumed = gCoopMailbox.partner.stepCount;
    sInitialDir = GetAdjacentDirToPartner();
    sInjectDir = 0;
    sLostFrames = 0;
}

static void StartLeading(void)
{
    gCoopMailbox.local.mode = COOP_MODE_LEAD;
    sInjectDir = 0;
}

static void Split(void)
{
    gCoopMailbox.local.mode = COOP_MODE_SPLIT;
    sInjectDir = 0;
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

// A step counts if the player moved by input on the overworld without a gap in
// frames (a gap means a warp or a screen in between).
static void TrackSteps(void)
{
    struct ObjectEvent *player = GetPlayerObject();
    u32 frame = gMain.vblankCounter2;
    bool8 moved = player->currentCoords.x != sLastX || player->currentCoords.y != sLastY
               || gSaveBlock1Ptr->location.mapNum != sLastMapNum || gSaveBlock1Ptr->location.mapGroup != sLastMapGroup;

    if (moved && frame - sLastFrame <= 2 && !ArePlayerFieldControlsLocked())
        RecordStep(player->movementDirection);
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
    s->running = (gPlayerAvatar.flags & (PLAYER_AVATAR_FLAG_DASH | PLAYER_AVATAR_FLAG_MACH_BIKE | PLAYER_AVATAR_FLAG_ACRO_BIKE)) != 0;
    s->inOverworld = TRUE;
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

    if (!OnSameMapAsPartner())
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

        ObjectEventSetHeldMovement(obj, p->running ? GetWalkFastMovementAction(dir) : GetWalkNormalMovementAction(dir));
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
    }
}

static bool8 CanRunCoopScript(void)
{
    return Coop_IsInOverworld() && !ArePlayerFieldControlsLocked() && !ScriptContext_IsEnabled();
}

static void RunPendingScripts(void)
{
    if (!CanRunCoopScript())
        return;
    if (sPendingScript != NULL)
    {
        ScriptContext_SetupScript(sPendingScript);
        sPendingScript = NULL;
    }
    else if (sPendingNotice != NULL)
    {
        Coop_BufferPartnerName();
        StringExpandPlaceholders(gStringVar4, sPendingNotice);
        sPendingNotice = NULL;
        ScriptContext_SetupScript(Coop_EventScript_Notice);
    }
    else if (sFollowerMenuRequested)
    {
        sFollowerMenuRequested = FALSE;
        ScriptContext_SetupScript(Coop_EventScript_FollowerMenu);
    }
}

// ---------------------------------------------------------------- following

static void WarpToLeader(void)
{
    struct CoopPlayerState *p = &gCoopMailbox.partner;

    SetWarpDestination(p->mapGroup, p->mapNum, -1, p->x - MAP_OFFSET, p->y - MAP_OFFSET);
    DoWarp();
    sConsumed = p->stepCount;
    sInitialDir = 0;
    sInjectDir = 0;
    sLostFrames = 0;
}

static void InjectStep(u8 dir)
{
    struct ObjectEvent *player = GetPlayerObject();

    sInjectDir = dir;
    sInjectFresh = TRUE;
    sInjectFromX = player->currentCoords.x;
    sInjectFromY = player->currentCoords.y;
    sInjectFromMapNum = gSaveBlock1Ptr->location.mapNum;
    sInjectFromMapGroup = gSaveBlock1Ptr->location.mapGroup;
}

static void UpdateFollowing(void)
{
    struct CoopPlayerState *p = &gCoopMailbox.partner;
    struct ObjectEvent *player = GetPlayerObject();
    u8 pending;

    if (!Coop_IsInOverworld())
        return;

    // The injected step has started once the player's position changes.
    if (sInjectDir != 0)
    {
        if (player->currentCoords.x != sInjectFromX || player->currentCoords.y != sInjectFromY
         || gSaveBlock1Ptr->location.mapNum != sInjectFromMapNum || gSaveBlock1Ptr->location.mapGroup != sInjectFromMapGroup)
            sInjectDir = 0;
        else
            return;
    }
    if (ArePlayerFieldControlsLocked())
        return;

    pending = p->stepCount - sConsumed;
    if (pending > COOP_STEP_RING - 2)
    {
        WarpToLeader();
        return;
    }
    if (sInitialDir != 0 && pending >= 1)
    {
        InjectStep(sInitialDir);
        sInitialDir = 0;
        return;
    }
    if (pending >= 2 || (pending >= 1 && !OnSameMapAsPartner()))
    {
        InjectStep(GetPartnerStep(sConsumed));
        sConsumed++;
        sLostFrames = 0;
        return;
    }
    // Leader is somewhere we can't walk to (flew, fell, was moved by a script).
    if (pending == 0 && !OnSameMapAsPartner() && p->inOverworld)
    {
        if (++sLostFrames > 90)
            WarpToLeader();
    }
    else
    {
        sLostFrames = 0;
    }
}

// Called from CB1_Overworld before the game reads input. The follower's own
// movement and A/B are replaced by the replayed steps; Start and Select still work.
void Coop_FilterInput(u16 *newKeys, u16 *heldKeys)
{
    u16 dirKey;

    if (!Coop_IsFollowing())
        return;

    // A opens the follower menu, but not the press that closes a message.
    if ((*newKeys & A_BUTTON) && sInjectDir == 0 && !ArePlayerFieldControlsLocked() && !ScriptContext_IsEnabled())
        sFollowerMenuRequested = TRUE;
    *newKeys &= START_BUTTON | SELECT_BUTTON;
    *heldKeys &= START_BUTTON | SELECT_BUTTON;
    if (sInjectDir == 0)
        return;

    switch (sInjectDir)
    {
    case DIR_SOUTH: dirKey = DPAD_DOWN; break;
    case DIR_NORTH: dirKey = DPAD_UP; break;
    case DIR_WEST:  dirKey = DPAD_LEFT; break;
    default:        dirKey = DPAD_RIGHT; break;
    }
    *heldKeys |= dirKey;
    if (sInjectFresh)
    {
        *newKeys |= dirKey;
        sInjectFresh = FALSE;
    }
    // Run to catch up when the leader is running or a few steps ahead.
    if (FlagGet(FLAG_SYS_B_DASH) && (gCoopMailbox.partner.running || (u8)(gCoopMailbox.partner.stepCount - sConsumed) >= 3))
        *heldKeys |= B_BUTTON;
}

// ---------------------------------------------------------------- main update

// Called every overworld frame.
void Coop_Update(void)
{
    gCoopMailbox.magic[0] = COOP_MAGIC_0;
    gCoopMailbox.magic[1] = COOP_MAGIC_1;
    TrackSteps();
    WriteLocalState();

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
    if (Coop_IsFollowing())
        UpdateFollowing();
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
