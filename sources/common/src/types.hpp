#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace yapper
{
using SessionId = std::uint32_t;
using ChannelId = std::uint32_t;

using Key = std::array<std::uint8_t, 32>;

inline constexpr std::uint16_t kProtocolVersion = 4;

inline constexpr std::uint16_t kDefaultPort = 5688;

inline constexpr std::size_t   kMaxUsernameBytes = 32;
inline constexpr std::size_t   kMaxServerNameBytes = 64;
inline constexpr std::size_t   kMaxChannelNameBytes = 32;
inline constexpr std::size_t   kMaxChannels = 64;
inline constexpr std::size_t   kMaxReasonBytes = 256;
inline constexpr std::size_t   kMaxPasswordBytes = 64;
inline constexpr std::size_t   kMaxChatBytes = 1024;
inline constexpr std::uint32_t kMaxPayloadBytes = 64 * 1024;
inline constexpr std::size_t   kMaxUdpPacketBytes = 1400;

// Video frames are split into UDP fragments small enough to fit kMaxUdpPacketBytes with all headers
inline constexpr std::size_t kVideoFragmentBytes = 1200;
inline constexpr std::size_t kMaxVideoFragments = 1000;
} // namespace yapper
