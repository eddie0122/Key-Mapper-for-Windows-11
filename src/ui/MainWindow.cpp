#include "ui/MainWindow.h"

#include "app/App.h"
#include "core/KeyCatalog.h"
#include "ui/SelectKeyDialog.h"
#include "ui/TextPrompt.h"
#include "core/DeviceMatch.h"
#include "ui/KeyboardsDialog.h"
#include "win/Autostart.h"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>

namespace km {
namespace {

enum : int {
    IDC_PROFILES_LABEL = 100,
    IDC_SEARCH,
    IDC_LIST,
    IDC_NEW,
    IDC_RENAME,
    IDC_DUPLICATE,
    IDC_DELETE,
    IDC_TITLE,
    IDC_ACTIVE_LABEL,
    IDC_ACTIVATE,
    IDC_ENABLED,
    IDC_PANEL,
    IDC_STATUS,
    IDC_SAVE,
    IDC_CANCEL,
    IDC_STARTUP,
    IDC_KBD_LABEL,
    IDC_KBD_SUMMARY,
    IDC_KEYBOARDS,
    IDC_HEADER0 = 200,
    IDC_DESC0 = 202,
    IDC_COLSRC0 = 204,
    IDC_COLDST0 = 206,
    IDC_EMPTY0 = 208,
    IDC_ADD0 = 210,
    IDC_NOTES = 212,
};

// Row controls: id = base(section) + row * kStride + part.
constexpr int kRowBase[2] = {10000, 30000};
constexpr int kRowStride = 5;
enum RowPart : int { PART_SRC = 0, PART_ARROW, PART_DST, PART_DEL, PART_ERR };
constexpr int kMaxRowsPerSection = (kRowBase[1] - kRowBase[0]) / kRowStride;

bool DecodeRowId(int id, int& section, size_t& row, int& part) {
    for (int s = 1; s >= 0; --s) {
        if (id >= kRowBase[s] && id < kRowBase[s] + kMaxRowsPerSection * kRowStride) {
            section = s;
            row = static_cast<size_t>((id - kRowBase[s]) / kRowStride);
            part = (id - kRowBase[s]) % kRowStride;
            return true;
        }
    }
    return false;
}

const wchar_t* const kSectionTitle[2] = {L"Remap a key", L"Remap a shortcut"};
const wchar_t* const kSectionDesc[2] = {
    L"Make a single key send another key or a shortcut. Example: Caps Lock → Ctrl, or F1 → Ctrl + C.",
    L"Make a shortcut send a key or another shortcut. Example: Ctrl + J → Enter.",
};
const wchar_t* const kColSrc[2] = {L"Physical key", L"Physical shortcut"};
const wchar_t* const kEmpty[2] = {L"No key remappings yet.", L"No shortcut remappings yet."};
const wchar_t* const kAdd[2] = {L"+  Add key remapping", L"+  Add shortcut remapping"};
const wchar_t* const kRowNoun[2] = {L"Key remapping", L"Shortcut remapping"};
const wchar_t* const kNotes =
    L"Keymapper remaps keys in apps that run with your permissions. To remap keys in apps running as "
    L"administrator, run Keymapper as administrator too. Windows doesn't allow remapping on the sign-in and "
    L"lock screens, in UAC prompts and other secure desktops, or of Ctrl+Alt+Del and Win+L. Some games and "
    L"remote-desktop apps read the keyboard directly and bypass remapping.";

constexpr COLORREF kErrorColor = RGB(196, 43, 28);
constexpr COLORREF kActiveColor = RGB(16, 124, 16);

// Save / Don't save / Cancel. Returns IDYES, IDNO or IDCANCEL.
int AskSaveChanges(HWND owner, const std::wstring& profileName, const wchar_t* context) {
    const std::wstring main = L"Save changes to “" + profileName + L"”?";
    TASKDIALOG_BUTTON buttons[] = {{IDYES, L"Save"}, {IDNO, L"Don't save"}};
    TASKDIALOGCONFIG tdc{};
    tdc.cbSize = sizeof tdc;
    tdc.hwndParent = owner;
    tdc.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW | TDF_ALLOW_DIALOG_CANCELLATION;
    tdc.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    tdc.pszWindowTitle = L"Keymapper";
    tdc.pszMainIcon = TD_WARNING_ICON;
    tdc.pszMainInstruction = main.c_str();
    tdc.pszContent = context;
    tdc.pButtons = buttons;
    tdc.cButtons = ARRAYSIZE(buttons);
    tdc.nDefaultButton = IDYES;
    int pressed = IDCANCEL;
    if (FAILED(TaskDialogIndirect(&tdc, &pressed, nullptr, nullptr)))
        pressed = MessageBoxW(owner, main.c_str(), L"Keymapper", MB_YESNOCANCEL | MB_ICONWARNING);
    return pressed;
}

bool ConfirmAction(HWND owner, const std::wstring& main, const std::wstring& content, const wchar_t* verb) {
    TASKDIALOG_BUTTON buttons[] = {{IDYES, verb}};
    TASKDIALOGCONFIG tdc{};
    tdc.cbSize = sizeof tdc;
    tdc.hwndParent = owner;
    tdc.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW | TDF_ALLOW_DIALOG_CANCELLATION;
    tdc.dwCommonButtons = TDCBF_CANCEL_BUTTON;
    tdc.pszWindowTitle = L"Keymapper";
    tdc.pszMainIcon = TD_WARNING_ICON;
    tdc.pszMainInstruction = main.c_str();
    tdc.pszContent = content.c_str();
    tdc.pButtons = buttons;
    tdc.cButtons = ARRAYSIZE(buttons);
    tdc.nDefaultButton = IDCANCEL;
    int pressed = IDCANCEL;
    if (FAILED(TaskDialogIndirect(&tdc, &pressed, nullptr, nullptr)))
        pressed = MessageBoxW(owner, (main + L"\n\n" + content).c_str(), L"Keymapper",
                              MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
    return pressed == IDYES;
}

std::wstring UniqueName(const Settings& s, const std::wstring& base) {
    auto taken = [&](const std::wstring& n) {
        for (const Profile& p : s.profiles)
            if (p.name == n) return true;
        return false;
    };
    if (!taken(base)) return base;
    for (int i = 2;; ++i) {
        std::wstring n = base + L" " + std::to_wstring(i);
        if (!taken(n)) return n;
    }
}

}  // namespace

MainWindow::~MainWindow() {
    if (hwnd_) DestroyWindow(hwnd_);
}

void MainWindow::create() {
    static const bool registered = [] {
        WNDCLASSEXW wc{sizeof wc};
        wc.lpfnWndProc = Proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.hIcon = ui::AppIcon(false, GetSystemMetrics(SM_CXICON));
        wc.hIconSm = ui::AppIcon(false, GetSystemMetrics(SM_CXSMICON));
        wc.lpszClassName = L"KmMainWindow";
        RegisterClassExW(&wc);
        WNDCLASSEXW pc{sizeof pc};
        pc.lpfnWndProc = PanelProc;
        pc.hInstance = GetModuleHandleW(nullptr);
        pc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        pc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        pc.lpszClassName = L"KmScrollPanel";
        RegisterClassExW(&pc);
        return true;
    }();
    (void)registered;

    BOOL elevated = FALSE;
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION te{};
        DWORD len = 0;
        if (GetTokenInformation(token, TokenElevation, &te, sizeof te, &len)) elevated = te.TokenIsElevated;
        CloseHandle(token);
    }
    const wchar_t* title = elevated ? L"Keymapper (Administrator)" : L"Keymapper";

    hwnd_ = CreateWindowExW(WS_EX_CONTROLPARENT, L"KmMainWindow", title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                            CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, nullptr, nullptr, GetModuleHandleW(nullptr),
                            this);
    const UINT dpi = ui::DpiOf(hwnd_);
    fonts_.create(dpi);

    // Creation order is the Tab order.
    profilesLabel_ = ui::Child(hwnd_, L"STATIC", L"Profiles", SS_NOPREFIX, IDC_PROFILES_LABEL);
    search_ = ui::Child(hwnd_, L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, IDC_SEARCH, WS_EX_CLIENTEDGE);
    SendMessageW(search_, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Search profiles"));
    list_ = ui::Child(hwnd_, L"LISTBOX", L"", WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, IDC_LIST,
                      WS_EX_CLIENTEDGE);
    newBtn_ = ui::Child(hwnd_, L"BUTTON", L"&New", WS_TABSTOP | BS_PUSHBUTTON, IDC_NEW);
    renameBtn_ = ui::Child(hwnd_, L"BUTTON", L"&Rename", WS_TABSTOP | BS_PUSHBUTTON, IDC_RENAME);
    dupBtn_ = ui::Child(hwnd_, L"BUTTON", L"D&uplicate", WS_TABSTOP | BS_PUSHBUTTON, IDC_DUPLICATE);
    deleteBtn_ = ui::Child(hwnd_, L"BUTTON", L"&Delete", WS_TABSTOP | BS_PUSHBUTTON, IDC_DELETE);

    title_ = ui::Child(hwnd_, L"STATIC", L"", SS_NOPREFIX | SS_ENDELLIPSIS, IDC_TITLE);
    activeLabel_ = ui::Child(hwnd_, L"STATIC", L"", SS_NOPREFIX | SS_CENTERIMAGE, IDC_ACTIVE_LABEL);
    activateBtn_ = ui::Child(hwnd_, L"BUTTON", L"&Activate this profile", WS_TABSTOP | BS_PUSHBUTTON, IDC_ACTIVATE);
    enabled_ = ui::Child(hwnd_, L"BUTTON", L"Remapping &enabled", WS_TABSTOP | BS_AUTOCHECKBOX, IDC_ENABLED);
    startup_ = ui::Child(hwnd_, L"BUTTON", L"Start Keymapper when I sign in to &Windows", WS_TABSTOP | BS_AUTOCHECKBOX,
                         IDC_STARTUP);
    kbdLabel_ = ui::Child(hwnd_, L"STATIC", L"Auto-activate with:", SS_NOPREFIX | SS_CENTERIMAGE, IDC_KBD_LABEL);
    kbdSummary_ = ui::Child(hwnd_, L"STATIC", L"", SS_NOPREFIX | SS_CENTERIMAGE | SS_ENDELLIPSIS, IDC_KBD_SUMMARY);
    keyboardsBtn_ = ui::Child(hwnd_, L"BUTTON", L"&Keyboards…", WS_TABSTOP | BS_PUSHBUTTON, IDC_KEYBOARDS);

    panel_ = ui::Child(hwnd_, L"KmScrollPanel", L"Mappings", WS_VSCROLL | WS_CLIPCHILDREN, IDC_PANEL,
                       WS_EX_CONTROLPARENT);
    for (int s = 0; s < 2; ++s) {
        header_[s] = ui::Child(panel_, L"STATIC", kSectionTitle[s], SS_NOPREFIX, IDC_HEADER0 + s);
        desc_[s] = ui::Child(panel_, L"STATIC", kSectionDesc[s], SS_NOPREFIX, IDC_DESC0 + s);
        colSrc_[s] = ui::Child(panel_, L"STATIC", kColSrc[s], SS_NOPREFIX, IDC_COLSRC0 + s);
        colDst_[s] = ui::Child(panel_, L"STATIC", L"Maps to", SS_NOPREFIX, IDC_COLDST0 + s);
        empty_[s] = ui::Child(panel_, L"STATIC", kEmpty[s], SS_NOPREFIX, IDC_EMPTY0 + s);
        add_[s] = ui::Child(panel_, L"BUTTON", kAdd[s], WS_TABSTOP | BS_PUSHBUTTON | BS_NOTIFY, IDC_ADD0 + s);
    }
    notes_ = ui::Child(panel_, L"STATIC", kNotes, SS_NOPREFIX, IDC_NOTES);

    status_ = ui::Child(hwnd_, L"STATIC", L"", SS_NOPREFIX | SS_CENTERIMAGE | SS_ENDELLIPSIS, IDC_STATUS);
    saveBtn_ = ui::Child(hwnd_, L"BUTTON", L"&Save", WS_TABSTOP | BS_PUSHBUTTON, IDC_SAVE);
    cancelBtn_ = ui::Child(hwnd_, L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, IDC_CANCEL);

    ui::SetAccessibleName(search_, L"Search profiles");
    ui::SetAccessibleName(list_, L"Profiles");
    ui::SetAccessibleName(saveBtn_, L"Save mappings");
    ui::SetAccessibleName(cancelBtn_, L"Discard unsaved changes");
    ui::EnableDialogNavigation(hwnd_);

    RECT r{0, 0, ui::Scale(980, dpi), ui::Scale(660, dpi)};
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, WS_EX_CONTROLPARENT, dpi);
    SetWindowPos(hwnd_, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);

    applyFonts();
    viewedId_ = app_.settings().activeProfileId;
    refreshProfileList();
    loadWorkingCopy();
    layout();
}

void MainWindow::applyFonts() {
    ui::SetFontTree(hwnd_, fonts_.normal);
    SendMessageW(title_, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_.title), TRUE);
    SendMessageW(profilesLabel_, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_.bold), TRUE);
    for (int s = 0; s < 2; ++s) SendMessageW(header_[s], WM_SETFONT, reinterpret_cast<WPARAM>(fonts_.bold), TRUE);
}

void MainWindow::show() {
    if (!hwnd_) create();
    // With nothing unsaved, open on the profile that is currently active.
    if (!dirty_ && !IsWindowVisible(hwnd_) && viewedId_ != app_.settings().activeProfileId) {
        viewedId_ = app_.settings().activeProfileId;
        loadWorkingCopy();
        refreshProfileList();
    }
    if (!positioned_) {
        ui::CenterWindow(hwnd_, nullptr);
        positioned_ = true;
    }
    refreshHeader();  // Startup may have been changed in Windows Settings meanwhile.
    if (IsIconic(hwnd_)) ShowWindow(hwnd_, SW_RESTORE);
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
}

// ---- Layout -----------------------------------------------------------------

void MainWindow::layout() {
    if (!hwnd_) return;
    const UINT dpi = ui::DpiOf(hwnd_);
    auto S = [dpi](int v) { return ui::Scale(v, dpi); };
    RECT rc;
    GetClientRect(hwnd_, &rc);
    const int W = rc.right, H = rc.bottom;
    const int M = S(12), leftW = S(250);

    ui::Place(profilesLabel_, M, M, leftW, S(22));
    ui::Place(search_, M, M + S(26), leftW, S(26));
    const int buttonsH = S(30) * 2 + S(6);
    ui::Place(list_, M, M + S(58), leftW, H - M - S(58) - buttonsH - S(10) - M);
    const int bw = (leftW - S(6)) / 2;
    const int by = H - M - buttonsH;
    ui::Place(newBtn_, M, by, bw, S(30));
    ui::Place(renameBtn_, M + bw + S(6), by, bw, S(30));
    ui::Place(dupBtn_, M, by + S(36), bw, S(30));
    ui::Place(deleteBtn_, M + bw + S(6), by + S(36), bw, S(30));

    const int x = M + leftW + S(18);
    const int rw = W - x - M;
    ui::Place(title_, x, M - S(2), rw, S(34));
    ui::Place(activeLabel_, x, M + S(38), rw - S(190), S(26));
    ui::Place(activateBtn_, x + rw - S(180), M + S(36), S(180), S(30));
    ui::Place(enabled_, x, M + S(70), S(190), S(24));
    ui::Place(startup_, x + S(200), M + S(70), rw - S(200), S(24));
    ui::Place(kbdLabel_, x, M + S(100), S(130), S(28));
    ui::Place(kbdSummary_, x + S(134), M + S(100), rw - S(134 + 150), S(28));
    ui::Place(keyboardsBtn_, x + rw - S(140), M + S(99), S(140), S(30));
    const int panelTop = M + S(138);
    const int footerH = S(32);
    ui::Place(panel_, x, panelTop, rw, H - panelTop - footerH - S(12) - M);
    ui::Place(status_, x, H - M - footerH, rw - S(2 * 100 + 16), footerH);
    ui::Place(saveBtn_, x + rw - S(200 + 8), H - M - footerH, S(100), footerH);
    ui::Place(cancelBtn_, x + rw - S(100), H - M - footerH, S(100), footerH);
    layoutPanel();
}

int MainWindow::contentHeight(int) const {
    const UINT dpi = ui::DpiOf(hwnd_);
    auto S = [dpi](int v) { return ui::Scale(v, dpi); };
    int y = S(12);
    for (int s = 0; s < 2; ++s) {
        y += S(28) + S(40) + S(24);
        y += rows_[s].empty() ? S(28) : static_cast<int>(rows_[s].size()) * S(42);
        y += S(32) + S(26);
    }
    return y + S(80) + S(12);
}

void MainWindow::layoutPanel() {
    if (!panel_) return;
    const UINT dpi = ui::DpiOf(hwnd_);
    auto S = [dpi](int v) { return ui::Scale(v, dpi); };
    RECT rc;
    GetClientRect(panel_, &rc);
    const int cw = rc.right, ch = rc.bottom;

    const int total = contentHeight(cw);
    scrollY_ = std::max(0, std::min(scrollY_, total - ch));
    SCROLLINFO si{sizeof si};
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS | SIF_DISABLENOSCROLL;
    si.nMin = 0;
    si.nMax = total - 1;
    si.nPage = static_cast<UINT>(std::max(ch, 0));
    si.nPos = scrollY_;
    SetScrollInfo(panel_, SB_VERT, &si, TRUE);

    const int M = S(12);
    const int arrowW = S(36), delW = S(80), gap = S(10);
    const int avail = cw - 2 * M - arrowW - delW - gap - S(170);
    const int fieldW = std::clamp(avail / 2, S(130), S(250));
    const int xArrow = M + fieldW, xDst = xArrow + arrowW, xDel = xDst + fieldW + gap, xErr = xDel + delW + gap;
    const int errW = std::max(S(80), cw - xErr - M);

    SendMessageW(panel_, WM_SETREDRAW, FALSE, 0);
    int y = S(12) - scrollY_;
    for (int s = 0; s < 2; ++s) {
        ui::Place(header_[s], M, y, cw - 2 * M, S(24));
        y += S(28);
        ui::Place(desc_[s], M, y, cw - 2 * M, S(36));
        y += S(40);
        ui::Place(colSrc_[s], M, y, fieldW, S(20));
        ui::Place(colDst_[s], xDst, y, fieldW, S(20));
        y += S(24);
        ShowWindow(empty_[s], rows_[s].empty() ? SW_SHOWNA : SW_HIDE);
        if (rows_[s].empty()) {
            ui::Place(empty_[s], M, y, cw - 2 * M, S(22));
            y += S(28);
        }
        for (const Row& r : rows_[s]) {
            ui::Place(r.src, M, y, fieldW, S(34));
            ui::Place(r.arrow, xArrow, y, arrowW, S(34));
            ui::Place(r.dst, xDst, y, fieldW, S(34));
            ui::Place(r.del, xDel, y, delW, S(34));
            ui::Place(r.err, xErr, y + S(7), errW, S(34));
            y += S(42);
        }
        ui::Place(add_[s], M, y, S(230), S(32));
        y += S(32) + S(26);
    }
    ui::Place(notes_, M, y, cw - 2 * M, S(80));
    SendMessageW(panel_, WM_SETREDRAW, TRUE, 0);
    RedrawWindow(panel_, nullptr, nullptr, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN);
}

void MainWindow::scrollTo(int y) {
    scrollY_ = y;
    layoutPanel();
}

void MainWindow::ensureVisible(HWND ctrl) {
    RECT rc, pr;
    GetWindowRect(ctrl, &rc);
    MapWindowPoints(nullptr, panel_, reinterpret_cast<POINT*>(&rc), 2);
    GetClientRect(panel_, &pr);
    const int margin = ui::Scale(8, ui::DpiOf(hwnd_));
    if (rc.top < 0) scrollTo(scrollY_ + rc.top - margin);
    else if (rc.bottom > pr.bottom) scrollTo(scrollY_ + rc.bottom - pr.bottom + margin);
}

void MainWindow::fixTabOrder() {
    // Rows are recreated on change; restore reading order for Tab navigation.
    std::vector<HWND> order;
    for (int s = 0; s < 2; ++s) {
        order.insert(order.end(), {header_[s], desc_[s], colSrc_[s], colDst_[s], empty_[s]});
        for (const Row& r : rows_[s]) order.insert(order.end(), {r.src, r.arrow, r.dst, r.del, r.err});
        order.push_back(add_[s]);
    }
    order.push_back(notes_);
    HWND after = HWND_TOP;
    for (HWND h : order) {
        SetWindowPos(h, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        after = h;
    }
}

// ---- Working copy -------------------------------------------------------------

const Profile* MainWindow::viewedProfile() const { return app_.settings().find(viewedId_); }

Profile MainWindow::workingProfile() const {
    Profile p;
    if (const Profile* v = viewedProfile()) p = *v;
    p.keys = work_[0];
    p.shortcuts = work_[1];
    return p;
}

void MainWindow::loadWorkingCopy() {
    const Profile* p = viewedProfile();
    if (!p) {
        viewedId_ = app_.settings().activeProfileId;
        p = viewedProfile();
    }
    if (!p && !app_.settings().profiles.empty()) {
        p = &app_.settings().profiles.front();
        viewedId_ = p->id;
    }
    work_[0] = p ? p->keys : std::vector<Mapping>{};
    work_[1] = p ? p->shortcuts : std::vector<Mapping>{};
    dirty_ = false;
    showMissing_ = false;
    scrollY_ = 0;
    setStatus(L"", false);
    rebuildRows();
    refreshHeader();
}

void MainWindow::rebuildRows() {
    for (int s = 0; s < 2; ++s) {
        for (Row& r : rows_[s])
            for (HWND h : {r.src, r.arrow, r.dst, r.del, r.err}) {
                ui::ClearAccessibleName(h);
                DestroyWindow(h);
            }
        rows_[s].clear();
        const size_t n = std::min<size_t>(work_[s].size(), kMaxRowsPerSection);
        for (size_t i = 0; i < n; ++i) {
            const int base = kRowBase[s] + static_cast<int>(i) * kRowStride;
            Row r;
            r.src = ui::Child(panel_, L"BUTTON", L"", WS_TABSTOP | BS_PUSHBUTTON | BS_NOTIFY, base + PART_SRC);
            r.arrow = ui::Child(panel_, L"STATIC", L"→", SS_CENTER | SS_CENTERIMAGE | SS_NOPREFIX, base + PART_ARROW);
            r.dst = ui::Child(panel_, L"BUTTON", L"", WS_TABSTOP | BS_PUSHBUTTON | BS_NOTIFY, base + PART_DST);
            r.del = ui::Child(panel_, L"BUTTON", L"Delete", WS_TABSTOP | BS_PUSHBUTTON | BS_NOTIFY, base + PART_DEL);
            r.err = ui::Child(panel_, L"STATIC", L"", SS_NOPREFIX, base + PART_ERR);
            for (HWND h : {r.src, r.arrow, r.dst, r.del, r.err})
                SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_.normal), FALSE);
            rows_[s].push_back(r);
        }
    }
    fixTabOrder();
    updateRowTexts();
    updateValidation();
    layoutPanel();
}

void MainWindow::updateRowTexts() {
    for (int s = 0; s < 2; ++s) {
        for (size_t i = 0; i < rows_[s].size(); ++i) {
            const Mapping& m = work_[s][i];
            const std::wstring src = m.from.empty() ? L"Select…" : FormatCombo(m.from);
            const std::wstring dst = m.to.empty() ? L"Select…" : FormatCombo(m.to);
            SetWindowTextW(rows_[s][i].src, src.c_str());
            SetWindowTextW(rows_[s][i].dst, dst.c_str());
            const std::wstring noun = std::wstring(kRowNoun[s]) + L" " + std::to_wstring(i + 1);
            ui::SetAccessibleName(rows_[s][i].src, noun + L", " + kColSrc[s] + L": " +
                                                       (m.from.empty() ? L"not set" : src));
            ui::SetAccessibleName(rows_[s][i].dst, noun + L", maps to: " + (m.to.empty() ? L"not set" : dst));
            ui::SetAccessibleName(rows_[s][i].del, L"Delete " + noun);
        }
    }
}

void MainWindow::updateValidation() {
    const auto issues = ValidateProfile(workingProfile());
    for (int s = 0; s < 2; ++s) rowError_[s].assign(work_[s].size(), L"");
    int visible = 0;
    for (const Issue& i : issues) {
        if (i.missing && !showMissing_) continue;
        rowError_[static_cast<int>(i.section)][i.row] = i.message;
        ++visible;
    }
    for (int s = 0; s < 2; ++s)
        for (size_t i = 0; i < rows_[s].size(); ++i) SetWindowTextW(rows_[s][i].err, rowError_[s][i].c_str());

    if (visible)
        setStatus(visible == 1 ? L"Fix 1 problem before saving." : L"Fix " + std::to_wstring(visible) + L" problems before saving.", true);
    else if (dirty_)
        setStatus(L"Unsaved changes.", false);
    else if (statusError_)
        setStatus(L"", false);
    EnableWindow(saveBtn_, dirty_);
    EnableWindow(cancelBtn_, dirty_);
}

void MainWindow::setStatus(const std::wstring& text, bool error) {
    statusError_ = error;
    if (status_) {
        SetWindowTextW(status_, text.c_str());
        InvalidateRect(status_, nullptr, TRUE);
    }
}

void MainWindow::refreshProfileList() {
    const Settings& s = app_.settings();
    const std::wstring filter = ui::Trim(ui::GetText(search_));
    SendMessageW(list_, WM_SETREDRAW, FALSE, 0);
    SendMessageW(list_, LB_RESETCONTENT, 0, 0);
    SendMessageW(list_, LB_INITSTORAGE, s.profiles.size(), s.profiles.size() * 40 * sizeof(wchar_t));
    int viewedRow = -1;
    for (size_t i = 0; i < s.profiles.size(); ++i) {
        const Profile& p = s.profiles[i];
        if (!ui::ContainsNoCase(p.name, filter)) continue;
        std::wstring text = p.name;
        if (p.id == s.activeProfileId) text += L"   (active)";
        const LRESULT row = SendMessageW(list_, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str()));
        SendMessageW(list_, LB_SETITEMDATA, static_cast<WPARAM>(row), static_cast<LPARAM>(i));
        if (p.id == viewedId_) viewedRow = static_cast<int>(row);
    }
    SendMessageW(list_, LB_SETCURSEL, static_cast<WPARAM>(viewedRow), 0);
    SendMessageW(list_, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list_, nullptr, TRUE);
    std::wstring label = L"Profiles (" + std::to_wstring(s.profiles.size()) + L")";
    SetWindowTextW(profilesLabel_, label.c_str());
    EnableWindow(deleteBtn_, s.profiles.size() > 1);
}

void MainWindow::refreshHeader() {
    const Settings& s = app_.settings();
    const Profile* p = viewedProfile();
    SetWindowTextW(title_, p ? p->name.c_str() : L"");
    const bool active = p && p->id == s.activeProfileId;
    const wchar_t* label = !active       ? L"This profile is not active."
                           : s.enabled   ? L"✔ Active profile — remapping is on"
                                         : L"✔ Active profile — remapping is paused";
    SetWindowTextW(activeLabel_, label);
    InvalidateRect(activeLabel_, nullptr, TRUE);
    ShowWindow(activateBtn_, active ? SW_HIDE : SW_SHOWNA);
    SendMessageW(enabled_, BM_SETCHECK, s.enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(startup_, BM_SETCHECK, autostart::IsEnabled() ? BST_CHECKED : BST_UNCHECKED, 0);

    std::wstring keyboards;
    if (p) {
        for (const KeyboardRef& k : p->keyboards) {
            if (!keyboards.empty()) keyboards += L", ";
            keyboards += k.name.empty() ? DefaultKeyboardName(k.id) : k.name;
            if (app_.connectedKeyboards().count(k.id)) keyboards += L" (connected)";
        }
    }
    if (keyboards.empty()) keyboards = L"No keyboard — activate this profile manually";
    else if (s.switchMode == SwitchMode::Manual) keyboards += L"   [manual switching — links are ignored]";
    else if (s.switchMode == SwitchMode::Typing) keyboards += L"   [switches when you type]";
    SetWindowTextW(kbdSummary_, keyboards.c_str());
}

void MainWindow::onSettingsChanged() {
    if (!hwnd_) return;
    const Profile* p = viewedProfile();
    if (!p) {
        loadWorkingCopy();
    } else if (!dirty_ && (p->keys != work_[0] || p->shortcuts != work_[1])) {
        loadWorkingCopy();
    }
    refreshProfileList();
    refreshHeader();
}

// ---- Commands -------------------------------------------------------------------

bool MainWindow::save() {
    showMissing_ = true;
    updateValidation();
    for (int s = 0; s < 2; ++s) {
        for (size_t i = 0; i < rowError_[s].size(); ++i) {
            if (rowError_[s][i].empty()) continue;
            MessageBeep(MB_ICONWARNING);
            SetFocus(rows_[s][i].src);
            ensureVisible(rows_[s][i].src);
            return false;
        }
    }
    Settings next = app_.settings();
    Profile* p = next.find(viewedId_);
    if (!p) return false;
    p->keys = work_[0];
    p->shortcuts = work_[1];
    if (!app_.commit(std::move(next))) {
        setStatus(L"Not saved. Your changes are still here.", true);
        return false;
    }
    dirty_ = false;
    showMissing_ = false;
    updateValidation();
    setStatus(viewedId_ == app_.settings().activeProfileId ? L"Saved and applied." : L"Saved.", false);
    return true;
}

bool MainWindow::confirmLeave() {
    if (!dirty_) return true;
    const Profile* p = viewedProfile();
    switch (AskSaveChanges(hwnd_, p ? p->name : L"", L"This profile has mappings that haven't been saved.")) {
        case IDYES:
            return save();
        case IDNO:
            loadWorkingCopy();
            return true;
        default:
            return false;
    }
}

void MainWindow::onListSelChange() {
    const LRESULT row = SendMessageW(list_, LB_GETCURSEL, 0, 0);
    if (row < 0) return;
    const auto index = static_cast<size_t>(SendMessageW(list_, LB_GETITEMDATA, static_cast<WPARAM>(row), 0));
    if (index >= app_.settings().profiles.size()) return;
    const std::string id = app_.settings().profiles[index].id;
    if (id == viewedId_) return;
    if (!confirmLeave()) {
        refreshProfileList();  // Put the selection back.
        return;
    }
    viewedId_ = id;
    loadWorkingCopy();
    refreshProfileList();
}

void MainWindow::onNewProfile() {
    if (!confirmLeave()) return;
    std::wstring name = UniqueName(app_.settings(), L"New profile");
    if (!PromptText(hwnd_, L"New profile", L"Profile name:", name)) return;
    Settings next = app_.settings();
    Profile p;
    p.id = NewProfileId();
    p.name = name;
    next.profiles.push_back(p);
    if (!app_.commit(std::move(next))) return;
    SetWindowTextW(search_, L"");
    viewedId_ = p.id;
    loadWorkingCopy();
    refreshProfileList();
}

void MainWindow::onRenameProfile() {
    const Profile* current = viewedProfile();
    if (!current) return;
    std::wstring name = current->name;
    if (!PromptText(hwnd_, L"Rename profile", L"Profile name:", name) || name == current->name) return;
    Settings next = app_.settings();
    next.find(viewedId_)->name = name;
    app_.commit(std::move(next));
    refreshHeader();
}

void MainWindow::onDuplicateProfile() {
    if (!confirmLeave()) return;
    const Profile* current = viewedProfile();
    if (!current) return;
    Settings next = app_.settings();
    Profile copy = *current;
    copy.id = NewProfileId();
    copy.name = UniqueName(next, current->name + L" (copy)");
    copy.keyboards.clear();  // A keyboard activates one profile only.
    const int at = next.indexOf(viewedId_);
    next.profiles.insert(next.profiles.begin() + at + 1, copy);
    if (!app_.commit(std::move(next))) return;
    SetWindowTextW(search_, L"");
    viewedId_ = copy.id;
    loadWorkingCopy();
    refreshProfileList();
}

void MainWindow::onDeleteProfile() {
    const Settings& s = app_.settings();
    const Profile* current = viewedProfile();
    if (!current) return;
    if (s.profiles.size() <= 1) {
        MessageBoxW(hwnd_, L"Keymapper needs at least one profile, so the last profile can't be deleted.",
                    L"Keymapper", MB_OK | MB_ICONINFORMATION);
        return;
    }
    const size_t count = current->keys.size() + current->shortcuts.size();
    const std::wstring content =
        count == 0   ? std::wstring(L"You can't undo this.")
        : count == 1 ? std::wstring(L"Its mapping will be deleted too. You can't undo this.")
                     : L"Its " + std::to_wstring(count) + L" mappings will be deleted too. You can't undo this.";
    if (!ConfirmAction(hwnd_, L"Delete profile “" + current->name + L"”?", content, L"Delete")) return;

    Settings next = s;
    const int at = next.indexOf(viewedId_);
    next.profiles.erase(next.profiles.begin() + at);
    const std::string neighbour = next.profiles[std::min<size_t>(static_cast<size_t>(at), next.profiles.size() - 1)].id;
    if (next.activeProfileId == viewedId_) next.activeProfileId = neighbour;
    // On success the unsaved edits go with the deleted profile.
    if (!app_.commit(std::move(next))) return;
    viewedId_ = neighbour;
    loadWorkingCopy();
    refreshProfileList();
}

void MainWindow::onActivate() {
    if (viewedId_ == app_.settings().activeProfileId) return;
    if (dirty_) {
        const Profile* p = viewedProfile();
        switch (AskSaveChanges(hwnd_, p ? p->name : L"", L"Save before activating, so the profile uses your latest mappings.")) {
            case IDYES:
                if (!save()) return;
                break;
            case IDNO:
                loadWorkingCopy();
                break;
            default:
                return;
        }
    }
    app_.activateProfile(viewedId_);
}

void MainWindow::onCancelEdits() {
    if (!dirty_) return;
    if (!ConfirmAction(hwnd_, L"Discard unsaved changes?", L"The profile goes back to its last saved mappings.",
                       L"Discard"))
        return;
    loadWorkingCopy();
    setStatus(L"Changes discarded.", false);
}

// Takes effect immediately; it is a setting of this PC, not of a profile.
void MainWindow::onStartupToggled() {
    const bool want = SendMessageW(startup_, BM_GETCHECK, 0, 0) == BST_CHECKED;
    std::wstring error;
    if (!autostart::SetEnabled(want, error)) {
        MessageBoxW(hwnd_,
                    (L"Keymapper couldn't change whether it starts with Windows.\n\nReason: " + error).c_str(),
                    L"Keymapper", MB_OK | MB_ICONERROR);
    }
    const bool now = autostart::IsEnabled();
    SendMessageW(startup_, BM_SETCHECK, now ? BST_CHECKED : BST_UNCHECKED, 0);
    if (now == want && !dirty_)
        setStatus(now ? L"Keymapper will start when you sign in to Windows." : L"Keymapper won't start with Windows.",
                  false);
}

void MainWindow::addRow(int section) {
    if (work_[section].size() >= static_cast<size_t>(kMaxRowsPerSection)) return;
    work_[section].push_back({});
    dirty_ = true;
    rebuildRows();
    HWND src = rows_[section].back().src;
    SetFocus(src);
    ensureVisible(rows_[section].back().err);
}

void MainWindow::deleteRow(int section, size_t row) {
    if (row >= work_[section].size()) return;
    work_[section].erase(work_[section].begin() + static_cast<ptrdiff_t>(row));
    dirty_ = true;
    rebuildRows();
    if (row < rows_[section].size()) SetFocus(rows_[section][row].del);
    else if (!rows_[section].empty()) SetFocus(rows_[section].back().del);
    else SetFocus(add_[section]);
}

void MainWindow::editEndpoint(int section, size_t row, bool destination) {
    if (row >= work_[section].size()) return;
    Mapping& m = work_[section][row];
    SelectKeyDialog::Mode mode = destination ? SelectKeyDialog::Mode::KeyOrShortcut
                                 : section == 0 ? SelectKeyDialog::Mode::SingleKey
                                                : SelectKeyDialog::Mode::Shortcut;
    const wchar_t* title = destination ? L"Select what to send"
                           : section == 0 ? L"Select the key to remap"
                                          : L"Select the shortcut to remap";
    KeyCombo value = destination ? m.to : m.from;
    if (!SelectKeyDialog::Show(hwnd_, app_.hook(), mode, title, value)) return;
    KeyCombo& target = destination ? m.to : m.from;
    if (target == value) return;
    target = value;
    dirty_ = true;
    updateRowTexts();
    updateValidation();
}

void MainWindow::onCommand(int id, int code, HWND ctrl) {
    int section = 0, part = 0;
    size_t row = 0;
    if (DecodeRowId(id, section, row, part)) {
        if (code == BN_SETFOCUS) ensureVisible(ctrl);
        else if (code == BN_CLICKED && part == PART_SRC) editEndpoint(section, row, false);
        else if (code == BN_CLICKED && part == PART_DST) editEndpoint(section, row, true);
        else if (code == BN_CLICKED && part == PART_DEL) deleteRow(section, row);
        return;
    }
    switch (id) {
        case IDC_SEARCH:
            if (code == EN_CHANGE) refreshProfileList();
            break;
        case IDC_LIST:
            if (code == LBN_SELCHANGE) onListSelChange();
            else if (code == LBN_DBLCLK) onActivate();
            break;
        case IDC_NEW: onNewProfile(); break;
        case IDC_RENAME: onRenameProfile(); break;
        case IDC_DUPLICATE: onDuplicateProfile(); break;
        case IDC_DELETE: onDeleteProfile(); break;
        case IDC_ACTIVATE: onActivate(); break;
        case IDC_ENABLED:
            if (code == BN_CLICKED) app_.setEnabled(SendMessageW(enabled_, BM_GETCHECK, 0, 0) == BST_CHECKED);
            break;
        case IDC_KEYBOARDS:
            if (code == BN_CLICKED && viewedProfile()) {
                KeyboardsDialog::Show(hwnd_, app_, viewedId_);
                refreshHeader();
            }
            break;
        case IDC_STARTUP:
            if (code == BN_CLICKED) onStartupToggled();
            break;
        case IDC_SAVE: save(); break;
        case IDC_CANCEL: onCancelEdits(); break;
        case IDC_ADD0:
        case IDC_ADD0 + 1:
            if (code == BN_SETFOCUS) ensureVisible(ctrl);
            else if (code == BN_CLICKED) addRow(id - IDC_ADD0);
            break;
        default: break;
    }
}

bool MainWindow::preTranslate(const MSG& msg) {
    if (!hwnd_ || msg.message != WM_KEYDOWN || msg.wParam != 'S' || !(GetKeyState(VK_CONTROL) & 0x8000)) return false;
    if (GetAncestor(msg.hwnd, GA_ROOT) != hwnd_ || !IsWindowEnabled(hwnd_)) return false;
    if (dirty_) save();
    return true;
}

LRESULT MainWindow::onColorStatic(HDC dc, HWND ctrl) {
    SetBkColor(dc, GetSysColor(COLOR_WINDOW));
    SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
    int section = 0, part = 0;
    size_t row = 0;
    const int id = GetDlgCtrlID(ctrl);
    if (DecodeRowId(id, section, row, part) && part == PART_ERR) SetTextColor(dc, kErrorColor);
    else if (ctrl == status_ && statusError_) SetTextColor(dc, kErrorColor);
    else if (ctrl == activeLabel_ && viewedId_ == app_.settings().activeProfileId) SetTextColor(dc, kActiveColor);
    else if (ctrl == desc_[0] || ctrl == desc_[1] || ctrl == notes_ || ctrl == colSrc_[0] || ctrl == colSrc_[1] ||
             ctrl == colDst_[0] || ctrl == colDst_[1] || ctrl == empty_[0] || ctrl == empty_[1])
        SetTextColor(dc, GetSysColor(COLOR_GRAYTEXT));
    return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
}

// ---- Window procedures ------------------------------------------------------------

LRESULT CALLBACK MainWindow::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        self->hwnd_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    return self ? self->handle(msg, wp, lp) : DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MainWindow::handle(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SIZE:
            layout();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            const UINT dpi = ui::DpiOf(hwnd_);
            mmi->ptMinTrackSize.x = ui::Scale(780, dpi);
            mmi->ptMinTrackSize.y = ui::Scale(500, dpi);
            return 0;
        }
        case WM_DPICHANGED: {
            fonts_.create(HIWORD(wp));
            applyFonts();
            for (int s = 0; s < 2; ++s)
                for (const Row& r : rows_[s])
                    for (HWND h : {r.src, r.arrow, r.dst, r.del, r.err})
                        SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(fonts_.normal), FALSE);
            const RECT* r = reinterpret_cast<const RECT*>(lp);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            layout();
            return 0;
        }
        case WM_COMMAND:
            onCommand(LOWORD(wp), HIWORD(wp), reinterpret_cast<HWND>(lp));
            return 0;
        case WM_CTLCOLORSTATIC:
            return onColorStatic(reinterpret_cast<HDC>(wp), reinterpret_cast<HWND>(lp));
        case WM_CTLCOLORBTN:
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_WINDOW));
        case WM_CLOSE:
            // Closing hides to the tray; unsaved edits prompt first.
            if (confirmLeave()) ShowWindow(hwnd_, SW_HIDE);
            return 0;
        case WM_NCDESTROY:
            SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
            hwnd_ = nullptr;
            break;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

LRESULT CALLBACK MainWindow::PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    HWND top = GetParent(hwnd);
    auto* self = top ? reinterpret_cast<MainWindow*>(GetWindowLongPtrW(top, GWLP_USERDATA)) : nullptr;
    if (self && self->panel_ == hwnd) return self->handlePanel(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT MainWindow::handlePanel(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_COMMAND:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
            return handle(msg, wp, lp);
        case WM_SIZE:
            layoutPanel();
            return 0;
        case WM_VSCROLL: {
            SCROLLINFO si{sizeof si};
            si.fMask = SIF_ALL;
            GetScrollInfo(panel_, SB_VERT, &si);
            const int line = ui::Scale(42, ui::DpiOf(hwnd_));
            int y = scrollY_;
            switch (LOWORD(wp)) {
                case SB_LINEUP: y -= line; break;
                case SB_LINEDOWN: y += line; break;
                case SB_PAGEUP: y -= static_cast<int>(si.nPage); break;
                case SB_PAGEDOWN: y += static_cast<int>(si.nPage); break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: y = si.nTrackPos; break;
                case SB_TOP: y = 0; break;
                case SB_BOTTOM: y = si.nMax; break;
                default: return 0;
            }
            scrollTo(y);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            const int delta = GET_WHEEL_DELTA_WPARAM(wp);
            scrollTo(scrollY_ - MulDiv(delta, ui::Scale(42, ui::DpiOf(hwnd_)) * 3, WHEEL_DELTA));
            return 0;
        }
    }
    return DefWindowProcW(panel_, msg, wp, lp);
}

}  // namespace km
