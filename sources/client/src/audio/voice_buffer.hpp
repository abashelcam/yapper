#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <vector>

namespace yapper
{
// A small jitter buffer for one person talking. Packets go in as they arrive from the network,
// and the audio callback takes one out every 20 ms, in order, after a short delay to smooth out network timing.
class VoiceBuffer
{
public:
  enum class Result
  {
    Empty,  // nothing to play
    Packet, // play the packet
    Lost,   // a packet is missing, let the decoder fill the gap
  };

  static constexpr std::size_t   kStartPackets = 3; // wait for 60 ms of audio before starting
  static constexpr std::size_t   kMaxPackets = 25;  // past 500 ms, drop the oldest so the delay can't keep growing
  static constexpr std::uint32_t kMaxLost = 3;      // a bigger gap is skipped instead of filled

  void Push(std::uint32_t             seq,
            std::vector<std::uint8_t> packet)
  {
    // Too late, that spot was already played
    if (seq < nextSeq_)
      return;

    packets_[seq] = std::move(packet);

    while (packets_.size() > kMaxPackets)
      packets_.erase(packets_.begin());
  }

  Result Pop(std::vector<std::uint8_t> &out)
  {
    if (!playing_)
    {
      if (packets_.size() < kStartPackets)
        return Result::Empty;

      playing_ = true;
      nextSeq_ = packets_.begin()->first;
    }

    // They stopped talking, or the network fell behind. Either way, buffer up again before playing more.
    if (packets_.empty())
    {
      playing_ = false;
      return Result::Empty;
    }

    if (packets_.begin()->first > nextSeq_ + kMaxLost)
      nextSeq_ = packets_.begin()->first;

    auto found = packets_.find(nextSeq_++);
    if (found == packets_.end())
      return Result::Lost;

    out = std::move(found->second);
    packets_.erase(found);
    return Result::Packet;
  }

private:
  std::map<std::uint32_t, std::vector<std::uint8_t>> packets_;
  std::uint32_t                                      nextSeq_ = 0;
  bool                                               playing_ = false;
};
} // namespace yapper
