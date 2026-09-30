cmake_minimum_required(VERSION 3.21)
project(Keymapper VERSION 1.0.0 LANGUAGES CXX RC)

if(NOT WIN32)
  message(FATAL_ERROR "Keymapper builds only for Windows.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Keymapper targets Windows x64 only.")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Keep the app and test executables side by side (build.ps1 relies on this).
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
                        _WIN32_WINNT=0x0A00 WINVER=0x0A00)

if(MSVC)
  # Static C/C++ runtime: the executable needs no redistributable.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/W4 /permissive- /utf-8 /EHsc /Zi)
  # Symbols go to a separate .pdb; the manifest comes from the .rc file.
  add_link_options(/DEBUG /OPT:REF /OPT:ICF /MANIFEST:NO)
else()
  add_compile_options(-Wall -Wextra -Wno-missing-field-initializers)
  # Link libc++/winpthreads statically so only OS DLLs are required.
  add_link_options(-static)
  if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Emit CodeView symbols into a separate .pdb next to the executable.
    add_compile_options(-gcodeview)
    add_link_options(-Wl,--pdb=)
  endif()
endif()

# Platform-independent logic: model, validation, JSON, persistence, engine.
add_library(km_core STATIC
  src/core/Utf.cpp
  src/core/KeyCatalog.cpp
  src/core/Model.cpp
  src/core/Json.cpp
  src/core/SettingsIO.cpp
  src/core/SettingsStore.cpp
  src/core/Engine.cpp
  src/core/DeviceMatch.cpp
)
target_include_directories(km_core PUBLIC src)

# Windows input integration: low-level hook thread and SendInput.
add_library(km_win STATIC
  src/win/InputHook.cpp
  src/win/Autostart.cpp
  src/win/KeyboardDevices.cpp
)
target_link_libraries(km_win PUBLIC km_core user32 advapi32 cfgmgr32)

add_executable(Keymapper WIN32
  src/app/main.cpp
  src/app/App.cpp
  src/ui/Ui.cpp
  src/ui/MainWindow.cpp
  src/ui/SelectKeyDialog.cpp
  src/ui/TextPrompt.cpp
  src/ui/ProfileChooser.cpp
  src/ui/KeyboardsDialog.cpp
  res/Keymapper.rc
)
target_include_directories(Keymapper PRIVATE res)
target_link_libraries(Keymapper PRIVATE km_win km_core
  comctl32 shell32 ole32 oleacc uuid wtsapi32 gdi32 user32 advapi32)
if(MINGW)
  target_link_options(Keymapper PRIVATE -municode)
endif()

enable_testing()
add_subdirectory(tests)
