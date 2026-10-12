#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#include "audio.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace yapper
{
constexpr int         kSampleRate = 48000;
constexpr std::size_t kFrameSamples = 960; // 20 ms, what Opus expects
constexpr int         kBitrate = 32000;
constexpr std::size_t kMaxOpusBytes = 400;

// Voice activation: send while the mic is louder than this, and keep sending a bit after it gets quiet
constexpr float kVoiceThreshold = 0.01f;
constexpr int   kGateHoldFrames = 15; // 300 ms

AudioDevices ListAudioDevices()
{
  AudioDevices devices{};

  ma_context context{};
  if (ma_context_init(nullptr, 0, nullptr, &context) != MA_SUCCESS)
  {
    return devices;
  }

  ma_device_info *playback = nullptr;
  ma_uint32       playbackCount = 0;
  ma_device_info *capture = nullptr;
  ma_uint32       captureCount = 0;
  if (ma_context_get_devices(&context, &playback, &playbackCount, &capture, &captureCount) == MA_SUCCESS)
  {
    for (ma_uint32 i = 0; i < captureCount; i++)
    {
      devices.microphones.emplace_back(capture[i].name);
    }
    for (ma_uint32 i = 0; i < playbackCount; i++)
    {
      devices.speakers.emplace_back(playback[i].name);
    }
  }

  ma_context_uninit(&context);
  return devices;
}

static const ma_device_id *FindDevice(ma_device_info    *devices,
                                      ma_uint32          count,
                                      const std::string &name)
{
  for (ma_uint32 i = 0; i < count; i++)
  {
    if (name == devices[i].name)
    {
      return &devices[i].id;
    }
  }
  return nullptr; // the default device
}

Audio::Audio(Connection        &connection,
             const std::string &microphone,
             const std::string &speakers)
    : connection_(connection)
{
  int error = 0;
  encoder_ = opus_encoder_create(kSampleRate, 1, OPUS_APPLICATION_VOIP, &error);
  if (error != OPUS_OK)
  {
    throw std::runtime_error("couldn't start the Opus encoder");
  }
  opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(kBitrate));

  if (ma_context_init(nullptr, 0, nullptr, &context_) != MA_SUCCESS)
  {
    opus_encoder_destroy(encoder_);
    throw std::runtime_error("couldn't start audio");
  }

  ma_device_info *playback = nullptr;
  ma_uint32       playbackCount = 0;
  ma_device_info *capture = nullptr;
  ma_uint32       captureCount = 0;
  ma_context_get_devices(&context_, &playback, &playbackCount, &capture, &captureCount);

  // One duplex device, so recording and playback share a single callback
  ma_device_config config = ma_device_config_init(ma_device_type_duplex);
  config.capture.pDeviceID = FindDevice(capture, captureCount, microphone);
  config.capture.format = ma_format_f32;
  config.capture.channels = 1;
  config.playback.pDeviceID = FindDevice(playback, playbackCount, speakers);
  config.playback.format = ma_format_f32;
  config.playback.channels = 1;
  config.sampleRate = kSampleRate;
  config.periodSizeInFrames = kFrameSamples;
  config.dataCallback = Callback;
  config.pUserData = this;

  if (ma_device_init(&context_, &config, &device_) != MA_SUCCESS)
  {
    ma_context_uninit(&context_);
    opus_encoder_destroy(encoder_);
    throw std::runtime_error("couldn't open the microphone or speakers");
  }

  if (ma_device_start(&device_) != MA_SUCCESS)
  {
    ma_device_uninit(&device_);
    ma_context_uninit(&context_);
    opus_encoder_destroy(encoder_);
    throw std::runtime_error("couldn't start the microphone or speakers");
  }
}

Audio::~Audio()
{
  // Waits for the callback to finish, so nothing below is in use anymore
  ma_device_uninit(&device_);
  ma_context_uninit(&context_);
  opus_encoder_destroy(encoder_);

  for (auto &[id, talker] : talkers_)
  {
    opus_decoder_destroy(talker.decoder);
  }
}

void Audio::Play(SessionId                     from,
                 std::uint32_t                 seq,
                 std::span<const std::uint8_t> opus)
{
  std::lock_guard lock(mutex_);

  // shortcut: talkers are never removed, fine for a friends server, clean up on UserLeft if it ever matters
  Talker &talker = talkers_[from];
  if (talker.decoder == nullptr)
  {
    int error = 0;
    talker.decoder = opus_decoder_create(kSampleRate, 1, &error);
    if (error != OPUS_OK)
    {
      talkers_.erase(from);
      return;
    }
  }

  talker.buffer.Push(seq, std::vector<std::uint8_t>(opus.begin(), opus.end()));
}

void Audio::SetMuted(bool muted)
{
  muted_ = muted;
}

void Audio::SetDeafened(bool deafened)
{
  deafened_ = deafened;
}

void Audio::Callback(ma_device  *device,
                     void       *output,
                     const void *input,
                     ma_uint32   frameCount)
{
  // Exceptions can't leave a miniaudio callback, so a failure here just means a moment of silence
  try
  {
    auto *audio = static_cast<Audio *>(device->pUserData);
    audio->Capture(static_cast<const float *>(input), frameCount);
    audio->Playback(static_cast<float *>(output), frameCount);
  }
  catch (...)
  {
  }
}

void Audio::Capture(const float *input,
                    ma_uint32    frameCount)
{
  if (input == nullptr)
  {
    return;
  }

  captured_.insert(captured_.end(), input, input + frameCount);

  while (captured_.size() >= kFrameSamples)
  {
    float sum = 0.0f;
    for (std::size_t i = 0; i < kFrameSamples; i++)
    {
      sum += captured_[i] * captured_[i];
    }
    const float loudness = std::sqrt(sum / kFrameSamples);

    if (loudness > kVoiceThreshold)
    {
      gateFrames_ = kGateHoldFrames;
    }
    else if (gateFrames_ > 0)
    {
      gateFrames_--;
    }

    if (gateFrames_ > 0 && !muted_ && !deafened_)
    {
      std::vector<std::uint8_t> packet(kMaxOpusBytes);
      const int                 size = opus_encode_float(encoder_,
                                         captured_.data(),
                                         static_cast<int>(kFrameSamples),
                                         packet.data(),
                                         static_cast<opus_int32>(packet.size()));
      if (size > 0)
      {
        packet.resize(size);
        connection_.SendVoice(std::move(packet));
      }
    }

    captured_.erase(captured_.begin(), captured_.begin() + kFrameSamples);
  }
}

void Audio::Playback(float    *output,
                     ma_uint32 frameCount)
{
  if (output == nullptr)
  {
    return;
  }

  while (mixed_.size() < frameCount)
  {
    MixFrame();
  }

  if (deafened_)
  {
    std::fill(output, output + frameCount, 0.0f);
  }
  else
  {
    std::copy(mixed_.begin(), mixed_.begin() + frameCount, output);
  }

  mixed_.erase(mixed_.begin(), mixed_.begin() + frameCount);
}

// Decodes 20 ms from everyone talking and adds it together
void Audio::MixFrame()
{
  std::array<float, kFrameSamples> mix{};
  std::array<float, kFrameSamples> decoded{};
  std::vector<std::uint8_t>        packet;

  {
    std::lock_guard lock(mutex_);
    for (auto &[id, talker] : talkers_)
    {
      int samples = 0;

      VoiceBuffer::Result result = talker.buffer.Pop(packet);
      if (result == VoiceBuffer::Result::Packet)
      {
        samples = opus_decode_float(talker.decoder,
                                    packet.data(),
                                    static_cast<opus_int32>(packet.size()),
                                    decoded.data(),
                                    static_cast<int>(kFrameSamples),
                                    0);
      }
      else if (result == VoiceBuffer::Result::Lost)
      {
        samples = opus_decode_float(talker.decoder, nullptr, 0, decoded.data(), static_cast<int>(kFrameSamples), 0);
      }

      for (int i = 0; i < samples; i++)
      {
        mix[i] += decoded[i];
      }
    }
  }

  for (float &sample : mix)
  {
    sample = std::clamp(sample, -1.0f, 1.0f);
  }

  mixed_.insert(mixed_.end(), mix.begin(), mix.end());
}
} // namespace yapper
