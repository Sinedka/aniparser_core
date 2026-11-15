#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "ipc_server.hpp"
#include "json.hpp"
#include "protocol.hpp"
#include "server.hpp"

namespace {
using Json = nlohmann::json;
using aniparser::ipc::error;
using aniparser::ipc::log_json;
using aniparser::ipc::log_server;

bool remove_stale_socket(const std::string &path) {
  struct stat info{};
  if (::lstat(path.c_str(), &info) != 0) return errno == ENOENT;
  if (!S_ISSOCK(info.st_mode)) return false;
  const int probe = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (probe < 0) return false;
  if (::fcntl(probe, F_SETFL, O_NONBLOCK) < 0) {
    ::close(probe);
    return false;
  }
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  const int result =
      ::connect(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address));
  const int failure = errno;
  ::close(probe);
  if (result == 0) return false;
  if (failure != ECONNREFUSED && failure != ENOENT) return false;
  struct stat current{};
  if (::lstat(path.c_str(), &current) != 0) return errno == ENOENT;
  if (!S_ISSOCK(current.st_mode) || current.st_dev != info.st_dev ||
      current.st_ino != info.st_ino)
    return false;
  return ::unlink(path.c_str()) == 0 || errno == ENOENT;
}

struct Client {
  explicit Client(int socket) : fd(socket) {
    timeval timeout{5, 0};
    ::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#if defined(SO_NOSIGPIPE)
    int enabled = 1;
    ::setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
  }
  ~Client() { close(); }

  bool send(const Json &message) {
    const auto event = message.value("event", std::string{});
    if (!message.value("ok", true) ||
        (event == "anime.updated" && !message.value("ok", true)))
      log_json("error", message);
    auto line = message.dump();
    line.push_back('\n');
    std::lock_guard lock(write_mutex);
    const auto socket = fd.load();
    if (socket < 0) return false;
#if defined(MSG_NOSIGNAL)
    constexpr int flags = MSG_NOSIGNAL | MSG_DONTWAIT;
#else
    constexpr int flags = MSG_DONTWAIT;
#endif
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    std::size_t offset = 0;
    while (offset < line.size()) {
      if (std::chrono::steady_clock::now() >= deadline) {
        ::shutdown(socket, SHUT_RDWR);
        return false;
      }
      const auto written =
          ::send(socket, line.data() + offset, line.size() - offset, flags);
      if (written < 0 && errno == EINTR) continue;
      if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        pollfd descriptor{socket, POLLOUT, 0};
        ::poll(&descriptor, 1, 50);
        continue;
      }
      if (written <= 0) {
        ::shutdown(socket, SHUT_RDWR);
        return false;
      }
      offset += static_cast<std::size_t>(written);
    }
    return true;
  }

  void interrupt() {
    std::lock_guard lock(descriptor_mutex);
    if (fd >= 0) ::shutdown(fd, SHUT_RDWR);
  }
  void close() {
    interrupt();
    std::lock_guard lock(write_mutex);
    std::lock_guard descriptor_lock(descriptor_mutex);
    const auto socket = fd.exchange(-1);
    if (socket >= 0) {
      ::shutdown(socket, SHUT_RDWR);
      ::close(socket);
    }
  }
  int descriptor() const { return fd.load(); }

  void add_history_subscription(std::uint64_t id) {
    std::lock_guard lock(history_mutex);
    history_subscriptions.push_back(id);
  }
  std::vector<std::uint64_t> take_history_subscriptions() {
    std::lock_guard lock(history_mutex);
    std::vector<std::uint64_t> result;
    result.swap(history_subscriptions);
    return result;
  }
  void begin_history_stream(const Json &id) {
    std::lock_guard lock(history_mutex);
    history_streams.insert(id.dump());
  }
  bool send_history_stream(const Json &id, const Json &message) {
    std::lock_guard lock(history_mutex);
    if (!history_streams.contains(id.dump())) return false;
    return send(message);
  }
  void clear_history_streams() {
    std::lock_guard lock(history_mutex);
    history_streams.clear();
  }

  void add_favorites_subscription(std::uint64_t id) {
    std::lock_guard lock(favorites_mutex);
    favorites_subscriptions.push_back(id);
  }
  std::vector<std::uint64_t> take_favorites_subscriptions() {
    std::lock_guard lock(favorites_mutex);
    std::vector<std::uint64_t> result;
    result.swap(favorites_subscriptions);
    return result;
  }
  void begin_favorites_stream(const Json &id) {
    std::lock_guard lock(favorites_mutex);
    favorites_streams.insert(id.dump());
  }
  bool send_favorites_stream(const Json &id, const Json &message) {
    std::lock_guard lock(favorites_mutex);
    if (!favorites_streams.contains(id.dump())) return false;
    return send(message);
  }
  void clear_favorites_streams() {
    std::lock_guard lock(favorites_mutex);
    favorites_streams.clear();
  }

  void add_planned_subscription(std::uint64_t id) {
    std::lock_guard lock(planned_mutex);
    planned_subscriptions.push_back(id);
  }
  std::vector<std::uint64_t> take_planned_subscriptions() {
    std::lock_guard lock(planned_mutex);
    std::vector<std::uint64_t> result;
    result.swap(planned_subscriptions);
    return result;
  }
  void begin_planned_stream(const Json &id) {
    std::lock_guard lock(planned_mutex);
    planned_streams.insert(id.dump());
  }
  bool send_planned_stream(const Json &id, const Json &message) {
    std::lock_guard lock(planned_mutex);
    if (!planned_streams.contains(id.dump())) return false;
    return send(message);
  }
  void clear_planned_streams() {
    std::lock_guard lock(planned_mutex);
    planned_streams.clear();
  }

  void add_watched_subscription(std::uint64_t id) {
    std::lock_guard lock(watched_mutex);
    watched_subscriptions.push_back(id);
  }
  std::vector<std::uint64_t> take_watched_subscriptions() {
    std::lock_guard lock(watched_mutex);
    std::vector<std::uint64_t> result;
    result.swap(watched_subscriptions);
    return result;
  }
  void begin_watched_stream(const Json &id) {
    std::lock_guard lock(watched_mutex);
    watched_streams.insert(id.dump());
  }
  bool send_watched_stream(const Json &id, const Json &message) {
    std::lock_guard lock(watched_mutex);
    if (!watched_streams.contains(id.dump())) return false;
    return send(message);
  }
  void clear_watched_streams() {
    std::lock_guard lock(watched_mutex);
    watched_streams.clear();
  }

  void add_completed_subscription(std::uint64_t id) {
    std::lock_guard lock(completed_mutex);
    completed_subscriptions.push_back(id);
  }
  std::vector<std::uint64_t> take_completed_subscriptions() {
    std::lock_guard lock(completed_mutex);
    std::vector<std::uint64_t> result;
    result.swap(completed_subscriptions);
    return result;
  }
  void begin_completed_stream(const Json &id) {
    std::lock_guard lock(completed_mutex);
    completed_streams.insert(id.dump());
  }
  bool send_completed_stream(const Json &id, const Json &message) {
    std::lock_guard lock(completed_mutex);
    if (!completed_streams.contains(id.dump())) return false;
    return send(message);
  }
  void clear_completed_streams() {
    std::lock_guard lock(completed_mutex);
    completed_streams.clear();
  }

  void add_dropped_subscription(std::uint64_t id) {
    std::lock_guard lock(dropped_mutex);
    dropped_subscriptions.push_back(id);
  }
  std::vector<std::uint64_t> take_dropped_subscriptions() {
    std::lock_guard lock(dropped_mutex);
    std::vector<std::uint64_t> result;
    result.swap(dropped_subscriptions);
    return result;
  }
  void begin_dropped_stream(const Json &id) {
    std::lock_guard lock(dropped_mutex);
    dropped_streams.insert(id.dump());
  }
  bool send_dropped_stream(const Json &id, const Json &message) {
    std::lock_guard lock(dropped_mutex);
    if (!dropped_streams.contains(id.dump())) return false;
    return send(message);
  }
  void clear_dropped_streams() {
    std::lock_guard lock(dropped_mutex);
    dropped_streams.clear();
  }

  bool begin_stream_subscription(std::string_view method, const Json &id) {
    std::lock_guard lock(stream_mutex);
    if (stream_subscriptions.size() >= 256) return false;
    return stream_subscriptions.insert(stream_key(method, id)).second;
  }
  bool send_stream_subscription(std::string_view method,
                                const Json &id,
                                const Json &message,
                                bool keep_active) {
    std::lock_guard lock(stream_mutex);
    const auto key = stream_key(method, id);
    if (!stream_subscriptions.contains(key)) return false;
    const bool sent = send(message);
    if (!keep_active) {
      stream_subscriptions.erase(key);
      stream_subscription_tokens.erase(key);
    } else if (!sent) {
      stream_subscriptions.erase(key);
    }
    return sent;
  }
  bool bind_stream_subscription(std::string_view method,
                                const Json &id,
                                std::uint64_t subscription_id) {
    if (subscription_id == 0) return false;
    std::lock_guard lock(stream_mutex);
    const auto key = stream_key(method, id);
    if (!stream_subscriptions.contains(key)) return false;
    stream_subscription_tokens.insert_or_assign(key, subscription_id);
    return true;
  }
  bool cancel_stream_subscription(std::string_view method,
                                  const Json &id,
                                  std::uint64_t* subscription_id = nullptr) {
    std::lock_guard lock(stream_mutex);
    const auto key = stream_key(method, id);
    const bool cancelled = stream_subscriptions.erase(key) != 0;
    if (const auto token = stream_subscription_tokens.find(key);
        token != stream_subscription_tokens.end()) {
      if (subscription_id) *subscription_id = token->second;
      stream_subscription_tokens.erase(token);
    }
    return cancelled;
  }
  std::vector<std::uint64_t> take_stream_subscription_tokens() {
    std::lock_guard lock(stream_mutex);
    std::vector<std::uint64_t> result;
    result.reserve(stream_subscription_tokens.size());
    for (const auto &[key, token] : stream_subscription_tokens)
      result.push_back(token);
    stream_subscription_tokens.clear();
    stream_subscriptions.clear();
    return result;
  }

  std::atomic<int> fd{-1};
  std::mutex write_mutex;
  std::mutex descriptor_mutex;
  std::atomic<bool> finished{false};
  std::mutex history_mutex;
  std::vector<std::uint64_t> history_subscriptions;
  std::unordered_set<std::string> history_streams;
  std::mutex favorites_mutex;
  std::vector<std::uint64_t> favorites_subscriptions;
  std::unordered_set<std::string> favorites_streams;
  std::mutex planned_mutex;
  std::vector<std::uint64_t> planned_subscriptions;
  std::unordered_set<std::string> planned_streams;
  std::mutex watched_mutex;
  std::vector<std::uint64_t> watched_subscriptions;
  std::unordered_set<std::string> watched_streams;
  std::mutex completed_mutex;
  std::vector<std::uint64_t> completed_subscriptions;
  std::unordered_set<std::string> completed_streams;
  std::mutex dropped_mutex;
  std::vector<std::uint64_t> dropped_subscriptions;
  std::unordered_set<std::string> dropped_streams;
  std::mutex stream_mutex;
  std::unordered_set<std::string> stream_subscriptions;
  std::unordered_map<std::string, std::uint64_t> stream_subscription_tokens;

  static std::string stream_key(std::string_view method, const Json &id) {
    return std::string(method) + ':' + id.dump();
  }
};

} // namespace

struct IpcServer::Impl {
  Server &server;
  std::string socket_path;
  std::atomic<bool> stopping{false};
  int listener = -1;
  std::optional<std::pair<dev_t, ino_t>> socket_identity;
  std::thread accept_thread;
  std::mutex clients_mutex;
  struct Connection {
    std::shared_ptr<Client> client;
    std::thread worker;
  };
  std::vector<Connection> connections;
  static constexpr std::size_t kClientLimit = 128;

  Impl(Server &owner, std::string path)
    : server(owner)
    , socket_path(std::move(path)) {}

  void unlink_owned_socket() {
    struct stat current{};
    if (socket_identity && ::lstat(socket_path.c_str(), &current) == 0 &&
        S_ISSOCK(current.st_mode) && current.st_dev == socket_identity->first &&
        current.st_ino == socket_identity->second)
      ::unlink(socket_path.c_str());
    socket_identity.reset();
  }

  bool start() {
    if (listener >= 0 || socket_path.empty() ||
        socket_path.size() >= sizeof(sockaddr_un{}.sun_path) ||
        !remove_stale_socket(socket_path))
      return false;
    connections.reserve(kClientLimit);
    listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener >= 0) ::fcntl(listener, F_SETFD, FD_CLOEXEC);
    if (listener < 0) return false;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
    if (::bind(listener, reinterpret_cast<sockaddr*>(&address),
               sizeof(address)) != 0) {
      ::close(listener);
      listener = -1;
      return false; // A racing listener's path does not belong to this server.
    }
    struct stat owned{};
    if (::lstat(socket_path.c_str(), &owned) != 0 || !S_ISSOCK(owned.st_mode)) {
      ::close(listener);
      listener = -1;
      return false;
    }
    socket_identity = std::pair{owned.st_dev, owned.st_ino};
    if (::chmod(socket_path.c_str(), 0600) != 0 ||
        ::listen(listener, 32) != 0) {
      ::close(listener);
      listener = -1;
      unlink_owned_socket();
      return false;
    }
    if (::fcntl(listener, F_SETFL, O_NONBLOCK) < 0) {
      ::close(listener);
      listener = -1;
      unlink_owned_socket();
      return false;
    }
    stopping = false;
    try {
      accept_thread = std::thread([this] { accept_loop(); });
    } catch (...) {
      ::close(listener);
      listener = -1;
      unlink_owned_socket();
      return false;
    }
    return true;
  }

  void stop() {
    if (listener < 0) return;
    stopping = true;
    ::shutdown(listener, SHUT_RDWR);
    if (accept_thread.joinable()) accept_thread.join();
    ::close(listener);
    listener = -1;
    {
      std::lock_guard lock(clients_mutex);
      for (const auto &connection : connections) connection.client->interrupt();
    }
    for (auto &connection : connections)
      if (connection.worker.joinable()) connection.worker.join();
    connections.clear();
    unlink_owned_socket();
  }

  void reap_connections() {
    std::vector<Connection> retired;
    {
      std::lock_guard lock(clients_mutex);
      for (auto it = connections.begin(); it != connections.end();) {
        if (it->client->finished.load()) {
          retired.push_back(std::move(*it));
          it = connections.erase(it);
        } else ++it;
      }
    }
    for (auto &connection : retired)
      if (connection.worker.joinable()) connection.worker.join();
  }

  void accept_loop() {
    while (!stopping) {
      reap_connections();
      pollfd descriptor{listener, POLLIN, 0};
      const auto ready = ::poll(&descriptor, 1, 200);
      if (ready <= 0 || stopping) continue;
      const int socket = ::accept(listener, nullptr, nullptr);
      if (socket < 0) continue;
      ::fcntl(socket, F_SETFD, FD_CLOEXEC);
      std::shared_ptr<Client> client;
      try {
        client = std::make_shared<Client>(socket);
      } catch (...) {
        ::close(socket);
        continue;
      }
      std::lock_guard lock(clients_mutex);
      if (stopping || connections.size() >= kClientLimit) continue;
      connections.push_back({client, {}});
      try {
        connections.back().worker = std::thread([this, client] {
          try {
            client_loop(client);
          } catch (...) { client->close(); }
          client->finished = true;
        });
      } catch (...) { connections.pop_back(); }
    }
  }

  void client_loop(const std::shared_ptr<Client> &client) {
    std::string pending;
    std::array<char, 8192> buffer{};
    try {
      while (!stopping) {
        const auto count =
            ::recv(client->descriptor(), buffer.data(), buffer.size(), 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        pending.append(buffer.data(), static_cast<std::size_t>(count));
        if (!aniparser::ipc::consume_lines(pending, [&](const auto &line) {
              if (!stopping) process(client, line);
            })) {
          client->send(error(nullptr, "request exceeds 1 MiB"));
          break;
        }
      }
    } catch (...) {}
    client->close();
    client->clear_history_streams();
    for (const auto id : client->take_history_subscriptions())
      server.cancelHistorySubscription(id);
    client->clear_favorites_streams();
    for (const auto id : client->take_favorites_subscriptions())
      server.cancelFavoritesSubscription(id);
    client->clear_planned_streams();
    for (const auto id : client->take_planned_subscriptions())
      server.cancelPlannedSubscription(id);
    client->clear_watched_streams();
    for (const auto id : client->take_watched_subscriptions())
      server.cancelWatchedSubscription(id);
    client->clear_completed_streams();
    for (const auto id : client->take_completed_subscriptions())
      server.cancelCompletedSubscription(id);
    client->clear_dropped_streams();
    for (const auto id : client->take_dropped_subscriptions())
      server.cancelDroppedSubscription(id);
    for (const auto id : client->take_stream_subscription_tokens())
      server.unsubscribeAnime(id);
  }

  void process(const std::shared_ptr<Client> &client, const std::string &line) {
    aniparser::ipc::process(server, client, line);
  }
};

IpcServer::IpcServer(Server &server, std::string endpoint)
  : impl_(std::make_unique<Impl>(server, std::move(endpoint))) {}
IpcServer::~IpcServer() { stop(); }
bool IpcServer::start() { return impl_->start(); }
void IpcServer::stop() { impl_->stop(); }
