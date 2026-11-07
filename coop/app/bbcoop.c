// Brainy Blue Co-op app.
//
// Runs the Brainy Blue ROM on the mGBA core in a window, with sound and
// keyboard/controller input, and connects to one partner over TCP. Every frame it
// sends this game's co-op mailbox (`local` block, see include/coop.h) to the
// partner and writes the partner's block into this game's mailbox.
//
// On Windows, starting it without options (double-clicking it) opens a launcher
// window instead (launcher_win.h).
//
// Usage:
//   bbcoop host [--port 27450] [--rom pokefirered.gba] [--speed 3]
//   bbcoop join <host address> [--port 27450] [--rom pokefirered.gba]
//   --save <file> picks the save file (default: the ROM's name with .sav)
//
// Test options (no window or sound, used for automated tests):
//   --headless --script <file> --shots <dir>
//   script lines: "<frames> <keys> [shot]"  keys: A B S E U D l r L R or '-'
//
// Keyboard: arrows, Z = A, X = B, Enter = Start, Backspace = Select, A = L, S = R,
//           Close the window to quit. A game controller works too.
//
// If the partner disconnects, the app closes. Only the host saves (and the guest's
// game saves along with it), so progress since the host's last save is not kept.

#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <mgba/gba/core.h>
#include <mgba/gba/interface.h>
#include <mgba/internal/gba/input.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/vfs.h>
#include "world_sync.h"
#include "link_relay.h"
#include <time.h>

#define SDL_MAIN_HANDLED // we provide main() ourselves
#include <SDL.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET sock_t;
#define SOCK_INVALID INVALID_SOCKET
#define sock_close closesocket
static int sock_would_block(void) { return WSAGetLastError() == WSAEWOULDBLOCK; }
static int sock_in_progress(void) { return WSAGetLastError() == WSAEWOULDBLOCK; }
static void sock_nonblock(sock_t s) { u_long on = 1; ioctlsocket(s, FIONBIO, &on); }
#include "launcher_win.h"
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define SOCK_INVALID (-1)
#define sock_close close
static int sock_would_block(void) { return errno == EAGAIN || errno == EWOULDBLOCK; }
static int sock_in_progress(void) { return errno == EINPROGRESS; }
static void sock_nonblock(sock_t s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
#endif

// ------------------------------------------------------------------ mailbox layout
// Must match struct CoopMailbox in include/coop.h (36-byte player state, then
// this game's and the partner's current field message text).
#define COOP_MAGIC_0 0x4F434242u
#define COOP_MAGIC_1 0x3130504Fu
#define MB_ACTIVE 8
#define MB_INDEX 9
#define MB_LOCAL 12
#define MB_PARTNER 48
#define MB_LOCALTEXT 84
#define MB_PARTNERTEXT 484
#define STATE_SIZE 36
#define STATE_MSGSEQ 0x1C
#define TEXT_SIZE 400
#define TEXT_CHUNK 200
#define TEXT_EOS 0xFF

#define EWRAM_START 0x02000000u
#define EWRAM_END 0x02040000u

// ------------------------------------------------------------------ network messages
// Every message: 'B' 'B' type len, then len bytes.
#define MSG_HELLO 1 // payload: protocol version, game speed (host only), then the save state picked to start from
#define MSG_STATE 2 // payload: STATE_SIZE bytes of the sender's local block
#define MSG_TEXT 3  // payload: chunk index, then up to TEXT_CHUNK bytes of the sender's message text
#define MSG_SAVESTATE 8 // payload: kind (0 auto, 1 manual), then the state's name: make one now
#define PROTOCOL_VERSION 7
#define DEFAULT_PORT 27450

struct Net {
    sock_t listener;
    sock_t peer;
    int isHost;
    int connected;
    int everConnected;
    unsigned char inbuf[4096];
    size_t inlen;
    // Bytes waiting to go out. When this player's partner stops reading for a while
    // (on Windows, dragging the window pauses the app), sends back up here instead
    // of being cut off half-way.
    unsigned char *outbuf;
    size_t outlen;
    unsigned char partnerState[STATE_SIZE];
    int havePartnerState;
    unsigned char partnerText[TEXT_SIZE];
    int sentMsgSeq; // -1 until the first message text has been sent
    struct WorldSync ws;
    struct LinkRelay lr;
    // guest: the host's address, tried again and again until the host answers
    struct sockaddr_in joinAddr;
    int joining;
    int connectPolls; // polls since the current attempt started (or failed)
    int failedTries;
    // set by the partner's hello
    int hostSpeed;
    int versionMismatch;
    int speed; // host: the speed it tells the guest
    // save states: the one this player starts from ("" = the normal save), the
    // partner's choice (from their hello), and a save state the partner just made
    char startState[64];
    int partnerHello;
    char partnerStartState[64];
    int partnerSaveKind;          // -1 = none waiting
    char partnerSaveName[64];
};

struct App {
    struct mCore *core;
    mColor *video;
    uint32_t mailbox;
    const char *romPath;
    int headless;
    struct Net net;
    // save states
    char stateDir[1024];  // next to the ROM: savestates/auto and savestates/manual
    int waitStateMatch;   // started from a save state: don't run until the partner picked the same one
    uint16_t lastLocalSeq, lastPartnerSeq;
    int localIdle, partnerIdle; // frames since that player's state last changed
    int manualPending;    // a manual save state was asked for; made once both players are free
    int autoPending;
    char notice[96];      // shown in the window title for a few seconds
    uint32_t noticeUntil;
    int noticeChanged;
};

static void quietLog(struct mLogger *l, int c, enum mLogLevel lv, const char *f, va_list a) { (void)l; (void)c; (void)lv; (void)f; (void)a; }
static struct mLogger sQuiet = { .log = quietLog };

// ------------------------------------------------------------------ networking

static int netStartHost(struct Net *n, int port) {
    struct sockaddr_in addr;
    int one = 1;

    n->listener = socket(AF_INET, SOCK_STREAM, 0);
    if (n->listener == SOCK_INVALID) return 0;
    setsockopt(n->listener, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(n->listener, (struct sockaddr *)&addr, sizeof addr) != 0) return 0;
    if (listen(n->listener, 1) != 0) return 0;
    sock_nonblock(n->listener);
    n->isHost = 1;
    printf("Hosting on port %d. Waiting for your partner to join...\n", port);
    return 1;
}

// The guest looks the host's address up once, then keeps trying to connect in the
// background (netPoll), so it doesn't matter which player starts first.
static int netJoin(struct Net *n, const char *host, int port) {
    struct addrinfo hints, *res = NULL;
    char portStr[16];

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portStr, sizeof portStr, "%d", port);
    if (getaddrinfo(host, portStr, &hints, &res) != 0 || !res) return 0;
    memcpy(&n->joinAddr, res->ai_addr, sizeof n->joinAddr);
    freeaddrinfo(res);
    n->isHost = 0;
    n->joining = 1;
    n->peer = SOCK_INVALID;
    n->connectPolls = 1000; // try at once
    printf("Connecting to %s:%d...\n", host, port);
    return 1;
}

static void netSend(struct Net *n, int type, const void *payload, int len);

static void netConnected(struct Net *n, int isHost) {
    int one = 1;
    unsigned char hello[2 + 64];
    int nameLen = (int)strlen(n->startState);

    setsockopt(n->peer, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
    n->connected = 1;
    n->everConnected = 1;
    n->inlen = 0;
    n->outlen = 0;
    n->sentMsgSeq = -1;
    wsInit(&n->ws, isHost);
    lrInit(&n->lr);
    printf(isHost ? "Your partner joined.\n" : "Connected to your partner.\n");
    hello[0] = PROTOCOL_VERSION;
    hello[1] = (unsigned char)(isHost ? n->speed : 0);
    memcpy(hello + 2, n->startState, nameLen);
    netSend(n, MSG_HELLO, hello, 2 + nameLen);
}

// One step of the guest's connection attempt; never blocks.
static void netTryJoin(struct Net *n) {
    n->connectPolls++;
    if (n->peer == SOCK_INVALID) {
        if (n->connectPolls < 120) return; // wait a little between attempts
        n->connectPolls = 0;
        n->peer = socket(AF_INET, SOCK_STREAM, 0);
        if (n->peer == SOCK_INVALID) return;
        sock_nonblock(n->peer);
        if (connect(n->peer, (struct sockaddr *)&n->joinAddr, sizeof n->joinAddr) == 0) { netConnected(n, 0); return; }
        if (!sock_in_progress()) { sock_close(n->peer); n->peer = SOCK_INVALID; n->failedTries++; }
        return;
    }
    {
        fd_set w, e;
        struct timeval tv = { 0, 0 };
        int err = 0;
        socklen_t errLen = sizeof err;

        FD_ZERO(&w); FD_ZERO(&e);
        FD_SET(n->peer, &w); FD_SET(n->peer, &e);
        if (select((int)n->peer + 1, NULL, &w, &e, &tv) > 0) {
            if (FD_ISSET(n->peer, &w) && !FD_ISSET(n->peer, &e)
             && getsockopt(n->peer, SOL_SOCKET, SO_ERROR, (char *)&err, &errLen) == 0 && err == 0) {
                netConnected(n, 0);
                return;
            }
        } else if (n->connectPolls < 900) {
            return; // still trying
        }
        // refused, unreachable or timed out: try again shortly
        sock_close(n->peer);
        n->peer = SOCK_INVALID;
        n->connectPolls = 0;
        n->failedTries++;
    }
}

#define OUTBUF_SIZE (8 * 1024 * 1024)

// Sends what it can of the waiting bytes; the rest goes on the next try.
static void netFlush(struct Net *n) {
    size_t sent = 0;
    while (n->connected && sent < n->outlen) {
        int r = send(n->peer, (const char *)n->outbuf + sent, (int)(n->outlen - sent), 0);
        if (r > 0) { sent += (size_t)r; continue; }
        if (r < 0 && sock_would_block()) break;
        n->connected = 0;
    }
    if (sent) {
        memmove(n->outbuf, n->outbuf + sent, n->outlen - sent);
        n->outlen -= sent;
    }
}

static void netSend(struct Net *n, int type, const void *payload, int len) {
    unsigned char *msg;
    if (!n->connected) return;
    if (!n->outbuf) {
        n->outbuf = malloc(OUTBUF_SIZE);
        if (!n->outbuf) { n->connected = 0; return; }
    }
    if (n->outlen + 4 + len > OUTBUF_SIZE) {
        // The partner hasn't read anything for minutes: let them go.
        n->connected = 0;
        return;
    }
    msg = n->outbuf + n->outlen;
    msg[0] = 'B'; msg[1] = 'B'; msg[2] = (unsigned char)type; msg[3] = (unsigned char)len;
    if (len) memcpy(msg + 4, payload, len);
    n->outlen += 4 + len;
    netFlush(n);
}

static void netPoll(struct Net *n) {
    if (n->isHost && !n->connected && n->listener != SOCK_INVALID) {
        sock_t s = accept(n->listener, NULL, NULL);
        if (s != SOCK_INVALID) {
            n->peer = s;
            sock_nonblock(s);
            netConnected(n, 1);
        }
    }
    if (!n->isHost && n->joining && !n->everConnected) netTryJoin(n);
    if (!n->connected) return;
    netFlush(n);

    // Read everything that has arrived, a buffer at a time (after a pause there can be
    // far more than one buffer's worth waiting).
    int more = 1;
    while (more && n->connected) {
    more = 0;
    for (;;) {
        if (n->inlen == sizeof n->inbuf) { more = 1; break; } // full: handle these first
        int got = recv(n->peer, (char *)n->inbuf + n->inlen, (int)(sizeof n->inbuf - n->inlen), 0);
        if (got > 0) { n->inlen += got; continue; }
        if (got == 0 || !sock_would_block()) { n->connected = 0; return; }
        break;
    }
    size_t pos = 0;
    while (n->inlen - pos >= 4) {
        unsigned char *m = n->inbuf + pos;
        size_t len = m[3];
        if (m[0] != 'B' || m[1] != 'B') { n->connected = 0; return; }
        if (n->inlen - pos < 4 + len) break;
        if (m[2] == MSG_HELLO && len >= 1) {
            if (m[4] != PROTOCOL_VERSION) { n->versionMismatch = 1; n->connected = 0; return; }
            if (!n->isHost && len >= 2 && m[5] >= 1 && m[5] <= 10) n->hostSpeed = m[5];
            {
                size_t nl = len > 2 ? len - 2 : 0;
                if (nl > sizeof n->partnerStartState - 1) nl = sizeof n->partnerStartState - 1;
                memcpy(n->partnerStartState, m + 6, nl);
                n->partnerStartState[nl] = 0;
                n->partnerHello = 1;
            }
        } else if (m[2] == MSG_STATE && len == STATE_SIZE) {
            memcpy(n->partnerState, m + 4, STATE_SIZE);
            n->havePartnerState = 1;
        } else if (m[2] == MSG_BLOCK) {
            lrReceive(&n->lr, m + 4, (int)len);
        } else if (m[2] >= MSG_WORLD_CHANGES && m[2] <= MSG_WORLD_REQUEST) {
            wsReceive(&n->ws, m[2], m + 4, (int)len);
        } else if (m[2] == MSG_SAVESTATE && len >= 2 && len < 2 + sizeof n->partnerSaveName) {
            n->partnerSaveKind = m[4];
            memcpy(n->partnerSaveName, m + 5, len - 1);
            n->partnerSaveName[len - 1] = 0;
        } else if (m[2] == MSG_TEXT && len >= 1) {
            size_t at = (size_t)m[4] * TEXT_CHUNK;
            if (at + len - 1 <= TEXT_SIZE)
                memcpy(n->partnerText + at, m + 5, len - 1);
        }
        pos += 4 + len;
    }
    memmove(n->inbuf, n->inbuf + pos, n->inlen - pos);
    n->inlen -= pos;
    }
}

// ------------------------------------------------------------------ mailbox

static void findMailbox(struct App *app) {
    if (app->mailbox) return;
    for (uint32_t a = EWRAM_START; a < EWRAM_END; a += 4) {
        if (app->core->busRead32(app->core, a) == COOP_MAGIC_0 && app->core->busRead32(app->core, a + 4) == COOP_MAGIC_1) {
            app->mailbox = a;
            return;
        }
    }
}

static void wsSendNet(void *ctx, int type, const void *payload, int len) {
    netSend((struct Net *)ctx, type, payload, len);
}

static void exchangeMailbox(struct App *app) {
    struct mCore *c = app->core;
    unsigned char local[STATE_SIZE];

    if (!app->mailbox) return;
    if (c->busRead32(c, app->mailbox) != COOP_MAGIC_0) {
        // The game was reset: the shared world starts over once it's back.
        app->mailbox = 0;
        wsInit(&app->net.ws, app->net.isHost);
        return;
    }
    for (int i = 0; i < STATE_SIZE; i++)
        local[i] = c->busRead8(c, app->mailbox + MB_LOCAL + i);
    // A new field message: send its text first, so it's there when the state says so.
    if (app->net.connected && app->net.sentMsgSeq != local[STATE_MSGSEQ]) {
        unsigned char text[TEXT_SIZE];
        int len = 0;
        while (len < TEXT_SIZE - 1) {
            text[len] = c->busRead8(c, app->mailbox + MB_LOCALTEXT + len);
            if (text[len++] == TEXT_EOS) break;
        }
        if (text[len - 1] != TEXT_EOS) text[len++] = TEXT_EOS;
        for (int chunk = 0; chunk * TEXT_CHUNK < len; chunk++) {
            unsigned char payload[1 + TEXT_CHUNK];
            int n = len - chunk * TEXT_CHUNK;
            if (n > TEXT_CHUNK) n = TEXT_CHUNK;
            payload[0] = (unsigned char)chunk;
            memcpy(payload + 1, text + chunk * TEXT_CHUNK, n);
            netSend(&app->net, MSG_TEXT, payload, 1 + n);
        }
        app->net.sentMsgSeq = local[STATE_MSGSEQ];
    }
    // Shared world: story flags and variables both ways (after the game ran this frame).
    wsTick(&app->net.ws, c, app->mailbox, app->net.connected, wsSendNet, &app->net);
    // Co-op battles: the game's link blocks.
    if (app->net.connected) lrTick(&app->net.lr, c, app->mailbox, wsSendNet, &app->net);
    netSend(&app->net, MSG_STATE, local, STATE_SIZE);

    c->busWrite8(c, app->mailbox + MB_INDEX, app->net.isHost ? 0 : 1);
    if (app->net.connected && app->net.havePartnerState) {
        for (int i = 0; i < TEXT_SIZE; i++) {
            c->busWrite8(c, app->mailbox + MB_PARTNERTEXT + i, app->net.partnerText[i]);
            if (app->net.partnerText[i] == TEXT_EOS) break;
        }
        for (int i = 0; i < STATE_SIZE; i++)
            c->busWrite8(c, app->mailbox + MB_PARTNER + i, app->net.partnerState[i]);
        c->busWrite8(c, app->mailbox + MB_ACTIVE, 1);
    } else {
        c->busWrite8(c, app->mailbox + MB_ACTIVE, 0);
    }
}

// ------------------------------------------------------------------ partner disconnected

// The game is only played with both players there: when the partner drops, the app
// closes. Anything since the host's last save is not kept.
// Tells the player something: in a message box (the Windows app has no console),
// and on the terminal.
static void showMessage(int error, const char *fmt, ...) {
    char text[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof text, fmt, ap);
    va_end(ap);
    fprintf(error ? stderr : stdout, "%s\n", text);
#ifdef _WIN32
    SDL_ShowSimpleMessageBox(error ? SDL_MESSAGEBOX_ERROR : SDL_MESSAGEBOX_INFORMATION, "Brainy Blue Co-op", text, NULL);
#endif
}

static void partnerLeft(struct Net *n) {
    if (n->versionMismatch)
        showMessage(1, "Your partner is using a different version of the co-op app.\n"
                       "Both players need the same version (and the same ROM build).");
    else
        showMessage(0, "Your partner disconnected, so the game will close.\n"
                       "Progress since the host last saved is not kept.");
}

// ------------------------------------------------------------------ input

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

// Keyboard controls (SDL scancodes), in the order of the launcher's Controls window:
// Up, Down, Left, Right, A, B, Start, Select, L, R, save state.
enum { KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_A, KEY_B, KEY_START, KEY_SELECT, KEY_L, KEY_R, KEY_SAVESTATE, KEY_COUNT };
static int sKeys[KEY_COUNT] = {
    SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
    SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_RETURN, SDL_SCANCODE_BACKSPACE,
    SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_F5,
};

static int readInput(SDL_GameController *pad) {
    static const int gbaKey[KEY_SAVESTATE] = {
        GBA_KEY_UP, GBA_KEY_DOWN, GBA_KEY_LEFT, GBA_KEY_RIGHT, GBA_KEY_A, GBA_KEY_B,
        GBA_KEY_START, GBA_KEY_SELECT, GBA_KEY_L, GBA_KEY_R,
    };
    const Uint8 *ks = SDL_GetKeyboardState(NULL);
    int k = 0;
    for (int i = 0; i < KEY_SAVESTATE; i++)
        if (sKeys[i] > 0 && sKeys[i] < SDL_NUM_SCANCODES && ks[sKeys[i]]) k |= 1 << gbaKey[i];
    if (pad) {
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_A)) k |= 1 << GBA_KEY_A;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_B)) k |= 1 << GBA_KEY_B;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_START)) k |= 1 << GBA_KEY_START;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_BACK)) k |= 1 << GBA_KEY_SELECT;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_UP)) k |= 1 << GBA_KEY_UP;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN)) k |= 1 << GBA_KEY_DOWN;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT)) k |= 1 << GBA_KEY_LEFT;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) k |= 1 << GBA_KEY_RIGHT;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) k |= 1 << GBA_KEY_L;
        if (SDL_GameControllerGetButton(pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) k |= 1 << GBA_KEY_R;
    }
    return k;
}

// ------------------------------------------------------------------ screenshots (test mode)

static void writeShot(struct App *app, const char *dir, int n) {
    char path[1024];
    snprintf(path, sizeof path, "%s/shot_%03d.ppm", dir, n);
    FILE *o = fopen(path, "wb");
    if (!o) return;
    fprintf(o, "P6\n240 160\n255\n");
    for (unsigned i = 0; i < 240 * 160; i++) {
        mColor c = app->video[(i / 240) * 256 + (i % 240)];
        unsigned char px[3] = { c & 0xFF, (c >> 8) & 0xFF, (c >> 16) & 0xFF };
        fwrite(px, 1, 3, o);
    }
    fclose(o);
}

// ------------------------------------------------------------------ main

static void usage(void) {
    fprintf(stderr,
        "Brainy Blue Co-op\n"
        "  bbcoop host [--port N] [--rom file.gba] [--speed N]\n"
        "  bbcoop join <address> [--port N] [--rom file.gba] [--speed N]\n"
        "  (the host's --speed is used by both games; 3x unless it says otherwise, 1 = normal)\n"
        "  --state savestates/auto/<name>.ss starts from a save state (both players pick the same one)\n"
        "  F5 in the game makes a save state for both players; one is made every 5 minutes too\n");
}

static void stepFrame(struct App *app) {
    app->core->runFrame(app->core);
    netPoll(&app->net);
    findMailbox(app);
    exchangeMailbox(app);
    // How long since each player's game last wrote its state (it stops in battles
    // and menus that leave the overworld).
    if (app->mailbox) {
        uint16_t seq = (uint16_t)app->core->busRead16(app->core, app->mailbox + MB_LOCAL);
        app->localIdle = seq != app->lastLocalSeq ? 0 : app->localIdle + 1;
        app->lastLocalSeq = seq;
    } else {
        app->localIdle = 1000;
    }
    if (app->net.havePartnerState) {
        uint16_t seq = (uint16_t)(app->net.partnerState[0] | app->net.partnerState[1] << 8);
        app->partnerIdle = seq != app->lastPartnerSeq ? 0 : app->partnerIdle + 1;
        app->lastPartnerSeq = seq;
    } else {
        app->partnerIdle = 1000;
    }
}

// ------------------------------------------------------------------ save states
//
// Save states are a fail safe on top of the game's own save. Every 5 minutes the
// host's app has both games make one at the same moment (savestates/auto, the newest
// 10 are kept), and either player can make one by hand (savestates/manual) with the
// save state key. Both players then pick the same one in the launcher to go back to it.

#define STATE_FIELD_FREE 0x20 // byte 0x0B of the state: walking around freely
static int sAutoStateSeconds = 5 * 60; // --autostate-seconds (for testing)
#define AUTO_STATE_SECONDS sAutoStateSeconds
#define AUTO_STATES_KEPT 10

static void setNotice(struct App *app, const char *text) {
    snprintf(app->notice, sizeof app->notice, "%s", text);
    app->noticeUntil = SDL_GetTicks() + 4000;
    app->noticeChanged = 1;
    printf("%s\n", text);
}

static void makeDir(const char *path) {
#ifdef _WIN32
    WCHAR w[1024];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, w, 1024)) CreateDirectoryW(w, NULL);
#else
    mkdir(path, 0777);
#endif
}

// savestates/ next to the ROM
static void initStateDir(struct App *app) {
    char *slash;
    snprintf(app->stateDir, sizeof app->stateDir, "%s", app->romPath);
    slash = strrchr(app->stateDir, '/');
    {
        char *bs = strrchr(app->stateDir, '\\');
        if (!slash || (bs && bs > slash)) slash = bs;
    }
    if (slash) slash[1] = 0;
    else app->stateDir[0] = 0;
    strncat(app->stateDir, "savestates", sizeof app->stateDir - strlen(app->stateDir) - 1);
}

static int compareNames(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

// Keeps the newest AUTO_STATES_KEPT automatic save states.
static void pruneAutoStates(struct App *app) {
    char dirPath[1100];
    static char names[256][64];
    int count = 0;
    struct VDir *dir;
    struct VDirEntry *e;

    snprintf(dirPath, sizeof dirPath, "%s/auto", app->stateDir);
    dir = VDirOpen(dirPath);
    if (!dir) return;
    while ((e = dir->listNext(dir)) && count < 256) {
        const char *name = e->name(e);
        size_t len = strlen(name);
        if (len > 3 && len < 64 && !strcmp(name + len - 3, ".ss") && e->type(e) != VFS_DIRECTORY)
            snprintf(names[count++], 64, "%s", name);
    }
    qsort(names, count, 64, compareNames); // names are timestamps: oldest first
    for (int i = 0; i < count - AUTO_STATES_KEPT; i++) dir->deleteFile(dir, names[i]);
    dir->close(dir);
}

// Makes the save state <kind>/<name>.ss for this player's game.
static int saveState(struct App *app, int kind, const char *name) {
    char path[1200];
    struct VFile *vf;
    int ok;

    makeDir(app->stateDir);
    snprintf(path, sizeof path, "%s/%s", app->stateDir, kind ? "manual" : "auto");
    makeDir(path);
    snprintf(path, sizeof path, "%s/%s/%s.ss", app->stateDir, kind ? "manual" : "auto", name);
    vf = VFileOpen(path, O_CREAT | O_TRUNC | O_RDWR);
    if (!vf) return 0;
    ok = mCoreSaveStateNamed(app->core, vf, SAVESTATE_SAVEDATA);
    vf->close(vf);
    if (!kind) pruneAutoStates(app);
    return ok;
}

static int bothPlayersFree(struct App *app) {
    if (!app->net.connected || !app->mailbox || !app->net.havePartnerState) return 0;
    if (app->localIdle > 2 || app->partnerIdle > 30) return 0;
    if (!(app->core->busRead8(app->core, app->mailbox + MB_LOCAL + 0x0B) & STATE_FIELD_FREE)) return 0;
    if (!(app->net.partnerState[0x0B] & STATE_FIELD_FREE)) return 0;
    return 1;
}

// Both games make a save state now: this one, and the partner's on its message.
static void makeSharedState(struct App *app, int kind) {
    unsigned char msg[1 + 64];
    char name[64];
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);

    strftime(name, sizeof name, "%Y-%m-%d_%H-%M-%S", tm);
    msg[0] = (unsigned char)kind;
    memcpy(msg + 1, name, strlen(name));
    netSend(&app->net, MSG_SAVESTATE, msg, 1 + (int)strlen(name));
    if (saveState(app, kind, name)) {
        char text[96];
        snprintf(text, sizeof text, "%s save state made for both players", kind ? "Manual" : "Automatic");
        setNotice(app, text);
    } else {
        setNotice(app, "Couldn't write the save state (is the folder read-only?)");
    }
}

// Once a frame: the timer, a manual request, and the partner's save states.
static void updateSaveStates(struct App *app, uint32_t *autoDue) {
    if (app->net.partnerSaveKind >= 0 && app->net.partnerSaveName[0]) {
        int kind = app->net.partnerSaveKind;
        if (saveState(app, kind, app->net.partnerSaveName))
            setNotice(app, kind ? "Your partner made a manual save state for both players" : "Automatic save state made for both players");
        app->net.partnerSaveKind = -1;
        app->net.partnerSaveName[0] = 0;
        if (!kind) app->autoPending = 0;
    }
    if (!app->net.connected || app->waitStateMatch) return;
    // only the host keeps time for the automatic ones
    if (app->net.isHost && (int32_t)(SDL_GetTicks() - *autoDue) >= 0) {
        app->autoPending = 1;
        *autoDue = SDL_GetTicks() + AUTO_STATE_SECONDS * 1000;
    }
    if ((app->manualPending || app->autoPending) && bothPlayersFree(app)) {
        if (app->manualPending) makeSharedState(app, 1);
        else makeSharedState(app, 0);
        app->manualPending = 0;
        app->autoPending = 0;
    }
}

// Started from a save state: run only once the partner has picked the same one.
// Returns 0 if they picked something else (the app then closes).
static int checkStateMatch(struct App *app) {
    if (!app->net.partnerHello) return 1;
    if (strcmp(app->net.partnerStartState, app->net.startState)) {
        const char *mine = app->net.startState[0] ? app->net.startState : "the normal save";
        const char *theirs = app->net.partnerStartState[0] ? app->net.partnerStartState : "the normal save";
        showMessage(1, "You and your partner picked different starting points:\n\n"
                       "You: %s\nYour partner: %s\n\nBoth players need to pick the same one in the launcher.", mine, theirs);
        return 0;
    }
    app->waitStateMatch = 0;
    return 1;
}

// Loads savestates/<name>.ss into the game (name is "auto/..." or "manual/...").
static int loadStartState(struct App *app, const char *path) {
    struct VFile *vf = VFileOpen(path, O_RDONLY);
    int ok;
    if (!vf) return 0;
    ok = mCoreLoadStateNamed(app->core, vf, SAVESTATE_SAVEDATA);
    vf->close(vf);
    return ok;
}

int main(int argc, char **argv) {
    struct App app;
    const char *mode = NULL, *address = NULL, *script = NULL, *shots = NULL, *saveArg = NULL, *statePath = NULL;
    int port = DEFAULT_PORT;
    int speed = 3; // game speed: 3x by default (--speed N, set by the launcher)

    memset(&app, 0, sizeof app);
    app.romPath = "pokefirered.gba";
    app.net.listener = SOCK_INVALID;
    app.net.peer = SOCK_INVALID;
    app.net.partnerSaveKind = -1;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "host") || !strcmp(argv[i], "join")) mode = argv[i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--speed") && i + 1 < argc) speed = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rom") && i + 1 < argc) app.romPath = argv[++i];
        else if (!strcmp(argv[i], "--save") && i + 1 < argc) saveArg = argv[++i];
        else if (!strcmp(argv[i], "--state") && i + 1 < argc) statePath = argv[++i];
        else if (!strcmp(argv[i], "--autostate-seconds") && i + 1 < argc) sAutoStateSeconds = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--headless")) app.headless = 1;
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) script = argv[++i];
        else if (!strcmp(argv[i], "--shots") && i + 1 < argc) shots = argv[++i];
        else if (mode && !strcmp(mode, "join") && !address) address = argv[i];
        else { usage(); return 1; }
    }
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    // Started without options (double-clicked): the launcher window asks how to play.
    static struct LaunchOptions launch;
    if (argc == 1) {
        if (!launcherRun(&launch)) return 0;
        mode = launch.host ? "host" : "join";
        address = launch.address;
        port = launch.port;
        speed = launch.speed;
        app.romPath = launch.rom;
        if (launch.state[0]) statePath = launch.state;
        for (int i = 0; i < KEY_COUNT; i++)
            if (launch.keys[i] > 0) sKeys[i] = launch.keys[i];
    }
#endif
    if (!mode || (!strcmp(mode, "join") && !address)) { usage(); return 1; }
    if (speed < 1) speed = 1;
    if (speed > 10) speed = 10;
    app.net.speed = speed;

    // --- emulator core
    mLogSetDefaultLogger(&sQuiet);
    app.core = GBACoreCreate();
    if (!app.core || !app.core->init(app.core)) { showMessage(1, "Could not start the emulator core."); return 1; }
    mCoreInitConfig(app.core, "bbcoop");
    if (!mCoreLoadFile(app.core, app.romPath)) { showMessage(1, "Could not load the ROM:\n%s", app.romPath); return 1; }
    app.video = calloc(256 * 256, sizeof(mColor));
    app.core->setVideoBuffer(app.core, app.video, 256);
    {
        char savePath[1024];
        snprintf(savePath, sizeof savePath, "%s", saveArg ? saveArg : app.romPath);
        if (!saveArg) {
            char *dot = strrchr(savePath, '.');
            if (dot) strcpy(dot, ".sav"); else strcat(savePath, ".sav");
        }
        app.core->loadSave(app.core, VFileOpen(savePath, O_CREAT | O_RDWR));
    }
    app.core->setAudioBufferSize(app.core, 1024);
    app.core->reset(app.core);
    initStateDir(&app);
    if (statePath) {
        // The partner's launcher lists the same kind/name ("auto/2026-11-06_21-30-00").
        char dir[1100];
        const char *base = statePath + strlen(statePath);
        while (base > statePath && base[-1] != '/' && base[-1] != '\\') base--;
        snprintf(dir, sizeof dir, "%.*s", (int)(base - statePath), statePath);
        snprintf(app.net.startState, sizeof app.net.startState, "%s/%.*s",
                 strstr(dir, "manual") ? "manual" : "auto",
                 (int)(strlen(base) > 3 && !strcmp(base + strlen(base) - 3, ".ss") ? strlen(base) - 3 : strlen(base)), base);
        if (!loadStartState(&app, statePath)) { showMessage(1, "Could not load the save state:\n%s", statePath); return 1; }
        app.waitStateMatch = 1;
    }

    // --- network
    if (!strcmp(mode, "host")) {
        if (!netStartHost(&app.net, port)) {
            showMessage(1, "Could not open port %d for your partner.\nIs another copy of the co-op app already running?", port);
            return 1;
        }
    } else if (!netJoin(&app.net, address, port)) {
        showMessage(1, "\"%s\" is not an address.\nUse the IP address shown in the host's launcher (for example 10.147.17.5).", address);
        return 1;
    }

    // --- test mode: scripted input, no window or sound
    if (app.headless) {
        FILE *f = script ? fopen(script, "r") : NULL;
        char line[256];
        int n = 0;
        if (!f) { fprintf(stderr, "--headless needs --script\n"); return 1; }
        while (fgets(line, sizeof line, f)) {
            int count; char keys[64] = "-", extra[64] = "";
            if (line[0] == '#' || line[0] == '\n') continue;
            if (!strncmp(line, "savestate", 9)) { app.manualPending = 1; continue; }
            if (!strncmp(line, "poke ", 5)) { // test only: poke <hex addr> <value>
                unsigned addr, val;
                if (sscanf(line + 5, "%x %u", &addr, &val) == 2) app.core->busWrite8(app.core, addr, (uint8_t)val);
                continue;
            }
            if (!strncmp(line, "waitpartner", 11)) {
                while (!app.net.connected) { netPoll(&app.net); SDL_Delay(5); }
                while (!app.net.partnerHello) { netPoll(&app.net); SDL_Delay(5); }
                if (!checkStateMatch(&app)) return 1;
                printf("starting from %s\n", app.net.startState[0] ? app.net.startState : "the normal save");
                continue;
            }
            if (sscanf(line, "%d %63s %63s", &count, keys, extra) < 1) continue;
            app.core->setKeys(app.core, parseKeys(keys));
            for (int i = 0; i < count; i++) {
                static uint32_t autoDue = 0;
                if (!autoDue) autoDue = SDL_GetTicks() + AUTO_STATE_SECONDS * 1000;
                stepFrame(&app);
                updateSaveStates(&app, &autoDue);
                SDL_Delay(1); // let the partner's packets arrive, like real play
                if (app.net.everConnected && !app.net.connected) { partnerLeft(&app.net); return 0; }
            }
            if (shots && !strcmp(extra, "shot")) { writeShot(&app, shots, n++); printf("shot %d\n", n - 1); }
            if (!app.net.isHost && app.net.hostSpeed && speed != app.net.hostSpeed) {
                speed = app.net.hostSpeed;
                printf("host's speed: %dx\n", speed);
            }
        }
        return 0;
    }

    // --- window, sound, controller
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        showMessage(1, "Could not open the game window: %s", SDL_GetError());
        return 1;
    }
    int winW = 240 * 3, winH = 160 * 3;
#ifdef _WIN32
    winW = launcherScale(winW);
    winH = launcherScale(winH);
#endif
    SDL_Window *win = SDL_CreateWindow(app.net.isHost ? "Brainy Blue Co-op - Host" : "Brainy Blue Co-op - Guest", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       winW, winH, SDL_WINDOW_RESIZABLE);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, 0);
    SDL_RenderSetLogicalSize(ren, 240, 160);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, 240, 160);

    SDL_AudioSpec want, have;
    memset(&want, 0, sizeof want);
    want.freq = 48000;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio) SDL_PauseAudioDevice(audio, 0);
    app.core->setAudioBufferSize(app.core, 2048);

    SDL_GameController *pad = NULL;
    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); break; }

    // GBA runs at ~59.73 fps; sound is resampled by speed: we play at the core's rate
    // and let SDL convert.
    unsigned coreRate = app.core->audioSampleRate(app.core);
    int curSpeed = speed; // the guest switches to the host's speed when it connects
    // Sped up, the sound plays faster too (as in other emulators' fast-forward).
    SDL_AudioStream *stream = audio ? SDL_NewAudioStream(AUDIO_S16SYS, 2, (int)coreRate * curSpeed, have.format, have.channels, have.freq) : NULL;
    int16_t samples[4096];
    Uint64 freq = SDL_GetPerformanceFrequency(), next = SDL_GetPerformanceCounter();
    const double frameTime = 280896.0 / 16777216.0; // one GBA frame in seconds
    int running = 1, lastConnected = -1;
    uint32_t autoDue = SDL_GetTicks() + AUTO_STATE_SECONDS * 1000;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_CONTROLLERDEVICEADDED && !pad) pad = SDL_GameControllerOpen(ev.cdevice.which);
            if (ev.type == SDL_KEYDOWN && !ev.key.repeat && (int)ev.key.keysym.scancode == sKeys[KEY_SAVESTATE]) {
                if (!app.net.connected || app.waitStateMatch) setNotice(&app, "Save states need both players connected");
                else if (!app.manualPending) {
                    app.manualPending = 1;
                    if (!bothPlayersFree(&app)) setNotice(&app, "Save state: made once both players are walking around");
                }
            }
        }

        // Started from a save state: wait for the partner to pick the same one.
        if (app.waitStateMatch) {
            netPoll(&app.net);
            if (app.net.everConnected && !app.net.connected) { partnerLeft(&app.net); break; }
            if (!checkStateMatch(&app)) break;
            if (app.waitStateMatch) {
                if (lastConnected != 100) {
                    char title[200];
                    lastConnected = 100;
                    snprintf(title, sizeof title, "Brainy Blue Co-op - %s (waiting for your partner to load save state %s)",
                             app.net.isHost ? "Host" : "Guest", app.net.startState);
                    SDL_SetWindowTitle(win, title);
                }
                SDL_RenderClear(ren);
                SDL_RenderPresent(ren);
                SDL_Delay(16);
                next = SDL_GetPerformanceCounter();
                continue;
            }
            autoDue = SDL_GetTicks() + AUTO_STATE_SECONDS * 1000;
        }

        if (!app.net.isHost && app.net.hostSpeed && app.net.hostSpeed != curSpeed) {
            curSpeed = app.net.hostSpeed; // the host picks the speed for both games
            if (stream) {
                SDL_FreeAudioStream(stream);
                stream = SDL_NewAudioStream(AUDIO_S16SYS, 2, (int)coreRate * curSpeed, have.format, have.channels, have.freq);
            }
            lastConnected = -1; // update the title
        }
        for (int f = 0; f < curSpeed && running; f++) {
            app.core->setKeys(app.core, readInput(pad));
            stepFrame(&app);
            if (app.net.everConnected && !app.net.connected) break;
        }
        if (app.net.everConnected && !app.net.connected) { partnerLeft(&app.net); break; }
        // In normal mode the partner's hello can still say they picked a save state.
        if (!checkStateMatch(&app)) break;
        updateSaveStates(&app, &autoDue);
        if (!app.net.connected) autoDue = SDL_GetTicks() + AUTO_STATE_SECONDS * 1000;
        {
            // Window title says what's going on, so a missing partner is easy to explain.
            int status;
            if (!app.net.connected) status = app.net.isHost ? 0 : app.net.failedTries < 3 ? 5 : 6; // no connection yet
            else if (!app.mailbox) status = 1;                                    // not in the overworld yet (or not a co-op ROM)
            else if (!app.net.havePartnerState) status = 2;                       // partner not in the overworld yet
            else {
                struct mCore *c = app.core;
                int sameMap = c->busRead8(c, app.mailbox + MB_LOCAL + 2) == app.net.partnerState[2]
                           && c->busRead8(c, app.mailbox + MB_LOCAL + 3) == app.net.partnerState[3];
                status = sameMap ? 4 : 3;
            }
            if (app.notice[0] && (int32_t)(SDL_GetTicks() - app.noticeUntil) >= 0) { app.notice[0] = 0; app.noticeChanged = 1; }
            if (status != lastConnected || app.noticeChanged) {
                static const char *const titles[] = {
                    "waiting for partner to connect",
                    "connected; start or load your game",
                    "connected; partner is still loading",
                    "connected; partner is on another map",
                    "connected; partner is on this map",
                    "connecting to the host...",
                    "can't reach the host yet - check the IP, ZeroTier and the host's firewall",
                };
                char title[300];
                lastConnected = status;
                app.noticeChanged = 0;
                snprintf(title, sizeof title, "Brainy Blue Co-op - %s (%s) - %dx speed", app.net.isHost ? "Host" : "Guest",
                         app.notice[0] ? app.notice : titles[status], curSpeed);
                SDL_SetWindowTitle(win, title);
            }
        }

        // sound
        struct mAudioBuffer *buf = app.core->getAudioBuffer(app.core);
        size_t avail = mAudioBufferAvailable(buf);
        while (avail > 0) {
            size_t chunk = avail > 2048 ? 2048 : avail;
            size_t got = mAudioBufferRead(buf, samples, chunk);
            if (stream) SDL_AudioStreamPut(stream, samples, (int)(got * 2 * sizeof(int16_t)));
            avail -= got;
            if (!got) break;
        }
        if (stream) {
            int ready = SDL_AudioStreamAvailable(stream);
            if (ready > 0) {
                static unsigned char out[65536];
                int len = SDL_AudioStreamGet(stream, out, ready > (int)sizeof out ? (int)sizeof out : ready);
                if (len > 0 && SDL_GetQueuedAudioSize(audio) < (Uint32)(have.freq * 4 / 5)) // < 200 ms queued
                    SDL_QueueAudio(audio, out, len);
            }
        }

        // picture
        SDL_UpdateTexture(tex, NULL, app.video, 256 * sizeof(mColor));
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);

        // keep to GBA speed
        next += (Uint64)(frameTime * freq);
        Uint64 now = SDL_GetPerformanceCounter();
        if (now < next) SDL_Delay((Uint32)((next - now) * 1000 / freq));
        else if (now - next > freq / 4) next = now; // fell far behind; don't try to catch up
    }

    if (app.net.peer != SOCK_INVALID) sock_close(app.net.peer);
    if (app.net.listener != SOCK_INVALID) sock_close(app.net.listener);
    SDL_Quit();
    return 0;
}
