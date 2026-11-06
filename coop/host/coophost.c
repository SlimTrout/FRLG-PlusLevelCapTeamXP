// Brainy Blue Co-op host, test version: runs two games side by side in one process
// and relays the co-op mailbox between them every frame.
//
//   ./coophost rom.gba script.txt outdir saveA saveB
//
// script lines: "<frames> <keysA> <keysB> [shot]"  (keys as in harness.c, '-' = none)
// "shot" writes outdir/A_NNN.ppm and outdir/B_NNN.ppm.
//
// The real app will do the same relay over the network instead of in memory.
#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/gba/interface.h>
#include <mgba/gba/core.h>
#include <mgba/internal/gba/input.h>
#include <mgba-util/vfs.h>
#include <mgba/core/log.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#define COOP_MAGIC_0 0x4F434242u
#define COOP_MAGIC_1 0x3130504Fu
#define MB_ACTIVE 8
#define MB_INDEX 9
#define MB_LOCAL 12
#define MB_PARTNER 40
#define STATE_SIZE 28

static int parseKeys(const char *s) {
    int k = 0;
    if (!strcmp(s, "-")) return 0;
    for (const char *p = s; *p; p++) {
        switch (*p) {
        case 'A': k |= 1 << GBA_KEY_A; break;
        case 'B': k |= 1 << GBA_KEY_B; break;
        case 'S': k |= 1 << GBA_KEY_START; break;
        case 'E': k |= 1 << GBA_KEY_SELECT; break;
        case 'U': k |= 1 << GBA_KEY_UP; break;
        case 'D': k |= 1 << GBA_KEY_DOWN; break;
        case 'l': k |= 1 << GBA_KEY_LEFT; break;
        case 'r': k |= 1 << GBA_KEY_RIGHT; break;
        case 'L': k |= 1 << GBA_KEY_L; break;
        case 'R': k |= 1 << GBA_KEY_R; break;
        }
    }
    return k;
}

static void quietLog(struct mLogger *l, int c, enum mLogLevel lv, const char *f, va_list a) { (void)l; (void)c; (void)lv; (void)f; (void)a; }
static struct mLogger sQuiet = { .log = quietLog };

struct Player {
    struct mCore *core;
    mColor *buf;
    uint32_t mailbox; // 0 until found
};

static int initPlayer(struct Player *p, const char *rom, const char *save) {
    p->core = GBACoreCreate();
    if (!p->core) return 0;
    p->core->init(p->core);
    mCoreInitConfig(p->core, NULL);
    if (!mCoreLoadFile(p->core, rom)) return 0;
    p->buf = calloc(256 * 256, sizeof(mColor));
    p->core->setVideoBuffer(p->core, p->buf, 256);
    p->core->loadSave(p->core, VFileOpen(save, O_RDWR | O_CREAT));
    p->core->reset(p->core);
    p->mailbox = 0;
    return 1;
}

static void findMailbox(struct Player *p) {
    if (p->mailbox) return;
    for (uint32_t a = 0x02000000; a < 0x02040000; a += 4) {
        if (p->core->busRead32(p->core, a) == COOP_MAGIC_0 && p->core->busRead32(p->core, a + 4) == COOP_MAGIC_1) {
            p->mailbox = a;
            printf("mailbox at %08x\n", a);
            return;
        }
    }
}

// Copy one game's local state into the other's partner block.
static void relay(struct Player *from, struct Player *to, int index) {
    if (!from->mailbox || !to->mailbox) return;
    to->core->busWrite8(to->core, to->mailbox + MB_ACTIVE, 1);
    to->core->busWrite8(to->core, to->mailbox + MB_INDEX, index);
    for (int i = 0; i < STATE_SIZE; i++)
        to->core->busWrite8(to->core, to->mailbox + MB_PARTNER + i,
                            from->core->busRead8(from->core, from->mailbox + MB_LOCAL + i));
}

static void shot(struct Player *p, const char *dir, const char *tag, int n) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s_%03d.ppm", dir, tag, n);
    FILE *o = fopen(path, "wb");
    fprintf(o, "P6\n240 160\n255\n");
    for (unsigned i = 0; i < 240 * 160; i++) {
        mColor c = p->buf[(i / 240) * 256 + (i % 240)];
        unsigned char px[3] = { c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF };
        fwrite(px, 1, 3, o);
    }
    fclose(o);
}

int main(int argc, char **argv) {
    struct Player a, b;
    mLogSetDefaultLogger(&sQuiet);
    if (argc < 6) { fprintf(stderr, "usage: coophost rom script outdir saveA saveB\n"); return 1; }
    if (!initPlayer(&a, argv[1], argv[4]) || !initPlayer(&b, argv[1], argv[5])) { fprintf(stderr, "init fail\n"); return 1; }

    FILE *f = fopen(argv[2], "r");
    char line[256];
    int n = 0;
    long frame = 0;
    while (fgets(line, sizeof line, f)) {
        int count; char ka[64] = "-", kb[64] = "-", extra[64] = "";
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "%d %63s %63s %63s", &count, ka, kb, extra) < 1) continue;
        a.core->setKeys(a.core, parseKeys(ka));
        b.core->setKeys(b.core, parseKeys(kb));
        for (int i = 0; i < count; i++) {
            a.core->runFrame(a.core);
            b.core->runFrame(b.core);
            if ((frame & 63) == 0) { findMailbox(&a); findMailbox(&b); }
            relay(&a, &b, 1);
            relay(&b, &a, 0);
            frame++;
        }
        if (!strcmp(extra, "shot")) {
            shot(&a, argv[3], "A", n);
            shot(&b, argv[3], "B", n);
            printf("frame %ld shot %d\n", frame, n);
            n++;
        }
    }
    fclose(f);
    return 0;
}
