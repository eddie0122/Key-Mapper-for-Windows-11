#include "ui/SelectKeyDialog.h"

#include "core/KeyCatalog.h"
#include "core/Utf.h"
#include "ui/Ui.h"

#include <commctrl.h>

#include <set>

namespace km {
namespace {

enum : int {
    IDC_INSTR = 100,
    IDC_DISPLAY,
    IDC_CAPTURE,
    IDC_SIDES,
    IDC_MANUAL,
    IDC_KEYLBL,
    IDC_SEARCH,
    IDC_KEYS,
    IDC_HINT,
    IDC_CLEAR,
    IDC_MODLBL0 = 120,  // + index
    IDC_MODCB0 = 130,   // + index
};

// Modifier pickers, laid out two per row.
constexpr int kModCount = MT_Count;
constexpr ModType kModOrder[MT_Count] = {MT_Ctrl, MT_Alt, MT_Shift, MT_Win};
constexpr const wchar_t* kModLabels[MT_Count] = {L"Ctrl:", L"Alt:", L"Shift:", L"Win:"};
constexpr const wchar_t* kSideNames[] = {L"Not used", L"Either side", L"Left only", L"Right only"};

class Dialog {
public:
    Dialog(InputHook& hook, SelectKeyDialog::Mode mode, const KeyCombo& value)
        : hook_(hook), mode_(mode), value_(value) {}

    bool run(HWND owner, const std::wstring& title);
    const KeyCombo& value() const { return value_; }

private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    int layout(bool apply);
    void startCapture();
    void stopCapture();
    void onCapture(uint16_t vk, bool down);
    KeyCombo heldModifiers() const;
    void fillKeys();
    void refresh();
    bool modsAllowed() const { return mode_ != SelectKeyDialog::Mode::SingleKey; }
    bool sidesChecked() const { return SendMessageW(sides_, BM_GETCHECK, 0, 0) == BST_CHECKED; }

    InputHook& hook_;
    SelectKeyDialog::Mode mode_;
    KeyCombo value_;
    HWND hwnd_ = nullptr;
    bool done_ = false;
    bool ok_ = false;
    bool capturing_ = false;
    std::set<uint16_t> held_;
    ui::Fonts fonts_;

    HWND instr_ = nullptr, display_ = nullptr, captureBtn_ = nullptr, sides_ = nullptr, manual_ = nullptr;
    HWND modLabel_[MT_Count]{}, modCombo_[MT_Count]{};
    HWND keyLabel_ = nullptr, search_ = nullptr, keys_ = nullptr, hint_ = nullptr;
    HWND clear_ = nullptr, okBtn_ = nullptr, cancel_ = nullptr;
};

bool Dialog::run(HWND owner, const std::wstring& title) {
    static const ATOM cls = [] {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = Proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"KmSelectKey";
        return RegisterClassExW(&wc);
    }();
    (void)cls;

    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const DWORD exStyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
    hwnd_ = CreateWindowExW(exStyle, L"KmSelectKey", title.c_str(), style, 0, 0, 100, 100, owner, nullptr,
                            GetModuleHandleW(nullptr), this);
    if (!hwnd_) return false;
    fonts_.create(ui::DpiOf(hwnd_));

    const wchar_t* instr =
        mode_ == SelectKeyDialog::Mode::SingleKey  ? L"Press the key you want to use, or choose it from the list."
        : mode_ == SelectKeyDialog::Mode::Shortcut ? L"Press the shortcut you want to use (modifiers first, then the key), or build it below."
                                                   : L"Press the key or shortcut to send, or build it below.";
    instr_ = ui::Child(hwnd_, L"STATIC", instr, SS_NOPREFIX, IDC_INSTR);
    display_ = ui::Child(hwnd_, L"STATIC", L"", SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX | SS_SUNKEN, IDC_DISPLAY);
    captureBtn_ = ui::Child(hwnd_, L"BUTTON", L"Stop capturing", WS_TABSTOP | BS_PUSHBUTTON, IDC_CAPTURE);
    sides_ = ui::Child(hwnd_, L"BUTTON", L"Tell left and right modifiers apart", WS_TABSTOP | BS_AUTOCHECKBOX,
                       IDC_SIDES);
    manual_ = ui::Child(hwnd_, L"STATIC", L"Or choose manually", SS_NOPREFIX, IDC_MANUAL);
    for (int i = 0; i < MT_Count; ++i) {
        modLabel_[i] = ui::Child(hwnd_, L"STATIC", kModLabels[i], SS_NOPREFIX | SS_CENTERIMAGE, IDC_MODLBL0 + i);
        modCombo_[i] = ui::Child(hwnd_, L"COMBOBOX", L"", WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST, IDC_MODCB0 + i);
        for (const wchar_t* n : kSideNames) SendMessageW(modCombo_[i], CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(n));
        if (!modsAllowed()) {
            ShowWindow(modLabel_[i], SW_HIDE);
            ShowWindow(modCombo_[i], SW_HIDE);
        }
    }
    keyLabel_ = ui::Child(hwnd_, L"STATIC", L"Key:", SS_NOPREFIX | SS_CENTERIMAGE, IDC_KEYLBL);
    search_ = ui::Child(hwnd_, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, IDC_SEARCH, WS_EX_CLIENTEDGE);
    SendMessageW(search_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search keys"));
    keys_ = ui::Child(hwnd_, L"LISTBOX", L"", WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, IDC_KEYS,
                      WS_EX_CLIENTEDGE);
    hint_ = ui::Child(hwnd_, L"STATIC", L"", SS_NOPREFIX, IDC_HINT);
    clear_ = ui::Child(hwnd_, L"BUTTON", L"Clear", WS_TABSTOP | BS_PUSHBUTTON, IDC_CLEAR);
    okBtn_ = ui::Child(hwnd_, L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK);
    cancel_ = ui::Child(hwnd_, L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, IDCANCEL);

    ui::SetAccessibleName(search_, L"Search keys");
    ui::SetAccessibleName(keys_, L"Keys");
    for (int i = 0; i < MT_Count; ++i)
        ui::SetAccessibleName(modCombo_[i], std::wstring(kModLabels[i]).substr(0, wcslen(kModLabels[i]) - 1) +
                                                L" modifier");

    ui::SetFontTree(hwnd_, fonts_.normal);
    SendMessageW(display_, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_.large), TRUE);
    SendMessageW(manual_, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_.bold), TRUE);

    // Size the window to its content.
    const UINT dpi = ui::DpiOf(hwnd_);
    RECT r{0, 0, ui::Scale(480, dpi), layout(false)};
    AdjustWindowRectExForDpi(&r, style, FALSE, exStyle, dpi);
    SetWindowPos(hwnd_, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
    layout(true);
    fillKeys();
    ui::EnableDialogNavigation(hwnd_);
    ui::CenterWindow(hwnd_, owner);
    ShowWindow(hwnd_, SW_SHOW);
    SetFocus(captureBtn_);
    startCapture();
    refresh();

    ui::RunModal(hwnd_, owner, done_);
    stopCapture();
    DestroyWindow(hwnd_);
    return ok_;
}

int Dialog::layout(bool apply) {
    const UINT dpi = ui::DpiOf(hwnd_);
    auto S = [dpi](int v) { return ui::Scale(v, dpi); };
    auto place = [&](HWND h, int x, int y, int w, int hh) {
        if (apply) ui::Place(h, S(x), S(y), S(w), S(hh));
    };
    const int W = 480, M = 12, inner = W - 2 * M;
    int y = M;
    place(instr_, M, y, inner, 36);
    y += 40;
    place(display_, M, y, inner, 52);
    y += 62;
    place(captureBtn_, M, y, 150, 30);
    place(sides_, M + 162, y + 3, inner - 162, 24);
    y += 44;
    place(manual_, M, y, inner, 22);
    y += 28;
    if (modsAllowed()) {
        for (int i = 0; i < MT_Count; ++i) {
            const int col = i % 2, row = i / 2;
            const int x = M + col * (inner / 2 + 6);
            place(modLabel_[i], x, y + row * 34, 44, 26);
            if (apply)
                ui::Place(modCombo_[i], S(x + 48), S(y + row * 34), S(inner / 2 - 60), S(200));
        }
        y += 70;
    }
    place(keyLabel_, M, y, 44, 26);
    place(search_, M + 48, y, inner - 48, 26);
    y += 34;
    place(keys_, M, y, inner, 190);
    y += 196;
    place(hint_, M, y, inner, 36);
    y += 42;
    place(clear_, M, y, 88, 30);
    place(okBtn_, W - M - 88 - 8 - 88, y, 88, 30);
    place(cancel_, W - M - 88, y, 88, 30);
    y += 30 + M;
    return S(y);
}

void Dialog::startCapture() {
    if (capturing_) return;
    held_.clear();
    capturing_ = true;
    hook_.beginCapture(hwnd_);
    SetWindowTextW(captureBtn_, L"Stop capturing");
    refresh();
}

void Dialog::stopCapture() {
    if (!capturing_) return;
    capturing_ = false;
    hook_.endCapture();
    if (captureBtn_) SetWindowTextW(captureBtn_, L"Capture keys");
    refresh();
}

KeyCombo Dialog::heldModifiers() const {
    KeyCombo c;
    const bool sides = sidesChecked();
    for (uint16_t vk : held_) {
        ModType t;
        Side s;
        if (!ModFromVk(vk, t, s)) continue;
        const Side want = sides ? s : Side::Either;
        c.mods[t] = (c.mods[t] == Side::None || c.mods[t] == want) ? want : Side::Either;
    }
    return c;
}

void Dialog::onCapture(uint16_t vk, bool down) {
    if (!capturing_) return;
    if (!down) {
        held_.erase(vk);
        return;
    }
    if (!held_.insert(vk).second) return;  // Auto-repeat.

    ModType t;
    Side s;
    const bool isMod = ModFromVk(vk, t, s);
    const bool sides = sidesChecked();
    const uint16_t asSingle = isMod && !sides ? VkFromMod(t, Side::Either) : vk;

    if (mode_ == SelectKeyDialog::Mode::SingleKey) {
        value_ = KeyCombo::Single(asSingle);
    } else if (!isMod) {
        KeyCombo c = heldModifiers();
        c.key = vk;
        value_ = c;
    } else {
        int modsHeld = 0, othersHeld = 0;
        for (uint16_t h : held_) (IsModifierVk(h) ? modsHeld : othersHeld)++;
        if (mode_ == SelectKeyDialog::Mode::KeyOrShortcut && modsHeld == 1 && othersHeld == 0) {
            value_ = KeyCombo::Single(asSingle);
        } else {
            value_ = heldModifiers();  // Incomplete: waiting for the action key.
        }
    }
    refresh();
}

void Dialog::fillKeys() {
    const std::wstring filter = ui::Trim(ui::GetText(search_));
    SendMessageW(keys_, WM_SETREDRAW, FALSE, 0);
    SendMessageW(keys_, LB_RESETCONTENT, 0, 0);
    for (const KeyInfo& k : AllKeys()) {
        if (mode_ == SelectKeyDialog::Mode::Shortcut && IsModifierVk(k.vk)) continue;
        if (!ui::ContainsNoCase(k.name, filter) && !ui::ContainsNoCase(Utf8ToWide(k.id), filter) &&
            !ui::ContainsNoCase(Utf8ToWide(k.group), filter))
            continue;
        LRESULT i = SendMessageW(keys_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(k.name));
        SendMessageW(keys_, LB_SETITEMDATA, static_cast<WPARAM>(i), k.vk);
    }
    SendMessageW(keys_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(keys_, nullptr, TRUE);
    refresh();
}

void Dialog::refresh() {
    if (!display_) return;
    std::wstring text;
    if (value_.empty() && !value_.hasMods()) text = capturing_ ? L"Press a key…" : L"Nothing selected";
    else text = FormatCombo(value_);
    SetWindowTextW(display_, text.c_str());
    ui::SetAccessibleName(display_, L"Selected: " + text);

    const bool singleModifier = value_.isSingleKey() && IsModifierVk(value_.key);
    for (int i = 0; i < MT_Count; ++i) {
        const Side s = singleModifier ? Side::None : value_.mods[kModOrder[i]];
        SendMessageW(modCombo_[i], CB_SETCURSEL, static_cast<WPARAM>(s), 0);
    }
    int sel = -1;
    const int count = static_cast<int>(SendMessageW(keys_, LB_GETCOUNT, 0, 0));
    for (int i = 0; i < count && value_.key; ++i)
        if (SendMessageW(keys_, LB_GETITEMDATA, static_cast<WPARAM>(i), 0) == value_.key) sel = i;
    SendMessageW(keys_, LB_SETCURSEL, static_cast<WPARAM>(sel), 0);

    bool ok = mode_ == SelectKeyDialog::Mode::SingleKey ? value_.isSingleKey()
              : mode_ == SelectKeyDialog::Mode::Shortcut ? value_.isShortcut()
                                                         : value_.isValidEndpoint();
    std::wstring hint;
    if (!ok) {
        if (value_.empty() && !value_.hasMods()) hint = L"";
        else if (mode_ == SelectKeyDialog::Mode::Shortcut && !value_.hasMods())
            hint = L"A shortcut needs at least one modifier: Ctrl, Alt, Shift, or Win.";
        else if (value_.key == 0) hint = L"Add a non-modifier key to complete the shortcut.";
        else if (IsModifierVk(value_.key)) hint = L"The last key of a shortcut can't be a modifier.";
    } else if (IsReservedCombo(value_, &hint)) {
        ok = false;
    }
    SetWindowTextW(hint_, hint.c_str());
    EnableWindow(okBtn_, ok);
}

LRESULT CALLBACK Dialog::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<Dialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<Dialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT Dialog::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_KM_CAPTURE:
            onCapture(static_cast<uint16_t>(wp), (lp & KCF_DOWN) != 0);
            return 0;
        case WM_ACTIVATE:
            // Never keep swallowing the keyboard while another window is active.
            if (LOWORD(wp) == WA_INACTIVE) stopCapture();
            break;
        case WM_COMMAND: {
            const int id = LOWORD(wp), code = HIWORD(wp);
            if (id == IDOK && code == BN_CLICKED) {
                if (IsWindowEnabled(okBtn_)) {
                    ok_ = true;
                    done_ = true;
                }
            } else if (id == IDCANCEL) {
                done_ = true;
            } else if (id == IDC_CAPTURE && code == BN_CLICKED) {
                capturing_ ? stopCapture() : startCapture();
            } else if (id == IDC_CLEAR && code == BN_CLICKED) {
                value_ = KeyCombo{};
                held_.clear();
                refresh();
            } else if (id == IDC_SEARCH && code == EN_CHANGE) {
                fillKeys();
            } else if ((id == IDC_SEARCH && code == EN_SETFOCUS) || (id == IDC_KEYS && code == LBN_SETFOCUS) ||
                       (id >= IDC_MODCB0 && id < IDC_MODCB0 + kModCount && code == CBN_SETFOCUS)) {
                // Using the manual controls needs the keyboard back.
                stopCapture();
            } else if (id == IDC_KEYS && code == LBN_SELCHANGE) {
                const LRESULT i = SendMessageW(keys_, LB_GETCURSEL, 0, 0);
                if (i >= 0) {
                    const auto vk = static_cast<uint16_t>(SendMessageW(keys_, LB_GETITEMDATA, static_cast<WPARAM>(i), 0));
                    if (mode_ == SelectKeyDialog::Mode::SingleKey) value_ = KeyCombo::Single(vk);
                    else if (value_.isSingleKey() && IsModifierVk(value_.key)) value_ = KeyCombo::Single(vk);
                    else value_.key = vk;
                    refresh();
                }
            } else if (id >= IDC_MODCB0 && id < IDC_MODCB0 + kModCount && code == CBN_SELCHANGE) {
                const int i = id - IDC_MODCB0;
                const LRESULT sel = SendMessageW(modCombo_[i], CB_GETCURSEL, 0, 0);
                if (value_.isSingleKey() && IsModifierVk(value_.key)) value_ = KeyCombo{};
                value_.mods[kModOrder[i]] = static_cast<Side>(sel < 0 ? 0 : sel);
                refresh();
            }
            return 0;
        }
        case WM_CLOSE:
            done_ = true;
            return 0;
        case WM_CTLCOLORSTATIC: {
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkColor(dc, GetSysColor(COLOR_WINDOW));
            if (reinterpret_cast<HWND>(lp) == hint_) SetTextColor(dc, RGB(196, 43, 28));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        }
        case WM_DPICHANGED: {
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            fonts_.create(HIWORD(wp));
            ui::SetFontTree(hwnd_, fonts_.normal);
            SendMessageW(display_, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_.large), TRUE);
            SendMessageW(manual_, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_.bold), TRUE);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            layout(true);
            return 0;
        }
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace

bool SelectKeyDialog::Show(HWND owner, InputHook& hook, Mode mode, const std::wstring& title, KeyCombo& value) {
    Dialog d(hook, mode, value);
    if (!d.run(owner, title)) return false;
    value = d.value();
    return true;
}

}  // namespace km
