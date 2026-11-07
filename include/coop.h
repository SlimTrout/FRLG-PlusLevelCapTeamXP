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
    COOP_CMD_WILD_BATTLE,     // the leader ran into a wild Pokemon: both go into a co-op battle
    COOP_CMD_TRAINER_BATTLE,  // the leader's trainer battle (trainer in battleTrainer): join it
    COOP_CMD_TRAINER_BATTLE_NO_WHITEOUT, // same, but losing doesn't white out (first rival battle)
    COOP_CMD_STORY_PULL,      // a story battle: come over (warp), link up and join it
    COOP_CMD_STORY_PULL_NO_WHITEOUT,
    COOP_CMD_STORY_ASK,       // "I'm about to start a story battle: get ready"
    COOP_CMD_STORY_READY,     // "ready, bring me over"
    COOP_CMD_HEAL,            // the leader healed at a Pokemon Center: heal too
    COOP_CMD_TRADE_ASK,       // "let's trade" (Pokemon Center upstairs)
    COOP_CMD_TRADE_ACCEPT,
    COOP_CMD_TRADE_DECLINE,   // (also: the asker gave up waiting)
};

#define COOP_TEXT_SIZE 400

// First word of the block each game sends once a co-op battle is over (battle_setup.c);
// the battle's own link code leaves such a block alone.
#define COOP_END_BATTLE_MARKER 0x444E4542 // "BEND"

// 36 bytes. The app copies it byte for byte, so keep the layout in sync with
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
             u8 hasPokemon:1;  // the player has at least one Pokemon
             u8 fieldFree:1;   // walking around freely: no script, menu, battle or link (save states are made then)
             u8 padding:2;
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
    /*0x20*/ u16 battleTrainer; // trainer ID + 1 of a trainer battle under way (talked to,
                                // spotted or fighting), 0 if none: the partner's game
                                // treats that trainer as taken
    /*0x22*/ u16 keySettings;  // this player's Key System settings (exp, team exp, level cap)
};

struct CoopMailbox
{
    /*0x00*/ u32 magic[2];
    /*0x08*/ u8 active;        // set by the app while a partner is connected
    /*0x09*/ u8 playerIndex;   // set by the app: 0 = host, 1 = guest
    /*0x0A*/ u8 padding[2];
    /*0x0C*/ struct CoopPlayerState local;   // written by this game
    /*0x30*/ struct CoopPlayerState partner; // written by the app
    /*0x54*/ u8 localText[COOP_TEXT_SIZE];   // this game's current field message
    /*0x1E4*/ u8 partnerText[COOP_TEXT_SIZE]; // the partner's, written by the app
    // Shared world: the app keeps both games' story flags and variables in step.
    /*0x374*/ u32 flagsPtr;    // this game's flags and vars, 0 until it's in the overworld
    /*0x378*/ u32 varsPtr;
    /*0x37C*/ u8 worldChanged; // set by the app after it applied the partner's changes
    /*0x37D*/ u8 padding3[3];
    // Co-op link: the game's link blocks (as used by link battles) go through the app.
    /*0x380*/ u32 blockSendBuf;   // gBlockSendBuffer
    /*0x384*/ u32 blockRecvBuf;   // gBlockRecvBuffer (BLOCK_BUFFER_SIZE bytes per player)
    /*0x388*/ u32 blockRecvStatus; // gBlockReceivedStatus (one byte per player)
    /*0x38C*/ u16 blockSendSize;
    /*0x38E*/ u8 blockSendSeq;    // bumped for each block sent
    /*0x38F*/ u8 blockSendPending; // set by the game, cleared by the app once it has the block
    /*0x390*/ u8 linkActive;      // co-op link is up (both games in a co-op battle)
    /*0x391*/ u8 padding5[3];
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
u8 Coop_TakeLeveledUpMons(void);
bool8 Coop_IsTrading(void);
void Coop_EndTrade(void);
void Coop_ForceSoloBattle(void);
void Coop_ShareItemGift(u16 item, u16 count);
void Coop_ShareMonGift(u16 species, u8 level, u16 item);
void Coop_AskPartnerToGetReady(void);
bool8 Coop_TakeStoryPartnerReady(void);
void Coop_SetWhiteOutPending(void);
void Coop_ResetItemUse(void);
void Coop_NoteItemUsed(u8 partyIndex, u8 moveIndex);
u8 Coop_GetItemParty(void);
u8 Coop_GetItemMove(void);
void Coop_OnItemChosen(u8 battler, u16 item, u8 partyId, u8 moveIndex);
u8 Coop_GetItemMonForBattler(u8 battler);
struct Pokemon;
void Coop_OnMonCaught(u8 battler, u8 target, struct Pokemon *mon);
u16 Coop_GetCaughtBy(void);
void Coop_SetCaughtBy(u16 caughtBy);
struct Pokemon *Coop_GetCaughtMons(void);
struct Pokemon *Coop_GetMyCaughtMon(u8 slot);
bool8 Coop_GuestCaughtAny(void);
void Coop_NoteMyCatch(void);
bool8 Coop_CaughtOneThisBattle(void);
void Coop_SetSpectator(u8 battler);
u8 Coop_GetSpectator(void);
bool8 Coop_CaughtByMe(void);
struct Pokemon *Coop_GetOwnWildMon(void);
void Coop_GiveCaughtMon(void);
void Coop_SetBattleMoney(u32 money);
u16 Coop_GetLinkMode(void);
void Coop_SendLinkRequest(void);
void Coop_AnswerLinkRequest(void);
void Coop_AnswerControlRequest(void);
void Coop_GiveControl(void);
void Coop_AskForControl(void);
void Coop_SplitOff(void);
void Coop_SaveWithHost(void);
u16 Coop_PartnerNeedsStarter(void);

// saving: only the host saves, and the guest's game saves along with it
bool8 Coop_IsGuest(void);

// co-op link: the game's link layer, carried by the app (link.c calls these)
void Coop_OpenLink(void);
void Coop_CloseLink(void);
bool8 Coop_IsLinkOpen(void);
bool8 Coop_SendBlock(const void *src, u16 size);
bool8 Coop_IsLinkTaskFinished(void);

// co-op battles
// A link battle against other people, not a co-op battle (which also uses the link).
#define BATTLE_IS_REAL_LINK ((gBattleTypeFlags & BATTLE_TYPE_LINK) && !Coop_IsLinkOpen())
bool8 Coop_IsHost(void);
void Coop_StartLinkedWildBattle(void);
void Coop_StartLinkedTrainerBattle(bool8 noWhiteOut, bool8 pullPartner);
void Coop_CreateLinkBattleTasks(void);
void Coop_BackUpParty(void);
void *Coop_GetBattleSendMons(u8 part);
void Coop_ReceiveBattleMons(u8 part, struct Pokemon *fromHost, struct Pokemon *fromGuest);
void Coop_BuildBattleParties(void);
void Coop_RestoreParty(void);
u16 Coop_UseExpOwnerSettings(u8 partyId);
void Coop_RestoreExpSettings(u16 saved);

// trainers: one player's battle keeps the trainer away from the other
void Coop_OnTrainerBattle(u16 trainerId);
bool8 Coop_IsTrainerTakenByPartner(u16 trainerId);
void Coop_OnGameSaved(void);

#endif // GUARD_COOP_H
