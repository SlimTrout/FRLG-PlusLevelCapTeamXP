// Brainy Blue Co-op: the Windows launcher window.
//
// Shown when bbcoop.exe is started without options (double-clicked). The player
// picks Host or Partner; the host sees its ZeroTier IP address to give to the
// partner and picks the game speed (used by both games), the partner types in the
// host's IP. Both can start from a save state instead of the normal save, and
// change the keyboard controls. The choices are remembered in bbcoop.ini next to
// the exe.
//
// Included by bbcoop.c on Windows only (after winsock2.h).

#ifndef LAUNCHER_WIN_H
#define LAUNCHER_WIN_H

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <iphlpapi.h>
#include <wchar.h>

struct LaunchOptions {
    int host;          // 1 = host, 0 = partner
    char address[256]; // partner: the host's address (UTF-8)
    int port;
    int speed;         // host: 1..10
    char rom[1024];    // UTF-8 path of the ROM
    char state[1024];  // UTF-8 path of the save state to start from ("" = the normal save)
    int keys[11];      // SDL scancodes: Up, Down, Left, Right, A, B, Start, Select, L, R, save state
};

#define L_KEYS 11
#define L_MAX_STATES 64

enum {
    ID_HOST = 101, ID_PARTNER, ID_IP, ID_COPY, ID_IPNOTE, ID_SPEED, ID_ADDRESS, ID_ROM, ID_BROWSE, ID_START, ID_QUIT,
    ID_HOSTHELP, ID_PARTNERHELP, ID_IPLABEL, ID_SPEEDLABEL, ID_ADDRLABEL, ID_ADDRNOTE,
    ID_STATE, ID_STATENOTE, ID_CONTROLS,
    ID_KEY0 = 200, ID_KEYS_RESET = 230, ID_KEYS_DONE, ID_KEYS_NOTE,
};

static struct {
    HWND win;
    HFONT font, bold, title, note;
    HBRUSH bg;
    COLORREF gray;
    int dpi;
    int done, ok;
    WCHAR ini[MAX_PATH];
    WCHAR exeDir[MAX_PATH];
    struct LaunchOptions *out;
    // save states found next to the ROM, newest first (index 0 of the list is the normal save)
    WCHAR statePaths[L_MAX_STATES][MAX_PATH];
    int stateCount;
    // the Controls window
    HWND keysWin;
    int keys[L_KEYS];
    int capturing; // which key is waiting for a key press, or -1
} sL;

static const int sDefaultKeys[L_KEYS] = { 82, 81, 80, 79, 29, 27, 40, 42, 4, 22, 62 };
static const WCHAR *const sKeyLabels[L_KEYS] = {
    L"Up", L"Down", L"Left", L"Right", L"A", L"B", L"Start", L"Select", L"L", L"R", L"Save state (both players)",
};

static int L_S(int v) { return MulDiv(v, sL.dpi, 96); }

// ---------------------------------------------------------------- settings

static void L_IniPath(void) {
    WCHAR *slash;
    GetModuleFileNameW(NULL, sL.exeDir, MAX_PATH);
    slash = wcsrchr(sL.exeDir, L'\\');
    if (slash) *slash = 0;
    _snwprintf(sL.ini, MAX_PATH, L"%ls\\bbcoop.ini", sL.exeDir);
}

static void L_IniGet(const WCHAR *key, const WCHAR *def, WCHAR *buf, int size) {
    GetPrivateProfileStringW(L"coop", key, def, buf, size, sL.ini);
}

static void L_IniSet(const WCHAR *key, const WCHAR *value) {
    WritePrivateProfileStringW(L"coop", key, value, sL.ini);
}

static int L_FileExists(const WCHAR *path) {
    DWORD a = GetFileAttributesW(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// The ROM to suggest: pokefirered.gba next to the exe, else any .gba there.
static void L_DefaultRom(WCHAR *buf, int size) {
    WIN32_FIND_DATAW fd;
    WCHAR pattern[MAX_PATH];
    HANDLE h;

    _snwprintf(buf, size, L"%ls\\pokefirered.gba", sL.exeDir);
    if (L_FileExists(buf)) return;
    _snwprintf(pattern, MAX_PATH, L"%ls\\*.gba", sL.exeDir);
    h = FindFirstFileW(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        _snwprintf(buf, size, L"%ls\\%ls", sL.exeDir, fd.cFileName);
        FindClose(h);
    }
}

// ---------------------------------------------------------------- this PC's addresses

// Finds this PC's IPv4 address on the ZeroTier network (an adapter whose name says
// ZeroTier), or failing that its first other address. Returns 1 if it was ZeroTier.
static int L_FindAddress(WCHAR *buf, int size) {
    ULONG len = 32 * 1024;
    IP_ADAPTER_ADDRESSES *list = NULL, *a;
    int found = 0;

    buf[0] = 0;
    for (int tries = 0; tries < 3; tries++) {
        list = (IP_ADAPTER_ADDRESSES *)malloc(len);
        if (!list) return 0;
        ULONG r = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, NULL, list, &len);
        if (r == NO_ERROR) break;
        free(list);
        list = NULL;
        if (r != ERROR_BUFFER_OVERFLOW) return 0;
    }
    if (!list) return 0;
    for (int pass = 0; pass < 2 && !found; pass++) {
        for (a = list; a && !found; a = a->Next) {
            int zt = (a->FriendlyName && wcsstr(a->FriendlyName, L"ZeroTier"))
                  || (a->Description && wcsstr(a->Description, L"ZeroTier"));
            if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            if (pass == 0 && !zt) continue;
            for (IP_ADAPTER_UNICAST_ADDRESS *u = a->FirstUnicastAddress; u; u = u->Next) {
                struct sockaddr_in *sin = (struct sockaddr_in *)u->Address.lpSockaddr;
                unsigned char *b;
                if (sin->sin_family != AF_INET) continue;
                b = (unsigned char *)&sin->sin_addr;
                if (b[0] == 169 && b[1] == 254) continue; // no address yet
                _snwprintf(buf, size, L"%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
                found = pass == 0 ? 1 : 2;
                break;
            }
        }
    }
    free(list);
    return found == 1;
}

// ---------------------------------------------------------------- keys

// Windows virtual keys -> SDL scancodes (the game reads SDL scancodes), with names.
static const struct { int vk, sc; const WCHAR *name; } sKeyTable[] = {
    { VK_UP, 82, L"Up arrow" }, { VK_DOWN, 81, L"Down arrow" }, { VK_LEFT, 80, L"Left arrow" }, { VK_RIGHT, 79, L"Right arrow" },
    { VK_RETURN, 40, L"Enter" }, { VK_BACK, 42, L"Backspace" }, { VK_TAB, 43, L"Tab" }, { VK_SPACE, 44, L"Space" },
    { VK_LSHIFT, 225, L"Left Shift" }, { VK_RSHIFT, 229, L"Right Shift" },
    { VK_LCONTROL, 224, L"Left Ctrl" }, { VK_RCONTROL, 228, L"Right Ctrl" },
    { VK_LMENU, 226, L"Left Alt" }, { VK_RMENU, 230, L"Right Alt" },
    { VK_INSERT, 73, L"Insert" }, { VK_HOME, 74, L"Home" }, { VK_PRIOR, 75, L"Page Up" },
    { VK_DELETE, 76, L"Delete" }, { VK_END, 77, L"End" }, { VK_NEXT, 78, L"Page Down" },
    { VK_OEM_MINUS, 45, L"-" }, { VK_OEM_PLUS, 46, L"=" }, { VK_OEM_4, 47, L"[" }, { VK_OEM_6, 48, L"]" },
    { VK_OEM_5, 49, L"\\" }, { VK_OEM_1, 51, L";" }, { VK_OEM_7, 52, L"'" }, { VK_OEM_3, 53, L"`" },
    { VK_OEM_COMMA, 54, L"," }, { VK_OEM_PERIOD, 55, L"." }, { VK_OEM_2, 56, L"/" },
    { VK_DIVIDE, 84, L"Numpad /" }, { VK_MULTIPLY, 85, L"Numpad *" }, { VK_SUBTRACT, 86, L"Numpad -" },
    { VK_ADD, 87, L"Numpad +" }, { VK_DECIMAL, 99, L"Numpad ." },
};

static void L_KeyName(int sc, WCHAR *buf, int size) {
    if (sc >= 4 && sc <= 29) { _snwprintf(buf, size, L"%lc", (wint_t)(L'A' + sc - 4)); return; }
    if (sc >= 30 && sc <= 38) { _snwprintf(buf, size, L"%lc", (wint_t)(L'1' + sc - 30)); return; }
    if (sc == 39) { _snwprintf(buf, size, L"0"); return; }
    if (sc >= 58 && sc <= 69) { _snwprintf(buf, size, L"F%d", sc - 57); return; }
    if (sc >= 89 && sc <= 97) { _snwprintf(buf, size, L"Numpad %d", sc - 88); return; }
    if (sc == 98) { _snwprintf(buf, size, L"Numpad 0"); return; }
    if (sc == 88) { _snwprintf(buf, size, L"Numpad Enter"); return; }
    for (unsigned i = 0; i < sizeof sKeyTable / sizeof sKeyTable[0]; i++)
        if (sKeyTable[i].sc == sc) { _snwprintf(buf, size, L"%ls", sKeyTable[i].name); return; }
    _snwprintf(buf, size, L"(key %d)", sc);
}

// The SDL scancode for a key press, or 0 for keys the game can't use.
static int L_KeyFromMessage(WPARAM vk, LPARAM lp) {
    int ext = (lp >> 24) & 1;
    if (vk == VK_SHIFT) vk = MapVirtualKeyW((lp >> 16) & 0xFF, MAPVK_VSC_TO_VK_EX);
    if (vk == VK_CONTROL) vk = ext ? VK_RCONTROL : VK_LCONTROL;
    if (vk == VK_MENU) vk = ext ? VK_RMENU : VK_LMENU;
    if (vk == VK_RETURN && ext) return 88; // numpad Enter
    if (vk >= 'A' && vk <= 'Z') return 4 + (int)(vk - 'A');
    if (vk >= '1' && vk <= '9') return 30 + (int)(vk - '1');
    if (vk == '0') return 39;
    if (vk >= VK_F1 && vk <= VK_F12) return 58 + (int)(vk - VK_F1);
    if (vk >= VK_NUMPAD1 && vk <= VK_NUMPAD9) return 89 + (int)(vk - VK_NUMPAD1);
    if (vk == VK_NUMPAD0) return 98;
    for (unsigned i = 0; i < sizeof sKeyTable / sizeof sKeyTable[0]; i++)
        if ((WPARAM)sKeyTable[i].vk == vk) return sKeyTable[i].sc;
    return 0;
}

static void L_LoadKeys(void) {
    for (int i = 0; i < L_KEYS; i++) {
        WCHAR key[16], buf[16];
        _snwprintf(key, 16, L"key%d", i);
        GetPrivateProfileStringW(L"keys", key, L"", buf, 16, sL.ini);
        sL.keys[i] = _wtoi(buf) > 0 ? _wtoi(buf) : sDefaultKeys[i];
    }
}

static void L_SaveKeys(void) {
    for (int i = 0; i < L_KEYS; i++) {
        WCHAR key[16], buf[16];
        _snwprintf(key, 16, L"key%d", i);
        _snwprintf(buf, 16, L"%d", sL.keys[i]);
        WritePrivateProfileStringW(L"keys", key, buf, sL.ini);
    }
}

static void L_KeysRefresh(void) {
    for (int i = 0; i < L_KEYS; i++) {
        WCHAR name[64];
        if (sL.capturing == i) _snwprintf(name, 64, L"Press a key\u2026");
        else L_KeyName(sL.keys[i], name, 64);
        SetWindowTextW(GetDlgItem(sL.keysWin, ID_KEY0 + i), name);
    }
}

// A key pressed while a control waits for one. Escape cancels.
static void L_KeysCapture(WPARAM vk, LPARAM lp) {
    int sc = vk == VK_ESCAPE ? 0 : L_KeyFromMessage(vk, lp);
    if (sc) {
        // a key does one thing: take it off whatever had it
        for (int i = 0; i < L_KEYS; i++)
            if (i != sL.capturing && sL.keys[i] == sc) sL.keys[i] = sL.keys[sL.capturing];
        sL.keys[sL.capturing] = sc;
    }
    sL.capturing = -1;
    L_KeysRefresh();
}

static LRESULT CALLBACK L_KeysProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wp, TRANSPARENT);
        if (GetDlgCtrlID((HWND)lp) == ID_KEYS_NOTE) SetTextColor((HDC)wp, sL.gray);
        return (LRESULT)sL.bg;
    case WM_COMMAND: {
        int id = LOWORD(wp);
        if (id >= ID_KEY0 && id < ID_KEY0 + L_KEYS) { sL.capturing = id - ID_KEY0; L_KeysRefresh(); }
        else if (id == ID_KEYS_RESET) { memcpy(sL.keys, sDefaultKeys, sizeof sL.keys); sL.capturing = -1; L_KeysRefresh(); }
        else if (id == ID_KEYS_DONE || id == IDOK || id == IDCANCEL) DestroyWindow(h);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        L_SaveKeys();
        sL.keysWin = NULL;
        sL.capturing = -1;
        EnableWindow(sL.win, TRUE);
        SetForegroundWindow(sL.win);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static HWND L_AddTo(HWND parent, const WCHAR *cls, const WCHAR *text, DWORD style, int x, int y, int w, int h, int id, HFONT font) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             L_S(x), L_S(y), L_S(w), L_S(h), parent, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
    return c;
}

static void L_ShowControls(void) {
    WNDCLASSEXW wc;
    RECT r, pr;
    const int W = 380, rowH = 32, top = 58, H = top + L_KEYS * rowH + 64;

    if (sL.keysWin) { SetForegroundWindow(sL.keysWin); return; }
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = L_KeysProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = sL.bg;
    wc.lpszClassName = L"BBCoopKeys";
    RegisterClassExW(&wc);
    r.left = 0; r.top = 0; r.right = L_S(W); r.bottom = L_S(H);
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    GetWindowRect(sL.win, &pr);
    sL.keysWin = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT, wc.lpszClassName, L"Controls", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
                                 pr.left + L_S(30), pr.top + L_S(30), r.right - r.left, r.bottom - r.top, sL.win, NULL, wc.hInstance, NULL);
    if (!sL.keysWin) return;
    sL.capturing = -1;
    L_AddTo(sL.keysWin, L"STATIC", L"Click a control, then press the key you want for it. "
                                   L"A game controller works too (it isn't changed here).",
            0, 20, 12, W - 40, 40, ID_KEYS_NOTE, sL.note);
    for (int i = 0; i < L_KEYS; i++) {
        L_AddTo(sL.keysWin, L"STATIC", sKeyLabels[i], 0, 20, top + i * rowH + 6, 190, 22, 0, sL.font);
        L_AddTo(sL.keysWin, L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, 214, top + i * rowH, 146, 28, ID_KEY0 + i, sL.font);
    }
    L_AddTo(sL.keysWin, L"BUTTON", L"Reset to defaults", BS_PUSHBUTTON | WS_TABSTOP, 20, H - 50, 150, 32, ID_KEYS_RESET, sL.font);
    L_AddTo(sL.keysWin, L"BUTTON", L"Done", BS_DEFPUSHBUTTON | WS_TABSTOP, W - 120, H - 50, 100, 32, ID_KEYS_DONE, sL.font);
    L_KeysRefresh();
    EnableWindow(sL.win, FALSE);
    ShowWindow(sL.keysWin, SW_SHOW);
}

// ---------------------------------------------------------------- save states

static int L_CompareStateNames(const void *a, const void *b) {
    // newest first: compare the file names (timestamps) without their folder
    const WCHAR *x = wcsrchr((const WCHAR *)a, L'\\'), *y = wcsrchr((const WCHAR *)b, L'\\');
    return wcscmp(y ? y : (const WCHAR *)b, x ? x : (const WCHAR *)a);
}

// Lists the save states next to the ROM in the "Start from" box.
static void L_FillStates(void) {
    WCHAR rom[MAX_PATH], dir[MAX_PATH], *slash;
    HWND box = GetDlgItem(sL.win, ID_STATE);
    static const WCHAR *const kinds[2] = { L"auto", L"manual" };

    GetWindowTextW(GetDlgItem(sL.win, ID_ROM), rom, MAX_PATH);
    wcscpy(dir, rom);
    slash = wcsrchr(dir, L'\\');
    if (!slash) slash = wcsrchr(dir, L'/');
    if (slash) slash[1] = 0; else dir[0] = 0;
    sL.stateCount = 0;
    for (int k = 0; k < 2; k++) {
        WIN32_FIND_DATAW fd;
        WCHAR pattern[MAX_PATH];
        HANDLE h;
        _snwprintf(pattern, MAX_PATH, L"%lssavestates\\%ls\\*.ss", dir, kinds[k]);
        h = FindFirstFileW(pattern, &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (sL.stateCount < L_MAX_STATES)
                _snwprintf(sL.statePaths[sL.stateCount++], MAX_PATH, L"%lssavestates\\%ls\\%ls", dir, kinds[k], fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    qsort(sL.statePaths, sL.stateCount, sizeof sL.statePaths[0], L_CompareStateNames);
    SendMessageW(box, CB_RESETCONTENT, 0, 0);
    SendMessageW(box, CB_ADDSTRING, 0, (LPARAM)L"Your normal save file");
    for (int i = 0; i < sL.stateCount; i++) {
        WCHAR label[128], stamp[32];
        const WCHAR *file = wcsrchr(sL.statePaths[i], L'\\') + 1;
        int manual = wcsstr(sL.statePaths[i], L"\\manual\\") != NULL;
        // 2026-11-06_23-15-02.ss -> 2026-11-06 23:15:02
        _snwprintf(stamp, 32, L"%ls", file);
        if (wcslen(stamp) >= 19) {
            stamp[10] = L' '; stamp[13] = L':'; stamp[16] = L':'; stamp[19] = 0;
        }
        _snwprintf(label, 128, L"%ls save state \u2014 %ls", manual ? L"Manual" : L"Automatic", stamp);
        SendMessageW(box, CB_ADDSTRING, 0, (LPARAM)label);
    }
    SendMessageW(box, CB_SETCURSEL, 0, 0);
}

// ---------------------------------------------------------------- window

static HWND L_Item(int id) { return GetDlgItem(sL.win, id); }

static HWND L_Add(const WCHAR *cls, const WCHAR *text, DWORD style, int x, int y, int w, int h, int id, HFONT font) {
    HWND c = CreateWindowExW(!wcscmp(cls, L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, text, WS_CHILD | WS_VISIBLE | style,
                             L_S(x), L_S(y), L_S(w), L_S(h), sL.win, (HMENU)(INT_PTR)id, GetModuleHandleW(NULL), NULL);
    SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
    return c;
}

static void L_ShowRole(void) {
    int host = SendMessageW(L_Item(ID_HOST), BM_GETCHECK, 0, 0) == BST_CHECKED;
    static const int hostIds[] = { ID_IPLABEL, ID_IP, ID_COPY, ID_IPNOTE, ID_SPEEDLABEL, ID_SPEED };
    static const int partnerIds[] = { ID_ADDRLABEL, ID_ADDRESS, ID_ADDRNOTE };
    for (unsigned i = 0; i < sizeof hostIds / sizeof hostIds[0]; i++) ShowWindow(L_Item(hostIds[i]), host ? SW_SHOW : SW_HIDE);
    for (unsigned i = 0; i < sizeof partnerIds / sizeof partnerIds[0]; i++) ShowWindow(L_Item(partnerIds[i]), host ? SW_HIDE : SW_SHOW);
    SetWindowTextW(L_Item(ID_START), host ? L"Host game" : L"Join game");
}

static void L_Browse(void) {
    WCHAR file[MAX_PATH];
    OPENFILENAMEW ofn;

    GetWindowTextW(L_Item(ID_ROM), file, MAX_PATH);
    memset(&ofn, 0, sizeof ofn);
    ofn.lStructSize = sizeof ofn;
    ofn.hwndOwner = sL.win;
    ofn.lpstrFilter = L"GBA ROM (*.gba)\0*.gba\0All files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Choose the Brainy Blue ROM";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) {
        SetWindowTextW(L_Item(ID_ROM), file);
        L_FillStates();
    }
}

static void L_CopyIp(void) {
    WCHAR ip[64];
    int len = GetWindowTextW(L_Item(ID_IP), ip, 64);
    if (!len || !OpenClipboard(sL.win)) return;
    EmptyClipboard();
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (len + 1) * sizeof(WCHAR));
    if (mem) {
        memcpy(GlobalLock(mem), ip, (len + 1) * sizeof(WCHAR));
        GlobalUnlock(mem);
        SetClipboardData(CF_UNICODETEXT, mem);
    }
    CloseClipboard();
    SetWindowTextW(L_Item(ID_COPY), L"Copied");
}

static void L_Start(void) {
    struct LaunchOptions *o = sL.out;
    WCHAR rom[MAX_PATH], addr[256], num[16];

    o->host = SendMessageW(L_Item(ID_HOST), BM_GETCHECK, 0, 0) == BST_CHECKED;
    GetWindowTextW(L_Item(ID_ROM), rom, MAX_PATH);
    GetWindowTextW(L_Item(ID_ADDRESS), addr, 256);
    // trim spaces around a pasted address
    {
        WCHAR *p = addr, *e;
        while (*p == L' ' || *p == L'\t') p++;
        memmove(addr, p, (wcslen(p) + 1) * sizeof(WCHAR));
        e = addr + wcslen(addr);
        while (e > addr && (e[-1] == L' ' || e[-1] == L'\t' || e[-1] == L'\r' || e[-1] == L'\n')) *--e = 0;
    }
    if (!L_FileExists(rom)) {
        MessageBoxW(sL.win, L"Choose the Brainy Blue ROM file (.gba) first.", L"Brainy Blue Co-op", MB_ICONWARNING);
        return;
    }
    if (!o->host && !addr[0]) {
        MessageBoxW(sL.win, L"Type in your host's ZeroTier IP address.\nIt's shown in the host's launcher, for example 10.147.17.5.",
                    L"Brainy Blue Co-op", MB_ICONWARNING);
        SetFocus(L_Item(ID_ADDRESS));
        return;
    }
    {
        int pick = (int)SendMessageW(L_Item(ID_STATE), CB_GETCURSEL, 0, 0);
        o->state[0] = 0;
        if (pick > 0 && pick <= sL.stateCount)
            WideCharToMultiByte(CP_UTF8, 0, sL.statePaths[pick - 1], -1, o->state, sizeof o->state, NULL, NULL);
    }
    memcpy(o->keys, sL.keys, sizeof o->keys);
    o->speed = (int)SendMessageW(L_Item(ID_SPEED), CB_GETCURSEL, 0, 0) + 1;
    if (o->speed < 1 || o->speed > 10) o->speed = 3;
    WideCharToMultiByte(CP_UTF8, 0, rom, -1, o->rom, sizeof o->rom, NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, addr, -1, o->address, sizeof o->address, NULL, NULL);

    L_IniSet(L"role", o->host ? L"host" : L"partner");
    L_IniSet(L"host_address", addr);
    _snwprintf(num, 16, L"%d", o->speed);
    L_IniSet(L"speed", num);
    L_IniSet(L"rom", rom);
    sL.ok = 1;
    DestroyWindow(sL.win);
}

static LRESULT CALLBACK L_Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wp;
        int id = GetDlgCtrlID((HWND)lp);
        if (id == ID_IP) return DefWindowProcW(h, msg, wp, lp); // read-only box keeps its look
        SetBkMode(dc, TRANSPARENT);
        if (id == ID_IPNOTE || id == ID_ADDRNOTE || id == ID_HOSTHELP || id == ID_PARTNERHELP || id == ID_STATENOTE || id == 99)
            SetTextColor(dc, sL.gray);
        return (LRESULT)sL.bg;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_HOST: case ID_PARTNER: L_ShowRole(); break;
        case ID_BROWSE: L_Browse(); break;
        case ID_COPY: L_CopyIp(); break;
        case ID_CONTROLS: L_ShowControls(); break;
        case ID_ROM: if (HIWORD(wp) == EN_KILLFOCUS) L_FillStates(); break;
        case ID_START: case IDOK: L_Start(); break;
        case ID_QUIT: case IDCANCEL: DestroyWindow(h); break;
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        sL.done = 1;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static HFONT L_Font(int pt, int weight) {
    NONCLIENTMETRICSW m;
    m.cbSize = sizeof m;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof m, &m, 0);
    m.lfMessageFont.lfHeight = -MulDiv(pt, sL.dpi, 72);
    m.lfMessageFont.lfWeight = weight;
    wcscpy(m.lfMessageFont.lfFaceName, L"Segoe UI");
    return CreateFontIndirectW(&m.lfMessageFont);
}

// Shows the launcher. Returns 1 with *out filled in, or 0 if the player closed it.
static int launcherRun(struct LaunchOptions *out) {
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_STANDARD_CLASSES };
    WNDCLASSEXW wc;
    HINSTANCE inst = GetModuleHandleW(NULL);
    WCHAR buf[MAX_PATH], ip[64];
    HDC screen;
    MSG msg;
    RECT r;
    const int W = 440, H = 560, X = 24, IW = W - 2 * X;

    memset(&sL, 0, sizeof sL);
    memset(out, 0, sizeof *out);
    sL.out = out;
    out->port = 27450;
    out->speed = 3;
    L_IniPath();
    InitCommonControlsEx(&icc);
    screen = GetDC(NULL);
    sL.dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(NULL, screen);
    sL.font = L_Font(10, FW_NORMAL);
    sL.note = L_Font(9, FW_NORMAL);
    sL.bold = L_Font(10, FW_SEMIBOLD);
    sL.title = L_Font(17, FW_SEMIBOLD);
    sL.bg = GetSysColorBrush(COLOR_WINDOW);
    sL.gray = RGB(96, 96, 96);

    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = L_Proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.hbrBackground = sL.bg;
    wc.lpszClassName = L"BBCoopLauncher";
    RegisterClassExW(&wc);

    r.left = 0; r.top = 0; r.right = L_S(W); r.bottom = L_S(H);
    AdjustWindowRect(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);
    sL.win = CreateWindowExW(0, wc.lpszClassName, L"Brainy Blue Co-op", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                             CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    if (!sL.win) return 0;

    int y = 18;
    L_Add(L"STATIC", L"Brainy Blue Co-op", 0, X, y, IW, 32, 98, sL.title);
    L_Add(L"STATIC", L"Play Pokémon Brainy Blue together over ZeroTier.", 0, X, y + 34, IW, 20, 99, sL.font);

    y = 88;
    L_Add(L"STATIC", L"How are you playing?", 0, X, y, IW, 20, 0, sL.bold);
    L_Add(L"BUTTON", L"Host", BS_AUTORADIOBUTTON | WS_GROUP | WS_TABSTOP, X, y + 26, 90, 22, ID_HOST, sL.font);
    L_Add(L"STATIC", L"Runs the shared game and picks the speed.", 0, X + 96, y + 29, IW - 96, 20, ID_HOSTHELP, sL.note);
    L_Add(L"BUTTON", L"Partner", BS_AUTORADIOBUTTON, X, y + 52, 90, 22, ID_PARTNER, sL.font);
    L_Add(L"STATIC", L"Joins the host's game.", 0, X + 96, y + 55, IW - 96, 20, ID_PARTNERHELP, sL.note);

    // host: this PC's address and the speed
    y = 176;
    L_Add(L"STATIC", L"Your ZeroTier IP — send it to your partner", 0, X, y, IW, 20, ID_IPLABEL, sL.bold);
    int zt = L_FindAddress(ip, 64);
    L_Add(L"EDIT", ip[0] ? ip : L"(no network found)", ES_READONLY | ES_AUTOHSCROLL, X, y + 24, IW - 96, 26, ID_IP, sL.font);
    L_Add(L"BUTTON", L"Copy", BS_PUSHBUTTON | WS_TABSTOP, X + IW - 86, y + 23, 86, 28, ID_COPY, sL.font);
    L_Add(L"STATIC", zt ? L"Found your ZeroTier network."
                        : L"ZeroTier wasn't found, so this is another address of this PC. "
                          L"Is ZeroTier running and joined?",
          0, X, y + 56, IW, 34, ID_IPNOTE, sL.note);
    L_Add(L"STATIC", L"Game speed", 0, X, y + 100, 100, 20, ID_SPEEDLABEL, sL.bold);
    HWND speed = L_Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, X + 104, y + 96, 150, 300, ID_SPEED, sL.font);
    for (int i = 1; i <= 10; i++) {
        WCHAR s[32];
        _snwprintf(s, 32, i == 1 ? L"1x (normal)" : L"%dx", i);
        SendMessageW(speed, CB_ADDSTRING, 0, (LPARAM)s);
    }

    // partner: the host's address
    L_Add(L"STATIC", L"Host's ZeroTier IP", 0, X, y, IW, 20, ID_ADDRLABEL, sL.bold);
    L_Add(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, X, y + 24, IW, 26, ID_ADDRESS, sL.font);
    L_Add(L"STATIC", L"Your host's launcher shows it, for example 10.147.17.5. "
                     L"The host picks the game speed.",
          0, X, y + 56, IW, 34, ID_ADDRNOTE, sL.note);

    // the ROM
    y = 314;
    L_Add(L"STATIC", L"Game ROM", 0, X, y, IW, 20, 0, sL.bold);
    L_Add(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, X, y + 24, IW - 96, 26, ID_ROM, sL.font);
    L_Add(L"BUTTON", L"Browse…", BS_PUSHBUTTON | WS_TABSTOP, X + IW - 86, y + 23, 86, 28, ID_BROWSE, sL.font);

    // where to start from
    y = 380;
    L_Add(L"STATIC", L"Start from", 0, X, y, IW, 20, 0, sL.bold);
    L_Add(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, X, y + 24, IW, 300, ID_STATE, sL.font);
    L_Add(L"STATIC", L"To go back to a save state, both players pick the same one.",
          0, X, y + 56, IW, 20, ID_STATENOTE, sL.note);

    L_Add(L"BUTTON", L"Controls\u2026", BS_PUSHBUTTON | WS_TABSTOP, X, H - 56, 100, 32, ID_CONTROLS, sL.font);
    L_Add(L"BUTTON", L"Quit", BS_PUSHBUTTON | WS_TABSTOP, W - X - 220, H - 56, 100, 32, ID_QUIT, sL.font);
    L_Add(L"BUTTON", L"Host game", BS_DEFPUSHBUTTON | WS_TABSTOP, W - X - 110, H - 56, 110, 32, ID_START, sL.font);

    // remembered choices
    L_IniGet(L"role", L"host", buf, MAX_PATH);
    SendMessageW(L_Item(wcscmp(buf, L"partner") ? ID_HOST : ID_PARTNER), BM_SETCHECK, BST_CHECKED, 0);
    L_IniGet(L"host_address", L"", buf, MAX_PATH);
    SetWindowTextW(L_Item(ID_ADDRESS), buf);
    L_IniGet(L"speed", L"3", buf, MAX_PATH);
    int sp = _wtoi(buf);
    SendMessageW(speed, CB_SETCURSEL, (sp >= 1 && sp <= 10 ? sp : 3) - 1, 0);
    L_IniGet(L"rom", L"", buf, MAX_PATH);
    if (!buf[0] || !L_FileExists(buf)) L_DefaultRom(buf, MAX_PATH);
    SetWindowTextW(L_Item(ID_ROM), buf);
    L_IniGet(L"port", L"27450", buf, MAX_PATH);
    out->port = _wtoi(buf) > 0 ? _wtoi(buf) : 27450;
    L_LoadKeys();
    sL.capturing = -1;
    L_FillStates();
    L_ShowRole();

    ShowWindow(sL.win, SW_SHOW);
    UpdateWindow(sL.win);
    SetFocus(L_Item(ID_START));
    while (!sL.done && GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (sL.keysWin && sL.capturing >= 0 && (msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN)) {
            L_KeysCapture(msg.wParam, msg.lParam); // the Controls window waits for a key
            continue;
        }
        if (sL.keysWin && (msg.message == WM_SYSKEYUP || msg.message == WM_KEYUP) && msg.hwnd != sL.win
         && GetAncestor(msg.hwnd, GA_ROOT) == sL.keysWin && msg.wParam == VK_MENU) continue; // no Alt menu beep
        if (sL.keysWin && IsDialogMessageW(sL.keysWin, &msg)) continue;
        if (IsDialogMessageW(sL.win, &msg)) continue; // Tab, Enter and Escape
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    DeleteObject(sL.font);
    DeleteObject(sL.note);
    DeleteObject(sL.bold);
    DeleteObject(sL.title);
    return sL.ok;
}

// Window sizes scaled for the screen's DPI (the app is DPI-aware, see bbcoop.manifest).
static int launcherScale(int v) {
    HDC screen = GetDC(NULL);
    int dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(NULL, screen);
    return MulDiv(v, dpi, 96);
}

#endif
