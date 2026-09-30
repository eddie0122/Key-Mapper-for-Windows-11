#pragma once

#include "core/Model.h"

#include <string>

namespace km {

// Reads and writes keymapper.settings.json in one directory (normally the
// executable's folder). Saves go through a temporary file and an atomic
// replacement; the previous valid file is kept as a single backup.
class SettingsStore {
public:
    static constexpr const wchar_t* kFileName = L"keymapper.settings.json";

    explicit SettingsStore(std::wstring directory);

    enum class LoadStatus { Loaded, Missing, Corrupt };

    struct LoadResult {
        LoadStatus status = LoadStatus::Missing;
        Settings settings;          // Valid only when status == Loaded.
        std::string error;          // Why the main file could not be used.
        std::wstring preservedCopy; // Copy of a corrupt main file, if one was made.
        bool backupValid = false;   // A readable, valid backup exists.
        Settings backup;
    };

    LoadResult load();
    // On failure returns false and a user-readable description in `error`.
    bool save(const Settings& s, std::wstring& error);

    // Whether the current main file holds valid settings. Only a valid main
    // file is rotated into the backup slot, so a corrupt file never replaces a
    // good backup.
    void setMainFileValid(bool valid) { mainValid_ = valid; }
    bool mainFileValid() const { return mainValid_; }

    const std::wstring& directory() const { return dir_; }
    std::wstring filePath() const;
    std::wstring backupPath() const;
    std::wstring tempPath() const;

    static std::wstring ErrorText(unsigned long code);

private:
    std::wstring dir_;
    bool mainValid_ = false;
};

}  // namespace km
