#include "ui/ProfileChooser.h"

#include "app/App.h"
#include "ui/Ui.h"

#include <commctrl.h>

#include <algorithm>

namespace km {
namespace {
enum : int { IDC_SEARCH = 100, IDC_LIST, IDC_ACTIVATE, IDC_OPEN };
}

ProfileChooser::~ProfileChooser() {
    if (hwnd_) DestroyWindow(hwnd_);
    if (font_) DeleteObject(font_);
}

void ProfileChooser::create() {
    static const ATOM cls = [] {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = Proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"KmProfileChooser";
        return RegisterClassExW(&wc);
    }();
    (void)cls;
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_CONTROLPARENT, L"KmProfileChooser",
                            L"Keymapper profiles", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, 0, 0, 100,
                            100, nullptr, nullptr, GetModuleHandleW(nullptr), this);
    search_ = ui::Child(hwnd_, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, IDC_SEARCH, WS_EX_CLIENTEDGE);
    SendMessageW(search_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search profiles"));
    list_ = ui::Child(hwnd_, L"LISTBOX", L"", WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, IDC_LIST,
                      WS_EX_CLIENTEDGE);
    activate_ = ui::Child(hwnd_, L"BUTTON", L"Activate", WS_TABSTOP | BS_DEFPUSHBUTTON, IDC_ACTIVATE);
    open_ = ui::Child(hwnd_, L"BUTTON", L"Open editor", WS_TABSTOP | BS_PUSHBUTTON, IDC_OPEN);
    ui::SetAccessibleName(search_, L"Search profiles");
    ui::SetAccessibleName(list_, L"Profiles");
    ui::EnableDialogNavigation(hwnd_);
}

void ProfileChooser::show() {
    if (!hwnd_) create();
    const UINT dpi = ui::DpiOf(hwnd_);
    if (font_) DeleteObject(font_);
    ui::Fonts f;
    f.create(dpi);
    font_ = f.normal;
    f.normal = nullptr;
    ui::SetFontTree(hwnd_, font_);

    // Open just above the notification area, near the cursor.
    POINT pt;
    GetCursorPos(&pt);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi);
    const int w = ui::Scale(320, dpi), h = ui::Scale(420, dpi);
    int x = std::min<int>(pt.x - w / 2, mi.rcWork.right - w - ui::Scale(8, dpi));
    int y = std::min<int>(pt.y - h, mi.rcWork.bottom - h - ui::Scale(8, dpi));
    x = std::max<int>(x, mi.rcWork.left);
    y = std::max<int>(y, mi.rcWork.top);
    SetWindowPos(hwnd_, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
    SetWindowTextW(search_, L"");
    refresh();
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
    SetFocus(list_);
}

void ProfileChooser::layout() {
    const UINT dpi = ui::DpiOf(hwnd_);
    auto S = [dpi](int v) { return ui::Scale(v, dpi); };
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int w = rc.right, h = rc.bottom;
    ui::Place(search_, S(8), S(8), w - S(16), S(26));
    ui::Place(list_, S(8), S(40), w - S(16), h - S(40 + 8 + 30 + 8));
    ui::Place(activate_, S(8), h - S(8 + 30), (w - S(24)) / 2, S(30));
    ui::Place(open_, S(16) + (w - S(24)) / 2, h - S(8 + 30), (w - S(24)) / 2, S(30));
}

void ProfileChooser::refresh() {
    if (!hwnd_) return;
    const Settings& s = app_.settings();
    const std::wstring filter = ui::Trim(ui::GetText(search_));
    SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    SendMessageW(list_, LB_INITSTORAGE, s.profiles.size(), s.profiles.size() * 32 * sizeof(wchar_t));
    int activeRow = -1;
    for (size_t i = 0; i < s.profiles.size(); ++i) {
        const Profile& p = s.profiles[i];
        if (!ui::ContainsNoCase(p.name, filter)) continue;
        const bool active = p.id == s.activeProfileId;
        std::wstring text = active ? L"✔  " + p.name : L"     " + p.name;
        LRESULT row = SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        SendMessageW(list_, LB_SETITEMDATA, static_cast<WPARAM>(row), static_cast<LPARAM>(i));
        if (active) activeRow = static_cast<int>(row);
    }
    SendMessageW(list_, LB_SETCURSEL, static_cast<WPARAM>(activeRow >= 0 ? activeRow : 0), 0);
    SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list_, nullptr, TRUE);
}

void ProfileChooser::activateSelection() {
    const LRESULT row = SendMessageW(list_, LB_GETCURSEL, 0, 0);
    if (row < 0) return;
    const auto index = static_cast<size_t>(SendMessageW(list_, LB_GETITEMDATA, static_cast<WPARAM>(row), 0));
    if (index >= app_.settings().profiles.size()) return;
    const std::string id = app_.settings().profiles[index].id;
    ShowWindow(hwnd_, SW_HIDE);
    app_.activateProfile(id);
}

LRESULT CALLBACK ProfileChooser::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<ProfileChooser*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<ProfileChooser*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT ProfileChooser::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            layout();
            return 0;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) ShowWindow(hwnd_, SW_HIDE);
            break;
        case WM_CLOSE:
            ShowWindow(hwnd_, SW_HIDE);
            return 0;
        case WM_COMMAND: {
            const int id = LOWORD(wp), code = HIWORD(wp);
            if (id == IDC_SEARCH && code == EN_CHANGE) {
                refresh();
            } else if ((id == IDC_ACTIVATE && code == BN_CLICKED) || (id == IDC_LIST && code == LBN_DBLCLK) ||
                       id == IDOK) {
                activateSelection();
            } else if (id == IDC_OPEN && code == BN_CLICKED) {
                ShowWindow(hwnd_, SW_HIDE);
                app_.showEditor();
            } else if (id == IDCANCEL) {
                ShowWindow(hwnd_, SW_HIDE);
            }
            return 0;
        }
        case WM_CTLCOLORSTATIC:
            SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        case WM_DPICHANGED: {
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            ui::Fonts f;
            f.create(HIWORD(wp));
            if (font_) DeleteObject(font_);
            font_ = f.normal;
            f.normal = nullptr;
            ui::SetFontTree(hwnd_, font_);
            layout();
            return 0;
        }
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace km
