#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include <sodium.h>

#include "types.hpp"

// Thin wrappers over libsodium. Encryption is client <-> server: the server can read everything.

namespace yapper
{
using Signature = std::array<std::uint8_t, crypto_sign_BYTES>;
using StreamHeader = std::array<std::uint8_t, crypto_secretstream_xchacha20poly1305_HEADERBYTES>;

// Throws if libsodium can't start. Safe to call more than once.
void InitCrypto();

// The server's long term identity. Clients pin publicKey the first time they connect.
struct Identity
{
  Key                                                  publicKey{};
  std::array<std::uint8_t, crypto_sign_SECRETKEYBYTES> secretKey{};
};

Identity GenerateIdentity();

Signature Sign(std::span<const std::uint8_t> message,
               const Identity               &identity);

bool Verify(const Signature              &signature,
            std::span<const std::uint8_t> message,
            const Key                    &publicKey);

// A fresh key pair for every connection, so a leaked identity key can't decrypt old traffic
struct ExchangeKeys
{
  Key publicKey{};
  Key secretKey{};
};

ExchangeKeys GenerateExchangeKeys();

struct SessionKeys
{
  Key streamRx{};
  Key streamTx{};
  Key packetRx{};
  Key packetTx{};
};

// nullopt if the other side sent a bad key
std::optional<SessionKeys> ClientSessionKeys(const ExchangeKeys &mine,
                                             const Key          &serverKey);

std::optional<SessionKeys> ServerSessionKeys(const ExchangeKeys &mine,
                                             const Key          &clientKey);

// TCP: an ordered stream of messages. Detects dropped, reordered, or replayed messages.
class StreamSender
{
public:
  StreamHeader Start(const Key &key);

  std::vector<std::uint8_t> Seal(std::span<const std::uint8_t> plain);

private:
  crypto_secretstream_xchacha20poly1305_state state_{};
};

class StreamReceiver
{
public:
  bool Start(const Key          &key,
             const StreamHeader &header);

  std::optional<std::vector<std::uint8_t>> Open(std::span<const std::uint8_t> sealed);

private:
  crypto_secretstream_xchacha20poly1305_state state_{};
};

// UDP: each packet sealed on its own with a random nonce, since packets can be lost or reordered
std::vector<std::uint8_t> SealPacket(const Key                    &key,
                                     std::span<const std::uint8_t> plain);

std::optional<std::vector<std::uint8_t>> OpenPacket(const Key                    &key,
                                                    std::span<const std::uint8_t> packet);

std::string ToHex(std::span<const std::uint8_t> bytes);
} // namespace yapper
