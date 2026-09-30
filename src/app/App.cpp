#include "app/App.h"

#include "resource.h"
#include "core/DeviceMatch.h"
#include "core/Utf.h"
#include "ui/MainWindow.h"
#include "ui/ProfileChooser.h"
#include "ui/Ui.h"
#include "win/KeyboardDevices.h"

#include <commctrl.h>
#include <dbt.h>
#include <shellapi.h>
#include <windowsx.h>
#include <wtsapi32.h>

#include <algorithm>

namespace km {
namespace {

constexpr UINT WM_KM_TRAY = WM_APP + 10;
constexpr UINT kTrayId = 1;
constexpr size_t kTrayMenuProfiles = 20;
constexpr UINT_PTR kKeyboardTimer = 2;
// One keyboard fires several interface events; act once they settle.
constexpr UINT kKeyboardSettleMs = 700;

enum : UINT { ID_OPEN = 1, ID_CHOOSER, ID_TOGGLE, ID_AUTOSWITCH, ID_EXIT, ID_PROFILE0 = 1000 };

std::wstring ExecutableDirectory() {
    std::wstring path(MAX_PATH, L'\0');
    while (true) {
        DWORD n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (n < path.size()) {
            path.resize(n);
            break;
        }
        path.resize(path.size() * 2);
    }
    const size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? L"." : path.substr(0, slash);
}

std::wstring MenuText(const std::wstring& s) {
    std::wstring out;
    for (wchar_t c : s) {
        if (c == L'&') out += L'&';
        out += c;
    }
    return out;
}

}  // namespace

App::App() = default;

int App::run(bool showEditorAtStart) {
    if (!loadSettings()) return 1;

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = ControllerProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kControllerClass;
    RegisterClassExW(&wc);
    // A hidden top-level window (not message-only) so a second instance can
    // find it and Explorer can broadcast TaskbarCreated to it.
    controller_ = CreateWindowExW(0, kControllerClass, L"Keymapper", WS_OVERLAPPED, 0, 0, 0, 0, nullptr, nullptr,
                                  GetModuleHandleW(nullptr), this);
    if (!controller_) return 1;

    taskbarCreatedMsg_ = RegisterWindowMessageW(L"TaskbarCreated");
    showMsg_ = RegisterWindowMessageW(kShowMessage);
    ChangeWindowMessageFilterEx(controller_, taskbarCreatedMsg_, MSGFLT_ALLOW, nullptr);
    ChangeWindowMessageFilterEx(controller_, showMsg_, MSGFLT_ALLOW, nullptr);
    WTSRegisterSessionNotification(controller_, NOTIFY_FOR_THIS_SESSION);

    editor_ = std::make_unique<MainWindow>(*this);
    chooser_ = std::make_unique<ProfileChooser>(*this);

    applyToEngine();
    std::wstring error;
    hookOk_ = hook_.start(error);
    if (!hookOk_) {
        MessageBoxW(nullptr,
                    (L"Keymapper couldn't install its keyboard hook, so keys won't be remapped.\n\n" + error).c_str(),
                    L"Keymapper", MB_OK | MB_ICONERROR);
    }
    addTrayIcon();
    keyboardNotify_ = WatchKeyboards(controller_);
    rescanKeyboards(/*reconcile=*/true);
    if (showEditorAtStart) showEditor();

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (editor_ && editor_->preTranslate(msg)) continue;
        if (ui::RouteDialogMessage(msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    shutdown();
    return 0;
}

bool App::loadSettings() {
    store_ = std::make_unique<SettingsStore>(ExecutableDirectory());
    settingsPath_ = store_->filePath();
    SettingsStore::LoadResult r = store_->load();

    auto restoreBackup = [&] {
        settings_ = r.backup;
        std::wstring err;
        if (!store_->save(settings_, err)) showSaveError(err, true);
    };
    auto startPausedEmpty = [&] {
        settings_ = DefaultSettings();
        settings_.enabled = false;
    };

    switch (r.status) {
        case SettingsStore::LoadStatus::Loaded:
            settings_ = std::move(r.settings);
            break;
        case SettingsStore::LoadStatus::Missing:
            if (r.backupValid &&
                MessageBoxW(nullptr,
                            (L"Keymapper's settings file is missing:\n" + settingsPath_ +
                             L"\n\nA backup from an earlier save was found. Restore your profiles from it?")
                                .c_str(),
                            L"Keymapper", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                restoreBackup();
            } else {
                settings_ = DefaultSettings();  // First launch.
            }
            break;
        case SettingsStore::LoadStatus::Corrupt: {
            std::wstring text = L"Keymapper couldn't read its settings file:\n" + settingsPath_ + L"\n\nReason: " +
                                Utf8ToWide(r.error);
            if (!r.preservedCopy.empty())
                text += L"\n\nThe original file is kept unchanged, and a copy was saved as:\n" + r.preservedCopy;
            if (r.backupValid) {
                text += L"\n\nRestore your profiles from the last good backup?\n\nIf you choose No, Keymapper starts "
                        L"with remapping paused and an empty profile.";
                if (MessageBoxW(nullptr, text.c_str(), L"Keymapper", MB_YESNO | MB_ICONWARNING) == IDYES)
                    restoreBackup();
                else
                    startPausedEmpty();
            } else {
                text += L"\n\nNo usable backup was found. Keymapper will start with remapping paused and an empty "
                        L"profile.";
                MessageBoxW(nullptr, text.c_str(), L"Keymapper", MB_OK | MB_ICONWARNING);
                startPausedEmpty();
            }
            break;
        }
    }
    return true;
}

void App::applyToEngine() {
    const Profile* p = settings_.find(settings_.activeProfileId);
    hook_.setProfile(p ? CompiledProfile::Compile(*p) : nullptr);
    hook_.setEnabled(settings_.enabled);
    updateTrayIcon();
}

bool App::commit(Settings next, bool applyEvenIfUnsaved) {
    std::wstring error;
    const bool saved = store_->save(next, error);
    if (!saved) {
        showSaveError(error, applyEvenIfUnsaved);
        if (!applyEvenIfUnsaved) return false;
    }
    settings_ = std::move(next);
    applyToEngine();
    notifyChanged();
    return saved;
}

void App::setEnabled(bool enabled) {
    if (settings_.enabled == enabled) return;
    Settings next = settings_;
    next.enabled = enabled;
    commit(std::move(next), true);
}

void App::activateProfile(const std::string& id) {
    if (!settings_.find(id)) return;
    if (settings_.activeProfileId == id && settings_.switchStack.empty()) return;
    Settings next = settings_;
    next.activeProfileId = id;
    autoswitch::OnManualActivation(next);
    commit(std::move(next), true);
}

void App::setAutoSwitch(bool enabled) {
    if (settings_.autoSwitch == enabled) return;
    Settings next = settings_;
    next.autoSwitch = enabled;
    if (!enabled) next.switchStack.clear();
    commit(std::move(next), true);
    if (enabled) rescanKeyboards(/*reconcile=*/true);
}

void App::keyboardsAssigned(const std::vector<std::string>& ids) {
    Settings next = settings_;
    std::string switchedBy;
    for (const std::string& id : ids)
        if (connected_.count(id) && autoswitch::OnConnected(next, id)) switchedBy = id;
    if (next == settings_) return;
    const std::string target = next.activeProfileId;
    commit(std::move(next), true);
    if (!switchedBy.empty())
        if (const Profile* p = settings_.find(target))
            showNotification(keyboardName(switchedBy) + L" is connected —switched to " + p->name + L".");
}

std::wstring App::keyboardName(const std::string& id) const {
    if (auto it = connected_.find(id); it != connected_.end()) return it->second;
    for (const Profile& p : settings_.profiles)
        for (const KeyboardRef& k : p.keyboards)
            if (k.id == id && !k.name.empty()) return k.name;
    return DefaultKeyboardName(id);
}

// Compares the keyboards present now with the last scan and applies
// automatic switches. With `reconcile`, re-evaluates everything (startup).
void App::rescanKeyboards(bool reconcile) {
    std::map<std::string, std::wstring> now;
    for (KeyboardDevice& d : EnumerateKeyboards()) now[d.id] = std::move(d.name);

    Settings next = settings_;
    std::wstring message;
    auto profileName = [&](const std::string& id) {
        const Profile* p = next.find(id);
        return p ? p->name : std::wstring();
    };
    if (reconcile) {
        std::set<std::string> ids;
        for (const auto& kv : now) ids.insert(kv.first);
        if (autoswitch::Reconcile(next, ids) && !next.switchStack.empty()) {
            const std::string& dev = next.switchStack.back().deviceId;
            message = (now.count(dev) ? now[dev] : keyboardName(dev)) + L" is connected —switched to " +
                      profileName(next.activeProfileId) + L".";
        }
    } else {
        for (const auto& [id, name] : connected_)
            if (!now.count(id) && autoswitch::OnDisconnected(next, id))
                message = name + L" disconnected —back to " + profileName(next.activeProfileId) + L".";
        for (const auto& [id, name] : now)
            if (!connected_.count(id) && autoswitch::OnConnected(next, id))
                message = name + L" connected —switched to " + profileName(next.activeProfileId) + L".";
    }
    connected_ = std::move(now);
    if (!(next == settings_)) commit(std::move(next), true);
    if (!message.empty()) showNotification(message);
    if (editor_) editor_->onSettingsChanged();  // Refresh "connected" states.
}

void App::showNotification(const std::wstring& text) {
    if (!trayAdded_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof nid;
    nid.hWnd = controller_;
    nid.uID = kTrayId;
    nid.uFlags = NIF_INFO;
    nid.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    wcsncpy_s(nid.szInfoTitle, L"Keymapper", _TRUNCATE);
    wcsncpy_s(nid.szInfo, text.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void App::notifyChanged() {
    if (editor_) editor_->onSettingsChanged();
    if (chooser_) chooser_->refresh();
}

void App::showSaveError(const std::wstring& error, bool appliedAnyway) {
    std::wstring text = L"Keymapper couldn't save its settings to:\n" + settingsPath_ + L"\n\nReason: " + error +
                        L"\n\nKeymapper keeps its settings in the same folder as Keymapper.exe and never stores "
                        L"them anywhere else. Move Keymapper.exe (together with keymapper.settings.json, if it "
                        L"exists) to a folder you can write to, such as one in Documents, and start it from there.";
    text += appliedAnyway ? L"\n\nThe change is in effect now, but it will be lost when Keymapper exits."
                          : L"\n\nNothing was changed. Your unsaved edits are still in the editor.";
    HWND owner = editor_ && editor_->visible() ? editor_->hwnd() : nullptr;
    MessageBoxW(owner, text.c_str(), L"Keymapper — settings not saved", MB_OK | MB_ICONERROR);
}

void App::showEditor() {
    if (editor_) editor_->show();
}

void App::showProfileChooser() {
    if (chooser_) chooser_->show();
}

void App::requestExit() {
    if (editor_ && editor_->hwnd() && !IsWindowEnabled(editor_->hwnd())) {
        // A dialog is open over the editor; let the user finish it first.
        editor_->show();
        MessageBeep(MB_ICONWARNING);
        return;
    }
    if (editor_ && editor_->dirty()) {
        editor_->show();
        if (!editor_->confirmLeave()) return;
    }
    shutdown();
    PostQuitMessage(0);
}

void App::shutdown() {
    if (exiting_) return;
    exiting_ = true;
    if (keyboardNotify_) {
        UnregisterDeviceNotification(keyboardNotify_);
        keyboardNotify_ = nullptr;
    }
    // Stopping the hook releases every key Keymapper is holding down.
    hook_.stop();
    removeTrayIcon();
}

App::~App() {
    shutdown();
    chooser_.reset();
    editor_.reset();
    if (controller_) {
        WTSUnRegisterSessionNotification(controller_);
        DestroyWindow(controller_);
        controller_ = nullptr;
    }
    if (trayIcon_) {
        DestroyIcon(trayIcon_);
        trayIcon_ = nullptr;
    }
}

// ---- Tray ---------------------------------------------------------------------

void App::addTrayIcon() {
    if (!controller_) return;
    if (trayIcon_) DestroyIcon(trayIcon_);
    trayIcon_ = nullptr;
    LoadIconMetric(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(settings_.enabled ? IDI_APP : IDI_PAUSED), LIM_SMALL,
                   &trayIcon_);
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof nid;
    nid.hWnd = controller_;
    nid.uID = kTrayId;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_KM_TRAY;
    nid.hIcon = trayIcon_;
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    if (trayAdded_) {
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
        trayPaused_ = !settings_.enabled;
        updateTrayIcon();
    }
}

void App::updateTrayIcon() {
    if (!trayAdded_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof nid;
    nid.hWnd = controller_;
    nid.uID = kTrayId;
    nid.uFlags = NIF_TIP | NIF_SHOWTIP;
    const bool paused = !settings_.enabled;
    if (paused != trayPaused_) {
        HICON icon = nullptr;
        LoadIconMetric(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(paused ? IDI_PAUSED : IDI_APP), LIM_SMALL, &icon);
        if (icon) {
            nid.uFlags |= NIF_ICON;
            nid.hIcon = icon;
            if (trayIcon_) DestroyIcon(trayIcon_);
            trayIcon_ = icon;
        }
        trayPaused_ = paused;
    }
    const Profile* p = settings_.find(settings_.activeProfileId);
    std::wstring tip = L"Keymapper — " + (p ? p->name : std::wstring(L"no profile"));
    if (paused) tip += L" (paused)";
    if (tip.size() >= ARRAYSIZE(nid.szTip)) tip = tip.substr(0, ARRAYSIZE(nid.szTip) - 2) + L"…";
    wcsncpy_s(nid.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void App::removeTrayIcon() {
    if (!trayAdded_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof nid;
    nid.hWnd = controller_;
    nid.uID = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
    trayAdded_ = false;
}

void App::showTrayMenu(POINT pt) {
    HMENU menu = CreatePopupMenu();
    HMENU profiles = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, ID_OPEN, L"&Open Keymapper");
    SetMenuDefaultItem(menu, ID_OPEN, FALSE);

    // Quick picks for the first profiles; the chooser window reaches all of them.
    const size_t n = std::min(settings_.profiles.size(), kTrayMenuProfiles);
    for (size_t i = 0; i < n; ++i) {
        const Profile& p = settings_.profiles[i];
        const UINT flags = MF_STRING | (p.id == settings_.activeProfileId ? MF_CHECKED : 0);
        AppendMenuW(profiles, flags, ID_PROFILE0 + i, MenuText(p.name).c_str());
    }
    if (n) AppendMenuW(profiles, MF_SEPARATOR, 0, nullptr);
    std::wstring all = settings_.profiles.size() > n
                           ? L"&All profiles (" + std::to_wstring(settings_.profiles.size()) + L")…"
                           : std::wstring(L"&Search profiles…");
    AppendMenuW(profiles, MF_STRING, ID_CHOOSER, all.c_str());
    const Profile* active = settings_.find(settings_.activeProfileId);
    const std::wstring label = L"&Profiles" + (active ? L" — " + MenuText(active->name) : std::wstring());
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(profiles), label.c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_TOGGLE, settings_.enabled ? L"P&ause remapping" : L"&Resume remapping");
    AppendMenuW(menu, MF_STRING | (settings_.autoSwitch ? MF_CHECKED : 0), ID_AUTOSWITCH,
                L"Switch profiles &automatically");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, ID_EXIT, L"E&xit");

    SetForegroundWindow(controller_);
    const UINT align = GetSystemMetrics(SM_MENUDROPALIGNMENT) ? TPM_RIGHTALIGN : TPM_LEFTALIGN;
    const UINT cmd = static_cast<UINT>(TrackPopupMenuEx(
        menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | align, pt.x, pt.y, controller_, nullptr));
    PostMessageW(controller_, WM_NULL, 0, 0);
    DestroyMenu(menu);  // Also destroys the submenu.

    if (cmd == ID_OPEN) {
        showEditor();
    } else if (cmd == ID_CHOOSER) {
        showProfileChooser();
    } else if (cmd == ID_TOGGLE) {
        setEnabled(!settings_.enabled);
    } else if (cmd == ID_AUTOSWITCH) {
        setAutoSwitch(!settings_.autoSwitch);
    } else if (cmd == ID_EXIT) {
        requestExit();
    } else if (cmd >= ID_PROFILE0 && cmd < ID_PROFILE0 + n) {
        activateProfile(settings_.profiles[cmd - ID_PROFILE0].id);
    }
}

// ---- Controller window ------------------------------------------------------------

LRESULT CALLBACK App::ControllerProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams));
    }
    auto* self = reinterpret_cast<App*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self && self->controller_ == hwnd) return self->handle(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT App::handle(UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KM_TRAY) {
        switch (LOWORD(lp)) {
            // Left click, keyboard selection and right click all open the tray
            // menu at the icon; "Open Keymapper" in it opens the editor.
            case NIN_SELECT:
            case NIN_KEYSELECT:
            case WM_CONTEXTMENU:
                showTrayMenu({GET_X_LPARAM(wp), GET_Y_LPARAM(wp)});
                break;
        }
        return 0;
    }
    if (msg == taskbarCreatedMsg_ && taskbarCreatedMsg_) {
        // Explorer restarted: the notification area forgot our icon.
        trayAdded_ = false;
        addTrayIcon();
        return 0;
    }
    if (msg == showMsg_ && showMsg_) {
        showEditor();
        return 0;
    }
    switch (msg) {
        case WM_DEVICECHANGE:
            if (wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE || wp == DBT_DEVNODES_CHANGED)
                SetTimer(controller_, kKeyboardTimer, kKeyboardSettleMs, nullptr);
            return TRUE;
        case WM_TIMER:
            if (wp == kKeyboardTimer) {
                KillTimer(controller_, kKeyboardTimer);
                rescanKeyboards(/*reconcile=*/false);
                return 0;
            }
            break;
        case WM_WTSSESSION_CHANGE:
            // Key releases are lost while the session is locked or switched.
            if (wp == WTS_SESSION_LOCK || wp == WTS_SESSION_UNLOCK || wp == WTS_CONSOLE_DISCONNECT ||
                wp == WTS_CONSOLE_CONNECT || wp == WTS_REMOTE_CONNECT || wp == WTS_REMOTE_DISCONNECT)
                hook_.reset();
            return 0;
        case WM_POWERBROADCAST:
            if (wp == PBT_APMRESUMEAUTOMATIC) hook_.reset();
            return TRUE;
        case WM_QUERYENDSESSION:
            return TRUE;
        case WM_ENDSESSION:
            if (wp) shutdown();
            return 0;
    }
    return DefWindowProcW(controller_, msg, wp, lp);
}

}  // namespace km
