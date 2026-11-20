#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <thread>

#include "../tui/ipc_client.hpp"

using namespace std::chrono_literals;
namespace {
using aniparser::tui::IpcClient;
using Json = nlohmann::json;
// The peer socket is closed only by its worker. The listener is an owning
// scoped descriptor. Test files remain under out/ and never touch user data.
class Peer {
  public:
  template <typename Action>
  Peer(const std::filesystem::path &path, Action action) {
    listener_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (listener_ < 0) throw std::runtime_error("Cannot create test socket");
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const auto name = path.string();
    if (name.size() >= sizeof(address.sun_path))
      throw std::runtime_error("Test socket path too long");
    std::memcpy(address.sun_path, name.c_str(), name.size() + 1);
    if (::bind(listener_, reinterpret_cast<sockaddr*>(&address),
               sizeof(address)) != 0 ||
        ::listen(listener_, 1) != 0)
      throw std::runtime_error("Cannot listen on test socket");
    worker_ = std::thread([this, action] {
      const int peer = ::accept(listener_, nullptr, nullptr);
      if (peer < 0) return;
      char input[4096];
      (void)::recv(peer, input, sizeof(input), 0);
      try {
        action(peer);
      } catch (...) {}
      ::close(peer);
    });
  }
  ~Peer() {
    ::shutdown(listener_, SHUT_RDWR);
    if (worker_.joinable()) worker_.join();
    ::close(listener_);
  }

  private:
  int listener_ = -1;
  std::thread worker_;
};
void send_bytes(int fd, std::string_view bytes) {
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const auto count =
        ::send(fd, bytes.data() + offset, bytes.size() - offset, MSG_NOSIGNAL);
    if (count <= 0) return;
    offset += static_cast<std::size_t>(count);
  }
}
std::filesystem::path test_directory() {
  const auto root =
      std::filesystem::path("out/client-checks") /
      std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count());
  std::filesystem::create_directories(root);
  return root;
}

TEST(TuiClient, FragmentedReplies) {
  const auto path = test_directory() / "fragment.sock";
  Peer peer(path, [](int fd) {
    send_bytes(fd, "{\"id\":9,\"event\":\"fixture.updated\",\"ok\":true}\n");
    send_bytes(fd, "{\"id\":1,\"ok\":");
    std::this_thread::sleep_for(10ms);
    send_bytes(fd, "true}\n");
    std::this_thread::sleep_for(100ms);
  });
  IpcClient client(1s);
  ASSERT_TRUE(client.connect_to(path));
  const auto reply = client.request("hello");
  const auto events = client.poll_messages();
  EXPECT_EQ(reply["ok"], true);
  ASSERT_FALSE(events.empty());
  EXPECT_EQ(events.front()["id"], 9);
}
TEST(TuiClient, Deadline) {
  const auto path = test_directory() / "timeout.sock";
  Peer peer(path, [](int) { std::this_thread::sleep_for(200ms); });
  IpcClient client(50ms);
  ASSERT_TRUE(client.connect_to(path));
  bool timed_out = false;
  try {
    (void)client.request("hello");
  } catch (const std::exception &error) {
    timed_out =
        std::string(error.what()).find("timed out") != std::string::npos;
  }
  EXPECT_TRUE(timed_out);
}
TEST(TuiClient, CancelRequest) {
  const auto path = test_directory() / "cancel.sock";
  Peer peer(path, [](int) {});
  IpcClient client;
  ASSERT_TRUE(client.connect_to(path));
  client.on_wait = [] { return false; };
  bool cancelled = false;
  try {
    (void)client.request("hello");
  } catch (const std::exception &error) {
    cancelled =
        std::string(error.what()).find("cancelled") != std::string::npos;
  }
  EXPECT_TRUE(cancelled);
}
TEST(TuiClient, PeerEof) {
  const auto path = test_directory() / "eof.sock";
  Peer peer(path, [](int fd) { send_bytes(fd, "{\"id\":1,\"ok\":true}\n"); });
  IpcClient client;
  ASSERT_TRUE(client.connect_to(path));
  (void)client.request("hello");
  std::this_thread::sleep_for(20ms);
  bool eof = false;
  try {
    (void)client.poll_messages();
  } catch (const std::exception &) { eof = true; }
  EXPECT_TRUE(eof);
}
} // namespace
