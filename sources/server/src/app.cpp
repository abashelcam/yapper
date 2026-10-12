#include "app.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace yapper
{
// The key file holds the 64 byte signing secret key, which also contains the public key
static Identity LoadIdentity(const std::string &path)
{
  Identity identity{};

  std::ifstream in(path, std::ios::binary);
  if (in)
  {
    in.read(reinterpret_cast<char *>(identity.secretKey.data()), identity.secretKey.size());
    if (in.gcount() != static_cast<std::streamsize>(identity.secretKey.size()))
    {
      throw std::runtime_error(path + " is broken, delete it to make a new key");
    }
    crypto_sign_ed25519_sk_to_pk(identity.publicKey.data(), identity.secretKey.data());
    return identity;
  }

  identity = GenerateIdentity();

  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char *>(identity.secretKey.data()), identity.secretKey.size());
  out.close();
  if (!out)
  {
    throw std::runtime_error("couldn't write " + path);
  }

  std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
  std::cout << "created a new server key in " << path << "\n";
  return identity;
}

static UserState StateOf(const auto &session)
{
  return UserState{session.id, session.username, session.channel, session.sharing};
}

Server::Server(ServerConfig config)
    : config_(std::move(config)),
      acceptor_(context_,
                tcp::endpoint(tcp::v4(),
                              config_.port)),
      udp_(context_,
           udp::endpoint(udp::v4(),
                         config_.port))
{
  InitCrypto();
  identity_ = LoadIdentity(config_.keyFile);

  // A video keyframe arrives as a burst of fragments, the default buffers can be too small for it
  udp_.set_option(asio::socket_base::receive_buffer_size(1 << 20));
  udp_.set_option(asio::socket_base::send_buffer_size(1 << 20));
}

void Server::Run()
{
  asio::co_spawn(context_, AcceptLoop(), asio::detached);
  asio::co_spawn(context_, UdpLoop(), asio::detached);
  context_.run();
}

void Server::Stop()
{
  context_.stop();
}

asio::awaitable<void> Server::AcceptLoop()
{
  while (true)
  {
    tcp::socket socket = co_await acceptor_.async_accept(asio::use_awaitable);

    // Each client gets its own coroutine, so one slow client never blocks the others
    asio::co_spawn(context_, HandleClient(std::move(socket)), asio::detached);
  }
}

bool Server::IsUsernameTaken(const std::string &username) const
{
  for (const auto &[id, session] : sessions_)
  {
    if (session->username == username)
    {
      return true;
    }
  }
  return false;
}

template <typename T>
void Server::Broadcast(const T &message)
{
  for (const auto &[id, session] : sessions_)
  {
    session->socket->Send(message);
  }
}

asio::awaitable<void> Server::HandleClient(tcp::socket tcpSocket)
{
  auto                     socket = std::make_shared<SecureSocket>(std::move(tcpSocket));
  std::shared_ptr<Session> session;

  try
  {
    SessionKeys keys = co_await socket->AcceptHandshake(identity_);

    std::optional<Login> login = Decode<Login>(co_await socket->Read());
    if (!login)
    {
      throw std::runtime_error("expected Login");
    }

    // shortcut: no limit on password guesses, add a per-IP delay if the server is ever public
    std::string refusal;
    if (!config_.password.empty() && login->password != config_.password)
    {
      refusal = "Wrong password.";
    }
    else if (sessions_.size() >= config_.maxUsers)
    {
      refusal = "The server is full.";
    }
    else if (IsUsernameTaken(login->username))
    {
      refusal = "That name is already taken.";
    }

    if (!refusal.empty())
    {
      socket->Send(LoginError{refusal});
      co_return;
    }

    session = std::make_shared<Session>();
    session->id = nextSessionId_++;
    session->username = login->username;
    session->socket = socket;
    session->keys = keys;

    LoginOk ok{};
    ok.sessionId = session->id;
    ok.serverName = config_.name;
    for (std::size_t i = 0; i < config_.channels.size(); i++)
    {
      ok.channels.push_back(ChannelInfo{static_cast<ChannelId>(i), config_.channels[i]});
    }
    socket->Send(ok);

    for (const auto &[id, other] : sessions_)
    {
      socket->Send(StateOf(*other));
    }

    sessions_[session->id] = session;
    Broadcast(StateOf(*session));

    std::cout << session->username << " joined (" << sessions_.size() << " online)\n";

    while (true)
    {
      HandleMessage(*session, co_await socket->Read());
    }
  }
  catch (const asio::system_error &e)
  {
    // eof and reset are the client closing the app, anything else is worth logging
    if (e.code() != asio::error::eof && e.code() != asio::error::connection_reset)
    {
      std::cout << "connection error: " << e.what() << "\n";
    }
  }
  catch (const std::exception &e)
  {
    std::cout << "dropping client: " << e.what() << "\n";
  }

  if (session)
  {
    sessions_.erase(session->id);
    Broadcast(UserLeft{session->id});
    std::cout << session->username << " left (" << sessions_.size() << " online)\n";
  }
}

void Server::HandleMessage(Session       &session,
                           const Message &message)
{
  if (message.header.type == MessageType::JoinChannel)
  {
    std::optional<JoinChannel> join = Decode<JoinChannel>(message);
    if (!join || join->channelId >= config_.channels.size())
    {
      throw std::runtime_error("bad JoinChannel");
    }

    // Moving stops sharing and watching, both only work inside a channel
    session.channel = join->channelId;
    session.sharing = false;
    session.watching = 0;
    Broadcast(StateOf(session));
    std::cout << session.username << " moved to " << config_.channels[session.channel] << "\n";
  }
  else if (message.header.type == MessageType::SendChat)
  {
    std::optional<SendChat> chat = Decode<SendChat>(message);
    if (!chat)
    {
      throw std::runtime_error("bad SendChat");
    }

    for (const auto &[id, other] : sessions_)
    {
      if (other->channel == session.channel)
      {
        other->socket->Send(Chat{session.username, chat->text});
      }
    }
  }
  else if (message.header.type == MessageType::StartShare)
  {
    for (const auto &[id, other] : sessions_)
    {
      if (other->channel == session.channel && other->sharing)
      {
        return; // someone in this channel already shares
      }
    }

    session.sharing = true;
    Broadcast(StateOf(session));
    std::cout << session.username << " started sharing\n";
  }
  else if (message.header.type == MessageType::StopShare)
  {
    if (session.sharing)
    {
      session.sharing = false;
      Broadcast(StateOf(session));
      std::cout << session.username << " stopped sharing\n";
    }
  }
  else if (std::optional<Watch> watch = Decode<Watch>(message))
  {
    session.watching = watch->sessionId;
    ForwardKeyframeRequest(session);
  }
  else if (message.header.type == MessageType::RequestKeyframe)
  {
    ForwardKeyframeRequest(session);
  }
  else
  {
    std::cout << session.username << " sent unknown message type " << static_cast<int>(message.header.type) << "\n";
  }
}

// A new or out of sync viewer needs a full frame before it can show anything
void Server::ForwardKeyframeRequest(const Session &viewer)
{
  auto sharer = sessions_.find(viewer.watching);
  if (sharer != sessions_.end() && sharer->second->sharing && sharer->second->channel == viewer.channel)
  {
    sharer->second->socket->Send(RequestKeyframe{});
  }
}

asio::awaitable<void> Server::UdpLoop()
{
  std::array<std::uint8_t, kMaxUdpPacketBytes> buffer{};
  udp::endpoint                                from;

  while (true)
  {
    asio::error_code  error;
    const std::size_t size =
        co_await udp_.async_receive_from(asio::buffer(buffer), from, asio::redirect_error(asio::use_awaitable, error));

    // Windows reports a bounced earlier send as an error on the next receive. Only those are worth retrying.
    if (error == asio::error::connection_reset || error == asio::error::connection_refused)
    {
      continue;
    }
    if (error)
    {
      std::cout << "UDP stopped: " << error.message() << "\n";
      co_return;
    }

    HandlePacket(from, std::span(buffer.data(), size));
  }
}

void Server::HandlePacket(const udp::endpoint          &from,
                          std::span<const std::uint8_t> packet)
{
  ByteReader      header(packet);
  const SessionId sessionId = header.ReadU32();

  auto found = sessions_.find(sessionId);
  if (!header.Ok() || found == sessions_.end())
  {
    return;
  }

  Session &sender = *found->second;

  std::optional<std::vector<std::uint8_t>> plain = OpenPacket(sender.keys.packetRx, packet.subspan(4));
  if (!plain)
  {
    return;
  }

  ByteReader                body(*plain);
  const std::uint8_t        kindIndex = body.ReadU8();
  const std::uint32_t       seq = body.ReadU32();
  std::vector<std::uint8_t> data = body.ReadRest();

  // Anything not newer than the last packet of its kind is a replay, or too late to be useful
  if (!body.Ok() || kindIndex >= kPacketKinds || seq <= sender.lastSeq[kindIndex])
  {
    return;
  }

  sender.lastSeq[kindIndex] = seq;
  sender.udpEndpoint = from;
  sender.udpLinked = true;

  const auto kind = static_cast<PacketKind>(kindIndex);
  if (kind == PacketKind::Ping || data.empty() || (kind == PacketKind::Video && !sender.sharing))
  {
    return;
  }

  ByteWriter forward;
  forward.WriteU8(kindIndex);
  forward.WriteU32(sender.id);
  forward.WriteU32(seq);
  forward.WriteBytes(data);

  for (const auto &[id, other] : sessions_)
  {
    // Voice goes to the whole channel, video only to the people watching
    const bool wanted = kind == PacketKind::Voice || other->watching == sender.id;
    if (other->id != sender.id && other->channel == sender.channel && other->udpLinked && wanted)
    {
      std::vector<std::uint8_t> sealed = SealPacket(other->keys.packetTx, forward.Data());
      asio::error_code          ignored;
      udp_.send_to(asio::buffer(sealed), other->udpEndpoint, 0, ignored);
    }
  }
}
} // namespace yapper
