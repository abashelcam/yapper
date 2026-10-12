#include "connection.hpp"

#include <algorithm>
#include <fstream>

namespace yapper
{
constexpr std::size_t kMaxChatLines = 200;

constexpr auto kPingInterval = std::chrono::seconds(5);

Connection::Connection(std::string   host,
                       std::uint16_t port,
                       std::string   username,
                       std::string   password,
                       std::string   knownServersPath)
    : host_(std::move(host)),
      port_(port),
      username_(std::move(username)),
      password_(std::move(password)),
      knownServersPath_(std::move(knownServersPath)),
      udp_(context_)
{
}

void Connection::SetVoiceHandler(VoiceHandler handler)
{
  voiceHandler_ = std::move(handler);
}

void Connection::SetVideoHandler(VideoHandler handler)
{
  videoHandler_ = std::move(handler);
}

void Connection::Run()
{
  asio::co_spawn(context_, Session(), asio::detached);
  context_.run();
}

void Connection::Stop()
{
  context_.stop();
}

ConnectionStatus Connection::GetStatus() const
{
  std::lock_guard lock(mutex_);
  return status_;
}

template <typename T>
void Connection::SendWhenLoggedIn(T message)
{
  asio::post(context_,
             [this, message = std::move(message)]
             {
               if (loggedIn_)
               {
                 socket_->Send(message);
               }
             });
}

void Connection::SwitchChannel(ChannelId channel)
{
  SendWhenLoggedIn(JoinChannel{channel});
}

void Connection::SendChatMessage(std::string text)
{
  if (text.empty() || text.size() > kMaxChatBytes)
  {
    return;
  }

  SendWhenLoggedIn(SendChat{std::move(text)});
}

void Connection::StartSharing()
{
  SendWhenLoggedIn(StartShare{});
}

void Connection::StopSharing()
{
  SendWhenLoggedIn(StopShare{});
}

void Connection::Watch(SessionId sharer)
{
  SendWhenLoggedIn(yapper::Watch{sharer});
}

void Connection::RequestKeyframe()
{
  SendWhenLoggedIn(yapper::RequestKeyframe{});
}

bool Connection::TakeKeyframeRequest()
{
  return keyframeRequested_.exchange(false);
}

// Splits the frame into fragments that each fit in one UDP packet
void Connection::SendVideoFrame(std::vector<std::uint8_t> frame,
                                bool                      keyframe)
{
  const std::size_t count = (frame.size() + kVideoFragmentBytes - 1) / kVideoFragmentBytes;
  if (count == 0 || count > kMaxVideoFragments)
  {
    return;
  }

  asio::post(context_,
             [this, frame = std::move(frame), keyframe, count]
             {
               if (!loggedIn_)
               {
                 return;
               }

               const std::uint32_t frameId = nextFrameId_++;
               for (std::size_t i = 0; i < count; i++)
               {
                 const std::size_t start = i * kVideoFragmentBytes;
                 const std::size_t size = std::min(kVideoFragmentBytes, frame.size() - start);

                 ByteWriter fragment;
                 fragment.WriteU32(frameId);
                 fragment.WriteU16(static_cast<std::uint16_t>(i));
                 fragment.WriteU16(static_cast<std::uint16_t>(count));
                 fragment.WriteU8(keyframe ? 1 : 0);
                 fragment.WriteBytes(std::span(frame).subspan(start, size));
                 SendPacket(PacketKind::Video, fragment.Data());
               }
             });
}

void Connection::SendVoice(std::vector<std::uint8_t> opus)
{
  asio::post(context_,
             [this, opus = std::move(opus)]
             {
               if (!loggedIn_)
               {
                 return;
               }

               SendPacket(PacketKind::Voice, opus);

               std::lock_guard lock(mutex_);
               for (User &user : status_.users)
               {
                 if (user.id == sessionId_)
                 {
                   user.lastVoice = std::chrono::steady_clock::now();
                 }
               }
             });
}

asio::awaitable<void> Connection::Session()
{
  try
  {
    co_await LoginAndListen();
  }
  catch (const std::exception &e)
  {
    std::lock_guard lock(mutex_);
    status_.state = ConnectionState::Disconnected;
    status_.error = e.what();
  }

  // Also ends the UDP loops, so Run() returns
  context_.stop();
}

asio::awaitable<void> Connection::LoginAndListen()
{
  tcp::resolver resolver(context_);
  auto          endpoints = co_await resolver.async_resolve(host_, std::to_string(port_), asio::use_awaitable);

  tcp::socket tcpSocket(context_);
  co_await asio::async_connect(tcpSocket, endpoints, asio::use_awaitable);
  socket_ = std::make_shared<SecureSocket>(std::move(tcpSocket));

  ClientHandshake handshake = co_await socket_->ConnectHandshake();

  std::string refusal;
  if (handshake.serverVersion != kProtocolVersion)
  {
    refusal = "The server runs Yapper protocol " + std::to_string(handshake.serverVersion) + ", you have " +
              std::to_string(kProtocolVersion) + ". Both need the same version.";
  }
  else
  {
    refusal = CheckServerKey(handshake.serverIdentity);
  }

  if (refusal.empty())
  {
    keys_ = handshake.keys;
    socket_->Send(Login{username_, password_});

    Message reply = co_await socket_->Read();
    if (std::optional<LoginError> error = Decode<LoginError>(reply))
    {
      refusal = error->reason;
    }
    else
    {
      std::optional<LoginOk> ok = Decode<LoginOk>(reply);
      if (!ok)
      {
        throw std::runtime_error("unexpected reply to Login");
      }

      sessionId_ = ok->sessionId;
      loggedIn_ = true;

      std::lock_guard lock(mutex_);
      status_.state = ConnectionState::LoggedIn;
      status_.sessionId = ok->sessionId;
      status_.serverName = ok->serverName;
      status_.channels = ok->channels;
    }
  }

  if (!refusal.empty())
  {
    std::lock_guard lock(mutex_);
    status_.state = ConnectionState::Refused;
    status_.error = refusal;
    co_return;
  }

  // Voice and video go over UDP to the same address and port as TCP
  tcp::endpoint server = socket_->Socket().remote_endpoint();
  udp_.connect(udp::endpoint(server.address(), server.port()));
  udp_.set_option(asio::socket_base::receive_buffer_size(1 << 20));
  udp_.set_option(asio::socket_base::send_buffer_size(1 << 20));
  asio::co_spawn(context_, UdpReceiveLoop(), asio::detached);
  asio::co_spawn(context_, PingLoop(), asio::detached);

  while (true)
  {
    HandleMessage(co_await socket_->Read());
  }
}

void Connection::HandleMessage(const Message &message)
{
  std::lock_guard lock(mutex_);

  if (std::optional<UserState> state = Decode<UserState>(message))
  {
    for (User &user : status_.users)
    {
      if (user.id == state->sessionId)
      {
        if (user.id == sessionId_ && user.channel != state->channelId)
        {
          status_.chat.clear();
        }

        user.name = state->username;
        user.channel = state->channelId;
        user.sharing = state->sharing;
        return;
      }
    }

    status_.users.push_back(User{state->sessionId, state->username, state->channelId, state->sharing});
  }
  else if (std::optional<UserLeft> left = Decode<UserLeft>(message))
  {
    std::erase_if(status_.users, [&](const User &user) { return user.id == left->sessionId; });
  }
  else if (std::optional<Chat> chat = Decode<Chat>(message))
  {
    status_.chat.push_back(ChatLine{chat->from, chat->text});
    if (status_.chat.size() > kMaxChatLines)
    {
      status_.chat.erase(status_.chat.begin());
    }
  }
  else if (message.header.type == MessageType::RequestKeyframe)
  {
    keyframeRequested_ = true;
  }
}

asio::awaitable<void> Connection::UdpReceiveLoop()
{
  std::array<std::uint8_t, kMaxUdpPacketBytes> buffer{};

  while (true)
  {
    asio::error_code  error;
    const std::size_t size =
        co_await udp_.async_receive(asio::buffer(buffer), asio::redirect_error(asio::use_awaitable, error));

    // Windows reports a bounced earlier send as an error on the next receive. Only those are worth retrying.
    if (error == asio::error::connection_reset || error == asio::error::connection_refused)
    {
      continue;
    }
    if (error)
    {
      co_return;
    }

    std::optional<std::vector<std::uint8_t>> plain = OpenPacket(keys_.packetRx, std::span(buffer.data(), size));
    if (plain)
    {
      HandlePacket(*plain);
    }
  }
}

void Connection::HandlePacket(std::span<const std::uint8_t> plain)
{
  ByteReader                reader(plain);
  const auto                kind = static_cast<PacketKind>(reader.ReadU8());
  const SessionId           from = reader.ReadU32();
  const std::uint32_t       seq = reader.ReadU32();
  std::vector<std::uint8_t> data = reader.ReadRest();
  if (!reader.Ok() || data.empty())
  {
    return;
  }

  if (kind == PacketKind::Voice)
  {
    {
      std::lock_guard lock(mutex_);
      for (User &user : status_.users)
      {
        if (user.id == from)
        {
          user.lastVoice = std::chrono::steady_clock::now();
        }
      }
    }

    if (voiceHandler_)
    {
      voiceHandler_(from, seq, data);
    }
  }
  else if (kind == PacketKind::Video)
  {
    ByteReader                fragment(data);
    const std::uint32_t       frameId = fragment.ReadU32();
    const std::uint16_t       index = fragment.ReadU16();
    const std::uint16_t       count = fragment.ReadU16();
    const bool                keyframe = fragment.ReadU8() != 0;
    std::vector<std::uint8_t> bytes = fragment.ReadRest();
    if (!fragment.Ok())
    {
      return;
    }

    std::optional<VideoFrame> frame = assemblers_[from].Add(frameId, index, count, keyframe, bytes);
    if (frame && videoHandler_)
    {
      videoHandler_(from, *frame);
    }
  }
}

asio::awaitable<void> Connection::PingLoop()
{
  asio::steady_timer timer(context_);

  while (true)
  {
    SendPacket(PacketKind::Ping, {});

    timer.expires_after(kPingInterval);
    co_await timer.async_wait(asio::use_awaitable);
  }
}

void Connection::SendPacket(PacketKind                    kind,
                            std::span<const std::uint8_t> data)
{
  const auto kindIndex = static_cast<std::uint8_t>(kind);

  ByteWriter body;
  body.WriteU8(kindIndex);
  body.WriteU32(++lastSentSeq_[kindIndex]);
  body.WriteBytes(data);

  ByteWriter packet;
  packet.WriteU32(sessionId_);
  packet.WriteBytes(SealPacket(keys_.packetTx, body.Data()));

  asio::error_code ignored;
  udp_.send(asio::buffer(packet.Data()), 0, ignored);
}

// Trust on first use: remember each server's key, refuse if it ever changes
std::string Connection::CheckServerKey(const Key &serverKey)
{
  const std::string server = host_ + ":" + std::to_string(port_);
  const std::string key = ToHex(serverKey);

  std::ifstream in(knownServersPath_);
  std::string   savedServer;
  std::string   savedKey;
  while (in >> savedServer >> savedKey)
  {
    if (savedServer == server)
    {
      if (savedKey == key)
      {
        return "";
      }

      return "The server's identity key changed since you last connected. Someone may be intercepting the "
             "connection. If the server owner reset the key on purpose, remove the line for " +
             server + " from " + knownServersPath_ + ".";
    }
  }

  std::ofstream out(knownServersPath_, std::ios::app);
  out << server << " " << key << "\n";
  return "";
}
} // namespace yapper
