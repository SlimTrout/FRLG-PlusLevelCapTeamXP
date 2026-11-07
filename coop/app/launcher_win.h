// Brainy Blue Co-op: the Windows launcher window.
//
// Shown when bbcoop.exe is started without options (double-clicked). The player
// picks Host or Partner; the host sees its ZeroTier IP address to give to the
// partner and picks the game speed (used by both games), the partner types in the
// host's IP. The choices are remembered in bbcoop.ini next to the exe.
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
};

enum {
    ID_HOST = 101, ID_PARTNER, ID_IP, ID_COPY, ID_IPNOTE, ID_SPEED, ID_ADDRESS, ID_ROM, ID_BROWSE, ID_START, ID_QUIT,
    ID_HOSTHELP, ID_PARTNERHELP, ID_IPLABEL, ID_SPEEDLABEL, ID_ADDRLABEL, ID_ADDRNOTE,
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
} sL;

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
    if (GetOpenFileNameW(&ofn)) SetWindowTextW(L_Item(ID_ROM), file);
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
        if (id == ID_IPNOTE || id == ID_ADDRNOTE || id == ID_HOSTHELP || id == ID_PARTNERHELP || id == 99)
            SetTextColor(dc, sL.gray);
        return (LRESULT)sL.bg;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_HOST: case ID_PARTNER: L_ShowRole(); break;
        case ID_BROWSE: L_Browse(); break;
        case ID_COPY: L_CopyIp(); break;
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
    const int W = 440, H = 470, X = 24, IW = W - 2 * X;

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
    L_ShowRole();

    ShowWindow(sL.win, SW_SHOW);
    UpdateWindow(sL.win);
    SetFocus(L_Item(ID_START));
    while (!sL.done && GetMessageW(&msg, NULL, 0, 0) > 0) {
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
