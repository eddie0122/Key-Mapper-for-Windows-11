// End-to-end check of the real low-level hook and SendInput pipeline.
//
// Creates a window with a text box, installs the InputHook with a test
// profile, injects "physical" keystrokes (SendInput without Keymapper's
// marker) and verifies the text the box receives. It refuses to inject
// anything unless its own window is in the foreground, so keystrokes never
// land in another application.

#include "core/Engine.h"
#include "core/KeyCatalog.h"
#include "win/InputHook.h"

#include <windows.h>

#include <cstdio>
#include <string>

using namespace km;

namespace {

HWND g_edit = nullptr;
HWND g_main = nullptr;

void Pump(DWORD ms) {
    const ULONGLONG until = GetTickCount64() + ms;
    MSG msg;
    while (GetTickCount64() < until) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(5);
    }
}

bool Key(uint16_t vk, bool down) {
    if (GetForegroundWindow() != g_main) {
        std::printf("ABORT: test window lost the foreground; not injecting keys.\n");
        return false;
    }
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    in.ki.dwFlags = down ? 0 : KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof in);
    Pump(30);
    return true;
}

bool Tap(uint16_t vk) { return Key(vk, true) && Key(vk, false); }

std::wstring EditText() {
    wchar_t buf[256] = {};
    GetWindowTextW(g_edit, buf, 256);
    return buf;
}

Profile TestProfile() {
    auto combo = [](const char* t) {
        KeyCombo c;
        ParseComboText(t, c);
        return c;
    };
    Profile p;
    p.id = "e2e";
    p.name = L"E2E";
    p.keys = {{combo("A"), combo("B")}, {combo("B"), combo("A")}, {combo("F1"), combo("Shift+X")},
              {combo("CapsLock"), combo("Ctrl")}};
    p.shortcuts = {{combo("Ctrl+J"), combo("Shift+Z")}, {combo("Ctrl+Shift+K"), combo("Q")}};
    return p;
}

}  // namespace

int main() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"KmE2E";
    RegisterClassW(&wc);
    g_main = CreateWindowExW(WS_EX_TOPMOST, L"KmE2E", L"Keymapper E2E test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100,
                             100, 500, 200, nullptr, nullptr, wc.hInstance, nullptr);
    g_edit = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_MULTILINE, 10, 10, 460, 120, g_main,
                             nullptr, wc.hInstance, nullptr);
    SetForegroundWindow(g_main);
    SetFocus(g_edit);
    Pump(300);

    InputHook hook;
    std::wstring err;
    if (!hook.start(err)) {
        std::printf("FAIL: hook did not start: %ls\n", err.c_str());
        return 2;
    }
    hook.setProfile(CompiledProfile::Compile(TestProfile()));
    hook.setEnabled(true);
    Pump(100);

    bool ok = Tap('A') && Tap('B') && Tap('C')                                   // key -> key, swap
              && Key(VK_LSHIFT, true) && Tap('A') && Key(VK_LSHIFT, false)       // modifiers pass through
              && Tap(VK_F1)                                                      // key -> shortcut
              && Key(VK_LCONTROL, true) && Tap('J') && Key(VK_LCONTROL, false)   // shortcut -> shortcut
              && Key(VK_CAPITAL, true) && Tap('J') && Key(VK_CAPITAL, false)     // Caps->Ctrl feeds shortcut
              && Key(VK_LCONTROL, true) && Key(VK_LSHIFT, true) && Tap('K')      // shortcut -> key
              && Key(VK_LSHIFT, false) && Key(VK_LCONTROL, false);
    if (!ok) return 3;
    const std::wstring expectedMapped = L"bacBXZZq";

    hook.setEnabled(false);
    Pump(100);
    ok = Tap('A');  // paused: passes through unchanged
    if (!ok) return 3;
    hook.setEnabled(true);
    Pump(100);
    ok = Tap('A');
    if (!ok) return 3;
    hook.stop();
    Pump(100);

    const std::wstring expected = expectedMapped + L"ab";
    const std::wstring got = EditText();
    int failures = 0;
    if (got != expected) {
        std::printf("FAIL: text box received \"%ls\", expected \"%ls\"\n", got.c_str(), expected.c_str());
        ++failures;
    } else {
        std::printf("ok: text box received \"%ls\"\n", got.c_str());
    }
    for (int vk : {VK_LCONTROL, VK_RCONTROL, VK_LSHIFT, VK_RSHIFT, VK_LMENU, VK_LWIN, int('B'), int('X'),
                   int('Z'), int('Q')}) {
        if (GetAsyncKeyState(vk) & 0x8000) {
            std::printf("FAIL: %s is stuck down\n", KeyId(vk).c_str());
            ++failures;
        }
    }
    if (!failures) std::printf("ok: no keys left pressed\n");
    DestroyWindow(g_main);
    return failures ? 1 : 0;
}
