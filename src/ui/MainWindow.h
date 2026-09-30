#pragma once

#include "core/Model.h"
#include "ui/Ui.h"

#include <windows.h>

#include <array>
#include <string>
#include <vector>

namespace km {

class App;

// The mapping editor: profile list and profile operations on the left, the
// selected profile's "Remap a key" and "Remap a shortcut" rows on the right.
// Row edits stay in a working copy until Save validates and applies them.
class MainWindow {
public:
    explicit MainWindow(App& app) : app_(app) {}
    ~MainWindow();

    void show();
    bool visible() const { return hwnd_ && IsWindowVisible(hwnd_); }
    bool dirty() const { return dirty_; }
    // Offers to save unsaved edits. Returns false if the user cancelled.
    bool confirmLeave();
    // Settings were committed (from here or elsewhere).
    void onSettingsChanged();
    // Window-level shortcuts (Ctrl+S). Returns true if handled.
    bool preTranslate(const MSG& msg);
    HWND hwnd() const { return hwnd_; }

private:
    struct Row {
        HWND src = nullptr, arrow = nullptr, dst = nullptr, del = nullptr, err = nullptr;
    };

    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK PanelProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    LRESULT handlePanel(UINT msg, WPARAM wp, LPARAM lp);
    void onCommand(int id, int code, HWND ctrl);
    LRESULT onColorStatic(HDC dc, HWND ctrl);

    void create();
    void applyFonts();
    void layout();
    int contentHeight(int width) const;
    void layoutPanel();
    void scrollTo(int y);
    void ensureVisible(HWND ctrl);
    void fixTabOrder();

    void loadWorkingCopy();
    Profile workingProfile() const;
    void rebuildRows();
    void updateRowTexts();
    void updateValidation();
    void refreshProfileList();
    void refreshHeader();
    void setStatus(const std::wstring& text, bool error);

    bool save();
    void onListSelChange();
    void onNewProfile();
    void onRenameProfile();
    void onDuplicateProfile();
    void onDeleteProfile();
    void onActivate();
    void onCancelEdits();
    void onStartupToggled();
    void addRow(int section);
    void deleteRow(int section, size_t row);
    void editEndpoint(int section, size_t row, bool destination);

    const Profile* viewedProfile() const;

    App& app_;
    HWND hwnd_ = nullptr;
    ui::Fonts fonts_;
    bool positioned_ = false;

    // Left pane.
    HWND profilesLabel_ = nullptr, search_ = nullptr, list_ = nullptr;
    HWND newBtn_ = nullptr, renameBtn_ = nullptr, dupBtn_ = nullptr, deleteBtn_ = nullptr;
    // Header and footer.
    HWND title_ = nullptr, activeLabel_ = nullptr, activateBtn_ = nullptr, enabled_ = nullptr, startup_ = nullptr;
    HWND kbdLabel_ = nullptr, kbdSummary_ = nullptr, keyboardsBtn_ = nullptr;
    HWND status_ = nullptr, saveBtn_ = nullptr, cancelBtn_ = nullptr;
    // Scrolling mapping panel.
    HWND panel_ = nullptr;
    std::array<HWND, 2> header_{}, desc_{}, colSrc_{}, colDst_{}, empty_{}, add_{};
    HWND notes_ = nullptr;
    int scrollY_ = 0;

    std::string viewedId_;
    std::array<std::vector<Mapping>, 2> work_;
    std::array<std::vector<Row>, 2> rows_;
    std::array<std::vector<std::wstring>, 2> rowError_;
    bool dirty_ = false;
    bool showMissing_ = false;  // Report unfilled rows only after a save attempt.
    bool statusError_ = false;
};

}  // namespace km
