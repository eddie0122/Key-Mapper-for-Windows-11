# Unit tests for the engine, model, JSON, settings persistence, autostart and
# keyboard matching.
add_executable(keymapper_tests
  unit/main.cpp
  unit/EngineTests.cpp
  unit/ModelTests.cpp
  unit/StoreTests.cpp
  unit/AutostartTests.cpp
  unit/DeviceTests.cpp
)
target_link_libraries(keymapper_tests PRIVATE km_win km_core)
add_test(NAME keymapper_tests COMMAND keymapper_tests)

# End-to-end check of the real hook + SendInput pipeline. It briefly injects
# keystrokes into its own window, so it is not part of the default test run.
add_executable(keymapper_e2e e2e/HookE2E.cpp)
target_link_libraries(keymapper_e2e PRIVATE km_win km_core)
