#include "global.h"
#include "coop.h"
#include "event_object_movement.h"
#include "field_player_avatar.h"
#include "fieldmap.h"
#include "constants/event_objects.h"
#include "constants/event_object_movement.h"

// Brainy Blue Co-op, step 1: the partner is shown as a walking character on the map.

#define COOP_PARTNER_LOCALID 0xF0

EWRAM_DATA struct CoopMailbox gCoopMailbox = {0};
static EWRAM_DATA u8 sPartnerObjId = OBJECT_EVENTS_COUNT;
static EWRAM_DATA u8 sPartnerMapGroup = 0;
static EWRAM_DATA u8 sPartnerMapNum = 0;

static void WriteLocalState(void)
{
    struct CoopPlayerState *s = &gCoopMailbox.local;
    struct ObjectEvent *player = &gObjectEvents[gPlayerAvatar.objectEventId];

    s->mapGroup = gSaveBlock1Ptr->location.mapGroup;
    s->mapNum = gSaveBlock1Ptr->location.mapNum;
    s->x = player->currentCoords.x;
    s->y = player->currentCoords.y;
    s->facing = player->facingDirection;
    s->elevation = player->currentElevation;
    s->gender = gSaveBlock2Ptr->playerGender;
    s->running = (gPlayerAvatar.flags & (PLAYER_AVATAR_FLAG_DASH | PLAYER_AVATAR_FLAG_MACH_BIKE | PLAYER_AVATAR_FLAG_ACRO_BIKE)) != 0;
    s->inOverworld = TRUE;
    s->seq++;
}

static struct ObjectEvent *GetPartnerObject(void)
{
    struct ObjectEvent *obj;

    if (sPartnerObjId >= OBJECT_EVENTS_COUNT)
        return NULL;
    obj = &gObjectEvents[sPartnerObjId];
    if (!obj->active || obj->localId != COOP_PARTNER_LOCALID
     || sPartnerMapGroup != gSaveBlock1Ptr->location.mapGroup || sPartnerMapNum != gSaveBlock1Ptr->location.mapNum)
    {
        if (obj->active && obj->localId == COOP_PARTNER_LOCALID)
            RemoveObjectEvent(obj);
        sPartnerObjId = OBJECT_EVENTS_COUNT;
        return NULL;
    }
    return obj;
}

static void RemovePartner(void)
{
    struct ObjectEvent *obj = GetPartnerObject();

    if (obj != NULL)
        RemoveObjectEvent(obj);
    sPartnerObjId = OBJECT_EVENTS_COUNT;
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

static void UpdatePartner(void)
{
    struct CoopPlayerState *p = &gCoopMailbox.partner;
    struct ObjectEvent *obj = GetPartnerObject();
    s16 dx, dy;

    if (p->mapGroup != gSaveBlock1Ptr->location.mapGroup || p->mapNum != gSaveBlock1Ptr->location.mapNum)
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

// Called every overworld frame.
void Coop_Update(void)
{
    gCoopMailbox.magic[0] = COOP_MAGIC_0;
    gCoopMailbox.magic[1] = COOP_MAGIC_1;
    WriteLocalState();
    if (!gCoopMailbox.active)
    {
        RemovePartner();
        return;
    }
    UpdatePartner();
}
