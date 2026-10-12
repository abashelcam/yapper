#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <vector>

#include <asio.hpp>

#include "crypto.hpp"
#include "protocol.hpp"

// An encrypted TCP connection, used by both the client and the server.
// Every frame on the wire is [u32 length][bytes]. After the handshake the bytes are encrypted.

namespace yapper
{
using asio::ip::tcp;
using asio::ip::udp;

struct ClientHandshake
{
  std::uint16_t serverVersion = 0; // if this isn't kProtocolVersion, nothing else is filled in
  Key           serverIdentity{};
  SessionKeys   keys{};
};

// Always create with std::make_shared, the write loop keeps itself alive with shared_from_this
class SecureSocket : public std::enable_shared_from_this<SecureSocket>
{
public:
  explicit SecureSocket(tcp::socket socket);

  // Server side of the handshake. Returns the keys for this connection's UDP packets.
  asio::awaitable<SessionKeys> AcceptHandshake(const Identity &identity);

  // Client side of the handshake
  asio::awaitable<ClientHandshake> ConnectHandshake();

  // Waits for the next message. Throws if the connection drops or a message fails to decrypt.
  asio::awaitable<Message> Read();

  // Queues the message and returns right away. Only call from the io_context thread.
  template <typename T>
  void Send(const T &message)
  {
    QueueFrame(sender_.Seal(Encode(message)));
  }

  void Close();

  tcp::socket &Socket()
  {
    return socket_;
  }

private:
  asio::awaitable<std::vector<std::uint8_t>> ReadFrame();
  asio::awaitable<Message>                   ReadPlain();

  void                  QueueFrame(std::span<const std::uint8_t> bytes);
  asio::awaitable<void> WriteLoop(std::shared_ptr<SecureSocket> self);

  tcp::socket    socket_;
  StreamSender   sender_;
  StreamReceiver receiver_;

  // asio allows only one write at a time per socket, so sends wait their turn here
  std::deque<std::vector<std::uint8_t>> outbox_;
  bool                                  writing_ = false;
};
} // namespace yapper
