#include "ui/Ui.h"

#include "resource.h"

#include <initguid.h>
#include <oleacc.h>

#include <algorithm>
#include <cwctype>

namespace km::ui {

UINT DpiOf(HWND hwnd) {
    UINT dpi = hwnd ? GetDpiForWindow(hwnd) : 0;
    return dpi ? dpi : GetDpiForSystem();
}

void Fonts::create(UINT dpi) {
    destroy();
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, dpi);
    LOGFONTW lf = ncm.lfMessageFont;
    normal = CreateFontIndirectW(&lf);
    LOGFONTW b = lf;
    b.lfWeight = FW_SEMIBOLD;
    bold = CreateFontIndirectW(&b);
    LOGFONTW t = b;
    t.lfHeight = lf.lfHeight * 3 / 2;
    title = CreateFontIndirectW(&t);
    LOGFONTW l = lf;
    l.lfHeight = lf.lfHeight * 8 / 5;
    l.lfWeight = FW_SEMIBOLD;
    large = CreateFontIndirectW(&l);
}

void Fonts::destroy() {
    for (HFONT* f : {&normal, &bold, &title, &large}) {
        if (*f) DeleteObject(*f);
        *f = nullptr;
    }
}

HWND Child(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD exStyle) {
    return CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, parent,
                           reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr),
                           nullptr);
}

void Place(HWND h, int x, int y, int w, int hgt) {
    SetWindowPos(h, nullptr, x, y, w < 0 ? 0 : w, hgt < 0 ? 0 : hgt, SWP_NOZORDER | SWP_NOACTIVATE);
}

void SetFontTree(HWND parent, HFONT font) {
    EnumChildWindows(
        parent,
        [](HWND h, LPARAM f) -> BOOL {
            SendMessageW(h, WM_SETFONT, static_cast<WPARAM>(f), TRUE);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(font));
}

std::wstring GetText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    s.resize(static_cast<size_t>(n));
    return s;
}

std::wstring Trim(const std::wstring& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::iswspace(s[a])) ++a;
    while (b > a && std::iswspace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty()) return true;
    return FindNLSStringEx(LOCALE_NAME_USER_DEFAULT, FIND_FROMSTART | LINGUISTIC_IGNORECASE, haystack.c_str(),
                           static_cast<int>(haystack.size()), needle.c_str(), static_cast<int>(needle.size()),
                           nullptr, nullptr, nullptr, 0) >= 0;
}

namespace {
IAccPropServices* AccServices() {
    static IAccPropServices* services = [] {
        IAccPropServices* p = nullptr;
        CoCreateInstance(CLSID_AccPropServices, nullptr, CLSCTX_INPROC_SERVER, IID_IAccPropServices,
                         reinterpret_cast<void**>(&p));
        return p;
    }();
    return services;
}
}  // namespace

void SetAccessibleName(HWND h, const std::wstring& name) {
    if (IAccPropServices* s = AccServices())
        s->SetHwndPropStr(h, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, PROPID_ACC_NAME, name.c_str());
}

void ClearAccessibleName(HWND h) {
    if (IAccPropServices* s = AccServices()) {
        MSAAPROPID props[] = {PROPID_ACC_NAME};
        s->ClearHwndProps(h, static_cast<DWORD>(OBJID_CLIENT), CHILDID_SELF, props, 1);
    }
}

void EnableDialogNavigation(HWND top) { SetPropW(top, L"KmDialogNav", reinterpret_cast<HANDLE>(1)); }

bool RouteDialogMessage(MSG& msg) {
    if (msg.message < WM_KEYFIRST || msg.message > WM_KEYLAST) return false;
    HWND root = GetAncestor(msg.hwnd, GA_ROOT);
    if (!root || !GetPropW(root, L"KmDialogNav")) return false;
    return IsDialogMessageW(root, &msg) != FALSE;
}

void RunModal(HWND dialog, HWND owner, const bool& done, const std::function<bool(const MSG&)>& filter) {
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    SetForegroundWindow(dialog);
    MSG msg;
    while (!done) {
        BOOL r = GetMessageW(&msg, nullptr, 0, 0);
        if (r <= 0) {
            PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        if (filter && filter(msg)) continue;  // Swallowed by the dialog.
        if (!RouteDialogMessage(msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    // Re-enable the owner before the dialog goes away so activation returns to it.
    if (owner) {
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
    }
}

void CenterWindow(HWND hwnd, HWND owner) {
    RECT rc;
    GetWindowRect(hwnd, &rc);
    const int w = rc.right - rc.left, h = rc.bottom - rc.top;
    RECT area;
    HMONITOR mon;
    if (owner && IsWindowVisible(owner) && !IsIconic(owner)) {
        GetWindowRect(owner, &area);
        mon = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    } else {
        POINT pt;
        GetCursorPos(&pt);
        mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{sizeof mi};
        GetMonitorInfoW(mon, &mi);
        area = mi.rcWork;
    }
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    int x = area.left + (area.right - area.left - w) / 2;
    int y = area.top + (area.bottom - area.top - h) / 2;
    x = std::max<int>(mi.rcWork.left, std::min<int>(x, mi.rcWork.right - w));
    y = std::max<int>(mi.rcWork.top, std::min<int>(y, mi.rcWork.bottom - h));
    SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

HICON AppIcon(bool paused, int size) {
    return static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(paused ? IDI_PAUSED : IDI_APP),
                                         IMAGE_ICON, size, size, LR_DEFAULTCOLOR));
}

}  // namespace km::ui
