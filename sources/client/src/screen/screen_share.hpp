#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "connection.hpp"

namespace yapper
{
// "Monitor 1 (1920x1080)" for each monitor, in the order ScreenShare numbers them
std::vector<std::string> ListMonitors();

// Captures one monitor, scales it to fit 720p, encodes H.264 at 30 fps and sends it through the connection.
// Runs on its own thread from construction until destruction.
class ScreenShare
{
public:
  ScreenShare(Connection &connection,
              int         monitor);
  ~ScreenShare();

  ScreenShare(const ScreenShare &) = delete;
  ScreenShare &operator=(const ScreenShare &) = delete;

  // Empty while everything works. Set once the capture thread gives up.
  std::string Error() const;

private:
  void Run();
  void Capture();

  Connection       &connection_;
  int               monitor_;
  std::atomic<bool> running_ = true;

  mutable std::mutex mutex_;
  std::string        error_;

  std::thread thread_;
};
} // namespace yapper
