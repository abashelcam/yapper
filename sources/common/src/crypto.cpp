#include "crypto.hpp"

#include <stdexcept>

namespace yapper
{
void InitCrypto()
{
  if (sodium_init() < 0)
  {
    throw std::runtime_error("libsodium failed to start");
  }
}

Identity GenerateIdentity()
{
  Identity identity{};
  crypto_sign_keypair(identity.publicKey.data(), identity.secretKey.data());
  return identity;
}

Signature Sign(std::span<const std::uint8_t> message,
               const Identity               &identity)
{
  Signature signature{};
  crypto_sign_detached(signature.data(), nullptr, message.data(), message.size(), identity.secretKey.data());
  return signature;
}

bool Verify(const Signature              &signature,
            std::span<const std::uint8_t> message,
            const Key                    &publicKey)
{
  return crypto_sign_verify_detached(signature.data(), message.data(), message.size(), publicKey.data()) == 0;
}

ExchangeKeys GenerateExchangeKeys()
{
  ExchangeKeys keys{};
  crypto_kx_keypair(keys.publicKey.data(), keys.secretKey.data());
  return keys;
}

// crypto_kx gives one key per direction. Split each into a TCP key and a UDP key.
static SessionKeys Split(const Key &rx,
                         const Key &tx)
{
  constexpr char kContext[crypto_kdf_CONTEXTBYTES + 1] = "yapperkx";

  SessionKeys keys{};
  crypto_kdf_derive_from_key(keys.streamRx.data(), keys.streamRx.size(), 1, kContext, rx.data());
  crypto_kdf_derive_from_key(keys.packetRx.data(), keys.packetRx.size(), 2, kContext, rx.data());
  crypto_kdf_derive_from_key(keys.streamTx.data(), keys.streamTx.size(), 1, kContext, tx.data());
  crypto_kdf_derive_from_key(keys.packetTx.data(), keys.packetTx.size(), 2, kContext, tx.data());
  return keys;
}

std::optional<SessionKeys> ClientSessionKeys(const ExchangeKeys &mine,
                                             const Key          &serverKey)
{
  Key rx{};
  Key tx{};
  if (crypto_kx_client_session_keys(
          rx.data(), tx.data(), mine.publicKey.data(), mine.secretKey.data(), serverKey.data()) != 0)
  {
    return std::nullopt;
  }
  return Split(rx, tx);
}

std::optional<SessionKeys> ServerSessionKeys(const ExchangeKeys &mine,
                                             const Key          &clientKey)
{
  Key rx{};
  Key tx{};
  if (crypto_kx_server_session_keys(
          rx.data(), tx.data(), mine.publicKey.data(), mine.secretKey.data(), clientKey.data()) != 0)
  {
    return std::nullopt;
  }
  return Split(rx, tx);
}

StreamHeader StreamSender::Start(const Key &key)
{
  StreamHeader header{};
  crypto_secretstream_xchacha20poly1305_init_push(&state_, header.data(), key.data());
  return header;
}

std::vector<std::uint8_t> StreamSender::Seal(std::span<const std::uint8_t> plain)
{
  std::vector<std::uint8_t> sealed(plain.size() + crypto_secretstream_xchacha20poly1305_ABYTES);
  crypto_secretstream_xchacha20poly1305_push(
      &state_, sealed.data(), nullptr, plain.data(), plain.size(), nullptr, 0, 0);
  return sealed;
}

bool StreamReceiver::Start(const Key          &key,
                           const StreamHeader &header)
{
  return crypto_secretstream_xchacha20poly1305_init_pull(&state_, header.data(), key.data()) == 0;
}

std::optional<std::vector<std::uint8_t>> StreamReceiver::Open(std::span<const std::uint8_t> sealed)
{
  if (sealed.size() < crypto_secretstream_xchacha20poly1305_ABYTES)
    return std::nullopt;

  std::vector<std::uint8_t> plain(sealed.size() - crypto_secretstream_xchacha20poly1305_ABYTES);
  unsigned char             tag = 0;
  if (crypto_secretstream_xchacha20poly1305_pull(
          &state_, plain.data(), nullptr, &tag, sealed.data(), sealed.size(), nullptr, 0) != 0)
  {
    return std::nullopt;
  }
  return plain;
}

std::vector<std::uint8_t> SealPacket(const Key                    &key,
                                     std::span<const std::uint8_t> plain)
{
  constexpr std::size_t kNonceSize = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;

  std::vector<std::uint8_t> packet(kNonceSize + plain.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
  randombytes_buf(packet.data(), kNonceSize);
  crypto_aead_xchacha20poly1305_ietf_encrypt(
      packet.data() + kNonceSize, nullptr, plain.data(), plain.size(), nullptr, 0, nullptr, packet.data(), key.data());
  return packet;
}

std::optional<std::vector<std::uint8_t>> OpenPacket(const Key                    &key,
                                                    std::span<const std::uint8_t> packet)
{
  constexpr std::size_t kNonceSize = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
  constexpr std::size_t kOverhead = kNonceSize + crypto_aead_xchacha20poly1305_ietf_ABYTES;

  if (packet.size() < kOverhead)
    return std::nullopt;

  std::vector<std::uint8_t> plain(packet.size() - kOverhead);
  if (crypto_aead_xchacha20poly1305_ietf_decrypt(plain.data(),
                                                 nullptr,
                                                 nullptr,
                                                 packet.data() + kNonceSize,
                                                 packet.size() - kNonceSize,
                                                 nullptr,
                                                 0,
                                                 packet.data(),
                                                 key.data()) != 0)
  {
    return std::nullopt;
  }
  return plain;
}

std::string ToHex(std::span<const std::uint8_t> bytes)
{
  std::string hex(bytes.size() * 2 + 1, '\0');
  sodium_bin2hex(hex.data(), hex.size(), bytes.data(), bytes.size());
  hex.pop_back();
  return hex;
}
} // namespace yapper
