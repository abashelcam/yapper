// Starts a real server and real clients, each on its own thread, all in this one process.
// Just run yapper_tests and read the output.

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "app.hpp"
#include "connection.hpp"
#include "frame_assembler.hpp"
#include "voice_buffer.hpp"

// Different from kDefaultPort so the tests still work while a real server is running
constexpr std::uint16_t kTestPort = 5689;

static int failures = 0;

static void Check(bool        condition,
                  const char *what)
{
  std::cout << (condition ? "  PASS  " : "  FAIL  ") << what << "\n";

  if (!condition)
  {
    failures++;
  }
}

static std::string TempFile(const char *name)
{
  return (std::filesystem::temp_directory_path() / name).string();
}

static const std::string kKnownServers = TempFile("yapper_test_known_servers.txt");

// Polls for up to two seconds
template <typename Condition>
static bool WaitFor(Condition condition)
{
  for (int i = 0; i < 200; i++)
  {
    if (condition())
    {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return false;
}

static const yapper::User *FindUser(const yapper::ConnectionStatus &status,
                                    const std::string              &name)
{
  for (const yapper::User &user : status.users)
  {
    if (user.name == name)
    {
      return &user;
    }
  }
  return nullptr;
}

static bool HasUserIn(const yapper::ConnectionStatus &status,
                      const std::string              &name,
                      yapper::ChannelId               channel)
{
  const yapper::User *user = FindUser(status, name);
  return user && user->channel == channel;
}

static bool IsSharing(const yapper::ConnectionStatus &status,
                      const std::string              &name)
{
  const yapper::User *user = FindUser(status, name);
  return user && user->sharing;
}

// A fresh server for each test, so tests don't depend on each other. Stops when destroyed.
class TestServer
{
public:
  TestServer() : server_(Config())
  {
    thread_ = std::thread([this] { server_.Run(); });
  }

  ~TestServer()
  {
    server_.Stop();
    thread_.join();
  }

private:
  static yapper::ServerConfig Config()
  {
    yapper::ServerConfig config{};
    config.port = kTestPort;
    config.name = "Test Server";
    config.maxUsers = 3;
    config.password = "secret";
    config.channels = {"Lobby", "Gaming"};
    config.keyFile = TempFile("yapper_test_server.key");
    return config;
  }

  yapper::Server server_;
  std::thread    thread_;
};

struct ReceivedVoice
{
  yapper::SessionId         from;
  std::vector<std::uint8_t> opus;
};

// A connection running on its own thread, the same way the real app runs it. Destroying it disconnects.
class TestClient
{
public:
  explicit TestClient(const std::string &username,
                      const std::string &password = "secret",
                      const std::string &knownServers = kKnownServers)
      : connection_("127.0.0.1",
                    kTestPort,
                    username,
                    password,
                    knownServers)
  {
    connection_.SetVoiceHandler(
        [this](yapper::SessionId from, std::uint32_t, std::span<const std::uint8_t> opus)
        {
          std::lock_guard lock(mutex_);
          voice_.push_back(ReceivedVoice{from, std::vector<std::uint8_t>(opus.begin(), opus.end())});
        });
    connection_.SetVideoHandler(
        [this](yapper::SessionId, const yapper::VideoFrame &frame)
        {
          std::lock_guard lock(mutex_);
          video_.push_back(frame);
        });
    thread_ = std::thread([this] { connection_.Run(); });
  }

  ~TestClient()
  {
    connection_.Stop();
    thread_.join();
  }

  yapper::ConnectionStatus WaitForLogin()
  {
    WaitFor([&] { return Status().state != yapper::ConnectionState::Connecting; });
    return Status();
  }

  // Logged in and the server's UDP knows our address, so voice and video can reach us
  void WaitUntilReady()
  {
    WaitForLogin();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  yapper::ConnectionStatus Status() const
  {
    return connection_.GetStatus();
  }

  yapper::Connection &Net()
  {
    return connection_;
  }

  std::vector<ReceivedVoice> Voice()
  {
    std::lock_guard lock(mutex_);
    return voice_;
  }

  std::vector<yapper::VideoFrame> Video()
  {
    std::lock_guard lock(mutex_);
    return video_;
  }

private:
  std::mutex                      mutex_;
  std::vector<ReceivedVoice>      voice_;
  std::vector<yapper::VideoFrame> video_;
  yapper::Connection              connection_;
  std::thread                     thread_;
};

static void TestVoiceBuffer()
{
  std::cout << "jitter buffer\n";

  using Result = yapper::VoiceBuffer::Result;
  yapper::VoiceBuffer       buffer;
  std::vector<std::uint8_t> out;

  buffer.Push(1, {1});
  buffer.Push(2, {2});
  Check(buffer.Pop(out) == Result::Empty, "waits for enough packets before playing");

  buffer.Push(3, {3});
  Check(buffer.Pop(out) == Result::Packet && out[0] == 1, "plays the first packet");

  buffer.Push(5, {5});
  buffer.Pop(out);
  buffer.Pop(out);
  Check(buffer.Pop(out) == Result::Lost, "reports the missing packet 4");
  Check(buffer.Pop(out) == Result::Packet && out[0] == 5, "then plays packet 5");

  buffer.Push(4, {4});
  Check(buffer.Pop(out) == Result::Empty, "drops packet 4 when it shows up too late");
}

static void TestFrameAssembler()
{
  std::cout << "video frame assembler\n";

  yapper::FrameAssembler    assembler;
  std::vector<std::uint8_t> a{1};
  std::vector<std::uint8_t> b{2};

  Check(!assembler.Add(1, 1, 2, true, b), "waits for every fragment");

  std::optional<yapper::VideoFrame> frame = assembler.Add(1, 0, 2, true, a);
  Check(frame && frame->data == std::vector<std::uint8_t>{1, 2} && frame->keyframe,
        "joins fragments in order, even when they arrive out of order");

  assembler.Add(2, 0, 2, false, a);
  frame = assembler.Add(3, 0, 1, false, b);
  Check(frame && frame->id == 3, "a newer frame completes while an older one is missing a piece");
  Check(!assembler.Add(2, 1, 2, false, b), "the older frame was dropped, its last piece is too late");

  Check(!assembler.Add(4, 5, 2, false, a), "rejects a fragment index past the count");
}

static void TestConfigFile()
{
  std::cout << "server config file\n";

  const std::string path = TempFile("yapper_test_server.json");
  std::filesystem::remove(path);

  yapper::ServerConfig config = yapper::LoadServerConfig(path);
  Check(std::filesystem::exists(path), "missing config file gets created");
  Check(config.channels.size() == 3 && config.port == yapper::kDefaultPort, "with the default values");
}

static void TestLogin()
{
  std::cout << "login\n";
  TestServer server;

  TestClient               johnny("johnny");
  yapper::ConnectionStatus status = johnny.WaitForLogin();
  Check(status.state == yapper::ConnectionState::LoggedIn, "server accepted the login");
  Check(status.serverName == "Test Server", "server name is correct");
  Check(status.channels.size() == 2, "2 channels");
  Check(WaitFor([&] { return HasUserIn(johnny.Status(), "johnny", 0); }), "johnny sees himself in Lobby");

  TestClient sam("sam");
  sam.WaitForLogin();
  Check(WaitFor([&] { return johnny.Status().users.size() == 2; }), "johnny sees sam join");

  TestClient fakeJohnny("johnny");
  Check(fakeJohnny.WaitForLogin().state == yapper::ConnectionState::Refused, "a duplicate name is refused");

  TestClient wrongPassword("eve", "guess");
  Check(wrongPassword.WaitForLogin().state == yapper::ConnectionState::Refused, "a wrong password is refused");

  TestClient alex("alex");
  alex.WaitForLogin();
  TestClient extra("extra");
  status = extra.WaitForLogin();
  Check(status.state == yapper::ConnectionState::Refused && status.error == "The server is full.",
        "a fourth user is refused, the server is full");
}

static void TestLeaveAndRejoin()
{
  std::cout << "leave and rejoin\n";
  TestServer server;

  TestClient alex("alex");
  alex.WaitForLogin();
  auto johnny = std::make_unique<TestClient>("johnny");
  johnny->WaitForLogin();
  Check(WaitFor([&] { return alex.Status().users.size() == 2; }), "alex sees johnny");

  johnny.reset();
  Check(WaitFor([&] { return alex.Status().users.size() == 1; }), "alex sees johnny leave");

  TestClient johnnyAgain("johnny");
  Check(johnnyAgain.WaitForLogin().state == yapper::ConnectionState::LoggedIn, "the name is free again");
}

static void TestChannelsAndChat()
{
  std::cout << "channels and chat\n";
  TestServer server;

  TestClient johnny("johnny");
  TestClient alex("alex");
  TestClient sam("sam");
  johnny.WaitForLogin();
  alex.WaitForLogin();
  sam.WaitForLogin();

  sam.Net().SwitchChannel(1);
  Check(WaitFor([&] { return HasUserIn(johnny.Status(), "sam", 1); }), "johnny sees sam move to Gaming");

  johnny.Net().SendChatMessage("hello");
  Check(WaitFor([&] { return alex.Status().chat.size() == 1; }), "alex in Lobby gets johnny's message");
  Check(alex.Status().chat[0].from == "johnny" && alex.Status().chat[0].text == "hello", "from johnny, saying hello");
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  Check(sam.Status().chat.empty(), "sam in Gaming does not");
}

static void TestVoice()
{
  std::cout << "voice\n";
  TestServer server;

  TestClient johnny("johnny");
  TestClient alex("alex");
  TestClient sam("sam");
  sam.WaitForLogin();
  sam.Net().SwitchChannel(1);
  johnny.WaitUntilReady();
  alex.WaitUntilReady();
  WaitFor([&] { return HasUserIn(sam.Status(), "sam", 1); });

  johnny.Net().SendVoice({1, 2, 3});
  Check(WaitFor([&] { return alex.Voice().size() == 1; }), "alex in Lobby hears johnny");
  Check(alex.Voice()[0].from == johnny.Status().sessionId && alex.Voice()[0].opus == std::vector<std::uint8_t>{1, 2, 3},
        "from johnny, with the right bytes");
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  Check(sam.Voice().empty(), "sam in Gaming does not");
  Check(johnny.Voice().empty(), "johnny does not hear himself");
}

static void TestScreenShare()
{
  std::cout << "screen sharing\n";
  TestServer server;

  TestClient johnny("johnny");
  TestClient alex("alex");
  TestClient sam("sam");
  johnny.WaitUntilReady();
  alex.WaitUntilReady();
  sam.WaitUntilReady();

  johnny.Net().StartSharing();
  Check(WaitFor([&] { return IsSharing(alex.Status(), "johnny"); }), "alex sees johnny sharing");
  alex.Net().StartSharing();
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  Check(!IsSharing(johnny.Status(), "alex"), "a second sharer in the same channel is refused");

  alex.Net().Watch(johnny.Status().sessionId);
  Check(WaitFor([&] { return johnny.Net().TakeKeyframeRequest(); }), "watching asks johnny for a keyframe");

  std::vector<std::uint8_t> picture(5000);
  for (std::size_t i = 0; i < picture.size(); i++)
  {
    picture[i] = static_cast<std::uint8_t>(i % 251);
  }
  johnny.Net().SendVideoFrame(picture, true);
  Check(WaitFor([&] { return alex.Video().size() == 1; }), "alex gets the video frame");
  Check(alex.Video()[0].data == picture && alex.Video()[0].keyframe, "in one piece, from 5 fragments");
  Check(sam.Video().empty() && johnny.Video().empty(), "sam, who isn't watching, does not");

  johnny.Net().StopSharing();
  Check(WaitFor([&] { return !IsSharing(alex.Status(), "johnny"); }), "johnny stops sharing");
}

static void TestServerKey()
{
  std::cout << "server key\n";
  TestServer server;

  const std::string badKnownServers = TempFile("yapper_test_bad_known_servers.txt");
  std::ofstream(badKnownServers) << "127.0.0.1:" << kTestPort << " " << std::string(64, '0') << "\n";

  TestClient                     mallory("mallory", "secret", badKnownServers);
  const yapper::ConnectionStatus status = mallory.WaitForLogin();
  Check(status.state == yapper::ConnectionState::Refused && status.error.find("key changed") != std::string::npos,
        "a client that knows a different key for this server refuses it");
}

static void TestNoServer()
{
  std::cout << "no server\n";

  yapper::Connection lost("127.0.0.1", kTestPort + 1, "lost", "", kKnownServers);
  lost.Run(); // returns on its own once the connection fails
  Check(lost.GetStatus().state == yapper::ConnectionState::Disconnected, "client reports it couldn't connect");
}

int main()
{
  std::filesystem::remove(kKnownServers);

  TestVoiceBuffer();
  TestFrameAssembler();
  TestConfigFile();
  TestLogin();
  TestLeaveAndRejoin();
  TestChannelsAndChat();
  TestVoice();
  TestScreenShare();
  TestServerKey();
  TestNoServer();

  if (failures == 0)
  {
    std::cout << "\nall tests passed\n";
    return 0;
  }

  std::cout << "\n" << failures << " test(s) failed\n";
  return 1;
}
