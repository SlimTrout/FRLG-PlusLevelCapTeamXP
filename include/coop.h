#ifndef GUARD_COOP_H
#define GUARD_COOP_H

// Brainy Blue Co-op: the ROM talks to the host app through a mailbox in EWRAM.
// The host app finds it by its magic words, sets `active`, and every frame copies
// this game's `local` block to the partner's game, where it lands in `partner`.

#define COOP_MAGIC_0 0x4F434242 // "BBCO"
#define COOP_MAGIC_1 0x3130504F // "OP01"

#define COOP_PARTNER_LOCALID 0xF0
#define COOP_STEP_RING 16

enum
{
    COOP_MODE_SPLIT,
    COOP_MODE_LEAD,
    COOP_MODE_FOLLOW,
};

enum
{
    COOP_CMD_NONE,
    COOP_CMD_LINK_ASK,        // "link up with me; I'll lead"
    COOP_CMD_LINK_ACCEPT,
    COOP_CMD_LINK_DECLINE,
    COOP_CMD_HANDOVER,        // "you lead now"
    COOP_CMD_CONTROL_ASK,     // follower asks the leader for control
    COOP_CMD_CONTROL_DECLINE,
    COOP_CMD_SPLIT,
    COOP_CMD_SAVED,           // the host saved; the guest saves too
};

#define COOP_TEXT_SIZE 400

// 32 bytes. The app copies it byte for byte, so keep the layout in sync with
// coop/app/bbcoop.c and coop/host/coophost.c (STATE_SIZE, MB_PARTNER, MB_*TEXT).
struct CoopPlayerState
{
    /*0x00*/ u16 seq;          // bumped every frame the state is written
    /*0x02*/ u8 mapGroup;
    /*0x03*/ u8 mapNum;
    /*0x04*/ s16 x;            // object coords (map coords + MAP_OFFSET)
    /*0x06*/ s16 y;
    /*0x08*/ u8 facing;        // DIR_*
    /*0x09*/ u8 elevation;
    /*0x0A*/ u8 gender;
    /*0x0B*/ u8 running:1;     // running: partner walks fast
             u8 inOverworld:1;
             u8 biking:1;      // on a bike: partner rides faster
             u8 msgActive:1;   // a field message is on screen (the follower mirrors it)
             u8 padding:4;
    /*0x0C*/ u8 mode;          // COOP_MODE_*
    /*0x0D*/ u8 stepCount;     // steps taken by input so far (wraps)
    /*0x0E*/ u8 steps[COOP_STEP_RING / 4]; // last 16 step directions, 2 bits each (DIR_* - 1)
    /*0x12*/ u8 cmdSeq;        // bumped when a new command is sent
    /*0x13*/ u8 cmd;           // COOP_CMD_*
    /*0x14*/ u8 name[8];       // player name
    /*0x1C*/ u8 msgSeq;        // bumped when a new field message starts (text in localText)
    /*0x1D*/ u8 msgPage;       // pages of that message the player has moved past
    /*0x1E*/ u8 msgColor;      // NPC_TEXT_COLOR_* of that message
    /*0x1F*/ u8 msgSignpost:1; // drawn in a signpost frame
             u8 padding2:7;
};

struct CoopMailbox
{
    /*0x00*/ u32 magic[2];
    /*0x08*/ u8 active;        // set by the app while a partner is connected
    /*0x09*/ u8 playerIndex;   // set by the app: 0 = host, 1 = guest
    /*0x0A*/ u8 padding[2];
    /*0x0C*/ struct CoopPlayerState local;   // written by this game
    /*0x2C*/ struct CoopPlayerState partner; // written by the app
    /*0x4C*/ u8 localText[COOP_TEXT_SIZE];   // this game's current field message
    /*0x1DC*/ u8 partnerText[COOP_TEXT_SIZE]; // the partner's, written by the app
    // Shared world: the app keeps both games' story flags and variables in step.
    /*0x36C*/ u32 flagsPtr;    // this game's flags and vars, 0 until it's in the overworld
    /*0x370*/ u32 varsPtr;
    /*0x374*/ u8 worldChanged; // set by the app after it applied the partner's changes
    /*0x375*/ u8 padding3[3];
};

extern struct CoopMailbox gCoopMailbox;

void Coop_Update(void);
bool8 Coop_IsInOverworld(void);
void Coop_FilterInput(u16 *newKeys, u16 *heldKeys);
bool8 Coop_IsLinked(void);
bool8 Coop_IsFollowing(void);
bool8 Coop_IsPartnerObject(struct ObjectEvent *objectEvent);
const u8 *Coop_GetPartnerInteractionScript(void);
const u8 *Coop_GetMenuScript(void);

// dialogue mirroring
extern u8 gCoopTextPages;
void Coop_OnFieldMessage(const u8 *text);
void Coop_OnFieldMessageHidden(void);
bool8 Coop_IsMirroringText(void);
bool8 Coop_ShouldAdvanceText(void);

// specials
void Coop_BufferPartnerName(void);
u16 Coop_GetLinkMode(void);
void Coop_SendLinkRequest(void);
void Coop_AnswerLinkRequest(void);
void Coop_AnswerControlRequest(void);
void Coop_GiveControl(void);
void Coop_AskForControl(void);
void Coop_SplitOff(void);
void Coop_SaveWithHost(void);

// saving: only the host saves, and the guest's game saves along with it
bool8 Coop_IsGuest(void);
void Coop_OnGameSaved(void);

#endif // GUARD_COOP_H
