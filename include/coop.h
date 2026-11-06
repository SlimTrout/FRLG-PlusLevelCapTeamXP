#ifndef GUARD_COOP_H
#define GUARD_COOP_H

// Brainy Blue Co-op: the ROM talks to the host app through a mailbox in EWRAM.
// The host app finds it by its magic words, sets `active`, and every frame copies
// one game's `local` block into the other game's `partner` block.

#define COOP_MAGIC_0 0x4F434242 // "BBCO"
#define COOP_MAGIC_1 0x3130504F // "OP01"

struct CoopPlayerState
{
    u16 seq;          // bumped every frame the state is written
    u8 mapGroup;
    u8 mapNum;
    s16 x;            // object coords (map coords + MAP_OFFSET)
    s16 y;
    u8 facing;        // DIR_*
    u8 elevation;
    u8 gender;
    u8 running:1;     // running or on a bike: partner walks fast
    u8 inOverworld:1; // 0 while in a battle or menu (state is stale)
    u8 padding:6;
};

struct CoopMailbox
{
    u32 magic[2];
    u8 active;        // set by the host when a partner is connected
    u8 playerIndex;   // set by the host: 0 = host, 1 = guest
    u8 padding[2];
    struct CoopPlayerState local;   // written by this game
    struct CoopPlayerState partner; // written by the host app
};

extern struct CoopMailbox gCoopMailbox;

void Coop_Update(void);

#endif // GUARD_COOP_H
