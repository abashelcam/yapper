#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>

#include "config.hpp"
#include "secure_socket.hpp"

namespace yapper
{
class Server
{
public:
  explicit Server(ServerConfig config);

  // Blocks until Stop() is called
  void Run();

  // Safe to call from any thread
  void Stop();

private:
  struct Session
  {
    SessionId                     id = 0;
    std::string                   username;
    ChannelId                     channel = 0;
    bool                          sharing = false;
    SessionId                     watching = 0;
    std::shared_ptr<SecureSocket> socket;
    SessionKeys                   keys;

    // Filled in by the first valid UDP packet from this user
    udp::endpoint udpEndpoint;
    bool          udpLinked = false;
    std::array<std::uint32_t, kPacketKinds> lastSeq{};
  };

  asio::awaitable<void> AcceptLoop();
  asio::awaitable<void> HandleClient(tcp::socket tcpSocket);
  asio::awaitable<void> UdpLoop();

  void HandleMessage(Session       &session,
                     const Message &message);
  void HandlePacket(const udp::endpoint          &from,
                    std::span<const std::uint8_t> packet);

  void ForwardKeyframeRequest(const Session &viewer);

  template <typename T>
  void Broadcast(const T &message);

  bool IsUsernameTaken(const std::string &username) const;

  ServerConfig     config_;
  Identity         identity_;
  asio::io_context context_;
  tcp::acceptor    acceptor_;
  udp::socket      udp_;

  // Logged in users. Everything runs on one thread, so no locking needed.
  std::map<SessionId, std::shared_ptr<Session>> sessions_;
  SessionId                                     nextSessionId_ = 1;
};
} // namespace yapper
