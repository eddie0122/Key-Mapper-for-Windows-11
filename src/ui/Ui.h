#pragma once

#include <windows.h>

#include <functional>
#include <string>

namespace km::ui {

// Layout is written in 96-DPI units and scaled for the window's monitor.
inline int Scale(int dip, UINT dpi) { return MulDiv(dip, static_cast<int>(dpi), 96); }
UINT DpiOf(HWND hwnd);

// Fonts derived from the system message font at a given DPI.
struct Fonts {
    HFONT normal = nullptr;
    HFONT bold = nullptr;
    HFONT title = nullptr;
    HFONT large = nullptr;
    void create(UINT dpi);
    void destroy();
    ~Fonts() { destroy(); }
};

HWND Child(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD exStyle = 0);
void Place(HWND h, int x, int y, int w, int hgt);
void SetFontTree(HWND parent, HFONT font);
std::wstring GetText(HWND h);
std::wstring Trim(const std::wstring& s);
bool ContainsNoCase(const std::wstring& haystack, const std::wstring& needle);

// Sets the name screen readers announce for a control.
void SetAccessibleName(HWND h, const std::wstring& name);
// Drops the annotation before a control is destroyed.
void ClearAccessibleName(HWND h);

// Keyboard navigation (Tab, arrows, Enter/Esc) for our dialog-like windows:
// mark the top-level window, then route messages through RouteDialogMessage.
void EnableDialogNavigation(HWND top);
bool RouteDialogMessage(MSG& msg);

// Runs a nested message loop until `done` becomes true. The owner is disabled
// meanwhile, so the dialog behaves modally.
void RunModal(HWND dialog, HWND owner, const bool& done, const std::function<bool(const MSG&)>& filter = {});

// Centers `hwnd` over `owner`, or on the monitor under the cursor.
void CenterWindow(HWND hwnd, HWND owner);

HICON AppIcon(bool paused, int size);

}  // namespace km::ui
