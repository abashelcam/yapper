#include "settings.hpp"

#include <cstdlib>
#include <fstream>

#include <nlohmann/json.hpp>

namespace yapper
{
std::filesystem::path DataFolder()
{
  char       *appData = nullptr;
  std::size_t length = 0;
  _dupenv_s(&appData, &length, "APPDATA");

  std::filesystem::path folder = appData ? std::filesystem::path(appData) / "Yapper" : "Yapper";
  std::free(appData);

  std::filesystem::create_directories(folder);
  return folder;
}

static std::filesystem::path SettingsFile()
{
  return DataFolder() / "settings.json";
}

Settings LoadSettings()
{
  Settings settings{};

  std::ifstream file(SettingsFile());
  if (!file)
  {
    return settings;
  }

  try
  {
    nlohmann::json json = nlohmann::json::parse(file);
    settings.microphone = json.value("microphone", "");
    settings.speakers = json.value("speakers", "");
    settings.address = json.value("address", "");
    settings.username = json.value("username", "");
  }
  catch (const nlohmann::json::exception &)
  {
    // A broken file just means starting from defaults
  }

  return settings;
}

void SaveSettings(const Settings &settings)
{
  nlohmann::ordered_json json;
  json["microphone"] = settings.microphone;
  json["speakers"] = settings.speakers;
  json["address"] = settings.address;
  json["username"] = settings.username;

  std::ofstream file(SettingsFile());
  file << json.dump(2) << "\n";
}
} // namespace yapper
