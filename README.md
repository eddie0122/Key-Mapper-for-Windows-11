# Key-Mapper-for-Windows-11

## What is Keymapper?

**Keymapper** changes what the keys on your keyboard do, everywhere in Windows 11. You decide, for example, that **Caps Lock** should act as **Ctrl**, that **F1** should copy (`Ctrl+C`), or that `Ctrl+J` should press **Enter**. Keymapper applies your choices in every app until you pause or close it.

It is built for people who:

- find a key in an awkward place or never use it, and want it to do something useful;
- switch between keyboards (a laptop keyboard, an external USB or Bluetooth keyboard, a keyboard with a different layout) and want each one to behave the way they expect;
- want the PowerToys Keyboard Manager style of editing without installing PowerToys.

**Main features**

- **Four kinds of remapping**

  | Kind | Example |
  |---|---|
  | Key → key | `Caps Lock → Ctrl` |
  | Key → shortcut | `F1 → Ctrl+C` |
  | Shortcut → key | `Ctrl+J → Enter` |
  | Shortcut → shortcut | `Ctrl+Shift+J → Alt+Tab` |

- **Profiles.** A profile is a named set of remappings, such as "Work", "Gaming", or "Mac keyboard". You can create as many as you like. One profile is active at a time, and you can switch profiles from the tray icon.
- **Automatic switching by keyboard.** You can link a keyboard to a profile. When that keyboard connects, its profile turns on. When it disconnects, the previous profile comes back.
- **Portable.** Keymapper is a single `Keymapper.exe`. It needs no installer, runtime, or driver, and no administrator rights. Your settings are saved in one file next to the program, so you can copy both files to another PC.
- **Runs quietly in the system tray.** You can also have Keymapper start when you sign in to Windows.

## Getting started (for beginners)

### 1. Put Keymapper in its own folder

Create a folder you can write to, such as `Documents\Keymapper`, and put `Keymapper.exe` in it. Keymapper saves its settings file (`keymapper.settings.json`) in this folder.

> Don't use `C:\Program Files`. Windows doesn't let normal programs write there, so Keymapper couldn't save your settings.

If you don't have `Keymapper.exe` yet, build it with the steps in [Building from source](#building-from-source).

### 2. Start Keymapper

Double-click `Keymapper.exe`. **No window opens.** Instead, a keyboard icon appears in the **system tray**, the area near the clock at the bottom right of the screen. If you can't see the icon, click the small **^** arrow next to the clock to show hidden icons.

The first time you start Keymapper, it creates an empty profile called **Default**.

> If Windows shows "Windows protected your PC" or says the app was blocked, see [Troubleshooting](#troubleshooting).

### 3. Open the editor

Click the tray icon and choose **Open Keymapper**. The editor window has:

- a **profile** list at the top, showing which profile you are editing and whether it is active;
- a **Remap a key** section for single keys;
- a **Remap a shortcut** section for key combinations such as `Ctrl+J`.

### 4. Remap your first key: Caps Lock → Ctrl

1. Under **Remap a key**, click **+ Add key remapping**. A new row appears.
2. Click the left **Select…** button, which is the **physical key** you press. Press **Caps Lock**, then click **OK**.
   *Or* type in the search box under **Or choose manually** and pick the key from the list.
3. Click the right **Select…** button, which is what the key should **send**. Press **Ctrl**, then click **OK**.
4. Click **Save**, or press **Ctrl+S**. The status line says **Saved and applied.**

Open any app, such as Notepad, and hold **Caps Lock** while pressing **C**: it now copies, just like `Ctrl+C`.

### 5. Remap a shortcut: Ctrl+J → Enter

1. Under **Remap a shortcut**, click **+ Add shortcut remapping**.
2. For the physical shortcut, press the modifiers first and then the key: hold **Ctrl**, press **J**, and click **OK**.
3. For what to send, press **Enter** and click **OK**.
4. Click **Save**.

A shortcut needs at least one modifier (**Ctrl**, **Alt**, **Shift**, or **Win**) plus one ordinary key.

If a row has a problem, such as a missing key or the same key mapped twice, a message appears next to it and nothing is saved until you fix it. To remove a row, click its **Delete** button and then save.

### 6. Work with profiles

In the editor:

- **New** creates an empty profile. **Duplicate** copies the selected one. **Rename** and **Delete** do what they say.
- The profile you pick in the list is the one you are **editing**. To make it the one Keymapper **uses**, click **Activate this profile**.
- If you have many profiles, use **Search profiles…**.

From the tray icon, open **Profiles** to switch the active profile without opening the editor.

### 7. Pause, close, and exit

- **Pause remapping** in the tray menu turns off all remapping, and **Resume remapping** turns it back on. The tray icon turns grey while remapping is paused.
- **Closing the editor window** only hides it. Keymapper keeps running in the tray.
- **Exit** in the tray menu stops Keymapper completely. Your keys go back to normal.
- Starting `Keymapper.exe` again while it is already running just opens the editor.

### 8. Start Keymapper automatically (optional)

Tick **Start Keymapper when I sign in to Windows** in the editor. This setting applies to the whole PC, not to one profile. It starts the copy of `Keymapper.exe` you ticked it from, so tick it again if you move the program. You can also turn it off in Windows **Settings → Apps → Startup** or in Task Manager.

### 9. Switch profiles automatically when a keyboard connects (optional)

1. In the editor, select the profile and click **Keyboards…** in the **Auto-activate with** row.
2. Tick the keyboards that should activate this profile. If several keyboards have similar names, click **Identify by typing** and press a key on the keyboard you mean. Keymapper selects its row.
3. Click **OK**. The links are saved right away.

After that:

- Connecting a linked keyboard (USB or Bluetooth) activates its profile and shows a silent notification. If a linked keyboard is already connected, its profile activates right away.
- Disconnecting it goes back to the profile that was active before.
- If you pick a profile yourself, it stays active until another linked keyboard connects.
- A keyboard can be linked to one profile only. Linking it to another profile moves it.
- Tip: link your laptop's built-in keyboard to your usual profile. Keymapper then goes back to that profile when other keyboards disconnect.
- To stop automatic switching for all profiles, untick **Switch profiles automatically** in the tray menu or in the dialog.

Keyboards are recognised by their USB/Bluetooth vendor and product IDs, so two keyboards of the same model count as one keyboard. A keyboard that can connect both through a USB receiver and through Bluetooth appears as two entries.

### Good to know

**How matching works**

- Shortcuts must match their modifiers exactly: a `Ctrl+J` mapping does **not** fire when you press `Ctrl+Shift+J`.
- Left/right-specific modifiers (such as *Left Ctrl*) take priority over generic ones (*Ctrl*). A generic modifier that Keymapper sends is the left one.
- A key remapped to a modifier works in shortcuts too: with `Caps Lock → Ctrl`, pressing `Caps Lock+J` triggers a `Ctrl+J` mapping.
- Keymapper never remaps its own output, so swaps such as `A → B` together with `B → A` work.

**What Keymapper can't do**

- Keymapper runs with your normal permissions. To remap keys in an app that runs as administrator, run Keymapper as administrator too.
- Windows doesn't allow remapping on the sign-in and lock screens, in UAC (administrator permission) prompts, or for `Ctrl+Alt+Del` and `Win+L`.
- Some games and remote-desktop apps read the keyboard directly and ignore Keymapper.
- The active profile applies to all connected keyboards at once. Windows doesn't tell apps which keyboard a key came from, so giving two keyboards different mappings at the same time would need a kernel driver.
- Not in this version: chords, key sequences, macros, tap/hold keys, per-app profiles, mouse remapping, and automatic updates.

### Troubleshooting

| Problem | What to do |
|---|---|
| "Windows protected your PC" (SmartScreen) | Click **More info**, then **Run anyway**, if you trust where the file came from. |
| "An Application Control policy has blocked this file" | Your PC uses **Smart App Control**, which blocks unsigned programs. Use a signed release of Keymapper. See [Smart App Control and code signing](#smart-app-control-and-code-signing). |
| "Keymapper couldn't save its settings" | The folder isn't writable. Move `Keymapper.exe` to a folder you own, such as `Documents\Keymapper`. Your unsaved edits stay in the editor until you save. |
| A remapping doesn't work in one app | That app may be running as administrator. Run Keymapper as administrator too. |
| Nothing is remapped at all | Check that remapping isn't paused (the tray icon is grey while paused) and that the profile you edited is the **active** one. |

## Project structure

```text
Key-Mapper-for-Windows-11/
├── CMakeLists.txt          Build definition: compiler settings, libraries, and the app
├── build.ps1               One-command build: compiles, runs unit tests, stages dist\
├── LICENSE
├── README.md
├── docs/
│   └── product-plan.md     Original product specification and acceptance criteria
├── res/                    Resources embedded into Keymapper.exe
│   ├── Keymapper.rc        Resource script: icons, manifest, version info
│   ├── Keymapper.manifest  PerMonitorV2 DPI, common controls v6, asInvoker
│   ├── resource.h          Resource IDs shared by the .rc file and the code
│   ├── keymapper.ico       Tray and app icon
│   └── keymapper-paused.ico  Tray icon while remapping is paused
├── src/
│   ├── core/               Platform-independent logic (library km_core)
│   │   ├── KeyCatalog      Names and virtual-key codes of every supported key
│   │   ├── Model           Profiles, mappings, and validation
│   │   ├── Engine          The mapping engine: turns key events into output
│   │   ├── DeviceMatch     Matches connected keyboards to linked profiles
│   │   ├── Json            Minimal JSON reader/writer
│   │   ├── SettingsIO      Converts settings to and from JSON
│   │   ├── SettingsStore   Atomic save, backup, and corrupt-file recovery
│   │   └── Utf             UTF-8/UTF-16 conversion
│   ├── win/                Windows integration (library km_win)
│   │   ├── InputHook       Low-level keyboard hook thread and SendInput
│   │   ├── KeyboardDevices Lists keyboards and watches them connect/disconnect
│   │   └── Autostart       "Start when I sign in" (per-user Run registry key)
│   ├── ui/                 Win32 windows and dialogs
│   │   ├── Ui              Shared helpers: fonts, DPI, layout, controls
│   │   ├── MainWindow      The editor window
│   │   ├── SelectKeyDialog Key/shortcut capture and searchable key list
│   │   ├── KeyboardsDialog Link keyboards to a profile
│   │   ├── ProfileChooser  Scrollable, searchable profile picker
│   │   └── TextPrompt      Name prompt for new/renamed profiles
│   └── app/                Application lifecycle
│       ├── main.cpp        Entry point, single-instance check
│       └── App             Tray icon and menu, settings loading, profile switching
├── tests/
│   ├── CMakeLists.txt      Test targets
│   ├── unit/               keymapper_tests: engine, model, JSON, storage, autostart, devices
│   │   └── Test.h          Tiny self-registering test harness
│   └── e2e/
│       └── HookE2E.cpp     keymapper_e2e: drives the real hook and SendInput
└── tools/
    └── make-icons.ps1      Regenerates the .ico files in res/
```

Each name without an extension under `src/` is a `.h` and `.cpp` pair. `core` doesn't use Windows APIs, so the unit tests cover most of the logic. `win` builds on `core`, and `ui` and `app` are compiled only into `Keymapper.exe`.

Build output is not part of the repository. `build/` (or `build-msvc/`) holds compiler output, and `dist/` holds the staged release. Both are listed in `.gitignore`.

## Building from source

You need Windows x64 and CMake 3.21 or newer, plus one of these toolchains:

- **Visual Studio 2022** with "Desktop development with C++" (MSVC, static runtime), or
- **[llvm-mingw](https://github.com/mstorsjo/llvm-mingw)** (Clang, UCRT) with Ninja.

```powershell
.\build.ps1                 # picks MSVC if installed, otherwise clang++ from PATH
.\build.ps1 -Toolchain mingw
.\build.ps1 -SkipTests      # build without running the unit tests
```

The script stages the release in `dist\`:

| File | Purpose |
|---|---|
| `dist\Keymapper.exe` | The single file users download. It imports only Windows system DLLs. |
| `dist\symbols\Keymapper.pdb` | Developer symbols, kept out of the download. |

To build manually:

```powershell
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --config Release
```

To regenerate the icons, run `powershell -ExecutionPolicy Bypass -File tools\make-icons.ps1`.

### Smart App Control and code signing

On PCs with Smart App Control enabled, Windows blocks newly built unsigned executables, including `Keymapper.exe` and the test programs. Sign the release with a certificate from a trusted CA (for example, Azure Trusted Signing) before distributing it, or build and test on a machine without Smart App Control.

## Tests

- `keymapper_tests` (`tests/unit`): unit tests for the mapping engine, validation, JSON, and settings persistence. They cover all four forms, exact matching, precedence, repeat, swaps, Alt/Win menu masking, pause, profile switching while keys are held, capture, a randomized no-stuck-keys test, corrupt and interrupted saves, backup rotation, unwritable folders, and 1,500 profiles. Run them with `ctest` in the build folder, or run `keymapper_tests.exe`.
- `keymapper_e2e` (`tests/e2e`): drives the real `WH_KEYBOARD_LL` hook and `SendInput` against a text box in its own window. It injects keystrokes only while that window is in the foreground, so it is left out of the default `ctest` run.

## Settings file

`keymapper.settings.json` is a versioned JSON document that holds the profiles (ID, name, key and shortcut mappings, linked keyboards), the active profile ID, and the enabled state. Endpoints are written as `{"key": "CapsLock"}` or `{"modifiers": ["Ctrl", "Shift"], "key": "J"}`.

Saves go through a temporary file and an atomic replace. The previous valid file is kept as `keymapper.settings.json.bak`.

If the file is corrupt, Keymapper keeps it and saves a copy as `keymapper.settings.corrupt-<time>.json`. It then offers to restore the backup. If you don't restore it, Keymapper starts paused with an empty profile.

If the folder isn't writable, Keymapper says so and keeps your unsaved edits in the editor. It never saves settings anywhere else.

## License

MIT; see [LICENSE](LICENSE).
