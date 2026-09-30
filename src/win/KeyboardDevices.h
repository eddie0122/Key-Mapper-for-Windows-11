#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace km {

struct KeyboardDevice {
    std::string id;     // KeyboardIdFromInterfacePath
    std::wstring name;  // Best friendly name Windows knows
};

// Keyboards currently present, one entry per physical keyboard (a keyboard
// with several HID interfaces is listed once). Virtual keyboards are skipped.
std::vector<KeyboardDevice> EnumerateKeyboards();

// Sends WM_DEVICECHANGE to `hwnd` when keyboards arrive or leave.
HDEVNOTIFY WatchKeyboards(HWND hwnd);

// Keyboard id for a raw-input device handle (WM_INPUT's RAWINPUTHEADER::hDevice);
// empty for injected input or unknown devices.
std::string KeyboardIdForRawDevice(HANDLE device);

}  // namespace km
