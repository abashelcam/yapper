#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include "frame_assembler.hpp"
#include "secure_socket.hpp"

namespace yapper
{
enum class ConnectionState
{
  Connecting,
  LoggedIn,
  Refused,      // the server said no, see ConnectionStatus::error
  Disconnected, // couldn't connect, or the connection dropped
};

struct User
{
  SessionId                             id = 0;
  std::string                           name;
  ChannelId                             channel = 0;
  bool                                  sharing = false;
  std::chrono::steady_clock::time_point lastVoice{};
};

struct ChatLine
{
  std::string from;
  std::string text;
};

struct ConnectionStatus
{
  ConnectionState          state = ConnectionState::Connecting;
  SessionId                sessionId = 0;
  std::string              serverName;
  std::vector<ChannelInfo> channels;
  std::vector<User>        users;
  std::vector<ChatLine>    chat; // only the current channel, cleared when you switch
  std::string              error;
};

using VoiceHandler = std::function<void(SessionId from, std::uint32_t seq, std::span<const std::uint8_t> opus)>;
using VideoHandler = std::function<void(SessionId from, const VideoFrame &frame)>;

class Connection
{
public:
  // knownServersPath is the file where server keys are remembered, so a changed key gets caught
  Connection(std::string   host,
             std::uint16_t port,
             std::string   username,
             std::string   password,
             std::string   knownServersPath);

  // Call before Run(). The handler runs on the network thread.
  void SetVoiceHandler(VoiceHandler handler);
  void SetVideoHandler(VideoHandler handler);

  // Connects, logs in, then blocks until Stop() is called or the connection drops
  void Run();

  // Safe to call from any thread
  void Stop();

  // Safe to call from any thread, so the UI can read it every frame
  ConnectionStatus GetStatus() const;

  // Safe to call from any thread. Ignored until logged in.
  void SwitchChannel(ChannelId channel);
  void SendChatMessage(std::string text);
  void SendVoice(std::vector<std::uint8_t> opus);
  void StartSharing();
  void StopSharing();
  void Watch(SessionId sharer); // 0 stops watching
  void RequestKeyframe();       // ask the sharer you watch for a full frame
  void SendVideoFrame(std::vector<std::uint8_t> frame,
                      bool                      keyframe);

  // True once after the server says a viewer needs a keyframe. Safe to call from any thread.
  bool TakeKeyframeRequest();

private:
  asio::awaitable<void> Session();
  asio::awaitable<void> LoginAndListen();
  asio::awaitable<void> UdpReceiveLoop();
  asio::awaitable<void> PingLoop();

  template <typename T>
  void SendWhenLoggedIn(T message);

  void        HandleMessage(const Message &message);
  void        HandlePacket(std::span<const std::uint8_t> plain);
  void        SendPacket(PacketKind                    kind,
                         std::span<const std::uint8_t> data);
  std::string CheckServerKey(const Key &serverKey);

  asio::io_context context_;
  std::string      host_;
  std::uint16_t    port_;
  std::string      username_;
  std::string      password_;
  std::string      knownServersPath_;
  VoiceHandler     voiceHandler_;
  VideoHandler     videoHandler_;

  // Only touched on the network thread
  std::shared_ptr<SecureSocket> socket_;
  udp::socket                   udp_;
  SessionKeys                   keys_;
  SessionId                     sessionId_ = 0;
  std::array<std::uint32_t, kPacketKinds> lastSentSeq_{};
  std::uint32_t                           nextFrameId_ = 1;
  std::map<SessionId, FrameAssembler>     assemblers_;
  bool                                    loggedIn_ = false;

  std::atomic<bool> keyframeRequested_ = false;

  mutable std::mutex mutex_;
  ConnectionStatus   status_;
};
} // namespace yapper
