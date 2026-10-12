#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

#include "connection.hpp"

class ISVCDecoder;

namespace yapper
{
// Decodes the H.264 stream of the person you watch into RGBA pictures for the UI
class ScreenViewer
{
public:
  explicit ScreenViewer(Connection &connection);
  ~ScreenViewer();

  ScreenViewer(const ScreenViewer &) = delete;
  ScreenViewer &operator=(const ScreenViewer &) = delete;

  // Called on the network thread
  void Push(const VideoFrame &frame);

  // Called on the UI thread. True if there's a newer picture than the last one taken.
  bool TakePicture(std::vector<std::uint8_t> &rgba,
                   int                       &width,
                   int                       &height);

private:
  void AskForKeyframe();

  Connection  &connection_;
  ISVCDecoder *decoder_ = nullptr;

  // Only touched on the network thread
  std::uint32_t                         lastFrameId_ = 0;
  bool                                  waitingForKeyframe_ = true;
  std::chrono::steady_clock::time_point lastRequest_{};

  std::mutex                mutex_;
  std::vector<std::uint8_t> yuv_; // the latest decoded picture, I420 without padding
  int                       width_ = 0;
  int                       height_ = 0;
  bool                      fresh_ = false;
};
} // namespace yapper
