#include "core/SettingsStore.h"

#include "core/SettingsIO.h"
#include "core/Utf.h"

#include <windows.h>

#include <cwchar>

namespace km {
namespace {

constexpr LONGLONG kMaxSettingsBytes = 64LL * 1024 * 1024;

// Returns ERROR_SUCCESS or the Win32 error code.
DWORD ReadWholeFile(const std::wstring& path, std::string& out) {
    out.clear();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return GetLastError();
    LARGE_INTEGER size{};
    DWORD result = ERROR_SUCCESS;
    if (!GetFileSizeEx(h, &size)) {
        result = GetLastError();
    } else if (size.QuadPart > kMaxSettingsBytes) {
        result = ERROR_FILE_TOO_LARGE;
    } else {
        out.resize(static_cast<size_t>(size.QuadPart));
        size_t done = 0;
        while (done < out.size()) {
            DWORD got = 0;
            if (!ReadFile(h, out.data() + done, static_cast<DWORD>(out.size() - done), &got, nullptr)) {
                result = GetLastError();
                break;
            }
            if (got == 0) break;
            done += got;
        }
        out.resize(done);
    }
    CloseHandle(h);
    return result;
}

DWORD WriteWholeFile(const std::wstring& path, const std::string& data) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return GetLastError();
    DWORD result = ERROR_SUCCESS;
    size_t done = 0;
    while (done < data.size()) {
        DWORD wrote = 0;
        if (!WriteFile(h, data.data() + done, static_cast<DWORD>(data.size() - done), &wrote, nullptr)) {
            result = GetLastError();
            break;
        }
        done += wrote;
    }
    // Make sure the bytes are on disk before the file replaces the original.
    if (result == ERROR_SUCCESS && !FlushFileBuffers(h)) result = GetLastError();
    CloseHandle(h);
    return result;
}

std::wstring Timestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[32];
    swprintf(buf, 32, L"%04u%02u%02u-%02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute,
             st.wSecond);
    return buf;
}

}  // namespace

SettingsStore::SettingsStore(std::wstring directory) : dir_(std::move(directory)) {
    while (!dir_.empty() && (dir_.back() == L'\\' || dir_.back() == L'/')) dir_.pop_back();
}

std::wstring SettingsStore::filePath() const { return dir_ + L"\\" + kFileName; }
std::wstring SettingsStore::backupPath() const { return filePath() + L".bak"; }
std::wstring SettingsStore::tempPath() const { return filePath() + L".tmp"; }

std::wstring SettingsStore::ErrorText(unsigned long code) {
    wchar_t* buf = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                 FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, code, 0, reinterpret_cast<wchar_t*>(&buf), 0, nullptr);
    std::wstring s = n ? std::wstring(buf, n) : L"Error " + std::to_wstring(code);
    if (buf) LocalFree(buf);
    while (!s.empty() && (s.back() == L'\n' || s.back() == L'\r' || s.back() == L' ')) s.pop_back();
    return s;
}

SettingsStore::LoadResult SettingsStore::load() {
    LoadResult r;
    std::string text;
    DWORD err = ReadWholeFile(filePath(), text);
    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
        r.status = LoadStatus::Missing;
        mainValid_ = false;
    } else if (err != ERROR_SUCCESS) {
        r.status = LoadStatus::Corrupt;
        r.error = WideToUtf8(ErrorText(err));
        mainValid_ = false;
    } else if (DeserializeSettings(text, r.settings, r.error)) {
        r.status = LoadStatus::Loaded;
        mainValid_ = true;
        return r;
    } else {
        r.status = LoadStatus::Corrupt;
        mainValid_ = false;
        // Keep the unreadable original untouched for the user to inspect.
        std::wstring copy = dir_ + L"\\keymapper.settings.corrupt-" + Timestamp() + L".json";
        if (CopyFileW(filePath().c_str(), copy.c_str(), TRUE)) r.preservedCopy = copy;
    }

    std::string backupText;
    std::string backupError;
    if (ReadWholeFile(backupPath(), backupText) == ERROR_SUCCESS &&
        DeserializeSettings(backupText, r.backup, backupError)) {
        r.backupValid = true;
    }
    return r;
}

bool SettingsStore::save(const Settings& s, std::wstring& error) {
    const std::string text = SerializeSettings(s);
    const std::wstring target = filePath();
    const std::wstring temp = tempPath();

    DWORD err = WriteWholeFile(temp, text);
    if (err != ERROR_SUCCESS) {
        DeleteFileW(temp.c_str());
        error = ErrorText(err);
        return false;
    }

    BOOL ok;
    if (GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES) {
        // Atomically swap in the new file; the old valid file becomes the backup.
        ok = ReplaceFileW(target.c_str(), temp.c_str(), mainValid_ ? backupPath().c_str() : nullptr,
                          REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr);
        if (!ok) {
            // Some file systems do not support ReplaceFile; fall back to
            // copy-to-backup plus an atomic rename.
            if (mainValid_) CopyFileW(target.c_str(), backupPath().c_str(), FALSE);
            ok = MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        }
    } else {
        ok = MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    }
    if (!ok) {
        err = GetLastError();
        DeleteFileW(temp.c_str());
        error = ErrorText(err);
        return false;
    }
    mainValid_ = true;
    return true;
}

}  // namespace km
