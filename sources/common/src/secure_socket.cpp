#include "secure_socket.hpp"

#include <stdexcept>

namespace yapper
{
SecureSocket::SecureSocket(tcp::socket socket) : socket_(std::move(socket))
{
  InitCrypto();
}

asio::awaitable<SessionKeys> SecureSocket::AcceptHandshake(const Identity &identity)
{
  ExchangeKeys exchange = GenerateExchangeKeys();

  ServerHello hello{};
  hello.identityKey = identity.publicKey;
  hello.exchangeKey = exchange.publicKey;
  hello.signature = Sign(exchange.publicKey, identity);
  QueueFrame(Encode(hello));

  std::optional<ClientHello> clientHello = Decode<ClientHello>(co_await ReadPlain());
  if (!clientHello)
  {
    throw std::runtime_error("expected ClientHello");
  }

  std::optional<SessionKeys> keys = ServerSessionKeys(exchange, clientHello->exchangeKey);
  if (!keys || !receiver_.Start(keys->streamRx, clientHello->streamHeader))
  {
    throw std::runtime_error("bad client key");
  }

  ServerReady ready{};
  ready.streamHeader = sender_.Start(keys->streamTx);
  QueueFrame(Encode(ready));

  co_return *keys;
}

asio::awaitable<ClientHandshake> SecureSocket::ConnectHandshake()
{
  ClientHandshake result{};

  Message first = co_await ReadPlain();

  // Check the version before decoding the rest, a different version may use a different layout
  ByteReader versionReader(first.payload);
  result.serverVersion = versionReader.ReadU16();
  if (first.header.type != MessageType::ServerHello || result.serverVersion != kProtocolVersion)
  {
    co_return result;
  }

  std::optional<ServerHello> hello = Decode<ServerHello>(first);
  if (!hello || !Verify(hello->signature, hello->exchangeKey, hello->identityKey))
  {
    throw std::runtime_error("server sent a bad signature");
  }

  ExchangeKeys               exchange = GenerateExchangeKeys();
  std::optional<SessionKeys> keys = ClientSessionKeys(exchange, hello->exchangeKey);
  if (!keys)
  {
    throw std::runtime_error("server sent a bad key");
  }

  ClientHello clientHello{};
  clientHello.exchangeKey = exchange.publicKey;
  clientHello.streamHeader = sender_.Start(keys->streamTx);
  QueueFrame(Encode(clientHello));

  std::optional<ServerReady> ready = Decode<ServerReady>(co_await ReadPlain());
  if (!ready || !receiver_.Start(keys->streamRx, ready->streamHeader))
  {
    throw std::runtime_error("handshake failed");
  }

  result.serverIdentity = hello->identityKey;
  result.keys = *keys;
  co_return result;
}

asio::awaitable<Message> SecureSocket::Read()
{
  std::vector<std::uint8_t> frame = co_await ReadFrame();

  std::optional<std::vector<std::uint8_t>> plain = receiver_.Open(frame);
  if (!plain)
  {
    throw std::runtime_error("message failed to decrypt");
  }

  std::optional<Message> message = ParseMessage(*plain);
  if (!message)
  {
    throw std::runtime_error("bad message");
  }

  co_return *message;
}

void SecureSocket::Close()
{
  asio::error_code ignored;
  socket_.close(ignored);
}

asio::awaitable<std::vector<std::uint8_t>> SecureSocket::ReadFrame()
{
  constexpr std::uint32_t kMaxFrameBytes =
      kHeaderSize + kMaxPayloadBytes + crypto_secretstream_xchacha20poly1305_ABYTES;

  std::array<std::uint8_t, 4> lengthBytes{};
  co_await asio::async_read(socket_, asio::buffer(lengthBytes), asio::use_awaitable);

  ByteReader          lengthReader(lengthBytes);
  const std::uint32_t length = lengthReader.ReadU32();
  if (length > kMaxFrameBytes)
  {
    throw std::runtime_error("frame too big");
  }

  std::vector<std::uint8_t> frame(length);
  co_await asio::async_read(socket_, asio::buffer(frame), asio::use_awaitable);
  co_return frame;
}

asio::awaitable<Message> SecureSocket::ReadPlain()
{
  std::optional<Message> message = ParseMessage(co_await ReadFrame());
  if (!message)
  {
    throw std::runtime_error("bad handshake message");
  }
  co_return *message;
}

void SecureSocket::QueueFrame(std::span<const std::uint8_t> bytes)
{
  ByteWriter frame;
  frame.WriteU32(static_cast<std::uint32_t>(bytes.size()));
  frame.WriteBytes(bytes);
  outbox_.push_back(frame.Take());

  if (!writing_)
  {
    writing_ = true;
    asio::co_spawn(socket_.get_executor(), WriteLoop(shared_from_this()), asio::detached);
  }
}

// self keeps this object alive until the queue is written, even if its owner is gone
asio::awaitable<void> SecureSocket::WriteLoop(std::shared_ptr<SecureSocket> self)
{
  try
  {
    while (!outbox_.empty())
    {
      co_await asio::async_write(socket_, asio::buffer(outbox_.front()), asio::use_awaitable);
      outbox_.pop_front();
    }
  }
  catch (const std::exception &)
  {
    // The reader sees the closed socket and handles the disconnect
    outbox_.clear();
    Close();
  }

  writing_ = false;
}
} // namespace yapper
