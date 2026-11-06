// Brainy Blue Co-op app.
//
// Runs the Brainy Blue ROM on the mGBA core in a window, with sound and
// keyboard/controller input, and connects to one partner over TCP. Every frame it
// sends this game's co-op mailbox (`local` block, see include/coop.h) to the
// partner and writes the partner's block into this game's mailbox.
//
// Usage:
//   bbcoop host [--port 27450] [--rom pokefirered.gba]
//   bbcoop join <host address> [--port 27450] [--rom pokefirered.gba]
//   --save <file> picks the save file (default: the ROM's name with .sav)
//
// Test options (no window or sound, used for automated tests):
//   --headless --script <file> --shots <dir>
//   script lines: "<frames> <keys> [shot]"  keys: A B S E U D l r L R or '-'
//
// Keyboard: arrows, Z = A, X = B, Enter = Start, Backspace = Select, A = L, S = R,
//           Escape = quit. A game controller works too.
//
// If the partner disconnects, the app saves a save state next to the ROM
// (<rom>.coop.ss) and closes, so the game can be picked up from that moment.

#include <mgba/core/core.h>
#include <mgba/core/config.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#include <mgba/gba/core.h>
#include <mgba/gba/interface.h>
#include <mgba/internal/gba/input.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/vfs.h>

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
static void sock_nonblock(sock_t s) { u_long on = 1; ioctlsocket(s, FIONBIO, &on); }
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define SOCK_INVALID (-1)
#define sock_close close
static int sock_would_block(void) { return errno == EAGAIN || errno == EWOULDBLOCK; }
static void sock_nonblock(sock_t s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
#endif

// ------------------------------------------------------------------ mailbox layout
// Must match struct CoopMailbox in include/coop.h.
#define COOP_MAGIC_0 0x4F434242u
#define COOP_MAGIC_1 0x3130504Fu
#define MB_ACTIVE 8
#define MB_INDEX 9
#define MB_LOCAL 12
#define MB_PARTNER 24
#define STATE_SIZE 12

#define EWRAM_START 0x02000000u
#define EWRAM_END 0x02040000u

// ------------------------------------------------------------------ network messages
// Every message: 'B' 'B' type len, then len bytes.
#define MSG_HELLO 1 // payload: protocol version
#define MSG_STATE 2 // payload: STATE_SIZE bytes of the sender's local block
#define PROTOCOL_VERSION 1
#define DEFAULT_PORT 27450

struct Net {
    sock_t listener;
    sock_t peer;
    int isHost;
    int connected;
    int everConnected;
    unsigned char inbuf[4096];
    size_t inlen;
    unsigned char partnerState[STATE_SIZE];
    int havePartnerState;
};

struct App {
    struct mCore *core;
    mColor *video;
    uint32_t mailbox;
    const char *romPath;
    int headless;
    struct Net net;
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

static int netJoin(struct Net *n, const char *host, int port) {
    struct addrinfo hints, *res = NULL;
    char portStr[16];
    int one = 1;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    snprintf(portStr, sizeof portStr, "%d", port);
    if (getaddrinfo(host, portStr, &hints, &res) != 0 || !res) return 0;
    n->peer = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (n->peer == SOCK_INVALID) { freeaddrinfo(res); return 0; }
    printf("Connecting to %s:%d...\n", host, port);
    if (connect(n->peer, res->ai_addr, (int)res->ai_addrlen) != 0) { freeaddrinfo(res); return 0; }
    freeaddrinfo(res);
    setsockopt(n->peer, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
    sock_nonblock(n->peer);
    n->connected = 1;
    n->everConnected = 1;
    n->isHost = 0;
    printf("Connected to your partner.\n");
    return 1;
}

static void netSend(struct Net *n, int type, const void *payload, int len) {
    unsigned char msg[4 + 255];
    if (!n->connected) return;
    msg[0] = 'B'; msg[1] = 'B'; msg[2] = (unsigned char)type; msg[3] = (unsigned char)len;
    memcpy(msg + 4, payload, len);
    if (send(n->peer, (const char *)msg, 4 + len, 0) < 0 && !sock_would_block())
        n->connected = 0;
}

static void netPoll(struct Net *n) {
    if (n->isHost && !n->connected && n->listener != SOCK_INVALID) {
        sock_t s = accept(n->listener, NULL, NULL);
        if (s != SOCK_INVALID) {
            int one = 1;
            n->peer = s;
            setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&one, sizeof one);
            sock_nonblock(s);
            n->connected = 1;
            n->everConnected = 1;
            printf("Your partner joined.\n");
            unsigned char v = PROTOCOL_VERSION;
            netSend(n, MSG_HELLO, &v, 1);
        }
    }
    if (!n->connected) return;

    for (;;) {
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
        if (m[2] == MSG_STATE && len == STATE_SIZE) {
            memcpy(n->partnerState, m + 4, STATE_SIZE);
            n->havePartnerState = 1;
        }
        pos += 4 + len;
    }
    memmove(n->inbuf, n->inbuf + pos, n->inlen - pos);
    n->inlen -= pos;
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

static void exchangeMailbox(struct App *app) {
    struct mCore *c = app->core;
    unsigned char local[STATE_SIZE];

    if (!app->mailbox) return;
    if (c->busRead32(c, app->mailbox) != COOP_MAGIC_0) { app->mailbox = 0; return; } // game reset
    for (int i = 0; i < STATE_SIZE; i++)
        local[i] = c->busRead8(c, app->mailbox + MB_LOCAL + i);
    netSend(&app->net, MSG_STATE, local, STATE_SIZE);

    c->busWrite8(c, app->mailbox + MB_INDEX, app->net.isHost ? 0 : 1);
    if (app->net.connected && app->net.havePartnerState) {
        for (int i = 0; i < STATE_SIZE; i++)
            c->busWrite8(c, app->mailbox + MB_PARTNER + i, app->net.partnerState[i]);
        c->busWrite8(c, app->mailbox + MB_ACTIVE, 1);
    } else {
        c->busWrite8(c, app->mailbox + MB_ACTIVE, 0);
    }
}

// ------------------------------------------------------------------ save state on disconnect

static void saveStateAndQuit(struct App *app) {
    char path[1024];
    snprintf(path, sizeof path, "%s.coop.ss", app->romPath);
    struct VFile *vf = VFileOpen(path, O_CREAT | O_TRUNC | O_RDWR);
    if (vf) {
        mCoreSaveStateNamed(app->core, vf, SAVESTATE_SAVEDATA | SAVESTATE_RTC);
        vf->close(vf);
    }
    printf("Your partner disconnected. The game was saved to %s and will close.\n", path);
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

static int readInput(SDL_GameController *pad) {
    const Uint8 *ks = SDL_GetKeyboardState(NULL);
    int k = 0;
    if (ks[SDL_SCANCODE_Z]) k |= 1 << GBA_KEY_A;
    if (ks[SDL_SCANCODE_X]) k |= 1 << GBA_KEY_B;
    if (ks[SDL_SCANCODE_RETURN]) k |= 1 << GBA_KEY_START;
    if (ks[SDL_SCANCODE_BACKSPACE]) k |= 1 << GBA_KEY_SELECT;
    if (ks[SDL_SCANCODE_UP]) k |= 1 << GBA_KEY_UP;
    if (ks[SDL_SCANCODE_DOWN]) k |= 1 << GBA_KEY_DOWN;
    if (ks[SDL_SCANCODE_LEFT]) k |= 1 << GBA_KEY_LEFT;
    if (ks[SDL_SCANCODE_RIGHT]) k |= 1 << GBA_KEY_RIGHT;
    if (ks[SDL_SCANCODE_A]) k |= 1 << GBA_KEY_L;
    if (ks[SDL_SCANCODE_S]) k |= 1 << GBA_KEY_R;
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
        "  bbcoop host [--port N] [--rom file.gba]\n"
        "  bbcoop join <address> [--port N] [--rom file.gba]\n");
}

static void stepFrame(struct App *app) {
    app->core->runFrame(app->core);
    netPoll(&app->net);
    findMailbox(app);
    exchangeMailbox(app);
}

int main(int argc, char **argv) {
    struct App app;
    const char *mode = NULL, *address = NULL, *script = NULL, *shots = NULL, *saveArg = NULL;
    int port = DEFAULT_PORT;

    memset(&app, 0, sizeof app);
    app.romPath = "pokefirered.gba";
    app.net.listener = SOCK_INVALID;
    app.net.peer = SOCK_INVALID;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "host") || !strcmp(argv[i], "join")) mode = argv[i];
        else if (!strcmp(argv[i], "--port") && i + 1 < argc) port = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rom") && i + 1 < argc) app.romPath = argv[++i];
        else if (!strcmp(argv[i], "--save") && i + 1 < argc) saveArg = argv[++i];
        else if (!strcmp(argv[i], "--headless")) app.headless = 1;
        else if (!strcmp(argv[i], "--script") && i + 1 < argc) script = argv[++i];
        else if (!strcmp(argv[i], "--shots") && i + 1 < argc) shots = argv[++i];
        else if (mode && !strcmp(mode, "join") && !address) address = argv[i];
        else { usage(); return 1; }
    }
    if (!mode || (!strcmp(mode, "join") && !address)) { usage(); return 1; }

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    // --- emulator core
    mLogSetDefaultLogger(&sQuiet);
    app.core = GBACoreCreate();
    if (!app.core || !app.core->init(app.core)) { fprintf(stderr, "Could not start the emulator core.\n"); return 1; }
    mCoreInitConfig(app.core, "bbcoop");
    if (!mCoreLoadFile(app.core, app.romPath)) { fprintf(stderr, "Could not load the ROM: %s\n", app.romPath); return 1; }
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

    // --- network
    if (!strcmp(mode, "host")) {
        if (!netStartHost(&app.net, port)) { fprintf(stderr, "Could not open port %d.\n", port); return 1; }
    } else if (!netJoin(&app.net, address, port)) {
        fprintf(stderr, "Could not connect to %s:%d.\n", address, port);
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
            if (!strncmp(line, "waitpartner", 11)) {
                while (!app.net.connected) { netPoll(&app.net); SDL_Delay(5); }
                continue;
            }
            if (sscanf(line, "%d %63s %63s", &count, keys, extra) < 1) continue;
            app.core->setKeys(app.core, parseKeys(keys));
            for (int i = 0; i < count; i++) {
                stepFrame(&app);
                SDL_Delay(1); // let the partner's packets arrive, like real play
                if (app.net.everConnected && !app.net.connected) { saveStateAndQuit(&app); return 0; }
            }
            if (shots && !strcmp(extra, "shot")) { writeShot(&app, shots, n++); printf("shot %d\n", n - 1); }
        }
        return 0;
    }

    // --- window, sound, controller
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow("Pokemon Brainy Blue - Co-op", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       240 * 3, 160 * 3, SDL_WINDOW_RESIZABLE);
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
    SDL_AudioStream *stream = audio ? SDL_NewAudioStream(AUDIO_S16SYS, 2, (int)coreRate, have.format, have.channels, have.freq) : NULL;
    int16_t samples[4096];
    Uint64 freq = SDL_GetPerformanceFrequency(), next = SDL_GetPerformanceCounter();
    const double frameTime = 280896.0 / 16777216.0; // one GBA frame in seconds
    int running = 1, lastConnected = -1;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.scancode == SDL_SCANCODE_ESCAPE) running = 0;
            if (ev.type == SDL_CONTROLLERDEVICEADDED && !pad) pad = SDL_GameControllerOpen(ev.cdevice.which);
        }

        app.core->setKeys(app.core, readInput(pad));
        stepFrame(&app);
        if (app.net.everConnected && !app.net.connected) { saveStateAndQuit(&app); break; }
        {
            // Window title says what's going on, so a missing partner is easy to explain.
            int status;
            if (!app.net.connected) status = 0;                                   // no connection yet
            else if (!app.mailbox) status = 1;                                    // not in the overworld yet (or not a co-op ROM)
            else if (!app.net.havePartnerState) status = 2;                       // partner not in the overworld yet
            else {
                struct mCore *c = app.core;
                int sameMap = c->busRead8(c, app.mailbox + MB_LOCAL + 2) == app.net.partnerState[2]
                           && c->busRead8(c, app.mailbox + MB_LOCAL + 3) == app.net.partnerState[3];
                status = sameMap ? 4 : 3;
            }
            if (status != lastConnected) {
                static const char *const titles[] = {
                    "Pokemon Brainy Blue - Co-op (waiting for partner to connect)",
                    "Pokemon Brainy Blue - Co-op (connected; start or load your game)",
                    "Pokemon Brainy Blue - Co-op (connected; partner is still loading)",
                    "Pokemon Brainy Blue - Co-op (connected; partner is on another map)",
                    "Pokemon Brainy Blue - Co-op (connected; partner is on this map)",
                };
                lastConnected = status;
                SDL_SetWindowTitle(win, titles[status]);
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
