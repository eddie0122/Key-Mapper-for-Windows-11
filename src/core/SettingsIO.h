#pragma once

#include "core/Model.h"

#include <string>
#include <string_view>

namespace km {

// Version of the settings document written by this build.
constexpr int kSettingsVersion = 1;

std::string SerializeSettings(const Settings& s);
// Fails (returning a description in `error`) when the document is not valid
// JSON, has an unsupported version, or is structurally inconsistent.
bool DeserializeSettings(std::string_view text, Settings& out, std::string& error);

}  // namespace km
