#pragma once

#include "core/Engine.h"

#include <windows.h>

#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace km {

// Posted to the capture window for every key event while capturing.
// wParam = virtual key, lParam = KeyCaptureFlags.
constexpr UINT WM_KM_CAPTURE = WM_APP + 50;
enum KeyCaptureFlags : LPARAM { KCF_DOWN = 1, KCF_EXTENDED = 2 };

// Owns the dedicated input thread: installs WH_KEYBOARD_LL, runs the Engine
// on every event, and injects output with SendInput. The hook callback never
// touches the disk or blocks on the UI; all configuration arrives as queued
// commands.
class InputHook {
public:
    // dwExtraInfo stamped on every injected event so the hook can recognise
    // (and never remap) its own output.
    static constexpr ULONG_PTR kMarker = 0x4B4D4150;  // "KMAP"

    InputHook() = default;
    ~InputHook();
    InputHook(const InputHook&) = delete;
    InputHook& operator=(const InputHook&) = delete;

    bool start(std::wstring& error);
    // Releases every key the engine holds, removes the hook and joins.
    void stop();
    bool running() const { return threadId_ != 0; }

    void setProfile(std::shared_ptr<const CompiledProfile> profile);
    void setEnabled(bool enabled);
    void beginCapture(HWND target);
    void endCapture();
    // Drop all key state, e.g. after a session lock hid key releases.
    void reset();

    // Best-effort release of injected keys from a crash handler.
    static void EmergencyRelease();

private:
    struct Command {
        enum class Type { Profile, Enabled, CaptureOn, CaptureOff, Reset } type;
        std::shared_ptr<const CompiledProfile> profile;
        bool flag = false;
        HWND hwnd = nullptr;
    };

    void post(Command c);
    void threadMain();
    void drainCommands();
    void checkStaleKeys();
    void send(const std::vector<OutEvent>& events);
    LRESULT onKey(int code, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK HookProc(int code, WPARAM wp, LPARAM lp);

    std::thread thread_;
    DWORD threadId_ = 0;
    HANDLE ready_ = nullptr;
    HHOOK hook_ = nullptr;
    std::wstring startError_;

    std::mutex mu_;
    std::deque<Command> queue_;

    // Owned by the hook thread.
    Engine engine_;
    HWND captureWnd_ = nullptr;
    std::array<uint8_t, 256> staleStrikes_{};
};

}  // namespace km
