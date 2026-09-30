# Windows Keymapper — Product Plan

## Summary

Build a portable Windows 11 x64 desktop application for system-wide keyboard remapping. Distribute one executable requiring no installer, runtime installation, or driver.

Users can create any number of profiles, activate one at a time, and configure mappings through a GUI modeled on the [PowerToys Keyboard Manager workflow](https://learn.microsoft.com/en-us/windows/powertoys/keyboard-manager).

## Product behavior

- Start in the system tray without opening the main window. Single-clicking the tray icon opens the editor.
- Provide a tray menu with Open, Profiles, Pause/Resume Remapping, and Exit. Use a scrollable profile chooser so many profiles remain accessible.
- Closing the window hides it in the tray. Exit stops remapping and terminates the application.
- Allow only one running instance per user session; launching again opens the existing window.
- Restore the last active profile and enabled/paused state on launch. First launch creates an empty “Default” profile.
- Support creating, renaming, duplicating, deleting, searching, and activating profiles without an application-defined count limit. Keep at least one profile; require confirmation before deletion.
- Store settings and profiles in `keymapper.settings.json` beside the executable. The distribution is one file; the settings file is created during use.

## Mapping editor and semantics

Use a profile selector, active-profile indicator, enable/pause control, and two mapping sections: **Remap a key** and **Remap a shortcut**.

Each row contains source, destination, and Delete controls. Users can capture input with a Select dialog or choose keys from searchable lists. Add Mapping, Save, and Cancel complete the editing flow. Unsaved changes prompt before switching profiles or closing the editor.

Support all four mapping forms:

| Form | Example |
|---|---|
| Key → key | `Caps Lock → Ctrl` |
| Key → shortcut | `F1 → Ctrl+C` |
| Shortcut → key | `Ctrl+J → Enter` |
| Shortcut → shortcut | `Ctrl+Shift+J → Alt+Tab` |

- A shortcut contains one or more modifiers—Ctrl, Alt, Shift, Win—and one non-modifier action key.
- Tab and Caps Lock are valid individual keys or action keys. They become modifiers only when explicitly remapped to a modifier.
- Allow generic or left/right source modifiers. Generic destination modifiers emit the left variant.
- Match shortcut modifiers exactly; additional modifiers prevent a shortcut match. Side-specific matches take precedence over generic matches.
- Evaluate shortcuts before ordinary action-key remapping. Key-to-modifier mappings contribute to the effective modifier state, allowing `Caps Lock → Ctrl` to work with `Ctrl+J`.
- Do not recursively remap generated output. Permit swaps such as `A → B` and `B → A`.
- Preserve key-down, key-up, and normal repeat behavior. Suppress source input when a mapping activates; temporarily reconcile source modifiers so they do not contaminate destination output.
- Reject missing inputs, duplicate sources, identity mappings, and unsupported reserved combinations. Show validation beside the affected row.
- Suspend remapping during input capture and resume its previous state afterward. Use explicit mouse-accessible confirmation controls so Enter and Escape can be captured.
- Save and apply a validated profile atomically. Defer activation changes until physical keys are released.

Arbitrary letter-key chords, sequences, macros, dual-role tap/hold keys, application-specific profiles, and mouse remapping are outside the first release.

## Implementation and packaging

- Use C++20, native Win32 controls, CMake, and the MSVC static runtime. Embed icons, application manifest, and required resources into `Keymapper.exe`.
- Build a Windows GUI executable with no console window and no third-party runtime dependencies. Support DPI scaling, resizing, keyboard navigation, and accessible control labels.
- Separate profile persistence, mapping evaluation, Windows input integration, GUI, and tray lifecycle into testable components.
- Use a dedicated message-loop thread with `WH_KEYBOARD_LL` and emit mapped events through `SendInput`. Keep hook callbacks free of disk access and blocking UI work. These APIs provide interception and injection, subject to Windows restrictions. [Keyboard hook documentation](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc), [SendInput documentation](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)
- Track physical and generated key state separately; mark generated events to prevent recursion. Release application-owned output keys when pausing, exiting, or recovering from errors.
- Persist a versioned JSON document containing profile IDs, names, mappings, active profile ID, and enabled state. Represent each endpoint as a key or modifier-plus-key shortcut.
- Save through a temporary file and atomic replacement, retaining one previous valid backup. On corrupt settings, preserve the original and offer backup recovery; otherwise start paused with an empty profile.
- If the executable folder is unwritable, display a clear error and retain unsaved edits without silently switching storage locations.
- Run with ordinary user permissions by default. Explain that elevated applications require launching the mapper as administrator. Do not promise remapping on secure desktops, login screens, or applications that bypass ordinary keyboard input.
- Exclude OS-reserved mappings such as `Ctrl+Alt+Delete` and `Win+L`, and hardware-only keys unavailable to Windows. [Microsoft’s documented restrictions](https://learn.microsoft.com/en-us/windows/powertoys/keyboard-manager)
- Produce the release executable on Windows; keep developer symbols separate from the user download. Automatic updates and launch-at-login configuration are outside the first release.

## Verification and acceptance

- Verify all four mapping forms, modifier remapping, left/right modifiers, exact matching, precedence, repeat behavior, swaps, and prevention of recursive output.
- Test modifier release ordering, Alt/Win side effects, pause/resume, profile switching while keys are held, and clean exit without stuck keys.
- Verify capture, manual selection, validation, unsaved-change handling, profile duplication/deletion, and persistence across restarts.
- Exercise at least 1,000 generated profiles to verify navigation and persistence without a hard-coded profile cap.
- Test corrupt files, backup recovery, interrupted saves, and unwritable executable folders.
- Verify tray-only startup, single-click opening, close-to-tray, second-launch activation, and tray restoration after Explorer restarts.
- On a clean Windows 11 x64 machine without developer runtimes, launch the copied executable and verify mappings in ordinary applications. Test elevated application behavior separately.
- Copy the executable and settings file to another writable folder and confirm that profiles and preferences travel together.

Acceptance requires a working portable executable, all four mapping forms, uncapped profile creation, persistent settings, and complete tray operation.
