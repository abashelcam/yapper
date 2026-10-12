#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "audio/audio.hpp"
#include "connection.hpp"
#include "screen/screen_share.hpp"
#include "screen/screen_viewer.hpp"
#include "settings.hpp"

namespace yapper
{
class Client
{
public:
  // With showGui the rest is ignored and the user connects from the UI.
  // Without it, connects straight to host:port, logs to yapper_client.log next to the exe,
  // and runs until the connection drops. That mode is only for testing.
  explicit Client(bool          showGui,
                  std::string   host = "",
                  std::uint16_t port = kDefaultPort,
                  std::string   username = "",
                  std::string   password = "");
  ~Client();

  Client(const Client &) = delete;
  Client &operator=(const Client &) = delete;

  int Run();

private:
  int RunGui();
  int RunHeadless();

  void Connect(const std::string &host,
               std::uint16_t      port,
               const std::string &username,
               const std::string &password);
  void Disconnect();
  void StartAudio();

  void BuildFrame();
  void BuildControls(const ConnectionStatus &status);
  void BuildChannels(const ConnectionStatus &status);
  void BuildChat(const ConnectionStatus &status);
  void BuildSettings();
  void BuildShareButton(const ConnectionStatus &status);
  void BuildViewer(const ConnectionStatus &status);

  void UpdateScreen(const ConnectionStatus &status);
  void StartWatching(SessionId sharer);
  void StopWatching();
  void UploadPicture();

  bool          showGui_;
  std::string   host_;
  std::uint16_t port_;
  std::string   username_;
  std::string   password_;

  Settings settings_;

  std::unique_ptr<Connection> connection_;
  std::thread                 networkThread_;

  // The network thread hands voice to audio_, so swapping it needs the lock
  std::mutex             audioMutex_;
  std::unique_ptr<Audio> audio_;
  std::string            audioError_;

  std::unique_ptr<ScreenShare> screenShare_;
  int                          shareMonitor_ = 0;
  bool                         shareRequested_ = false; // waiting for the server to say yes
  std::string                  shareError_;
  std::vector<std::string>     monitors_;

  // The network thread hands video to viewer_, so swapping it needs the lock
  std::mutex                    viewerMutex_;
  std::unique_ptr<ScreenViewer> viewer_;
  SessionId                     watching_ = 0;
  unsigned int                  texture_ = 0;
  std::vector<std::uint8_t>     picture_;
  int                           pictureWidth_ = 0;
  int                           pictureHeight_ = 0;

  // UI state
  std::string  addressInput_;
  std::string  usernameInput_;
  std::string  passwordInput_;
  std::string  chatInput_;
  std::string  formError_;
  bool         muted_ = false;
  bool         deafened_ = false;
  AudioDevices devices_;
};
} // namespace yapper
