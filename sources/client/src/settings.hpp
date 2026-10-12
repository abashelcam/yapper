#pragma once

#include <filesystem>
#include <string>

namespace yapper
{
// Saved in %APPDATA%\Yapper\settings.json. Empty device names mean the Windows default.
struct Settings
{
  std::string microphone;
  std::string speakers;
  std::string address;
  std::string username;
};

// %APPDATA%\Yapper, created if missing
std::filesystem::path DataFolder();

// Returns defaults if the file is missing or broken
Settings LoadSettings();
void     SaveSettings(const Settings &settings);
} // namespace yapper
