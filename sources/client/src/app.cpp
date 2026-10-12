#include "app.hpp"

#include <Windows.h>

#include <GLFW/glfw3.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "ui/window.hpp"

namespace yapper
{
// Shows "talking" for this long after the last voice packet
constexpr auto kTalkingTime = std::chrono::milliseconds(300);

const ImVec4 kTalkingColor(0.3f,
                           0.9f,
                           0.3f,
                           1.0f);
const ImVec4 kErrorColor(1.0f,
                         0.4f,
                         0.4f,
                         1.0f);

static const User *FindUser(const ConnectionStatus &status,
                            SessionId               id)
{
  for (const User &user : status.users)
  {
    if (user.id == id)
    {
      return &user;
    }
  }
  return nullptr;
}

Client::Client(bool          showGui,
               std::string   host,
               std::uint16_t port,
               std::string   username,
               std::string   password)
    : showGui_(showGui),
      host_(std::move(host)),
      port_(port),
      username_(std::move(username)),
      password_(std::move(password))
{
}

Client::~Client()
{
  Disconnect();
}

int Client::Run()
{
  return showGui_ ? RunGui() : RunHeadless();
}

void Client::Connect(const std::string &host,
                     std::uint16_t      port,
                     const std::string &username,
                     const std::string &password)
{
  Disconnect();

  const std::string knownServers = (DataFolder() / "known_servers.txt").string();
  connection_ = std::make_unique<Connection>(host, port, username, password, knownServers);

  connection_->SetVoiceHandler(
      [this](SessionId from, std::uint32_t seq, std::span<const std::uint8_t> opus)
      {
        std::lock_guard lock(audioMutex_);
        if (audio_)
        {
          audio_->Play(from, seq, opus);
        }
      });

  connection_->SetVideoHandler(
      [this](SessionId, const VideoFrame &frame)
      {
        std::lock_guard lock(viewerMutex_);
        if (viewer_)
        {
          viewer_->Push(frame);
        }
      });

  StartAudio();

  Connection *connection = connection_.get();
  networkThread_ = std::thread(
      [connection]
      {
        // Exceptions can't cross threads, and Connection already reports its errors through GetStatus()
        try
        {
          connection->Run();
        }
        catch (...)
        {
        }
      });
}

// Order matters: stop the network thread first so no more voice or video arrives,
// then everything that sends through the connection, then the connection itself
void Client::Disconnect()
{
  if (!connection_)
  {
    return;
  }

  connection_->Stop();
  networkThread_.join();

  {
    std::lock_guard lock(audioMutex_);
    audio_.reset();
  }

  screenShare_.reset();
  shareRequested_ = false;

  {
    std::lock_guard lock(viewerMutex_);
    viewer_.reset();
  }
  watching_ = 0;
  pictureWidth_ = 0;

  connection_.reset();
}

void Client::StartAudio()
{
  std::lock_guard lock(audioMutex_);

  audio_.reset();
  audioError_.clear();

  try
  {
    audio_ = std::make_unique<Audio>(*connection_, settings_.microphone, settings_.speakers);
    audio_->SetMuted(muted_);
    audio_->SetDeafened(deafened_);
  }
  catch (const std::exception &e)
  {
    audioError_ = e.what();
  }
}

int Client::RunHeadless()
{
  char exePath[MAX_PATH] = {};
  GetModuleFileNameA(nullptr, exePath, MAX_PATH);
  std::ofstream log(std::filesystem::path(exePath).parent_path() / "yapper_client.log");
  log << std::unitbuf;

  log << "connecting to " << host_ << ":" << port_ << " as " << username_ << "\n";
  Connect(host_, port_, username_, password_);

  if (!audioError_.empty())
  {
    log << "audio: " << audioError_ << "\n";
  }

  bool        loggedIn = false;
  std::size_t userCount = 0;
  std::size_t chatLogged = 0;

  while (true)
  {
    ConnectionStatus status = connection_->GetStatus();

    if (status.state == ConnectionState::LoggedIn && !loggedIn)
    {
      loggedIn = true;
      log << "logged in to " << status.serverName << "\n";
    }

    if (status.users.size() != userCount)
    {
      userCount = status.users.size();
      log << userCount << " users online\n";
    }

    // shortcut: misses lines once the chat passes its 200 line cap, fine for testing
    for (; chatLogged < status.chat.size(); chatLogged++)
    {
      log << status.chat[chatLogged].from << ": " << status.chat[chatLogged].text << "\n";
    }

    if (status.state == ConnectionState::Refused || status.state == ConnectionState::Disconnected)
    {
      log << "disconnected: " << status.error << "\n";
      return EXIT_FAILURE;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
}

int Client::RunGui()
{
  settings_ = LoadSettings();
  addressInput_ = settings_.address;
  usernameInput_ = settings_.username;

  UI ui;
  return ui.Run([this] { BuildFrame(); });
}

void Client::BuildFrame()
{
  ConnectionStatus status{};
  status.state = ConnectionState::Disconnected;
  if (connection_)
  {
    status = connection_->GetStatus();
  }

  // Keep the chat visible after a disconnect, but not a channel list that's no longer live
  if (status.state != ConnectionState::LoggedIn)
  {
    status.channels.clear();
    status.users.clear();
  }

  UpdateScreen(status);

  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);

  constexpr ImGuiWindowFlags rootFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                                         ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoSavedSettings;

  constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_Resizable | ImGuiTableFlags_NoPadOuterX;

  ImGui::Begin("##Yapper", nullptr, rootFlags);

  BuildControls(status);

  if (ImGui::BeginTable("##Panels", 2, tableFlags))
  {
    ImGui::TableSetupColumn("Channels", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Chat", ImGuiTableColumnFlags_WidthStretch, 1.5f);
    ImGui::TableNextRow();

    ImGui::TableNextColumn();
    BuildChannels(status);

    ImGui::TableNextColumn();
    BuildChat(status);

    ImGui::EndTable();
  }

  ImGui::End();

  BuildViewer(status);
}

void Client::BuildControls(const ConnectionStatus &status)
{
  const bool active = status.state == ConnectionState::Connecting || status.state == ConnectionState::LoggedIn;

  ImGui::BeginChild("##Controls",
                    ImVec2(0.0f, 0.0f),
                    ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12.0f);
  ImGui::InputTextWithHint("##Address", "host:port", &addressInput_);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
  ImGui::InputTextWithHint("##Username", "name", &usernameInput_);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
  ImGui::InputTextWithHint("##Password", "password", &passwordInput_, ImGuiInputTextFlags_Password);
  ImGui::SameLine();

  if (active)
  {
    if (ImGui::Button("Disconnect"))
    {
      Disconnect();
    }
  }
  else if (ImGui::Button("Connect"))
  {
    // "host" or "host:port". shortcut: IPv6 addresses aren't supported, add [::1]:port parsing if needed.
    std::string   host = addressInput_;
    std::uint16_t port = kDefaultPort;
    formError_.clear();

    const std::size_t colon = addressInput_.rfind(':');
    if (colon != std::string::npos)
    {
      host = addressInput_.substr(0, colon);
      const int parsed = std::atoi(addressInput_.c_str() + colon + 1);
      if (parsed < 1 || parsed > 65535)
      {
        formError_ = "The port must be a number from 1 to 65535.";
      }
      port = static_cast<std::uint16_t>(parsed);
    }

    if (host.empty())
    {
      formError_ = "Enter the server address.";
    }
    else if (usernameInput_.empty() || usernameInput_.size() > kMaxUsernameBytes)
    {
      formError_ = "Your name must be 1 to 32 characters.";
    }

    if (formError_.empty())
    {
      settings_.address = addressInput_;
      settings_.username = usernameInput_;
      SaveSettings(settings_);

      Connect(host, port, usernameInput_, passwordInput_);
    }
  }

  ImGui::SameLine();
  if (ImGui::Button("Settings"))
  {
    devices_ = ListAudioDevices();
    ImGui::OpenPopup("Settings");
  }
  BuildSettings();

  if (ImGui::Checkbox("Mute mic", &muted_))
  {
    std::lock_guard lock(audioMutex_);
    if (audio_)
    {
      audio_->SetMuted(muted_);
    }
  }
  ImGui::SameLine();
  if (ImGui::Checkbox("Deafen", &deafened_))
  {
    std::lock_guard lock(audioMutex_);
    if (audio_)
    {
      audio_->SetDeafened(deafened_);
    }
  }
  ImGui::SameLine();
  BuildShareButton(status);

  ImGui::SameLine(0.0f, ImGui::GetFontSize() * 2.0f);
  if (!formError_.empty())
  {
    ImGui::TextColored(kErrorColor, "%s", formError_.c_str());
  }
  else if (status.state == ConnectionState::Connecting)
  {
    ImGui::TextUnformatted("Connecting...");
  }
  else if (status.state == ConnectionState::LoggedIn)
  {
    ImGui::Text("Connected to %s", status.serverName.c_str());
  }
  else if (!status.error.empty())
  {
    ImGui::TextColored(kErrorColor, "%s", status.error.c_str());
  }
  else
  {
    ImGui::TextUnformatted("Not connected");
  }

  if (active && !audioError_.empty())
  {
    ImGui::TextColored(kErrorColor, "Audio: %s", audioError_.c_str());
  }
  if (active && !shareError_.empty())
  {
    ImGui::TextColored(kErrorColor, "Screen share: %s", shareError_.c_str());
  }

  ImGui::EndChild();
}

void Client::BuildChannels(const ConnectionStatus &status)
{
  ImGui::BeginChild("##Channels", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders);

  ChannelId myChannel = 0;
  for (const User &user : status.users)
  {
    if (user.id == status.sessionId)
    {
      myChannel = user.channel;
    }
  }

  const auto now = std::chrono::steady_clock::now();

  for (const ChannelInfo &channel : status.channels)
  {
    ImGui::PushID(static_cast<int>(channel.id));
    const std::string label = "# " + channel.name;
    if (ImGui::Selectable(label.c_str(), channel.id == myChannel) && channel.id != myChannel)
    {
      connection_->SwitchChannel(channel.id);
    }
    ImGui::PopID();

    ImGui::Indent();
    for (const User &user : status.users)
    {
      if (user.channel != channel.id)
      {
        continue;
      }

      if (now - user.lastVoice < kTalkingTime)
      {
        ImGui::TextColored(kTalkingColor, "%s", user.name.c_str());
      }
      else
      {
        ImGui::TextUnformatted(user.name.c_str());
      }

      if (user.sharing)
      {
        ImGui::SameLine();
        ImGui::TextDisabled("[sharing]");

        if (user.id != status.sessionId && user.channel == myChannel)
        {
          ImGui::SameLine();
          ImGui::PushID(static_cast<int>(user.id));
          if (ImGui::SmallButton(watching_ == user.id ? "Stop watching" : "Watch"))
          {
            if (watching_ == user.id)
            {
              StopWatching();
            }
            else
            {
              StartWatching(user.id);
            }
          }
          ImGui::PopID();
        }
      }
    }
    ImGui::Unindent();
  }

  ImGui::EndChild();
}

void Client::BuildChat(const ConnectionStatus &status)
{
  ImGui::BeginChild("##Chat", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_Borders);

  ImGui::SeparatorText("Chat");

  // Leave room for the message box at the bottom
  const float footer = ImGui::GetFrameHeightWithSpacing();
  ImGui::BeginChild("##Messages", ImVec2(0.0f, -footer));
  for (const ChatLine &line : status.chat)
  {
    ImGui::TextWrapped("%s: %s", line.from.c_str(), line.text.c_str());
  }
  if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
  {
    ImGui::SetScrollHereY(1.0f);
  }
  ImGui::EndChild();

  const bool loggedIn = status.state == ConnectionState::LoggedIn;
  ImGui::BeginDisabled(!loggedIn);

  ImGui::SetNextItemWidth(-ImGui::CalcTextSize("Send").x - ImGui::GetStyle().FramePadding.x * 2.0f -
                          ImGui::GetStyle().ItemSpacing.x);
  const bool enter = ImGui::InputText("##Message", &chatInput_, ImGuiInputTextFlags_EnterReturnsTrue);
  if (enter)
  {
    // Enter takes focus away from the box, put it back so you can keep typing
    ImGui::SetKeyboardFocusHere(-1);
  }
  ImGui::SameLine();
  const bool send = ImGui::Button("Send");

  if ((enter || send) && loggedIn && !chatInput_.empty())
  {
    connection_->SendChatMessage(chatInput_);
    chatInput_.clear();
  }

  ImGui::EndDisabled();
  ImGui::EndChild();
}

static bool DeviceCombo(const char                     *label,
                        const std::vector<std::string> &devices,
                        std::string                    &selected)
{
  bool changed = false;

  if (ImGui::BeginCombo(label, selected.empty() ? "Default" : selected.c_str()))
  {
    if (ImGui::Selectable("Default", selected.empty()))
    {
      selected.clear();
      changed = true;
    }
    for (const std::string &device : devices)
    {
      if (ImGui::Selectable(device.c_str(), device == selected))
      {
        selected = device;
        changed = true;
      }
    }
    ImGui::EndCombo();
  }

  return changed;
}

void Client::BuildSettings()
{
  if (!ImGui::BeginPopupModal("Settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
  {
    return;
  }

  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20.0f);
  bool changed = DeviceCombo("Microphone", devices_.microphones, settings_.microphone);
  ImGui::SetNextItemWidth(ImGui::GetFontSize() * 20.0f);
  changed |= DeviceCombo("Speakers", devices_.speakers, settings_.speakers);

  if (changed)
  {
    SaveSettings(settings_);
    if (connection_)
    {
      StartAudio();
    }
  }

  if (!audioError_.empty())
  {
    ImGui::TextColored(kErrorColor, "Audio: %s", audioError_.c_str());
  }

  if (ImGui::Button("Close"))
  {
    ImGui::CloseCurrentPopup();
  }

  ImGui::EndPopup();
}
void Client::BuildShareButton(const ConnectionStatus &status)
{
  const User *me = FindUser(status, status.sessionId);
  if (me && me->sharing)
  {
    if (ImGui::Button("Stop sharing"))
    {
      connection_->StopSharing();
      screenShare_.reset();
    }
    return;
  }

  bool someoneSharing = false;
  for (const User &user : status.users)
  {
    someoneSharing = someoneSharing || (me && user.channel == me->channel && user.sharing);
  }

  // One sharer per channel
  ImGui::BeginDisabled(!me || someoneSharing);
  if (ImGui::Button("Share screen"))
  {
    monitors_ = ListMonitors();
    ImGui::OpenPopup("##Monitors");
  }
  ImGui::EndDisabled();

  if (ImGui::BeginPopup("##Monitors"))
  {
    if (monitors_.empty())
    {
      ImGui::TextUnformatted("No monitors found.");
    }
    for (std::size_t i = 0; i < monitors_.size(); i++)
    {
      if (ImGui::Selectable(monitors_[i].c_str()))
      {
        shareMonitor_ = static_cast<int>(i);
        shareRequested_ = true;
        shareError_.clear();
        connection_->StartSharing();
      }
    }
    ImGui::EndPopup();
  }
}

// Keeps the local screen share and viewer in step with what the server says
void Client::UpdateScreen(const ConnectionStatus &status)
{
  const User *me = FindUser(status, status.sessionId);
  const bool  sharing = me && me->sharing;

  if (sharing && shareRequested_ && !screenShare_)
  {
    screenShare_ = std::make_unique<ScreenShare>(*connection_, shareMonitor_);
    shareRequested_ = false;
  }
  if (!sharing && screenShare_)
  {
    screenShare_.reset();
  }
  if (screenShare_ && !screenShare_->Error().empty())
  {
    shareError_ = screenShare_->Error();
    screenShare_.reset();
    connection_->StopSharing();
  }

  // Stop watching once they stop sharing, leave, or aren't in your channel anymore
  if (watching_ != 0)
  {
    const User *sharer = FindUser(status, watching_);
    if (!sharer || !sharer->sharing || !me || sharer->channel != me->channel)
    {
      StopWatching();
    }
  }
}

void Client::StartWatching(SessionId sharer)
{
  StopWatching();

  try
  {
    std::lock_guard lock(viewerMutex_);
    viewer_ = std::make_unique<ScreenViewer>(*connection_);
  }
  catch (const std::exception &e)
  {
    shareError_ = e.what();
    return;
  }

  watching_ = sharer;
  connection_->Watch(sharer);
}

void Client::StopWatching()
{
  if (watching_ == 0)
  {
    return;
  }

  connection_->Watch(0);
  {
    std::lock_guard lock(viewerMutex_);
    viewer_.reset();
  }
  watching_ = 0;
  pictureWidth_ = 0;
}

void Client::UploadPicture()
{
  if (texture_ == 0)
  {
    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  }

  glBindTexture(GL_TEXTURE_2D, texture_);
  glTexImage2D(
      GL_TEXTURE_2D, 0, GL_RGBA, pictureWidth_, pictureHeight_, 0, GL_RGBA, GL_UNSIGNED_BYTE, picture_.data());
}

void Client::BuildViewer(const ConnectionStatus &status)
{
  if (watching_ == 0)
  {
    return;
  }

  // No viewerMutex_ here: only this thread replaces viewer_, and holding it would stall the network thread
  if (viewer_ && viewer_->TakePicture(picture_, pictureWidth_, pictureHeight_))
  {
    UploadPicture();
  }

  const User       *sharer = FindUser(status, watching_);
  const std::string title = (sharer ? sharer->name : std::string("Someone")) + "'s screen###Viewer";

  ImGui::SetNextWindowSize(ImVec2(640.0f, 400.0f), ImGuiCond_FirstUseEver);
  bool open = true;
  if (ImGui::Begin(title.c_str(), &open))
  {
    if (pictureWidth_ == 0)
    {
      ImGui::TextUnformatted("Waiting for the picture...");
    }
    else
    {
      // Fit the window, keep the shape, center it
      const ImVec2 space = ImGui::GetContentRegionAvail();
      const float  scale = std::min(space.x / pictureWidth_, space.y / pictureHeight_);
      const ImVec2 size(pictureWidth_ * scale, pictureHeight_ * scale);
      const ImVec2 cursor = ImGui::GetCursorPos();
      ImGui::SetCursorPos(ImVec2(cursor.x + (space.x - size.x) / 2.0f, cursor.y + (space.y - size.y) / 2.0f));
      ImGui::Image(static_cast<ImTextureID>(texture_), size);
    }
  }
  ImGui::End();

  if (!open)
  {
    StopWatching();
  }
}
} // namespace yapper
