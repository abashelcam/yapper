#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

#include "types.hpp"

namespace yapper
{
struct VideoFrame
{
  std::uint32_t             id = 0;
  bool                      keyframe = false;
  std::vector<std::uint8_t> data;
};

// Puts video frames back together from their UDP fragments, which can arrive in any order or not at all
class FrameAssembler
{
public:
  // Frames older than this many are given up on, their missing fragments aren't coming
  static constexpr std::size_t kMaxPartialFrames = 8;

  // Returns the frame once its last missing fragment arrives
  std::optional<VideoFrame> Add(std::uint32_t                 frameId,
                                std::uint16_t                 index,
                                std::uint16_t                 count,
                                bool                          keyframe,
                                std::span<const std::uint8_t> data)
  {
    if (count == 0 || count > kMaxVideoFragments || index >= count || data.empty())
      return std::nullopt;

    Partial &partial = partial_[frameId];
    if (partial.fragments.empty())
      partial.fragments.resize(count);

    if (partial.fragments.size() != count || !partial.fragments[index].empty())
      return std::nullopt;

    partial.fragments[index].assign(data.begin(), data.end());
    partial.keyframe = partial.keyframe || keyframe;
    partial.received++;

    if (partial.received < count)
    {
      while (partial_.size() > kMaxPartialFrames)
        partial_.erase(partial_.begin());
      return std::nullopt;
    }

    VideoFrame frame{frameId, partial.keyframe, {}};
    for (const std::vector<std::uint8_t> &fragment : partial.fragments)
      frame.data.insert(frame.data.end(), fragment.begin(), fragment.end());

    // Anything older can't be used anymore, the decoder has moved past it
    partial_.erase(partial_.begin(), partial_.upper_bound(frameId));
    return frame;
  }

private:
  struct Partial
  {
    std::vector<std::vector<std::uint8_t>> fragments;
    std::size_t                            received = 0;
    bool                                   keyframe = false;
  };

  std::map<std::uint32_t, Partial> partial_;
};
} // namespace yapper
