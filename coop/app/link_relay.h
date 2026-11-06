// Brainy Blue Co-op: link relay.
//
// During a co-op battle the game's link layer (the one link battles use) is carried
// over the network. When the game sends a link block, it leaves it in its send
// buffer and raises a flag in the mailbox; the app takes it, sends it to the
// partner, and clears the flag. Blocks from the partner are written into this
// game's receive buffer for the partner's slot as soon as that slot is free.
//
// Used by the app (coop/app/bbcoop.c) and the test host (coop/host/coophost.c).
#ifndef BB_LINK_RELAY_H
#define BB_LINK_RELAY_H

#include <mgba/core/core.h>
#include <string.h>

#define LR_MB_INDEX 0x09
#define LR_MB_SEND_BUF 0x380
#define LR_MB_RECV_BUF 0x384
#define LR_MB_RECV_STATUS 0x388
#define LR_MB_SEND_SIZE 0x38C
#define LR_MB_SEND_SEQ 0x38E
#define LR_MB_SEND_PENDING 0x38F
#define LR_MB_LINK_ACTIVE 0x390

#define LR_BLOCK_SIZE 0x100
#define LR_CHUNK 200
#define LR_QUEUE 64

#define MSG_BLOCK 7 // payload: chunk index, last chunk flag, then up to LR_CHUNK bytes

typedef void (*LrSendFn)(void *ctx, int type, const void *payload, int len);

struct LinkRelay {
    int wasActive;
    unsigned char assembling[LR_BLOCK_SIZE];
    int assembledLen;
    unsigned char queue[LR_QUEUE][LR_BLOCK_SIZE];
    int queueLen[LR_QUEUE];
    int head, count;
};

static void lrInit(struct LinkRelay *lr) {
    memset(lr, 0, sizeof *lr);
}

static void lrReceive(struct LinkRelay *lr, const unsigned char *p, int len) {
    if (len < 2) return;
    int at = p[0] * LR_CHUNK, n = len - 2;
    if (at + n > LR_BLOCK_SIZE) return;
    memcpy(lr->assembling + at, p + 2, n);
    if (at + n > lr->assembledLen) lr->assembledLen = at + n;
    if (p[1] && lr->count < LR_QUEUE) {
        int slot = (lr->head + lr->count) % LR_QUEUE;
        memcpy(lr->queue[slot], lr->assembling, LR_BLOCK_SIZE);
        lr->queueLen[slot] = lr->assembledLen;
        lr->count++;
        lr->assembledLen = 0;
    }
}

// Once per frame, after the game ran it.
static void lrTick(struct LinkRelay *lr, struct mCore *c, uint32_t mb, LrSendFn send, void *ctx) {
    int active = mb && c->busRead8(c, mb + LR_MB_LINK_ACTIVE);

    if (!active) {
        if (lr->wasActive) { lr->head = lr->count = 0; lr->assembledLen = 0; } // battle over: drop leftovers
        lr->wasActive = 0;
        return;
    }
    lr->wasActive = 1;

    // This game sent a block: pass it on.
    if (c->busRead8(c, mb + LR_MB_SEND_PENDING)) {
        uint32_t buf = c->busRead32(c, mb + LR_MB_SEND_BUF);
        int size = c->busRead16(c, mb + LR_MB_SEND_SIZE);
        unsigned char block[LR_BLOCK_SIZE];
        if (size > LR_BLOCK_SIZE) size = LR_BLOCK_SIZE;
        for (int i = 0; i < size; i++) block[i] = c->busRead8(c, buf + i);
        for (int chunk = 0; chunk == 0 || chunk * LR_CHUNK < size; chunk++) {
            unsigned char payload[2 + LR_CHUNK];
            int n = size - chunk * LR_CHUNK;
            if (n > LR_CHUNK) n = LR_CHUNK;
            if (n < 0) n = 0;
            payload[0] = (unsigned char)chunk;
            payload[1] = (chunk + 1) * LR_CHUNK >= size;
            memcpy(payload + 2, block + chunk * LR_CHUNK, n);
            send(ctx, MSG_BLOCK, payload, 2 + n);
        }
        c->busWrite8(c, mb + LR_MB_SEND_PENDING, 0);
    }

    // The partner's next block goes in as soon as its slot is free.
    if (lr->count) {
        int partner = c->busRead8(c, mb + LR_MB_INDEX) ^ 1;
        uint32_t status = c->busRead32(c, mb + LR_MB_RECV_STATUS) + partner;
        uint32_t buf = c->busRead32(c, mb + LR_MB_RECV_BUF) + partner * LR_BLOCK_SIZE;
        if (!c->busRead8(c, status)) {
            int slot = lr->head;
            for (int i = 0; i < lr->queueLen[slot]; i++) c->busWrite8(c, buf + i, lr->queue[slot][i]);
            c->busWrite8(c, status, 1);
            lr->head = (lr->head + 1) % LR_QUEUE;
            lr->count--;
        }
    }
}

#endif // BB_LINK_RELAY_H
