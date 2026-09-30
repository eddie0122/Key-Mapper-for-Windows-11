#include "Test.h"

#include "core/Engine.h"
#include "core/SettingsIO.h"
#include "core/SettingsStore.h"
#include "core/Utf.h"

#include <windows.h>

#include <chrono>

using namespace km;

namespace {

// A fresh, empty directory under %TEMP% that is removed afterwards.
struct TempDir {
    std::wstring path;
    TempDir() {
        wchar_t base[MAX_PATH];
        GetTempPathW(MAX_PATH, base);
        path = std::wstring(base) + L"km-test-" + Utf8ToWide(NewProfileId());
        CreateDirectoryW(path.c_str(), nullptr);
    }
    ~TempDir() {
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW((path + L"\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (fd.cFileName[0] != L'.') DeleteFileW((path + L"\\" + fd.cFileName).c_str());
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        RemoveDirectoryW(path.c_str());
    }
    std::wstring file(const wchar_t* name) const { return path + L"\\" + name; }
};

void WriteRaw(const std::wstring& path, const std::string& bytes) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    DWORD w = 0;
    WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &w, nullptr);
    CloseHandle(h);
}

std::string ReadRaw(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return "<missing>";
    std::string s(1 << 20, '\0');
    DWORD r = 0;
    ReadFile(h, s.data(), static_cast<DWORD>(s.size()), &r, nullptr);
    CloseHandle(h);
    s.resize(r);
    return s;
}

bool Exists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

Settings Named(const wchar_t* name) {
    Settings s = DefaultSettings();
    s.profiles[0].name = name;
    return s;
}

Settings LoadFrom(const std::wstring& path) {
    Settings s;
    std::string err;
    if (!DeserializeSettings(ReadRaw(path), s, err)) kmtest::Fail(__FILE__, __LINE__, "unreadable: " + err);
    return s;
}

}  // namespace

TEST(StoreFirstLaunchIsMissing) {
    TempDir d;
    SettingsStore store(d.path);
    auto r = store.load();
    CHECK(r.status == SettingsStore::LoadStatus::Missing);
    CHECK(!r.backupValid);
    CHECK(!Exists(store.filePath()));  // Nothing is written until the first save.
}

TEST(StoreSaveLoadRoundTrip) {
    TempDir d;
    SettingsStore store(d.path);
    Settings s = Named(L"프로필");
    std::wstring err;
    CHECK(store.save(s, err));
    SettingsStore again(d.path);
    auto r = again.load();
    CHECK(r.status == SettingsStore::LoadStatus::Loaded);
    CHECK(r.settings == s);
    CHECK(!Exists(store.tempPath()));
}

TEST(StoreKeepsOnePreviousValidBackup) {
    TempDir d;
    SettingsStore store(d.path);
    std::wstring err;
    CHECK(store.save(Named(L"one"), err));
    CHECK(!Exists(store.backupPath()));
    CHECK(store.save(Named(L"two"), err));
    CHECK(LoadFrom(store.backupPath()).profiles[0].name == L"one");
    CHECK(store.save(Named(L"three"), err));
    CHECK(LoadFrom(store.backupPath()).profiles[0].name == L"two");
    CHECK(LoadFrom(store.filePath()).profiles[0].name == L"three");
}

TEST(StoreCorruptFileIsPreservedAndBackupOffered) {
    TempDir d;
    SettingsStore store(d.path);
    std::wstring err;
    CHECK(store.save(Named(L"one"), err));
    CHECK(store.save(Named(L"two"), err));
    const std::string garbage = "{\"version\":1, \"profiles\": [ oops";
    WriteRaw(store.filePath(), garbage);

    SettingsStore reopened(d.path);
    auto r = reopened.load();
    CHECK(r.status == SettingsStore::LoadStatus::Corrupt);
    CHECK(!r.error.empty());
    CHECK(r.backupValid);
    CHECK(r.backup.profiles[0].name == L"one");
    CHECK(!r.preservedCopy.empty());
    CHECK(ReadRaw(r.preservedCopy) == garbage);
    CHECK(ReadRaw(reopened.filePath()) == garbage);  // Original left untouched.

    // Saving over a corrupt file must not rotate it into the backup slot.
    CHECK(reopened.save(Named(L"three"), err));
    CHECK(LoadFrom(reopened.backupPath()).profiles[0].name == L"one");
    CHECK(LoadFrom(reopened.filePath()).profiles[0].name == L"three");
}

TEST(StoreIgnoresInterruptedSave) {
    TempDir d;
    SettingsStore store(d.path);
    std::wstring err;
    CHECK(store.save(Named(L"good"), err));
    // Simulate a crash mid-save: a half-written temporary file is left behind.
    WriteRaw(store.tempPath(), "{\"version\":1,\"prof");
    SettingsStore reopened(d.path);
    auto r = reopened.load();
    CHECK(r.status == SettingsStore::LoadStatus::Loaded);
    CHECK(r.settings.profiles[0].name == L"good");
    CHECK(reopened.save(Named(L"next"), err));
    CHECK(!Exists(reopened.tempPath()));
    CHECK(LoadFrom(reopened.filePath()).profiles[0].name == L"next");
}

TEST(StoreReportsUnwritableFolder) {
    TempDir d;
    // A path whose "directory" is actually a file cannot hold settings.
    WriteRaw(d.file(L"blocker"), "x");
    SettingsStore store(d.file(L"blocker"));
    std::wstring err;
    CHECK(!store.save(Named(L"x"), err));
    CHECK(!err.empty());
}

TEST(StoreReportsReadOnlySettingsFile) {
    TempDir d;
    SettingsStore store(d.path);
    std::wstring err;
    CHECK(store.save(Named(L"one"), err));
    SetFileAttributesW(store.filePath().c_str(), FILE_ATTRIBUTE_READONLY);
    bool saved = store.save(Named(L"two"), err);
    SetFileAttributesW(store.filePath().c_str(), FILE_ATTRIBUTE_NORMAL);
    CHECK(!saved);
    CHECK(!err.empty());
    CHECK(LoadFrom(store.filePath()).profiles[0].name == L"one");
}

TEST(StoreHandlesThousandsOfProfiles) {
    TempDir d;
    SettingsStore store(d.path);
    Settings s;
    const int kCount = 1500;
    for (int i = 0; i < kCount; ++i) {
        Profile p;
        p.id = NewProfileId();
        p.name = L"Keyboard profile " + std::to_wstring(i);
        KeyCombo a, b, c, e;
        ParseComboText("CapsLock", a);
        ParseComboText(i % 2 ? "Ctrl" : "Esc", b);
        ParseComboText("Ctrl+J", c);
        ParseComboText("Enter", e);
        p.keys.push_back({a, b});
        p.shortcuts.push_back({c, e});
        s.profiles.push_back(std::move(p));
    }
    s.activeProfileId = s.profiles[kCount - 1].id;
    auto t0 = std::chrono::steady_clock::now();
    std::wstring err;
    CHECK(store.save(s, err));
    SettingsStore again(d.path);
    auto r = again.load();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(r.status == SettingsStore::LoadStatus::Loaded);
    CHECK_EQ(r.settings.profiles.size(), size_t(kCount));
    CHECK(r.settings == s);
    CHECK(r.settings.find(s.activeProfileId) != nullptr);
    CHECK(ms < 5000);
    for (const Profile& p : r.settings.profiles) CHECK(CompiledProfile::Compile(p)->findKey(VK_CAPITAL));
}
