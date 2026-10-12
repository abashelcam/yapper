#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "serialize.hpp"
#include "types.hpp"

namespace yapper
{
enum class MessageType : std::uint16_t
{
  ServerHello = 1, // server -> client, plain
  ClientHello = 2, // client -> server, plain
  ServerReady = 3, // server -> client, plain. Everything after this is encrypted.
  Login = 4,       // client -> server
  LoginOk = 5,     // server -> client
  LoginError = 6,  // server -> client
  UserState = 7,   // server -> client, a user joined or moved channel
  UserLeft = 8,    // server -> client
  JoinChannel = 9, // client -> server
  SendChat = 10,   // client -> server
  Chat = 11,       // server -> client
  StartShare = 12,      // client -> server, wants to share their screen
  StopShare = 13,       // client -> server
  Watch = 14,           // client -> server, which sharer to receive video from (0 = none)
  RequestKeyframe = 15, // both ways: viewer -> server -> sharer, "send a full frame"
};

struct Header
{
  MessageType   type;
  std::uint32_t length;
};

inline constexpr std::size_t kHeaderSize = 2 + 4;

std::optional<Header> ParseHeader(std::span<const std::uint8_t> bytes);

struct Message
{
  Header                    header;
  std::vector<std::uint8_t> payload;
};

// Splits one encoded message back into header + payload, checking the length matches
std::optional<Message> ParseMessage(std::span<const std::uint8_t> bytes);

// Must always start with protocolVersion, so any client can tell it needs to update
struct ServerHello
{
  static constexpr MessageType kType = MessageType::ServerHello;

  std::uint16_t                protocolVersion = kProtocolVersion;
  Key                          identityKey{}; // long term signing key, pinned by the client
  Key                          exchangeKey{}; // fresh for every connection
  std::array<std::uint8_t, 64> signature{};   // identityKey signing exchangeKey

  void Write(ByteWriter &w) const;
  bool Read(ByteReader &r);
};

struct ClientHello
{
  static constexpr MessageType kType = MessageType::ClientHello;

  Key                          exchangeKey{};
  std::array<std::uint8_t, 24> streamHeader{};

  void Write(ByteWriter &w) const;
  bool Read(ByteReader &r);
};

struct ServerReady
{
  static constexpr MessageType kType = MessageType::ServerReady;

  std::array<std::uint8_t, 24> streamHeader{};

  void Write(ByteWriter &w) const;
  bool Read(ByteReader &r);
};

struct Login
{
  static constexpr MessageType kType = MessageType::Login;

  std::string username;
  std::string password;

  void Write(ByteWriter &w) const;
  bool Read(ByteReader &r);
};

struct ChannelInfo
{
  ChannelId   id = 0;
  std::string name;
};

struct LoginOk
{
  static constexpr MessageType kType = MessageType::LoginOk;

  SessionId                sessionId = 0;
  std::string              serverName;
  std::vector<ChannelInfo> channels;

  void Write(ByteWriter &w) const;
  bool Read(ByteReader &r);
};

struct LoginError
{
  static constexpr MessageType kType = MessageType::LoginError;

  std::string reason;

  void Write(ByteWriter &w) const;
  bool Read(ByteReader &r);
};

struct UserState
{
  static constexpr MessageType kType = MessageType::UserState;

  SessionId   sessionId = 0;
  std::string username;
  ChannelId   channelId = 0;
  bool        sharing = false;

  void Write(ByteWriter &w) const;
  bool Read(ByteReader &r);
};

struct UserLeft
{
  static constexpr MessageType kType = MessageType::UserLeft;

  SessionId sessionId = 0;

  void Write(ByteWriter &w) const;
  bool Read(ByteReader &r);
};

struct JoinChannel
{
  static constexpr MessageType kType = MessageType::JoinChannel;

  ChannelId channelId = 0;

  void Write(ByteWriter &w) const;
  bool Read(ByteReader &r);
};

struct SendChat
{
  static constexpr MessageType kType = MessageType::SendChat;

  std::string text;

  void Write(ByteWriter &w) const;
  bool Read(ByteReader &r);
};

struct Chat
{
  static constexpr MessageType kType = MessageType::Chat;

  std::string from;
  std::string text;

  void Write(ByteWriter &w) const;
  bool Read(ByteReader &r);
};

// The server only allows one sharer per channel
struct StartShare
{
  static constexpr MessageType kType = MessageType::StartShare;

  void Write(ByteWriter &w) const;
  bool Read(ByteReader &r);
};

struct StopShare
{
  static constexpr MessageType kType = MessageType::StopShare;

  void Write(ByteWriter &w) const;
  bool Read(ByteReader &r);
};

struct Watch
{
  static constexpr MessageType kType = MessageType::Watch;

  SessionId sessionId = 0;

  void Write(ByteWriter &w) const;
  bool Read(ByteReader &r);
};

struct RequestKeyframe
{
  static constexpr MessageType kType = MessageType::RequestKeyframe;

  void Write(ByteWriter &w) const;
  bool Read(ByteReader &r);
};

// UDP packets. Client -> server: [u32 sessionId][sealed: u8 kind, u32 seq, data...]
// Server -> client: [sealed: u8 kind, u32 fromSessionId, u32 seq, data...]
// Each kind has its own seq counter, so voice seqs stay gapless for the jitter buffer.
// Video data: [u32 frameId][u16 fragmentIndex][u16 fragmentCount][u8 keyframe][bytes...]
enum class PacketKind : std::uint8_t
{
  Ping = 0, // keeps the NAT mapping open and tells the server our address
  Voice = 1,
  Video = 2,
};

inline constexpr std::size_t kPacketKinds = 3;

template <typename T>
std::vector<std::uint8_t> Encode(const T &message)
{
  ByteWriter payload;
  message.Write(payload);

  ByteWriter out;
  out.WriteU16(static_cast<std::uint16_t>(T::kType));
  out.WriteU32(static_cast<std::uint32_t>(payload.Data().size()));
  out.WriteBytes(payload.Data());
  return out.Take();
}

template <typename T>
std::optional<T> Decode(std::span<const std::uint8_t> payload)
{
  ByteReader reader(payload);
  T          message;

  if (!message.Read(reader) || !reader.Ok() || !reader.AtEnd())
    return std::nullopt;

  return message;
}

// Decodes a message and checks it is the expected type
template <typename T>
std::optional<T> Decode(const Message &message)
{
  if (message.header.type != T::kType)
    return std::nullopt;

  return Decode<T>(message.payload);
}
} // namespace yapper
