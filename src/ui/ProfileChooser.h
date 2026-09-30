#pragma once

#include <windows.h>

namespace km {

class App;

// Small searchable, scrollable profile list opened from the tray menu, so
// any number of profiles stays reachable without a giant popup menu.
class ProfileChooser {
public:
    explicit ProfileChooser(App& app) : app_(app) {}
    ~ProfileChooser();

    void show();
    void refresh();

private:
    static LRESULT CALLBACK Proc(HWND, UINT, WPARAM, LPARAM);
    LRESULT handle(UINT msg, WPARAM wp, LPARAM lp);
    void create();
    void layout();
    void activateSelection();

    App& app_;
    HWND hwnd_ = nullptr;
    HWND search_ = nullptr, list_ = nullptr, activate_ = nullptr, open_ = nullptr;
    HFONT font_ = nullptr;
};

}  // namespace km
