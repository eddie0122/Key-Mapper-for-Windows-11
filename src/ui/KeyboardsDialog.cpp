#include "ui/KeyboardsDialog.h"

#include "app/App.h"
#include "core/DeviceMatch.h"
#include "ui/Ui.h"
#include "win/KeyboardDevices.h"

#include <commctrl.h>

#include <algorithm>
#include <vector>

namespace km {
namespace {

enum : int { IDC_INTRO = 100, IDC_LIST, IDC_IDENTIFY, IDC_REFRESH, IDC_STATUS, IDC_AUTOSWITCH, IDC_NOTE };
enum Column : int { COL_NAME = 0, COL_STATUS, COL_OWNER };

struct Row {
    std::string id;
    std::wstring name;
    bool connected = false;
    std::string owner;  // Profile the keyboard belongs to now ("" = none).
    bool checked = false;
};

class Dialog {
public:
    Dialog(App& app, std::string profileId) : app_(app), profileId_(std::move(profileId)) {}
    void run(HWND owner);

private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    int layout(bool apply);
    void loadRows();
    void fillList();
    void updateRow(int index);
    std::wstring ownerText(const Row& r) const;
    std::wstring profileName(const std::string& id) const;
    void startIdentify();
    void stopIdentify();
    void onRawInput(HRAWINPUT input);
    bool save();

    App& app_;
    std::string profileId_;
    HWND hwnd_ = nullptr;
    bool done_ = false;
    bool populating_ = false;
    bool identifying_ = false;
    ULONGLONG ignoreKeysUntil_ = 0;
    std::vector<Row> rows_;
    ui::Fonts fonts_;
    HWND intro_ = nullptr, list_ = nullptr, identify_ = nullptr, refresh_ = nullptr, status_ = nullptr;
    HWND autoSwitch_ = nullptr, note_ = nullptr, ok_ = nullptr, cancel_ = nullptr;
};

std::wstring Dialog::profileName(const std::string& id) const {
    const Profile* p = app_.settings().find(id);
    return p ? p->name : std::wstring();
}

void Dialog::loadRows() {
    // Keep the user's ticks across a refresh.
    std::vector<Row> old = std::move(rows_);
    rows_.clear();
    auto previousCheck = [&](const std::string& id, bool fallback) {
        for (const Row& r : old)
            if (r.id == id) return r.checked;
        return fallback;
    };
    const Settings& s = app_.settings();
    for (const auto& [id, name] : app_.connectedKeyboards()) {
        const Profile* owner = ProfileForKeyboard(s, id);
        Row r{id, name, true, owner ? owner->id : std::string()};
        r.checked = previousCheck(id, r.owner == profileId_);
        rows_.push_back(r);
    }
    for (const Profile& p : s.profiles) {
        for (const KeyboardRef& k : p.keyboards) {
            if (std::any_of(rows_.begin(), rows_.end(), [&](const Row& r) { return r.id == k.id; })) continue;
            Row r{k.id, k.name.empty() ? DefaultKeyboardName(k.id) : k.name, false, p.id};
            r.checked = previousCheck(k.id, p.id == profileId_);
            rows_.push_back(r);
        }
    }
    std::stable_sort(rows_.begin(), rows_.end(), [](const Row& a, const Row& b) {
        if (a.connected != b.connected) return a.connected;
        return a.name < b.name;
    });
}

std::wstring Dialog::ownerText(const Row& r) const {
    const bool other = !r.owner.empty() && r.owner != profileId_;
    if (r.checked) return other ? L"Moves here from “" + profileName(r.owner) + L"”" : L"This profile";
    if (other) return profileName(r.owner);
    return r.owner == profileId_ ? L"Will be removed" : L"";
}

void Dialog::fillList() {
    populating_ = true;
    SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list_);
    for (size_t i = 0; i < rows_.size(); ++i) {
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(i);
        item.pszText = const_cast<wchar_t*>(rows_[i].name.c_str());
        item.lParam = static_cast<LPARAM>(i);
        ListView_InsertItem(list_, &item);
        updateRow(static_cast<int>(i));
    }
    SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
    populating_ = false;
    if (rows_.empty())
        SetWindowTextW(status_, L"Windows reports no keyboards that can be told apart right now.");
}

void Dialog::updateRow(int i) {
    const Row& r = rows_[static_cast<size_t>(i)];
    std::wstring status = r.connected ? L"Connected" : L"Not connected";
    std::wstring owner = ownerText(r);
    ListView_SetItemText(list_, i, COL_STATUS, status.data());
    ListView_SetItemText(list_, i, COL_OWNER, owner.data());
    const bool wasPopulating = populating_;
    populating_ = true;
    ListView_SetCheckState(list_, i, r.checked);
    populating_ = wasPopulating;
}

void Dialog::startIdentify() {
    RAWINPUTDEVICE rid{0x01, 0x06, RIDEV_INPUTSINK, hwnd_};  // Generic desktop / keyboard
    if (!RegisterRawInputDevices(&rid, 1, sizeof rid)) {
        SetWindowTextW(status_, L"Windows didn't allow identifying keyboards right now.");
        return;
    }
    identifying_ = true;
    // Remapped keys never reach raw input; let physical keys through meanwhile.
    app_.hook().setEnabled(false);
    SetWindowTextW(identify_, L"Stop identifying");
    SetWindowTextW(status_, L"Press any key on the keyboard you want to find…");
}

void Dialog::stopIdentify() {
    if (!identifying_) return;
    identifying_ = false;
    RAWINPUTDEVICE rid{0x01, 0x06, RIDEV_REMOVE, nullptr};
    RegisterRawInputDevices(&rid, 1, sizeof rid);
    app_.hook().setEnabled(app_.settings().enabled);
    // The identifying keystroke must not also press a button here.
    ignoreKeysUntil_ = GetTickCount64() + 400;
    if (identify_) SetWindowTextW(identify_, L"Identify by typing");
}

void Dialog::onRawInput(HRAWINPUT input) {
    RAWINPUTHEADER header{};
    UINT size = sizeof header;
    if (GetRawInputData(input, RID_HEADER, &header, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1)) return;
    const std::string id = KeyboardIdForRawDevice(header.hDevice);
    if (id.empty()) return;  // Injected or virtual input.
    stopIdentify();
    auto it = std::find_if(rows_.begin(), rows_.end(), [&](const Row& r) { return r.id == id; });
    if (it == rows_.end()) {
        // Connected so recently that the last scan missed it.
        std::wstring name = DefaultKeyboardName(id);
        for (const KeyboardDevice& d : EnumerateKeyboards())
            if (d.id == id) name = d.name;
        const Profile* owner = ProfileForKeyboard(app_.settings(), id);
        rows_.push_back({id, name, true, owner ? owner->id : std::string(), false});
        fillList();
        it = rows_.end() - 1;
    }
    const int index = static_cast<int>(it - rows_.begin());
    ListView_SetItemState(list_, -1, 0, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_SetItemState(list_, index, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    ListView_EnsureVisible(list_, index, FALSE);
    SetFocus(list_);
    SetWindowTextW(status_, (L"You typed on “" + it->name + L"”. Tick it to link it to this profile.").c_str());
}

bool Dialog::save() {
    Settings next = app_.settings();
    Profile* self = next.find(profileId_);
    if (!self) return true;
    std::vector<std::string> newlyAssigned;
    for (const Row& r : rows_) {
        if (r.checked) {
            // A keyboard belongs to one profile: take it from any other.
            for (Profile& p : next.profiles) {
                if (p.id == profileId_) continue;
                auto& kbs = p.keyboards;
                kbs.erase(std::remove_if(kbs.begin(), kbs.end(), [&](const KeyboardRef& k) { return k.id == r.id; }),
                          kbs.end());
            }
            auto& mine = self->keyboards;
            if (std::none_of(mine.begin(), mine.end(), [&](const KeyboardRef& k) { return k.id == r.id; })) {
                mine.push_back({r.id, r.name});
                newlyAssigned.push_back(r.id);
            }
        } else {
            auto& mine = self->keyboards;
            mine.erase(std::remove_if(mine.begin(), mine.end(), [&](const KeyboardRef& k) { return k.id == r.id; }),
                       mine.end());
        }
    }
    if (!(next == app_.settings()) && !app_.commit(std::move(next))) return false;
    app_.setAutoSwitch(SendMessageW(autoSwitch_, BM_GETCHECK, 0, 0) == BST_CHECKED);
    if (app_.settings().autoSwitch) app_.keyboardsAssigned(newlyAssigned);
    return true;
}

void Dialog::run(HWND owner) {
    static const ATOM cls = [] {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = Proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = L"KmKeyboards";
        return RegisterClassExW(&wc);
    }();
    (void)cls;

    const std::wstring title = L"Keyboards for “" + profileName(profileId_) + L"”";
    const DWORD style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
    const DWORD exStyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
    hwnd_ = CreateWindowExW(exStyle, L"KmKeyboards", title.c_str(), style, 0, 0, 100, 100, owner, nullptr,
                            GetModuleHandleW(nullptr), this);
    if (!hwnd_) return;
    fonts_.create(ui::DpiOf(hwnd_));

    intro_ = ui::Child(hwnd_, L"STATIC",
                       (L"Tick the keyboards that should activate “" + profileName(profileId_) +
                        L"” when they connect. When a ticked keyboard disconnects, Keymapper returns to the "
                        L"profile that was active before.")
                           .c_str(),
                       SS_NOPREFIX, IDC_INTRO);
    list_ = ui::Child(hwnd_, WC_LISTVIEWW, L"",
                      WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER, IDC_LIST,
                      WS_EX_CLIENTEDGE);
    ListView_SetExtendedListViewStyle(list_, LVS_EX_CHECKBOXES | LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    identify_ = ui::Child(hwnd_, L"BUTTON", L"Identify by typing", WS_TABSTOP | BS_PUSHBUTTON, IDC_IDENTIFY);
    refresh_ = ui::Child(hwnd_, L"BUTTON", L"Refresh", WS_TABSTOP | BS_PUSHBUTTON, IDC_REFRESH);
    status_ = ui::Child(hwnd_, L"STATIC", L"", SS_NOPREFIX, IDC_STATUS);
    autoSwitch_ = ui::Child(hwnd_, L"BUTTON", L"Switch profiles automatically when keyboards connect (all profiles)",
                            WS_TABSTOP | BS_AUTOCHECKBOX, IDC_AUTOSWITCH);
    SendMessageW(autoSwitch_, BM_SETCHECK, app_.settings().autoSwitch ? BST_CHECKED : BST_UNCHECKED, 0);
    note_ = ui::Child(hwnd_, L"STATIC",
                      L"While a profile is active its mappings apply to every keyboard: Windows doesn't tell apps "
                      L"which keyboard a key came from.",
                      SS_NOPREFIX, IDC_NOTE);
    ok_ = ui::Child(hwnd_, L"BUTTON", L"OK", WS_TABSTOP | BS_DEFPUSHBUTTON, IDOK);
    cancel_ = ui::Child(hwnd_, L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, IDCANCEL);
    ui::SetAccessibleName(list_, L"Keyboards");
    ui::SetFontTree(hwnd_, fonts_.normal);

    const UINT dpi = ui::DpiOf(hwnd_);
    RECT r{0, 0, ui::Scale(600, dpi), layout(false)};
    AdjustWindowRectExForDpi(&r, style, FALSE, exStyle, dpi);
    SetWindowPos(hwnd_, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
    layout(true);

    const wchar_t* headers[] = {L"Keyboard", L"Status", L"Linked to"};
    const int widths[] = {270, 110, 170};
    for (int c = 0; c < 3; ++c) {
        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<wchar_t*>(headers[c]);
        col.cx = ui::Scale(widths[c], dpi);
        ListView_InsertColumn(list_, c, &col);
    }
    loadRows();
    fillList();

    ui::EnableDialogNavigation(hwnd_);
    ui::CenterWindow(hwnd_, owner);
    ShowWindow(hwnd_, SW_SHOW);
    SetFocus(list_);
    ui::RunModal(hwnd_, owner, done_, [this](const MSG& m) {
        return m.message >= WM_KEYFIRST && m.message <= WM_KEYLAST &&
               (identifying_ || GetTickCount64() < ignoreKeysUntil_) && GetAncestor(m.hwnd, GA_ROOT) == hwnd_;
    });
    stopIdentify();
    DestroyWindow(hwnd_);
}

int Dialog::layout(bool apply) {
    const UINT dpi = ui::DpiOf(hwnd_);
    auto S = [dpi](int v) { return ui::Scale(v, dpi); };
    auto place = [&](HWND h, int x, int y, int w, int hh) {
        if (apply) ui::Place(h, S(x), S(y), S(w), S(hh));
    };
    const int W = 600, M = 12, inner = W - 2 * M;
    int y = M;
    place(intro_, M, y, inner, 36);
    y += 42;
    place(list_, M, y, inner, 210);
    y += 218;
    place(identify_, M, y, 170, 30);
    place(refresh_, M + 178, y, 100, 30);
    y += 38;
    place(status_, M, y, inner, 22);
    y += 28;
    place(autoSwitch_, M, y, inner, 24);
    y += 30;
    place(note_, M, y, inner, 36);
    y += 44;
    place(ok_, W - M - 88 - 8 - 88, y, 88, 30);
    place(cancel_, W - M - 88, y, 88, 30);
    y += 30 + M;
    return S(y);
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
        case WM_INPUT:
            if (identifying_) onRawInput(reinterpret_cast<HRAWINPUT>(lp));
            return DefWindowProcW(hwnd_, msg, wp, lp);
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) stopIdentify();
            break;
        case WM_NOTIFY: {
            const auto* nm = reinterpret_cast<const NMLISTVIEW*>(lp);
            if (nm->hdr.hwndFrom == list_ && nm->hdr.code == LVN_ITEMCHANGED && !populating_ &&
                (nm->uChanged & LVIF_STATE) && ((nm->uNewState ^ nm->uOldState) & LVIS_STATEIMAGEMASK)) {
                const auto index = static_cast<size_t>(nm->lParam);
                if (index < rows_.size()) {
                    rows_[index].checked = ListView_GetCheckState(list_, nm->iItem) != FALSE;
                    updateRow(nm->iItem);
                }
            }
            return 0;
        }
        case WM_COMMAND: {
            const int id = LOWORD(wp), code = HIWORD(wp);
            if (id == IDOK && code == BN_CLICKED) {
                stopIdentify();
                if (save()) done_ = true;
            } else if (id == IDCANCEL) {
                done_ = true;
            } else if (id == IDC_IDENTIFY && code == BN_CLICKED) {
                identifying_ ? stopIdentify() : startIdentify();
            } else if (id == IDC_REFRESH && code == BN_CLICKED) {
                loadRows();
                fillList();
                SetWindowTextW(status_, L"");
            }
            return 0;
        }
        case WM_CLOSE:
            done_ = true;
            return 0;
        case WM_CTLCOLORSTATIC:
            SetBkColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_WINDOW));
            if (reinterpret_cast<HWND>(lp) == note_) SetTextColor(reinterpret_cast<HDC>(wp), GetSysColor(COLOR_GRAYTEXT));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        case WM_CTLCOLORBTN:
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        case WM_DPICHANGED: {
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            fonts_.create(HIWORD(wp));
            ui::SetFontTree(hwnd_, fonts_.normal);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            layout(true);
            return 0;
        }
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace

void KeyboardsDialog::Show(HWND owner, App& app, const std::string& profileId) {
    Dialog d(app, profileId);
    d.run(owner);
}

}  // namespace km
