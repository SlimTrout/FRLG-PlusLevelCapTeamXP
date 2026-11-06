// Brainy Blue Co-op: shared world.
//
// Both games keep their own save, but the story is shared: event flags (badges,
// beaten trainers, items picked up, people who left) and story variables are kept
// the same in both. The host's world is the one that counts: when the guest joins,
// it takes the host's flags and variables, and from then on every change either
// game makes is sent to the other.
//
// Per-player things stay apart: temporary flags and vars, each player's Nuzlocke
// encounters, step counters, the starter choice, the trainer card and so on.
//
// Used by the app (coop/app/bbcoop.c) and the test host (coop/host/coophost.c).
// The game publishes where its flags and vars are in the mailbox (include/coop.h).
#ifndef BB_WORLD_SYNC_H
#define BB_WORLD_SYNC_H

#include <mgba/core/core.h>
#include <string.h>

#define WS_MB_FLAGS_PTR 0x374
#define WS_MB_VARS_PTR 0x378
#define WS_MB_WORLD_CHANGED 0x37C
#define WS_MB_LOCAL_BATTLE_TRAINER (0x0C + 0x20) // u16, nonzero during a trainer battle

#define WS_FLAG_BYTES 288 // 0x900 flags
#define WS_VARS 256       // 0x4000-0x40FF
#define WS_IMAGE (WS_FLAG_BYTES + WS_VARS * 2)
#define WS_CHUNK 200
#define WS_CHUNKS ((WS_IMAGE + WS_CHUNK - 1) / WS_CHUNK)
#define WS_MAX_CHANGES 60

#define MSG_WORLD_CHANGES 4  // payload: up to 60 x (u16 key, u16 value); key bit 15 = var
#define MSG_WORLD_SNAPSHOT 5 // payload: chunk index, then up to WS_CHUNK bytes of flags+vars
#define MSG_WORLD_REQUEST 6  // guest -> host: send me your world

typedef void (*WsSendFn)(void *ctx, int type, const void *payload, int len);

struct WorldSync {
    int isHost;
    int haveShadow;        // shadow matches this game's flags/vars
    unsigned char shadow[WS_IMAGE];
    // guest: snapshot being received
    unsigned char snap[WS_IMAGE];
    unsigned snapChunks;   // bitmask of chunks received
    int requestTimer;
    // host: the guest asked for the world
    int snapshotRequested;
    // changes from the partner waiting to be applied
    unsigned char pending[4096];
    int pendingLen;
};

static int wsFlagShared(int flag) {
    if (flag < 0x20) return 0;                       // temporary flags
    if (flag >= 0x300 && flag <= 0x355) return 0;    // each player's Nuzlocke encounters
    switch (flag) {
    case 0x800 + 0x2A: // FLAG_SYS_CTRL_OBJ_DELETE
    case 0x800 + 0x2B: // FLAG_SYS_IS_LINKING
    case 0x800 + 0x30: // FLAG_SYS_ON_CYCLING_ROAD
    case 0x800 + 0x33: // FLAG_SYS_ON_RESUME
        return 0;
    }
    return 1;
}

static int wsVarShared(int v) { // v = var - 0x4000
    if (v < 0x20) return 0;                 // temporary vars and object graphics vars
    if (v >= 0x20 && v <= 0x23) return 0;   // repel, happiness, poison, item step counters
    if (v >= 0x25 && v <= 0x29) return 0;   // step counters, quest log, last repel
    if (v == 0x2D || v == 0x2F || v == 0x30 || v == 0x31) return 0; // daycare, ash, ice, starter
    if (v == 0x37 || v == 0x3A) return 0;   // PC box to send to, elevator floor
    if (v >= 0x42 && v <= 0x48) return 0;   // trainer card
    if (v == 0x4D || v == 0x8C) return 0;   // quest log entrance, Faraway Island steps
    if (v >= 0xAA && v <= 0xB3) return 0;   // quest log backups
    return 1;
}

static int wsInGame(struct mCore *c, uint32_t mb, uint32_t *flags, uint32_t *vars) {
    *flags = c->busRead32(c, mb + WS_MB_FLAGS_PTR);
    *vars = c->busRead32(c, mb + WS_MB_VARS_PTR);
    return (*flags >> 24) == 0x02 && (*vars >> 24) == 0x02;
}

static void wsRead(struct mCore *c, uint32_t flags, uint32_t vars, unsigned char *img) {
    for (int i = 0; i < WS_FLAG_BYTES; i++) img[i] = c->busRead8(c, flags + i);
    for (int i = 0; i < WS_VARS * 2; i++) img[WS_FLAG_BYTES + i] = c->busRead8(c, vars + i);
}

static void wsWriteByte(struct mCore *c, uint32_t flags, uint32_t vars, int i, unsigned char v) {
    if (i < WS_FLAG_BYTES) c->busWrite8(c, flags + i, v);
    else c->busWrite8(c, vars + (i - WS_FLAG_BYTES), v);
}

static void wsInit(struct WorldSync *ws, int isHost) {
    memset(ws, 0, sizeof *ws);
    ws->isHost = isHost;
}

static void wsReceive(struct WorldSync *ws, int type, const unsigned char *p, int len) {
    if (type == MSG_WORLD_REQUEST) {
        if (ws->isHost) ws->snapshotRequested = 1;
    } else if (type == MSG_WORLD_SNAPSHOT && len >= 1) {
        int at = p[0] * WS_CHUNK;
        if (!ws->isHost && !ws->haveShadow && at + len - 1 <= WS_IMAGE) {
            memcpy(ws->snap + at, p + 1, len - 1);
            ws->snapChunks |= 1u << p[0];
        }
    } else if (type == MSG_WORLD_CHANGES) {
        if (ws->pendingLen + len <= (int)sizeof ws->pending) {
            memcpy(ws->pending + ws->pendingLen, p, len);
            ws->pendingLen += len;
        }
    }
}

// Once per frame, after the game ran it. Returns 1 if the partner's changes were applied.
static int wsTick(struct WorldSync *ws, struct mCore *c, uint32_t mb, int connected, WsSendFn send, void *ctx) {
    uint32_t flags, vars;
    unsigned char now[WS_IMAGE];
    int applied = 0;

    if (!mb || !wsInGame(c, mb, &flags, &vars)) {
        // Not in the overworld yet (title screen, intro): nothing to keep in step.
        ws->haveShadow = 0;
        ws->pendingLen = 0;
        ws->snapChunks = 0;
        return 0;
    }
    if (!connected) {
        ws->pendingLen = 0;
        if (!ws->isHost) { ws->haveShadow = 0; ws->snapChunks = 0; }
        return 0;
    }
    wsRead(c, flags, vars, now);

    if (ws->isHost) {
        if (!ws->haveShadow) {
            memcpy(ws->shadow, now, WS_IMAGE);
            ws->haveShadow = 1;
        }
    } else if (!ws->haveShadow) {
        // Guest: take the host's world once it has all arrived.
        if (ws->snapChunks == (1u << WS_CHUNKS) - 1) {
            for (int i = 0; i < WS_IMAGE; i++) {
                unsigned char v = now[i];
                if (i < WS_FLAG_BYTES) {
                    for (int b = 0; b < 8; b++)
                        if (wsFlagShared(i * 8 + b))
                            v = (v & ~(1 << b)) | (ws->snap[i] & (1 << b));
                } else if (wsVarShared((i - WS_FLAG_BYTES) / 2)) {
                    v = ws->snap[i];
                }
                if (v != now[i]) { wsWriteByte(c, flags, vars, i, v); now[i] = v; }
            }
            memcpy(ws->shadow, now, WS_IMAGE);
            ws->haveShadow = 1;
            ws->snapChunks = 0;
            ws->pendingLen = 0; // anything older is already in the snapshot
            applied = 1;
        } else {
            if (--ws->requestTimer <= 0) {
                send(ctx, MSG_WORLD_REQUEST, NULL, 0);
                ws->requestTimer = 120;
            }
            return 0;
        }
    }

    // This game's own changes since last frame go to the partner.
    {
        unsigned char msg[WS_MAX_CHANGES * 4];
        int n = 0;
        for (int i = 0; i < WS_IMAGE; i++) {
            if (now[i] == ws->shadow[i]) continue;
            if (i < WS_FLAG_BYTES) {
                for (int b = 0; b < 8; b++) {
                    int flag = i * 8 + b, bit = (now[i] >> b) & 1;
                    if (((ws->shadow[i] >> b) & 1) == bit || !wsFlagShared(flag)) continue;
                    msg[n * 4] = flag & 0xFF; msg[n * 4 + 1] = flag >> 8;
                    msg[n * 4 + 2] = bit; msg[n * 4 + 3] = 0;
                    if (++n == WS_MAX_CHANGES) { send(ctx, MSG_WORLD_CHANGES, msg, n * 4); n = 0; }
                }
            } else {
                int v = (i - WS_FLAG_BYTES) / 2;
                if ((i - WS_FLAG_BYTES) % 2 == 1 && now[i - 1] != ws->shadow[i - 1]) continue; // sent with its low byte
                if (!wsVarShared(v)) continue;
                int key = 0x8000 | v, base = WS_FLAG_BYTES + v * 2;
                msg[n * 4] = key & 0xFF; msg[n * 4 + 1] = key >> 8;
                msg[n * 4 + 2] = now[base]; msg[n * 4 + 3] = now[base + 1];
                if (++n == WS_MAX_CHANGES) { send(ctx, MSG_WORLD_CHANGES, msg, n * 4); n = 0; }
            }
        }
        if (n) send(ctx, MSG_WORLD_CHANGES, msg, n * 4);
        memcpy(ws->shadow, now, WS_IMAGE);
    }

    // The host answers a request for its world.
    if (ws->isHost && ws->snapshotRequested) {
        for (int chunk = 0; chunk < WS_CHUNKS; chunk++) {
            unsigned char payload[1 + WS_CHUNK];
            int n = WS_IMAGE - chunk * WS_CHUNK;
            if (n > WS_CHUNK) n = WS_CHUNK;
            payload[0] = (unsigned char)chunk;
            memcpy(payload + 1, now + chunk * WS_CHUNK, n);
            send(ctx, MSG_WORLD_SNAPSHOT, payload, 1 + n);
        }
        ws->snapshotRequested = 0;
    }

    // Then the partner's changes come in, unless this player is in the middle of a
    // trainer battle: they wait until it's over, so nothing shifts under its script.
    if (c->busRead16(c, mb + WS_MB_LOCAL_BATTLE_TRAINER))
        return 0;
    for (int at = 0; at + 4 <= ws->pendingLen; at += 4) {
        const unsigned char *e = ws->pending + at;
        int key = e[0] | (e[1] << 8);
        if (key & 0x8000) {
            int v = key & 0x7FFF;
            if (v >= WS_VARS || !wsVarShared(v)) continue;
            int base = WS_FLAG_BYTES + v * 2;
            for (int k = 0; k < 2; k++) {
                if (now[base + k] != e[2 + k]) {
                    now[base + k] = e[2 + k];
                    wsWriteByte(c, flags, vars, base + k, e[2 + k]);
                    applied = 1;
                }
            }
            ws->shadow[base] = now[base]; ws->shadow[base + 1] = now[base + 1];
        } else {
            int i = key / 8, b = key % 8;
            if (i >= WS_FLAG_BYTES || !wsFlagShared(key)) continue;
            unsigned char v = (now[i] & ~(1 << b)) | ((e[2] & 1) << b);
            if (v != now[i]) {
                now[i] = v;
                wsWriteByte(c, flags, vars, i, v);
                applied = 1;
            }
            ws->shadow[i] = now[i];
        }
    }
    ws->pendingLen = 0;
    if (applied) c->busWrite8(c, mb + WS_MB_WORLD_CHANGED, 1);
    return applied;
}

#endif // BB_WORLD_SYNC_H
