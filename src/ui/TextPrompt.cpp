#include "ui/TextPrompt.h"

#include "ui/Ui.h"

namespace km {
namespace {

constexpr int IDC_LABEL = 100;
constexpr int IDC_EDIT = 101;
constexpr int kMaxNameLength = 100;

struct PromptState {
    std::wstring value;
    bool done = false;
    bool ok = false;
    HWND label = nullptr, edit = nullptr, okBtn = nullptr, cancelBtn = nullptr;
    ui::Fonts fonts;
};

void Layout(HWND hwnd, PromptState& s) {
    const UINT dpi = ui::DpiOf(hwnd);
    auto S = [dpi](int v) { return ui::Scale(v, dpi); };
    RECT rc;
    GetClientRect(hwnd, &rc);
    const int w = rc.right;
    ui::Place(s.label, S(12), S(12), w - S(24), S(20));
    ui::Place(s.edit, S(12), S(36), w - S(24), S(26));
    ui::Place(s.okBtn, w - S(12 + 88 + 8 + 88), S(76), S(88), S(28));
    ui::Place(s.cancelBtn, w - S(12 + 88), S(76), S(88), S(28));
}

LRESULT CALLBACK PromptProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    auto* s = reinterpret_cast<PromptState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE:
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
            break;
        case WM_COMMAND:
            if (!s) break;
            if (LOWORD(wp) == IDOK && IsWindowEnabled(s->okBtn)) {
                s->value = ui::Trim(ui::GetText(s->edit));
                s->ok = true;
                s->done = true;
            } else if (LOWORD(wp) == IDCANCEL) {
                s->done = true;
            } else if (LOWORD(wp) == IDC_EDIT && HIWORD(wp) == EN_CHANGE) {
                EnableWindow(s->okBtn, !ui::Trim(ui::GetText(s->edit)).empty());
            }
            return 0;
        case WM_CLOSE:
            if (s) s->done = true;
            return 0;
        case WM_DPICHANGED: {
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            s->fonts.create(HIWORD(wp));
            ui::SetFontTree(hwnd, s->fonts.normal);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            Layout(hwnd, *s);
            return 0;
        }
        case WM_CTLCOLORSTATIC:
            SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_WINDOW));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

bool PromptText(HWND owner, const std::wstring& title, const std::wstring& label, std::wstring& value) {
    static const ATOM cls = [] {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = PromptProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"KmTextPrompt";
        return RegisterClassExW(&wc);
    }();
    (void)cls;

    PromptState s;
    const UINT dpi = ui::DpiOf(owner);
    RECT r{0, 0, ui::Scale(380, dpi), ui::Scale(116, dpi)};
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const DWORD exStyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
    AdjustWindowRectExForDpi(&r, style, FALSE, exStyle, dpi);
    HWND hwnd = CreateWindowExW(exStyle, L"KmTextPrompt", title.c_str(), style, 0, 0, r.right - r.left,
                                r.bottom - r.top, owner, nullptr, GetModuleHandleW(nullptr), &s);
    if (!hwnd) return false;
    s.fonts.create(ui::DpiOf(hwnd));
    s.label = ui::Child(hwnd, L"STATIC", label.c_str(), SS_NOPREFIX, IDC_LABEL);
    s.edit = ui::Child(hwnd, L"EDIT", value.c_str(), WS_TABSTOP | ES_AUTOHSCROLL, IDC_EDIT, WS_EX_CLIENTEDGE);
    SendMessageW(s.edit, EM_SETLIMITTEXT, kMaxNameLength, 0);
    s.okBtn = ui::Child(hwnd, L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK);
    s.cancelBtn = ui::Child(hwnd, L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, IDCANCEL);
    EnableWindow(s.okBtn, !ui::Trim(value).empty());
    ui::SetFontTree(hwnd, s.fonts.normal);
    Layout(hwnd, s);
    ui::EnableDialogNavigation(hwnd);
    ui::CenterWindow(hwnd, owner);
    ShowWindow(hwnd, SW_SHOW);
    SetFocus(s.edit);
    SendMessageW(s.edit, EM_SETSEL, 0, -1);

    ui::RunModal(hwnd, owner, s.done);
    DestroyWindow(hwnd);
    if (s.ok) value = s.value;
    return s.ok;
}

}  // namespace km
