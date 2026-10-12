#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "types.hpp"

namespace yapper
{
struct ServerConfig
{
  std::uint16_t            port = kDefaultPort;
  std::string              name = "Yapper Server";
  std::uint32_t            maxUsers = 32;
  std::string              password; // empty means anyone can join
  std::vector<std::string> channels = {"Lobby", "Gaming", "AFK"};

  // Not in the JSON. Created on first run, delete it and every client will warn that the key changed.
  std::string keyFile = "server.key";
};

// Reads the JSON file. If it doesn't exist, writes one with the defaults first.
// Throws with a readable message if the file is broken.
ServerConfig LoadServerConfig(const std::string &path);
} // namespace yapper
