#pragma once

#include <atomic>
#include <map>
#include <mutex>
#include <span>
#include <string>
#include <vector>

// connection.hpp first: asio needs winsock2.h included before miniaudio pulls in windows.h
#include "connection.hpp"
#include "voice_buffer.hpp"

#include <miniaudio.h>
#include <opus.h>

namespace yapper
{
struct AudioDevices
{
  std::vector<std::string> microphones;
  std::vector<std::string> speakers;
};

AudioDevices ListAudioDevices();

// Records the mic and sends voice through the connection, and plays everyone else's voice.
// Throws if the devices can't be opened.
class Audio
{
public:
  // Empty names use the Windows default device
  Audio(Connection        &connection,
        const std::string &microphone,
        const std::string &speakers);
  ~Audio();

  Audio(const Audio &) = delete;
  Audio &operator=(const Audio &) = delete;

  // Safe to call from any thread
  void Play(SessionId                     from,
            std::uint32_t                 seq,
            std::span<const std::uint8_t> opus);

  void SetMuted(bool muted);
  void SetDeafened(bool deafened);

private:
  static void Callback(ma_device  *device,
                       void       *output,
                       const void *input,
                       ma_uint32   frameCount);

  void Capture(const float *input,
               ma_uint32    frameCount);
  void Playback(float    *output,
                ma_uint32 frameCount);
  void MixFrame();

  struct Talker
  {
    VoiceBuffer  buffer;
    OpusDecoder *decoder = nullptr;
  };

  Connection  &connection_;
  ma_context   context_{};
  ma_device    device_{};
  OpusEncoder *encoder_ = nullptr;

  std::atomic<bool> muted_ = false;
  std::atomic<bool> deafened_ = false;

  // Only touched by the audio callback
  std::vector<float> captured_;
  std::vector<float> mixed_;
  int                gateFrames_ = 0;

  std::mutex                  mutex_;
  std::map<SessionId, Talker> talkers_;
};
} // namespace yapper
