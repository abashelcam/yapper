#include "config.hpp"

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace yapper
{
static void WriteDefaultConfig(const std::string &path)
{
  ServerConfig defaults{};

  nlohmann::ordered_json json;
  json["port"] = defaults.port;
  json["name"] = defaults.name;
  json["maxUsers"] = defaults.maxUsers;
  json["password"] = defaults.password;
  json["channels"] = defaults.channels;

  std::ofstream file(path);
  file << json.dump(2) << "\n";
  if (!file)
  {
    throw std::runtime_error("couldn't write " + path);
  }
}

static void Check(bool               condition,
                  const std::string &problem)
{
  if (!condition)
  {
    throw std::runtime_error("server config: " + problem);
  }
}

ServerConfig LoadServerConfig(const std::string &path)
{
  if (!std::filesystem::exists(path))
  {
    WriteDefaultConfig(path);
  }

  std::ifstream file(path);
  if (!file)
  {
    throw std::runtime_error("couldn't open " + path);
  }

  ServerConfig   config{};
  nlohmann::json json = nlohmann::json::parse(file);

  config.port = json.value("port", config.port);
  config.name = json.value("name", config.name);
  config.maxUsers = json.value("maxUsers", config.maxUsers);
  config.password = json.value("password", config.password);
  config.channels = json.value("channels", config.channels);

  Check(config.port != 0, "port can't be 0");
  Check(!config.name.empty() && config.name.size() <= kMaxServerNameBytes, "name must be 1 to 64 bytes");
  Check(config.maxUsers > 0, "maxUsers must be at least 1");
  Check(config.password.size() <= kMaxPasswordBytes, "password must be at most 64 bytes");
  Check(!config.channels.empty() && config.channels.size() <= kMaxChannels, "need 1 to 64 channels");
  for (const std::string &channel : config.channels)
  {
    Check(!channel.empty() && channel.size() <= kMaxChannelNameBytes, "channel names must be 1 to 32 bytes");
  }

  return config;
}
} // namespace yapper
