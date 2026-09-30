#include "core/Utf.h"

#include <windows.h>

namespace km {

std::wstring Utf8ToWide(std::string_view s) {
    std::wstring out;
    if (s.empty()) return out;
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    out.resize(static_cast<size_t>(n));
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

bool TryUtf8ToWide(std::string_view s, std::wstring& out) {
    out.clear();
    if (s.empty()) return true;
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()),
                                nullptr, 0);
    if (n <= 0) return false;
    out.resize(static_cast<size_t>(n));
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()),
                        out.data(), n);
    return true;
}

std::string WideToUtf8(std::wstring_view s) {
    std::string out;
    if (s.empty()) return out;
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0,
                                nullptr, nullptr);
    out.resize(static_cast<size_t>(n));
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr,
                        nullptr);
    return out;
}

}  // namespace km
