#pragma once

#include <string>
#include <string_view>

namespace km {

std::wstring Utf8ToWide(std::string_view s);
// Returns false when `s` is not valid UTF-8.
bool TryUtf8ToWide(std::string_view s, std::wstring& out);
std::string WideToUtf8(std::wstring_view s);

}  // namespace km
