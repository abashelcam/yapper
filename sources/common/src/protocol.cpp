#include "protocol.hpp"

namespace yapper
{
std::optional<Header> ParseHeader(std::span<const std::uint8_t> bytes)
{
  ByteReader reader(bytes);

  Header header{};
  header.type = static_cast<MessageType>(reader.ReadU16());
  header.length = reader.ReadU32();

  if (!reader.Ok() || header.length > kMaxPayloadBytes)
    return std::nullopt;

  return header;
}

std::optional<Message> ParseMessage(std::span<const std::uint8_t> bytes)
{
  std::optional<Header> header = ParseHeader(bytes);
  if (!header || bytes.size() != kHeaderSize + header->length)
    return std::nullopt;

  return Message{*header, std::vector<std::uint8_t>(bytes.begin() + kHeaderSize, bytes.end())};
}

void ServerHello::Write(ByteWriter &w) const
{
  w.WriteU16(protocolVersion);
  w.WriteBytes(identityKey);
  w.WriteBytes(exchangeKey);
  w.WriteBytes(signature);
}

bool ServerHello::Read(ByteReader &r)
{
  protocolVersion = r.ReadU16();
  r.ReadBytes(identityKey);
  r.ReadBytes(exchangeKey);
  r.ReadBytes(signature);

  return r.Ok();
}

void ClientHello::Write(ByteWriter &w) const
{
  w.WriteBytes(exchangeKey);
  w.WriteBytes(streamHeader);
}

bool ClientHello::Read(ByteReader &r)
{
  r.ReadBytes(exchangeKey);
  r.ReadBytes(streamHeader);

  return r.Ok();
}

void ServerReady::Write(ByteWriter &w) const
{
  w.WriteBytes(streamHeader);
}

bool ServerReady::Read(ByteReader &r)
{
  r.ReadBytes(streamHeader);

  return r.Ok();
}

void Login::Write(ByteWriter &w) const
{
  w.WriteString(username);
  w.WriteString(password);
}

bool Login::Read(ByteReader &r)
{
  username = r.ReadString(kMaxUsernameBytes);
  password = r.ReadString(kMaxPasswordBytes);

  return r.Ok() && !username.empty();
}

void LoginOk::Write(ByteWriter &w) const
{
  w.WriteU32(sessionId);
  w.WriteString(serverName);
  w.WriteU16(static_cast<std::uint16_t>(channels.size()));
  for (const ChannelInfo &channel : channels)
  {
    w.WriteU32(channel.id);
    w.WriteString(channel.name);
  }
}

bool LoginOk::Read(ByteReader &r)
{
  sessionId = r.ReadU32();
  serverName = r.ReadString(kMaxServerNameBytes);

  const std::uint16_t count = r.ReadU16();
  if (count > kMaxChannels)
    return false;

  channels.clear();
  for (std::uint16_t i = 0; i < count && r.Ok(); i++)
  {
    ChannelInfo channel{};
    channel.id = r.ReadU32();
    channel.name = r.ReadString(kMaxChannelNameBytes);
    channels.push_back(channel);
  }

  return r.Ok();
}

void LoginError::Write(ByteWriter &w) const
{
  w.WriteString(reason);
}

bool LoginError::Read(ByteReader &r)
{
  reason = r.ReadString(kMaxReasonBytes);

  return r.Ok();
}

void UserState::Write(ByteWriter &w) const
{
  w.WriteU32(sessionId);
  w.WriteString(username);
  w.WriteU32(channelId);
  w.WriteU8(sharing ? 1 : 0);
}

bool UserState::Read(ByteReader &r)
{
  sessionId = r.ReadU32();
  username = r.ReadString(kMaxUsernameBytes);
  channelId = r.ReadU32();
  sharing = r.ReadU8() != 0;

  return r.Ok();
}

void UserLeft::Write(ByteWriter &w) const
{
  w.WriteU32(sessionId);
}

bool UserLeft::Read(ByteReader &r)
{
  sessionId = r.ReadU32();

  return r.Ok();
}

void JoinChannel::Write(ByteWriter &w) const
{
  w.WriteU32(channelId);
}

bool JoinChannel::Read(ByteReader &r)
{
  channelId = r.ReadU32();

  return r.Ok();
}

void SendChat::Write(ByteWriter &w) const
{
  w.WriteString(text);
}

bool SendChat::Read(ByteReader &r)
{
  text = r.ReadString(kMaxChatBytes);

  return r.Ok() && !text.empty();
}

void Chat::Write(ByteWriter &w) const
{
  w.WriteString(from);
  w.WriteString(text);
}

bool Chat::Read(ByteReader &r)
{
  from = r.ReadString(kMaxUsernameBytes);
  text = r.ReadString(kMaxChatBytes);

  return r.Ok();
}

void StartShare::Write(ByteWriter &) const
{
}

bool StartShare::Read(ByteReader &r)
{
  return r.Ok();
}

void StopShare::Write(ByteWriter &) const
{
}

bool StopShare::Read(ByteReader &r)
{
  return r.Ok();
}

void Watch::Write(ByteWriter &w) const
{
  w.WriteU32(sessionId);
}

bool Watch::Read(ByteReader &r)
{
  sessionId = r.ReadU32();

  return r.Ok();
}

void RequestKeyframe::Write(ByteWriter &) const
{
}

bool RequestKeyframe::Read(ByteReader &r)
{
  return r.Ok();
}
} // namespace yapper
