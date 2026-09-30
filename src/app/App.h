#pragma once

#include "core/Model.h"
#include "core/SettingsStore.h"
#include "win/InputHook.h"

#include <windows.h>

#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace km {

class MainWindow;
class ProfileChooser;

// Application lifecycle: settings, the input hook, the tray icon and the
// windows. Everything here runs on the UI thread.
class App {
public:
    // Window class of the hidden controller window; a second instance finds
    // the first through it.
    static constexpr const wchar_t* kControllerClass = L"Keymapper.Controller.7C1E3A52";
    static constexpr const wchar_t* kShowMessage = L"Keymapper.ShowEditor.7C1E3A52";

    App();
    ~App();

    int run(bool showEditorAtStart);

    const Settings& settings() const { return settings_; }
    const std::wstring& settingsPath() const { return settingsPath_; }
    InputHook& hook() { return hook_; }

    // Saves `next` and, when that succeeds, makes it current and applies it.
    // Shows an error on failure. With applyEvenIfUnsaved, `next` becomes
    // current even when it could not be written (used for pause/activate).
    bool commit(Settings next, bool applyEvenIfUnsaved = false);
    void setEnabled(bool enabled);
    // Manual choice from the editor or tray; ends any automatic switch chain.
    void activateProfile(const std::string& id);
    void setSwitchMode(SwitchMode mode);
    // Keyboards were just assigned to a profile: those already connected
    // count as connecting now.
    void keyboardsAssigned(const std::vector<std::string>& ids);
    // Keyboards connected right now (id -> name).
    const std::map<std::string, std::wstring>& connectedKeyboards() const { return connected_; }
    // Routes the id of each keyboard typed on to `sink` instead of switching
    // profiles, until endIdentify. Returns false if Windows refused.
    bool beginIdentify(std::function<void(const std::string&)> sink);
    void endIdentify();

    void showEditor();
    void showProfileChooser();
    void requestExit();

private:
    static LRESULT CALLBACK ControllerProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);

    bool loadSettings();
    void applyToEngine();
    void addTrayIcon();
    void updateTrayIcon();
    void removeTrayIcon();
    void showTrayMenu(POINT pt);
    void showSaveError(const std::wstring& error, bool appliedAnyway);
    void notifyChanged();
    void rescanKeyboards(bool reconcile);
    bool updateRawInput();
    // `foreground`: one of Keymapper's windows had the focus (RIM_INPUT).
    void onRawInput(HRAWINPUT input, bool foreground);
    void showNotification(const std::wstring& text);
    std::wstring keyboardName(const std::string& id) const;
    void shutdown();

    Settings settings_;
    std::unique_ptr<SettingsStore> store_;
    std::wstring settingsPath_;
    InputHook hook_;
    bool hookOk_ = false;

    HWND controller_ = nullptr;
    UINT taskbarCreatedMsg_ = 0;
    UINT showMsg_ = 0;
    HICON trayIcon_ = nullptr;
    bool trayAdded_ = false;
    bool trayPaused_ = false;
    bool exiting_ = false;

    std::unique_ptr<MainWindow> editor_;
    std::unique_ptr<ProfileChooser> chooser_;

    HDEVNOTIFY keyboardNotify_ = nullptr;
    std::map<std::string, std::wstring> connected_;
    bool rawInputOn_ = false;
    std::map<HANDLE, std::string> rawIds_;  // Raw-input device handle -> keyboard id.
    std::string lastTyped_;                 // Keyboard typed on most recently (Typing mode).
    std::function<void(const std::string&)> identifySink_;
};

}  // namespace km
