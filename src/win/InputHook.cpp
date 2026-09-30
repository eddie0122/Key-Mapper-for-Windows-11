#include "win/InputHook.h"

#include "core/SettingsStore.h"

#include <atomic>

namespace km {
namespace {

constexpr UINT WM_KM_WAKE = WM_APP + 1;
constexpr UINT kStaleCheckMs = 1000;

InputHook* g_instance = nullptr;
// Keys this process has injected as down, readable from any thread.
std::atomic<uint8_t> g_injectedDown[256];

bool IsExtendedVk(uint16_t vk) {
    switch (vk) {
        case VK_RCONTROL: case VK_RMENU: case VK_LWIN: case VK_RWIN: case VK_APPS:
        case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
        case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
        case VK_NUMLOCK: case VK_DIVIDE: case VK_SNAPSHOT: case VK_CANCEL:
            return true;
        default:
            return vk >= VK_BROWSER_BACK && vk <= VK_LAUNCH_APP2;
    }
}

INPUT MakeInput(uint16_t vk, bool down, uint16_t scan, bool extended) {
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = vk;
    in.ki.wScan = scan;
    in.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP) | (extended ? KEYEVENTF_EXTENDEDKEY : 0);
    in.ki.dwExtraInfo = InputHook::kMarker;
    return in;
}

INPUT GeneratedInput(uint16_t vk, bool down) {
    if (vk == kMaskVk) return MakeInput(vk, down, 0, false);
    const UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
    const bool extended = (sc & 0xFF00) == 0xE000 || IsExtendedVk(vk);
    return MakeInput(vk, down, static_cast<uint16_t>(sc & 0xFF), extended);
}

}  // namespace

InputHook::~InputHook() { stop(); }

bool InputHook::start(std::wstring& error) {
    if (running()) return true;
    g_instance = this;
    ready_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = std::thread(&InputHook::threadMain, this);
    WaitForSingleObject(ready_, INFINITE);
    CloseHandle(ready_);
    ready_ = nullptr;
    if (!hook_) {
        error = startError_;
        if (thread_.joinable()) thread_.join();
        threadId_ = 0;
        g_instance = nullptr;
        return false;
    }
    return true;
}

void InputHook::stop() {
    if (!thread_.joinable()) return;
    PostThreadMessageW(threadId_, WM_QUIT, 0, 0);
    thread_.join();
    threadId_ = 0;
    g_instance = nullptr;
}

void InputHook::post(Command c) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        queue_.push_back(std::move(c));
    }
    if (threadId_) PostThreadMessageW(threadId_, WM_KM_WAKE, 0, 0);
}

void InputHook::setProfile(std::shared_ptr<const CompiledProfile> profile) {
    post({Command::Type::Profile, std::move(profile)});
}
void InputHook::setEnabled(bool enabled) { post({Command::Type::Enabled, nullptr, enabled}); }
void InputHook::beginCapture(HWND target) { post({Command::Type::CaptureOn, nullptr, true, target}); }
void InputHook::endCapture() { post({Command::Type::CaptureOff}); }
void InputHook::reset() { post({Command::Type::Reset}); }

void InputHook::threadMain() {
    MSG msg;
    // Create the thread's message queue before anyone posts to it.
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    threadId_ = GetCurrentThreadId();
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

    hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, HookProc, GetModuleHandleW(nullptr), 0);
    if (!hook_) startError_ = SettingsStore::ErrorText(GetLastError());
    SetEvent(ready_);
    if (!hook_) return;

    drainCommands();
    const UINT_PTR timer = SetTimer(nullptr, 0, kStaleCheckMs, nullptr);
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.hwnd == nullptr && msg.message == WM_KM_WAKE) {
            drainCommands();
        } else if (msg.hwnd == nullptr && msg.message == WM_TIMER) {
            checkStaleKeys();
        } else {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    KillTimer(nullptr, timer);

    // Leave nothing pressed behind.
    std::vector<OutEvent> out;
    engine_.reset(out);
    send(out);
    UnhookWindowsHookEx(hook_);
    hook_ = nullptr;
}

void InputHook::drainCommands() {
    std::deque<Command> cmds;
    {
        std::lock_guard<std::mutex> lock(mu_);
        cmds.swap(queue_);
    }
    std::vector<OutEvent> out;
    for (Command& c : cmds) {
        switch (c.type) {
            case Command::Type::Profile: engine_.setProfile(std::move(c.profile)); break;
            case Command::Type::Enabled: engine_.setEnabled(c.flag, out); break;
            case Command::Type::CaptureOn:
                captureWnd_ = c.hwnd;
                engine_.setCapture(true, out);
                break;
            case Command::Type::CaptureOff:
                captureWnd_ = nullptr;
                engine_.setCapture(false, out);
                break;
            case Command::Type::Reset: engine_.reset(out); break;
        }
    }
    send(out);
}

// Releases can go missing (secure desktop, elevated window in the
// foreground). For keys that passed through, Windows' own state tells us
// whether the key is still down; two consecutive misses forget it.
void InputHook::checkStaleKeys() {
    for (uint16_t vk : engine_.keysExpectedDown()) {
        if (GetAsyncKeyState(vk) & 0x8000) {
            staleStrikes_[vk] = 0;
        } else if (++staleStrikes_[vk] >= 2) {
            staleStrikes_[vk] = 0;
            engine_.forget(vk);
        }
    }
}

void InputHook::send(const std::vector<OutEvent>& events) {
    if (events.empty()) return;
    std::vector<INPUT> inputs;
    inputs.reserve(events.size());
    for (const OutEvent& o : events)
        inputs.push_back(o.original ? MakeInput(o.vk, o.down, o.scan, o.extended) : GeneratedInput(o.vk, o.down));
    const UINT sent = SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    for (UINT i = 0; i < sent; ++i) {
        const OutEvent& o = events[i];
        if (!o.original && o.vk != kMaskVk) g_injectedDown[o.vk & 0xFF] = o.down ? 1 : 0;
    }
}

LRESULT CALLBACK InputHook::HookProc(int code, WPARAM wp, LPARAM lp) {
    if (g_instance) return g_instance->onKey(code, wp, lp);
    return CallNextHookEx(nullptr, code, wp, lp);
}

LRESULT InputHook::onKey(int code, WPARAM wp, LPARAM lp) {
    if (code != HC_ACTION) return CallNextHookEx(nullptr, code, wp, lp);
    const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);

    // Our own output is never remapped again.
    if ((k->flags & LLKHF_INJECTED) && k->dwExtraInfo == kMarker) return CallNextHookEx(nullptr, code, wp, lp);
    const DWORD vk = k->vkCode;
    if (vk == 0 || vk >= 0xFF || vk == VK_PACKET) return CallNextHookEx(nullptr, code, wp, lp);
    // Synthetic Ctrl that AltGr layouts send before Right Alt.
    if (vk == VK_LCONTROL && (k->scanCode & 0x200)) return CallNextHookEx(nullptr, code, wp, lp);
    // Synthetic Shift releases generated around numpad keys when Num Lock is on.
    if ((vk == VK_LSHIFT || vk == VK_RSHIFT) && (k->flags & LLKHF_EXTENDED))
        return CallNextHookEx(nullptr, code, wp, lp);

    InEvent ev;
    ev.vk = static_cast<uint16_t>(vk);
    ev.scan = static_cast<uint16_t>(k->scanCode);
    ev.down = !(k->flags & LLKHF_UP);
    ev.extended = (k->flags & LLKHF_EXTENDED) != 0;

    Decision d = engine_.process(ev);
    if (d.capture && captureWnd_)
        PostMessageW(captureWnd_, WM_KM_CAPTURE, vk, (ev.down ? KCF_DOWN : 0) | (ev.extended ? KCF_EXTENDED : 0));
    send(d.out);
    if (d.suppress) return 1;
    return CallNextHookEx(nullptr, code, wp, lp);
}

void InputHook::EmergencyRelease() {
    INPUT inputs[256];
    UINT n = 0;
    for (int vk = 0; vk < 256; ++vk)
        if (g_injectedDown[vk].exchange(0)) inputs[n++] = GeneratedInput(static_cast<uint16_t>(vk), false);
    if (n) SendInput(n, inputs, sizeof(INPUT));
}

}  // namespace km
