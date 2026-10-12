#include "screen_viewer.hpp"

#include <wels/codec_api.h>

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace yapper
{
constexpr auto kKeyframeRequestGap = std::chrono::milliseconds(500);

ScreenViewer::ScreenViewer(Connection &connection) : connection_(connection)
{
  if (WelsCreateDecoder(&decoder_) != 0 || decoder_ == nullptr)
  {
    throw std::runtime_error("couldn't create the H.264 decoder");
  }

  // Report damaged frames as errors instead of guessing, so we know to ask for a keyframe
  SDecodingParam params{};
  params.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_DEFAULT;
  params.eEcActiveIdc = ERROR_CON_DISABLE;
  if (decoder_->Initialize(&params) != cmResultSuccess)
  {
    WelsDestroyDecoder(decoder_);
    throw std::runtime_error("couldn't start the H.264 decoder");
  }
}

ScreenViewer::~ScreenViewer()
{
  decoder_->Uninitialize();
  WelsDestroyDecoder(decoder_);
}

void ScreenViewer::AskForKeyframe()
{
  const auto now = std::chrono::steady_clock::now();
  if (now - lastRequest_ > kKeyframeRequestGap)
  {
    lastRequest_ = now;
    connection_.RequestKeyframe();
  }
}

void ScreenViewer::Push(const VideoFrame &frame)
{
  // Late or repeated
  if (frame.id <= lastFrameId_)
  {
    return;
  }

  // Each frame builds on the one before it, so after a lost frame everything is garbage until the next keyframe
  if (!frame.keyframe && (waitingForKeyframe_ || frame.id != lastFrameId_ + 1))
  {
    waitingForKeyframe_ = true;
    AskForKeyframe();
    return;
  }

  lastFrameId_ = frame.id;

  unsigned char *planes[3] = {};
  SBufferInfo    info{};
  const DECODING_STATE state =
      decoder_->DecodeFrameNoDelay(frame.data.data(), static_cast<int>(frame.data.size()), planes, &info);
  if (state != dsErrorFree)
  {
    waitingForKeyframe_ = true;
    AskForKeyframe();
    return;
  }

  waitingForKeyframe_ = false;
  if (info.iBufferStatus != 1)
  {
    return;
  }

  const int width = info.UsrData.sSystemBuffer.iWidth;
  const int height = info.UsrData.sSystemBuffer.iHeight;
  const int yStride = info.UsrData.sSystemBuffer.iStride[0];
  const int uvStride = info.UsrData.sSystemBuffer.iStride[1];

  // Only copy the planes here. Turning them into RGBA happens on the UI thread, so voice isn't held up.
  std::lock_guard lock(mutex_);
  yuv_.resize(static_cast<std::size_t>(width) * height * 3 / 2);
  std::uint8_t *yPlane = yuv_.data();
  std::uint8_t *uPlane = yPlane + width * height;
  std::uint8_t *vPlane = uPlane + (width / 2) * (height / 2);
  for (int y = 0; y < height; y++)
  {
    std::memcpy(yPlane + y * width, planes[0] + y * yStride, width);
  }
  for (int y = 0; y < height / 2; y++)
  {
    std::memcpy(uPlane + y * (width / 2), planes[1] + y * uvStride, width / 2);
    std::memcpy(vPlane + y * (width / 2), planes[2] + y * uvStride, width / 2);
  }

  width_ = width;
  height_ = height;
  fresh_ = true;
}

bool ScreenViewer::TakePicture(std::vector<std::uint8_t> &rgba,
                               int                       &width,
                               int                       &height)
{
  std::vector<std::uint8_t> yuv;
  {
    std::lock_guard lock(mutex_);
    if (!fresh_)
    {
      return false;
    }

    yuv.swap(yuv_);
    width = width_;
    height = height_;
    fresh_ = false;
  }

  const std::uint8_t *yPlane = yuv.data();
  const std::uint8_t *uPlane = yPlane + width * height;
  const std::uint8_t *vPlane = uPlane + (width / 2) * (height / 2);
  rgba.resize(static_cast<std::size_t>(width) * height * 4);

  // I420 (BT.601, limited range) back to RGBA, the reverse of what the sharer did
  for (int y = 0; y < height; y++)
  {
    std::uint8_t *out = &rgba[static_cast<std::size_t>(y) * width * 4];
    for (int x = 0; x < width; x++, out += 4)
    {
      const int c = 298 * (yPlane[y * width + x] - 16) + 128;
      const int d = uPlane[(y / 2) * (width / 2) + x / 2] - 128;
      const int e = vPlane[(y / 2) * (width / 2) + x / 2] - 128;

      out[0] = static_cast<std::uint8_t>(std::clamp((c + 409 * e) >> 8, 0, 255));
      out[1] = static_cast<std::uint8_t>(std::clamp((c - 100 * d - 208 * e) >> 8, 0, 255));
      out[2] = static_cast<std::uint8_t>(std::clamp((c + 516 * d) >> 8, 0, 255));
      out[3] = 255;
    }
  }
  return true;
}
} // namespace yapper
