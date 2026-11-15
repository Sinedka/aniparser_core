#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <nlohmann/json.hpp>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
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
using aniparser::ipc::log_json;
using aniparser::ipc::log_server;
const HANDLE kInvalid = INVALID_HANDLE_VALUE;
inline Json failure(Json id, std::string message) {
  return aniparser::ipc::error(std::move(id), std::move(message));
}
std::wstring widen(std::string_view value) {
  return std::filesystem::u8path(value).wstring();
}

HANDLE create_pipe(const std::wstring &name) {
  return ::CreateNamedPipeW(
      name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
          PIPE_REJECT_REMOTE_CLIENTS,
      PIPE_UNLIMITED_INSTANCES, 64 * 1024, 64 * 1024, 0, nullptr);
}

struct Event {
  HANDLE value = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
  Event() = default;
  Event(const Event &) = delete;
  Event &operator=(const Event &) = delete;
  ~Event() {
    if (value) ::CloseHandle(value);
  }
};

bool pipe_transfer(HANDLE pipe,
                   HANDLE cancelled,
                   void* buffer,
                   DWORD size,
                   DWORD &transferred,
                   bool writing) {
  if (::WaitForSingleObject(cancelled, 0) != WAIT_TIMEOUT) return false;
  Event event;
  if (!event.value) return false;
  OVERLAPPED operation{};
  operation.hEvent = event.value;
  const BOOL completed =
      writing ? ::WriteFile(pipe, buffer, size, &transferred, &operation)
              : ::ReadFile(pipe, buffer, size, &transferred, &operation);
  if (completed) return true;
  if (::GetLastError() != ERROR_IO_PENDING) return false;
  HANDLE waits[]{cancelled, event.value};
  const auto result =
      ::WaitForMultipleObjects(2, waits, FALSE, writing ? 5000 : INFINITE);
  if (result == WAIT_OBJECT_0 + 1)
    return ::GetOverlappedResult(pipe, &operation, &transferred, FALSE);
  ::CancelIoEx(pipe, &operation);
  ::GetOverlappedResult(pipe, &operation, &transferred, TRUE);
  return false;
}

struct Client {
  explicit Client(HANDLE value) : pipe(value) {
    if (!cancelled.value)
      throw std::runtime_error("Cannot create pipe cancellation event");
  }
  ~Client() { close(); }

  bool send(const Json &message) {
    const auto event = message.value("event", std::string{});
    if (!message.value("ok", true) ||
        (event == "anime.updated" && !message.value("ok", true)))
      log_json("error", message);
    const auto bytes = message.dump() + '\n';
    std::lock_guard lock(mutex);
    if (pipe == kInvalid) return false;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
      DWORD written = 0;
      const auto request = static_cast<DWORD>(std::min<std::size_t>(
          bytes.size() - offset, static_cast<std::size_t>(MAXDWORD)));
      if (!pipe_transfer(pipe, cancelled.value,
                         const_cast<char*>(bytes.data() + offset), request,
                         written, true) ||
          written == 0) {
        interrupt();
        return false;
      }
      offset += written;
    }
    return true;
  }

  void interrupt() {
    ::SetEvent(cancelled.value);
    std::lock_guard lock(descriptor_mutex);
    if (pipe != kInvalid) ::CancelIoEx(pipe, nullptr);
  }
  void close() {
    interrupt();
    std::lock_guard lock(mutex);
    std::lock_guard descriptor_lock(descriptor_mutex);
    if (pipe != kInvalid) {
      ::CancelIoEx(pipe, nullptr);
      ::DisconnectNamedPipe(pipe);
      ::CloseHandle(pipe);
      pipe = kInvalid;
    }
  }
  HANDLE descriptor() const {
    std::lock_guard lock(descriptor_mutex);
    return pipe;
  }

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

  mutable std::mutex mutex;
  mutable std::mutex descriptor_mutex;
  Event cancelled;
  std::atomic<bool> finished{false};
  HANDLE pipe = kInvalid;
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
  std::wstring pipe_name;
  std::atomic<bool> stopping{false};
  std::thread accept_thread;
  std::mutex clients_mutex;
  struct Connection {
    std::shared_ptr<Client> client;
    std::thread worker;
  };
  std::vector<Connection> connections;
  Event cancelled;
  static constexpr std::size_t kClientLimit = 128;

  Impl(Server &owner, std::string name)
    : server(owner)
    , pipe_name(widen(name)) {}

  bool start() {
    if (pipe_name.empty() || accept_thread.joinable() || !cancelled.value)
      return false;
    connections.reserve(kClientLimit);
    HANDLE first = create_pipe(pipe_name);
    if (first == kInvalid) return false;
    stopping = false;
    ::ResetEvent(cancelled.value);
    try {
      accept_thread = std::thread([this, first] { accept_loop(first); });
    } catch (...) {
      ::CloseHandle(first);
      return false;
    }
    return true;
  }

  void stop() {
    if (!accept_thread.joinable()) return;
    stopping = true;
    ::SetEvent(cancelled.value);
    accept_thread.join();
    {
      std::lock_guard lock(clients_mutex);
      for (const auto &connection : connections) connection.client->interrupt();
    }
    for (auto &connection : connections)
      if (connection.worker.joinable()) connection.worker.join();
    connections.clear();
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

  void accept_loop(HANDLE pipe) {
    while (!stopping) {
      Event event;
      if (!event.value) {
        ::CloseHandle(pipe);
        return;
      }
      OVERLAPPED operation{};
      operation.hEvent = event.value;
      bool connected = ::ConnectNamedPipe(pipe, &operation);
      if (!connected) {
        const auto error = ::GetLastError();
        connected = error == ERROR_PIPE_CONNECTED;
        if (error == ERROR_IO_PENDING) {
          HANDLE waits[]{cancelled.value, event.value};
          DWORD result;
          do {
            reap_connections();
            result = ::WaitForMultipleObjects(2, waits, FALSE, 200);
          } while (result == WAIT_TIMEOUT);
          DWORD transferred = 0;
          if (result == WAIT_OBJECT_0 + 1)
            connected =
                ::GetOverlappedResult(pipe, &operation, &transferred, FALSE);
          else {
            ::CancelIoEx(pipe, &operation);
            ::GetOverlappedResult(pipe, &operation, &transferred, TRUE);
          }
        }
      }
      if (stopping || !connected) {
        ::CloseHandle(pipe);
        return;
      }
      reap_connections();
      std::shared_ptr<Client> client;
      try {
        client = std::make_shared<Client>(pipe);
      } catch (...) { ::CloseHandle(pipe); }
      if (client) {
        std::lock_guard lock(clients_mutex);
        if (!stopping && connections.size() < kClientLimit) {
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
      client.reset();
      if (stopping) return;
      pipe = create_pipe(pipe_name);
      if (pipe == kInvalid) return;
    }
    ::CloseHandle(pipe);
  }

  void client_loop(const std::shared_ptr<Client> &client) {
    std::string pending;
    std::array<char, 8192> buffer{};
    try {
      while (!stopping) {
        DWORD count = 0;
        if (!pipe_transfer(client->descriptor(), client->cancelled.value,
                           buffer.data(), static_cast<DWORD>(buffer.size()),
                           count, false) ||
            count == 0)
          break;
        pending.append(buffer.data(), count);
        if (!aniparser::ipc::consume_lines(pending, [&](const auto &line) {
              if (!stopping) process(client, line);
            })) {
          client->send(failure(nullptr, "request exceeds 1 MiB"));
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
void IpcServer::stop() {
  if (impl_) impl_->stop();
}
