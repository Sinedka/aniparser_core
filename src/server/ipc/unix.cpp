#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
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
#include "server.hpp"

namespace {
using Json = nlohmann::json;
std::mutex log_mutex;

void log_server(std::string_view kind, std::string_view message) {
  std::lock_guard lock(log_mutex);
  std::cerr << "[ipc " << kind << "] " << message << '\n';
}

void log_json(std::string_view kind, const Json &message) {
  log_server(kind, message.dump());
}

template <typename T>
void read_optional(const Json &json, const char* key, std::optional<T> &out) {
  const auto it = json.find(key);
  if (it != json.end() && !it->is_null()) out = it->get<T>();
}

yani::SearchInput parse_search(const Json &json) {
  if (!json.is_object()) throw std::runtime_error("params must be an object");
  yani::SearchInput p{};
#define READ(field) read_optional(json, #field, p.field)
  READ(studio_ids);
  READ(q);
  READ(director_ids);
  READ(worldart_ids);
  READ(mal_ids);
  READ(kp_ids);
  READ(shikimori_ids);
  READ(min_age);
  READ(max_rating_counters);
  READ(min_rating_counters);
  READ(ids);
  READ(season);
  READ(status);
  READ(types);
  READ(exclude_genres);
  READ(genres);
  READ(max_rating);
  READ(min_rating);
  READ(ep_to);
  READ(ep_from);
  READ(to_year);
  READ(sort_forward);
  READ(sort);
  READ(offset);
  READ(limit);
#undef READ
  return p;
}

Json ok(Json id) { return {{"id", std::move(id)}, {"ok", true}}; }
Json error(Json id, std::string message) {
  return {{"id", std::move(id)}, {"ok", false}, {"error", std::move(message)}};
}

bool remove_stale_socket(const std::string &path) {
  struct stat info{};
  if (::lstat(path.c_str(), &info) != 0) return errno == ENOENT;
  if (!S_ISSOCK(info.st_mode)) return false;
  const int probe = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (probe < 0) return false;
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
  const int result =
      ::connect(probe, reinterpret_cast<sockaddr*>(&address), sizeof(address));
  const int failure = errno;
  ::close(probe);
  if (result == 0) return false;
  if (failure != ECONNREFUSED && failure != ENOENT) return false;
  return ::unlink(path.c_str()) == 0 || errno == ENOENT;
}

struct Client {
  explicit Client(int socket) : fd(socket) {}
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
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    std::size_t offset = 0;
    while (offset < line.size()) {
      const auto written =
          ::send(socket, line.data() + offset, line.size() - offset, flags);
      if (written <= 0) return false;
      offset += static_cast<std::size_t>(written);
    }
    return true;
  }

  void interrupt() {
    std::lock_guard lock(write_mutex);
    if (fd >= 0) ::shutdown(fd, SHUT_RDWR);
  }
  void close() {
    std::lock_guard lock(write_mutex);
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

  void begin_stream_subscription(std::string_view method, const Json &id) {
    std::lock_guard lock(stream_mutex);
    stream_subscriptions.insert(stream_key(method, id));
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
  std::thread accept_thread;
  std::mutex clients_mutex;
  std::vector<std::shared_ptr<Client>> clients;
  std::vector<std::thread> client_threads;

  Impl(Server &owner, std::string path)
    : server(owner)
    , socket_path(std::move(path)) {}

  bool start() {
    if (listener >= 0 || socket_path.empty() ||
        socket_path.size() >= sizeof(sockaddr_un{}.sun_path) ||
        !remove_stale_socket(socket_path))
      return false;
    listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener < 0) return false;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
    if (::bind(listener, reinterpret_cast<sockaddr*>(&address),
               sizeof(address)) != 0 ||
        ::chmod(socket_path.c_str(), 0600) != 0 ||
        ::listen(listener, 32) != 0) {
      ::close(listener);
      listener = -1;
      ::unlink(socket_path.c_str());
      return false;
    }
    stopping = false;
    accept_thread = std::thread([this] { accept_loop(); });
    return true;
  }

  void stop() {
    if (listener < 0) return;
    stopping = true;
    ::shutdown(listener, SHUT_RDWR);
    ::close(listener);
    listener = -1;
    if (accept_thread.joinable()) accept_thread.join();
    {
      std::lock_guard lock(clients_mutex);
      for (const auto &client : clients) client->interrupt();
    }
    for (auto &worker : client_threads)
      if (worker.joinable()) worker.join();
    client_threads.clear();
    clients.clear();
    ::unlink(socket_path.c_str());
  }

  void accept_loop() {
    while (!stopping) {
      const int socket = ::accept(listener, nullptr, nullptr);
      if (socket < 0) {
        if (stopping || errno == EINTR) continue;
        continue;
      }
      auto client = std::make_shared<Client>(socket);
      std::lock_guard lock(clients_mutex);
      clients.push_back(client);
      client_threads.emplace_back([this, client] { client_loop(client); });
    }
  }

  void client_loop(const std::shared_ptr<Client> &client) {
    std::string pending;
    std::array<char, 8192> buffer{};
    while (!stopping) {
      const auto count =
          ::recv(client->descriptor(), buffer.data(), buffer.size(), 0);
      if (count <= 0) break;
      pending.append(buffer.data(), static_cast<std::size_t>(count));
      if (pending.size() > 1024 * 1024) {
        client->send(error(nullptr, "request exceeds 1 MiB"));
        break;
      }
      std::size_t newline;
      while ((newline = pending.find('\n')) != std::string::npos) {
        auto line = pending.substr(0, newline);
        pending.erase(0, newline + 1);
        if (!line.empty()) process(client, line);
      }
    }
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
    Json id = nullptr;
    try {
      const auto request = Json::parse(line);
      id = request.value("id", Json(nullptr));
      const auto method = request.at("method").get<std::string>();
      log_json("command", request);
      if (method == "hello") {
        auto reply = ok(id);
        reply["protocol"] = 3;
        reply["transport"] = "json-lines";
        client->send(reply);
      } else if (method == "subscribeHistory") {
        const auto offset = request.value("offset", std::size_t{0});
        const auto limit = std::clamp(request.value("limit", std::size_t{20}),
                                      std::size_t{1}, Core::kHistoryPageLimit);
        client->clear_history_streams();
        for (const auto old_id : client->take_history_subscriptions())
          server.cancelHistorySubscription(old_id);
        client->begin_history_stream(id);
        std::weak_ptr<Client> weak_client = client;
        const auto subscription_id = server.subscribeHistory(
            [weak_client, id, offset, limit](Core::HistorySnapshot snapshot,
                                             bool updated) {
              const auto client = weak_client.lock();
              if (!client) return;
              auto reply = ok(id);
              if (updated) reply["event"] = "history.updated";
              reply["revision"] = snapshot.revision;
              reply["sequence"] = snapshot.sequence;
              reply["offset"] = offset;
              reply["limit"] = limit;
              reply["refreshing"] = snapshot.refreshing;
              reply["reason"] = snapshot.update_reason;
              reply["items"] = Server::historyToJson(snapshot.items);
              client->send_history_stream(id, reply);
            },
            offset, limit);
        if (subscription_id != 0)
          client->add_history_subscription(subscription_id);
      } else if (method == "unsubscribeHistory") {
        client->clear_history_streams();
        const auto subscriptions = client->take_history_subscriptions();
        for (const auto subscription_id : subscriptions)
          server.cancelHistorySubscription(subscription_id);
        auto reply = ok(id);
        reply["unsubscribed"] = !subscriptions.empty();
        client->send(reply);
      } else if (method == "subscribeFavorites") {
        const auto offset = request.value("offset", std::size_t{0});
        const auto limit = std::clamp(request.value("limit", std::size_t{20}),
                                      std::size_t{1}, Core::kHistoryPageLimit);
        client->clear_favorites_streams();
        for (const auto old_id : client->take_favorites_subscriptions())
          server.cancelFavoritesSubscription(old_id);
        client->begin_favorites_stream(id);
        std::weak_ptr<Client> weak_client = client;
        const auto subscription_id = server.subscribeFavorites(
            [weak_client, id, offset, limit](Core::HistorySnapshot snapshot,
                                             bool updated) {
              const auto client = weak_client.lock();
              if (!client) return;
              auto reply = ok(id);
              if (updated) reply["event"] = "favorites.updated";
              reply["revision"] = snapshot.revision;
              reply["sequence"] = snapshot.sequence;
              reply["offset"] = offset;
              reply["limit"] = limit;
              reply["refreshing"] = snapshot.refreshing;
              reply["reason"] = snapshot.update_reason;
              reply["items"] = Server::historyToJson(snapshot.items);
              client->send_favorites_stream(id, reply);
            },
            offset, limit);
        if (subscription_id != 0)
          client->add_favorites_subscription(subscription_id);
      } else if (method == "unsubscribeFavorites") {
        client->clear_favorites_streams();
        const auto subscriptions = client->take_favorites_subscriptions();
        for (const auto subscription_id : subscriptions)
          server.cancelFavoritesSubscription(subscription_id);
        auto reply = ok(id);
        reply["unsubscribed"] = !subscriptions.empty();
        client->send(reply);
      } else if (method == "addFavorite") {
        const auto anime_id = request.at("anime_id").get<std::int64_t>();
        if (!server.addFavorite(anime_id)) {
          client->send(error(id, "invalid favorite entry"));
        } else {
          auto reply = ok(id);
          reply["added"] = true;
          reply["anime_id"] = anime_id;
          reply["revision"] = server.getFavoritesRevision();
          client->send(reply);
        }
      } else if (method == "removeFavorite") {
        const auto anime_id = request.at("anime_id").get<std::int64_t>();
        auto reply = ok(id);
        reply["removed"] = server.removeFavorite(anime_id);
        reply["anime_id"] = anime_id;
        reply["revision"] = server.getFavoritesRevision();
        client->send(reply);
      } else if (method == "subscribePlanned") {
        const auto offset = request.value("offset", std::size_t{0});
        const auto limit = std::clamp(request.value("limit", std::size_t{20}),
                                      std::size_t{1}, Core::kHistoryPageLimit);
        client->clear_planned_streams();
        for (const auto old_id : client->take_planned_subscriptions())
          server.cancelPlannedSubscription(old_id);
        client->begin_planned_stream(id);
        std::weak_ptr<Client> weak_client = client;
        const auto subscription_id = server.subscribePlanned(
            [weak_client, id, offset, limit](Core::HistorySnapshot snapshot,
                                             bool updated) {
              const auto client = weak_client.lock();
              if (!client) return;
              auto reply = ok(id);
              if (updated) reply["event"] = "planned.updated";
              reply["revision"] = snapshot.revision;
              reply["sequence"] = snapshot.sequence;
              reply["offset"] = offset;
              reply["limit"] = limit;
              reply["refreshing"] = snapshot.refreshing;
              reply["reason"] = snapshot.update_reason;
              reply["items"] = Server::historyToJson(snapshot.items);
              client->send_planned_stream(id, reply);
            },
            offset, limit);
        if (subscription_id != 0)
          client->add_planned_subscription(subscription_id);
      } else if (method == "unsubscribePlanned") {
        client->clear_planned_streams();
        const auto subscriptions = client->take_planned_subscriptions();
        for (const auto subscription_id : subscriptions)
          server.cancelPlannedSubscription(subscription_id);
        auto reply = ok(id);
        reply["unsubscribed"] = !subscriptions.empty();
        client->send(reply);
      } else if (method == "addPlanned") {
        const auto anime_id = request.at("anime_id").get<std::int64_t>();
        if (!server.addPlanned(anime_id)) {
          client->send(error(id, "invalid anime state entry"));
        } else {
          auto reply = ok(id);
          reply["added"] = true;
          reply["anime_id"] = anime_id;
          reply["revision"] = server.getPlannedRevision();
          client->send(reply);
        }
      } else if (method == "removePlanned") {
        const auto anime_id = request.at("anime_id").get<std::int64_t>();
        auto reply = ok(id);
        reply["removed"] = server.removePlanned(anime_id);
        reply["anime_id"] = anime_id;
        reply["revision"] = server.getPlannedRevision();
        client->send(reply);
      } else if (method == "subscribeWatched") {
        const auto offset = request.value("offset", std::size_t{0});
        const auto limit = std::clamp(request.value("limit", std::size_t{20}),
                                      std::size_t{1}, Core::kHistoryPageLimit);
        client->clear_watched_streams();
        for (const auto old_id : client->take_watched_subscriptions())
          server.cancelWatchedSubscription(old_id);
        client->begin_watched_stream(id);
        std::weak_ptr<Client> weak_client = client;
        const auto subscription_id = server.subscribeWatched(
            [weak_client, id, offset, limit](Core::HistorySnapshot snapshot,
                                             bool updated) {
              const auto client = weak_client.lock();
              if (!client) return;
              auto reply = ok(id);
              if (updated) reply["event"] = "watched.updated";
              reply["revision"] = snapshot.revision;
              reply["sequence"] = snapshot.sequence;
              reply["offset"] = offset;
              reply["limit"] = limit;
              reply["refreshing"] = snapshot.refreshing;
              reply["reason"] = snapshot.update_reason;
              reply["items"] = Server::historyToJson(snapshot.items);
              client->send_watched_stream(id, reply);
            },
            offset, limit);
        if (subscription_id != 0)
          client->add_watched_subscription(subscription_id);
      } else if (method == "unsubscribeWatched") {
        client->clear_watched_streams();
        const auto subscriptions = client->take_watched_subscriptions();
        for (const auto subscription_id : subscriptions)
          server.cancelWatchedSubscription(subscription_id);
        auto reply = ok(id);
        reply["unsubscribed"] = !subscriptions.empty();
        client->send(reply);
      } else if (method == "addWatched") {
        const auto anime_id = request.at("anime_id").get<std::int64_t>();
        if (!server.addWatched(anime_id)) {
          client->send(error(id, "invalid anime state entry"));
        } else {
          auto reply = ok(id);
          reply["added"] = true;
          reply["anime_id"] = anime_id;
          reply["revision"] = server.getWatchedRevision();
          client->send(reply);
        }
      } else if (method == "removeWatched") {
        const auto anime_id = request.at("anime_id").get<std::int64_t>();
        auto reply = ok(id);
        reply["removed"] = server.removeWatched(anime_id);
        reply["anime_id"] = anime_id;
        reply["revision"] = server.getWatchedRevision();
        client->send(reply);
      } else if (method == "subscribeCompleted") {
        const auto offset = request.value("offset", std::size_t{0});
        const auto limit = std::clamp(request.value("limit", std::size_t{20}),
                                      std::size_t{1}, Core::kHistoryPageLimit);
        client->clear_completed_streams();
        for (const auto old_id : client->take_completed_subscriptions())
          server.cancelCompletedSubscription(old_id);
        client->begin_completed_stream(id);
        std::weak_ptr<Client> weak_client = client;
        const auto subscription_id = server.subscribeCompleted(
            [weak_client, id, offset, limit](Core::HistorySnapshot snapshot,
                                             bool updated) {
              const auto client = weak_client.lock();
              if (!client) return;
              auto reply = ok(id);
              if (updated) reply["event"] = "completed.updated";
              reply["revision"] = snapshot.revision;
              reply["sequence"] = snapshot.sequence;
              reply["offset"] = offset;
              reply["limit"] = limit;
              reply["refreshing"] = snapshot.refreshing;
              reply["reason"] = snapshot.update_reason;
              reply["items"] = Server::historyToJson(snapshot.items);
              client->send_completed_stream(id, reply);
            },
            offset, limit);
        if (subscription_id != 0)
          client->add_completed_subscription(subscription_id);
      } else if (method == "unsubscribeCompleted") {
        client->clear_completed_streams();
        const auto subscriptions = client->take_completed_subscriptions();
        for (const auto subscription_id : subscriptions)
          server.cancelCompletedSubscription(subscription_id);
        auto reply = ok(id);
        reply["unsubscribed"] = !subscriptions.empty();
        client->send(reply);
      } else if (method == "addCompleted") {
        const auto anime_id = request.at("anime_id").get<std::int64_t>();
        if (!server.addCompleted(anime_id)) {
          client->send(error(id, "invalid anime state entry"));
        } else {
          auto reply = ok(id);
          reply["added"] = true;
          reply["anime_id"] = anime_id;
          reply["revision"] = server.getCompletedRevision();
          client->send(reply);
        }
      } else if (method == "removeCompleted") {
        const auto anime_id = request.at("anime_id").get<std::int64_t>();
        auto reply = ok(id);
        reply["removed"] = server.removeCompleted(anime_id);
        reply["anime_id"] = anime_id;
        reply["revision"] = server.getCompletedRevision();
        client->send(reply);
      } else if (method == "subscribeDropped") {
        const auto offset = request.value("offset", std::size_t{0});
        const auto limit = std::clamp(request.value("limit", std::size_t{20}),
                                      std::size_t{1}, Core::kHistoryPageLimit);
        client->clear_dropped_streams();
        for (const auto old_id : client->take_dropped_subscriptions())
          server.cancelDroppedSubscription(old_id);
        client->begin_dropped_stream(id);
        std::weak_ptr<Client> weak_client = client;
        const auto subscription_id = server.subscribeDropped(
            [weak_client, id, offset, limit](Core::HistorySnapshot snapshot,
                                             bool updated) {
              const auto client = weak_client.lock();
              if (!client) return;
              auto reply = ok(id);
              if (updated) reply["event"] = "dropped.updated";
              reply["revision"] = snapshot.revision;
              reply["sequence"] = snapshot.sequence;
              reply["offset"] = offset;
              reply["limit"] = limit;
              reply["refreshing"] = snapshot.refreshing;
              reply["reason"] = snapshot.update_reason;
              reply["items"] = Server::historyToJson(snapshot.items);
              client->send_dropped_stream(id, reply);
            },
            offset, limit);
        if (subscription_id != 0)
          client->add_dropped_subscription(subscription_id);
      } else if (method == "unsubscribeDropped") {
        client->clear_dropped_streams();
        const auto subscriptions = client->take_dropped_subscriptions();
        for (const auto subscription_id : subscriptions)
          server.cancelDroppedSubscription(subscription_id);
        auto reply = ok(id);
        reply["unsubscribed"] = !subscriptions.empty();
        client->send(reply);
      } else if (method == "addDropped") {
        const auto anime_id = request.at("anime_id").get<std::int64_t>();
        if (!server.addDropped(anime_id)) {
          client->send(error(id, "invalid anime state entry"));
        } else {
          auto reply = ok(id);
          reply["added"] = true;
          reply["anime_id"] = anime_id;
          reply["revision"] = server.getDroppedRevision();
          client->send(reply);
        }
      } else if (method == "removeDropped") {
        const auto anime_id = request.at("anime_id").get<std::int64_t>();
        auto reply = ok(id);
        reply["removed"] = server.removeDropped(anime_id);
        reply["anime_id"] = anime_id;
        reply["revision"] = server.getDroppedRevision();
        client->send(reply);
      } else if (method == "isFavorite" || method == "isFavourite" ||
                 method == "isfavorite" || method == "isfavourite") {
        const auto anime_id = request.at("anime_id").get<std::int64_t>();
        auto reply = ok(id);
        reply["anime_id"] = anime_id;
        reply["value"] = server.isFavorite(anime_id);
        client->send(reply);
      } else if (method == "getStatus" || method == "getstatus") {
        const auto anime_id = request.at("anime_id").get<std::int64_t>();
        auto reply = ok(id);
        reply["anime_id"] = anime_id;
        reply["status"] = server.getStatus(anime_id);
        client->send(reply);
      } else if (method == "addHistory") {
        const auto anime_id = request.at("anime_id").get<std::int64_t>();
        const bool added = server.addHistory(anime_id);
        if (!added) {
          client->send(error(id, "invalid history entry"));
        } else {
          auto reply = ok(id);
          reply["added"] = true;
          reply["anime_id"] = anime_id;
          reply["revision"] = server.getHistoryRevision();
          client->send(reply);
        }
      } else if (method == "getAnimeSaveData") {
        const auto anime_id = request.at("anime_id").get<std::int64_t>();
        auto reply = ok(id);
        const auto save_data = server.getAnimeSaveData(anime_id);
        reply["save_data"] =
            save_data ? Server::animeSaveDataToJson(*save_data) : Json(nullptr);
        client->send(reply);
      } else if (method == "updateAnimeSaveData") {
        Core::AnimeSaveData save_data{
            .anime_id = request.at("anime_id").get<std::int64_t>(),
            .player = request.at("player").get<std::string>(),
            .dubber = request.at("dubber").get<std::string>(),
            .episode = request.at("episode").get<std::string>(),
            .time = request.at("time").get<std::int64_t>()};
        if (!server.updateAnimeSaveData(save_data)) {
          client->send(error(id, "invalid anime save data"));
        } else {
          auto reply = ok(id);
          reply["save_data"] = Server::animeSaveDataToJson(save_data);
          reply["revision"] = server.getHistoryRevision();
          client->send(reply);
        }
      } else if (method == "removeHistory") {
        const auto anime_id = request.at("anime_id").get<std::int64_t>();
        auto reply = ok(id);
        reply["removed"] = server.removeHistory(anime_id);
        reply["anime_id"] = anime_id;
        reply["revision"] = server.getHistoryRevision();
        client->send(reply);
      } else if (method == "unsubscribeAnime" ||
                 method == "unsubscribeSearch" || method == "unsubscribeFeed") {
        const auto target_id = request.at("target_id");
        auto reply = ok(id);
        reply["target_id"] = target_id;
        const std::string_view subscription_method =
            method == "unsubscribeAnime"    ? "subscribeAnime"
            : method == "unsubscribeSearch" ? "subscribeSearch"
                                            : "subscribeFeed";
        std::uint64_t subscription_id = 0;
        reply["unsubscribed"] = client->cancel_stream_subscription(
            subscription_method, target_id, &subscription_id);
        if (subscription_method == "subscribeAnime" && subscription_id != 0)
          server.unsubscribeAnime(subscription_id);
        client->send(reply);
      } else if (method == "subscribeAnime") {
        client->begin_stream_subscription("subscribeAnime", id);
        const auto anime_id = request.at("anime_id").get<std::int64_t>();
        std::weak_ptr<Client> weak_client = client;
        const auto subscription_id = server.subscribeAnime(
            anime_id,
            [weak_client, id, owner = &server](
                std::int64_t updated_id, std::optional<yani::AnimeResult> anime,
                bool updated, std::optional<yani::ApiError> error) {
              const auto client = weak_client.lock();
              if (!client) return;
              auto reply = ok(id);
              if (updated) {
                reply["event"] = "anime.updated";
                reply["anime_id"] = updated_id;
                reply["ok"] = anime.has_value();
                if (error) {
                  reply["error"] = error->message;
                  reply["status"] = error->status;
                }
              } else {
                reply["refreshing"] = owner->animeNeedsRefresh(updated_id);
                reply["loaded"] = anime.has_value();
              }
              reply["anime"] =
                  anime ? Server::animeToJson(*anime) : Json(nullptr);
              const bool refreshing = reply.value("refreshing", false);
              client->send_stream_subscription("subscribeAnime", id, reply,
                                               !updated && refreshing);
            });
        client->bind_stream_subscription("subscribeAnime", id, subscription_id);
      } else if (method == "subscribeSearch") {
        client->begin_stream_subscription("subscribeSearch", id);
        auto params = parse_search(request.value("params", Json::object()));
        std::weak_ptr<Client> weak_client = client;
        const bool scheduled = server.subscribeSearch(
            params, [weak_client, id, owner = &server](
                        yani::SearchInput page,
                        std::vector<yani::SearchResult> rows, bool updated) {
              const auto client = weak_client.lock();
              if (!client) return;
              auto reply = ok(id);
              if (updated) reply["event"] = "search.updated";
              else reply["refreshing"] = owner->searchPageNeedsRefresh(page);
              reply["offset"] = page.offset.value_or(0);
              reply["limit"] = page.limit.value_or(10);
              reply["results"] = std::move(rows);
              const bool refreshing = reply.value("refreshing", false);
              client->send_stream_subscription("subscribeSearch", id, reply,
                                               !updated && refreshing);
            });
        if (!scheduled) {
          client->cancel_stream_subscription("subscribeSearch", id);
          log_server("error", "subscribeSearch: API task pool is stopping");
        }
      } else if (method == "subscribeFeed") {
        client->begin_stream_subscription("subscribeFeed", id);
        const auto owner = &server;
        std::weak_ptr<Client> weak_client = client;
        const bool scheduled = server.subscribeFeed(
            [weak_client, id, owner](yani::FeedResponse feed, bool updated) {
              const auto client = weak_client.lock();
              if (!client) return;
              auto reply = ok(id);
              if (updated) reply["event"] = "feed.updated";
              else reply["refreshing"] = owner->feedNeedsRefresh();
              reply["feed"] = std::move(feed);
              const bool refreshing = reply.value("refreshing", false);
              client->send_stream_subscription("subscribeFeed", id, reply,
                                               !updated && refreshing);
            });
        if (!scheduled) {
          client->cancel_stream_subscription("subscribeFeed", id);
          log_server("error", "subscribeFeed: API task pool is stopping");
        }
      } else if (method == "getVideo") {
        const auto iframe = request.at("iframe").get<std::string>();
        const auto quality = request.value("quality", std::string{"720"});
        const auto result = server.getVideo(iframe, quality);
        if (!result) client->send(error(id, result.error().message));
        else {
          auto reply = ok(id);
          reply["url"] = *result;
          client->send(reply);
        }
      } else if (method == "getVideoOptions") {
        const auto iframe = request.at("iframe").get<std::string>();
        const auto result = server.getVideoOptions(iframe);
        if (!result) client->send(error(id, result.error().message));
        else {
          auto reply = ok(id);
          reply["qualities"] = Json::array();
          for (const auto &quality : *result)
            reply["qualities"].push_back(
                {{"quality", quality.quality}, {"url", quality.url}});
          client->send(reply);
        }
      } else {
        client->send(error(id, "unknown method"));
      }
    } catch (const std::exception &exception) {
      log_server("error", "request id=" + id.dump() + ": " + exception.what());
      client->cancel_stream_subscription("subscribeAnime", id);
      client->cancel_stream_subscription("subscribeSearch", id);
      client->cancel_stream_subscription("subscribeFeed", id);
      client->send(error(id, exception.what()));
    } catch (...) {
      log_server("error", "request id=" + id.dump() + ": unknown server error");
      client->cancel_stream_subscription("subscribeAnime", id);
      client->cancel_stream_subscription("subscribeSearch", id);
      client->cancel_stream_subscription("subscribeFeed", id);
      client->send(error(id, "unknown server error"));
    }
  }
};

IpcServer::IpcServer(Server &server, std::string endpoint)
  : impl_(std::make_unique<Impl>(server, std::move(endpoint))) {}
IpcServer::~IpcServer() { stop(); }
bool IpcServer::start() { return impl_->start(); }
void IpcServer::stop() { impl_->stop(); }
