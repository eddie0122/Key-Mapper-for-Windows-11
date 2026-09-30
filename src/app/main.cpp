#include "app/App.h"

#include <commctrl.h>
#include <objbase.h>

#include <cwchar>

namespace {

constexpr const wchar_t* kInstanceMutex = L"Local\\Keymapper.SingleInstance.7C1E3A52";

LONG WINAPI OnCrash(EXCEPTION_POINTERS*) {
    // Never leave injected keys pressed if the process dies.
    km::InputHook::EmergencyRelease();
    return EXCEPTION_CONTINUE_SEARCH;
}

// Asks the running instance to open its editor.
void ActivateExistingInstance() {
    const UINT msg = RegisterWindowMessageW(km::App::kShowMessage);
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (HWND h = FindWindowW(km::App::kControllerClass, nullptr)) {
            DWORD pid = 0;
            GetWindowThreadProcessId(h, &pid);
            AllowSetForegroundWindow(pid);
            PostMessageW(h, msg, 0, 0);
            return;
        }
        Sleep(100);  // The first instance may still be starting.
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR cmdLine, int) {
    SetUnhandledExceptionFilter(OnCrash);

    // One instance per user session; launching again opens the editor.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kInstanceMutex);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        ActivateExistingInstance();
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&icc);

    const bool showEditor = cmdLine && std::wcsstr(cmdLine, L"--show") != nullptr;
    int rc;
    {
        km::App app;
        rc = app.run(showEditor);
    }

    CoUninitialize();
    if (mutex) {
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }
    return rc;
}
