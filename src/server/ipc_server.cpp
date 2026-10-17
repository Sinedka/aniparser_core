#include "ipc_server.hpp"

#include <fcntl.h>
#include <nlohmann/json.hpp>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "server.hpp"

namespace {
using Json = nlohmann::json;

Json handle_json(shm::HeapHandle handle) {
  if (!handle) return nullptr;
  return {{"segment_id", handle.segment_id}, {"offset", handle.offset}};
}

std::optional<shm::HeapHandle> parse_handle(const Json &value) {
  if (!value.is_object() || !value.contains("segment_id") ||
      !value.contains("offset"))
    return std::nullopt;
  try {
    return shm::HeapHandle{value.at("segment_id").get<shm::SegmentId>(),
                           value.at("offset").get<shm::Offset>()};
  } catch (...) { return std::nullopt; }
}

template <typename T>
void read_optional(const Json &json, const char* key, std::optional<T> &out) {
  const auto it = json.find(key);
  if (it != json.end() && !it->is_null()) out = it->get<T>();
}

yani::SearchInput parse_search(const Json &json) {
  if (!json.is_object()) throw std::runtime_error("params must be an object");
  yani::SearchInput params{};
  read_optional(json, "studio_ids", params.studio_ids);
  read_optional(json, "q", params.q);
  read_optional(json, "director_ids", params.director_ids);
  read_optional(json, "worldart_ids", params.worldart_ids);
  read_optional(json, "mal_ids", params.mal_ids);
  read_optional(json, "kp_ids", params.kp_ids);
  read_optional(json, "shikimori_ids", params.shikimori_ids);
  read_optional(json, "min_age", params.min_age);
  read_optional(json, "max_rating_counters", params.max_rating_counters);
  read_optional(json, "min_rating_counters", params.min_rating_counters);
  read_optional(json, "ids", params.ids);
  read_optional(json, "season", params.season);
  read_optional(json, "status", params.status);
  read_optional(json, "types", params.types);
  read_optional(json, "exclude_genres", params.exclude_genres);
  read_optional(json, "genres", params.genres);
  read_optional(json, "max_rating", params.max_rating);
  read_optional(json, "min_rating", params.min_rating);
  read_optional(json, "ep_to", params.ep_to);
  read_optional(json, "ep_from", params.ep_from);
  read_optional(json, "to_year", params.to_year);
  read_optional(json, "sort_forward", params.sort_forward);
  read_optional(json, "sort", params.sort);
  read_optional(json, "offset", params.offset);
  read_optional(json, "limit", params.limit);
  return params;
}

struct Client {
  explicit Client(int socket) : fd(socket) {}
  ~Client() { close(); }

  bool send(const Json &message) {
    auto bytes = message.dump();
    bytes.push_back('\n');
    std::lock_guard lock(write_mutex);
    const int socket = fd.load();
    if (socket < 0) return false;
#if defined(MSG_NOSIGNAL)
    constexpr int send_flags = MSG_NOSIGNAL;
#else
    constexpr int send_flags = 0;
#endif
    std::size_t sent = 0;
    while (sent < bytes.size()) {
      const auto result =
          ::send(socket, bytes.data() + sent, bytes.size() - sent, send_flags);
      if (result <= 0) return false;
      sent += static_cast<std::size_t>(result);
    }
    return true;
  }

  void close() {
    std::lock_guard lock(write_mutex);
    const int socket = fd.exchange(-1);
    if (socket >= 0) {
      ::shutdown(socket, SHUT_RDWR);
      ::close(socket);
    }
  }

  void interrupt() {
    std::lock_guard lock(write_mutex);
    const int socket = fd.load();
    if (socket >= 0) ::shutdown(socket, SHUT_RDWR);
  }

  int descriptor() const { return fd.load(); }
  std::mutex write_mutex;
  std::atomic<int> fd{-1};
};

Json success(Json id) { return {{"id", std::move(id)}, {"ok", true}}; }
Json failure(Json id, std::string message) {
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
  const int connect_error = errno;
  ::close(probe);
  if (result == 0) return false;
  if (connect_error != ECONNREFUSED && connect_error != ENOENT) return false;
  return ::unlink(path.c_str()) == 0 || errno == ENOENT;
}
} // namespace

struct IpcServer::Impl {
  Server &server;
  std::string socket_path;
  std::string cache_name;
  std::string shared_memory_directory;
  std::atomic<bool> stopping{false};
  int listener = -1;
  std::thread accept_thread;
  std::mutex clients_mutex;
  std::vector<std::shared_ptr<Client>> clients;
  std::vector<std::thread> client_threads;

  Impl(Server &server_,
       std::string socket_path_,
       std::string cache_name_,
       std::string shared_memory_directory_)
    : server(server_)
    , socket_path(std::move(socket_path_))
    , cache_name(std::move(cache_name_))
    , shared_memory_directory(std::move(shared_memory_directory_)) {}

  bool start() {
    if (listener >= 0 || socket_path.empty() ||
        socket_path.size() >= sizeof(sockaddr_un{}.sun_path))
      return false;
    if (!remove_stale_socket(socket_path)) return false;
    listener = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listener < 0) return false;
#if defined(FD_CLOEXEC)
    const int listener_flags = ::fcntl(listener, F_GETFD);
    if (listener_flags >= 0)
      ::fcntl(listener, F_SETFD, listener_flags | FD_CLOEXEC);
#endif
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
    if (::bind(listener, reinterpret_cast<sockaddr*>(&address),
               sizeof(address)) != 0) {
      ::close(listener);
      listener = -1;
      return false;
    }
    if (::chmod(socket_path.c_str(), 0600) != 0 ||
        ::listen(listener, 32) != 0) {
      ::close(listener);
      listener = -1;
      ::unlink(socket_path.c_str());
      return false;
    }
    stopping.store(false);
    accept_thread = std::thread([this] { accept_loop(); });
    return true;
  }

  void stop() {
    if (listener < 0) return;
    stopping.store(true);
    ::shutdown(listener, SHUT_RDWR);
    ::close(listener);
    listener = -1;
    if (accept_thread.joinable()) accept_thread.join();
    {
      std::lock_guard lock(clients_mutex);
      for (const auto &client : clients) client->interrupt();
    }
    for (auto &thread : client_threads)
      if (thread.joinable()) thread.join();
    client_threads.clear();
    clients.clear();
    ::unlink(socket_path.c_str());
  }

  void accept_loop() {
    while (!stopping.load()) {
#if defined(__linux__)
      const int fd = ::accept4(listener, nullptr, nullptr, SOCK_CLOEXEC);
#else
      const int fd = ::accept(listener, nullptr, nullptr);
#endif
      if (fd < 0) {
        if (stopping.load() || errno == EINTR) continue;
        continue;
      }
#if defined(SO_NOSIGPIPE)
      const int no_sigpipe = 1;
      ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe,
                   sizeof(no_sigpipe));
#endif
#if !defined(__linux__) && defined(FD_CLOEXEC)
      const int client_flags = ::fcntl(fd, F_GETFD);
      if (client_flags >= 0) ::fcntl(fd, F_SETFD, client_flags | FD_CLOEXEC);
#endif
      auto client = std::make_shared<Client>(fd);
      std::lock_guard lock(clients_mutex);
      clients.push_back(client);
      client_threads.emplace_back([this, client] { client_loop(client); });
    }
  }

  void client_loop(const std::shared_ptr<Client> &client) {
    std::string pending;
    std::array<char, 4096> buffer{};
    while (!stopping.load()) {
      const auto count =
          ::recv(client->descriptor(), buffer.data(), buffer.size(), 0);
      if (count <= 0) break;
      pending.append(buffer.data(), static_cast<std::size_t>(count));
      if (pending.size() > 1024 * 1024) {
        client->send(failure(nullptr, "request exceeds 1 MiB"));
        break;
      }
      std::size_t newline = 0;
      while ((newline = pending.find('\n')) != std::string::npos) {
        auto line = pending.substr(0, newline);
        pending.erase(0, newline + 1);
        if (line.empty()) continue;
        process(client, line);
      }
    }
    client->close();
  }

  void process(const std::shared_ptr<Client> &client, const std::string &line) {
    Json request;
    Json id = nullptr;
    try {
      request = Json::parse(line);
      id = request.value("id", Json(nullptr));
      if (!request.is_object() || !request.contains("method"))
        throw std::runtime_error("request needs a method");
      const auto method = request.at("method").get<std::string>();
      if (method == "hello") {
        const std::string prefix = cache_name + ".v8.";
        auto reply = success(id);
        reply["protocol"] = 1;
        reply["layout"] = 8;
        if (!shared_memory_directory.empty())
          reply["shared_memory_directory"] = shared_memory_directory;
        reply["heaps"] = {
            {"anime",
             {{"name", prefix + "anime"},
              {"segments", Cache::anime_heap().segment_count()}}},
            {"variable",
             {{"name", prefix + "variable"},
              {"segments", Cache::variable_heap().segment_count()}}},
            {"search",
             {{"name", prefix + "search"},
              {"segments", Cache::search_heap().segment_count()}}},
            {"feed",
             {{"name", prefix + "feed"},
              {"segments", Cache::feed_heap().storage().segment_count()}}}};
        client->send(reply);
      } else if (method == "getAnime") {
        const auto anime_id = request.at("anime_id").get<std::int64_t>();
        const bool refreshing = server.animeNeedsRefresh(anime_id);
        const bool loaded = server.animeIsLoaded(anime_id);
        struct Gate {
          std::mutex mutex;
          std::condition_variable cv;
          bool sent = false;
        };
        auto gate = std::make_shared<Gate>();
        const auto handle = server.getAnime(
            anime_id, [client, gate, id](std::int64_t updated_id,
                                         shm::HeapHandle updated, bool ok) {
              std::unique_lock lock(gate->mutex);
              gate->cv.wait(lock, [&] { return gate->sent; });
              lock.unlock();
              auto event = Json{{"event", "anime.updated"},
                                {"id", id},
                                {"anime_id", updated_id},
                                {"handle", handle_json(updated)},
                                {"ok", ok}};
              client->send(event);
            });
        auto reply = success(id);
        reply["handle"] = handle_json(handle);
        reply["refreshing"] = refreshing;
        reply["loaded"] = loaded;
        client->send(reply);
        {
          std::lock_guard lock(gate->mutex);
          gate->sent = true;
        }
        gate->cv.notify_all();
      } else if (method == "getSearch") {
        const auto params =
            parse_search(request.value("params", Json::object()));
        const bool refreshing = server.searchPageNeedsRefresh(params);
        auto delivered = std::make_shared<std::atomic<bool>>(false);
        const bool accepted =
            server.getSearch(params, [client, id, delivered,
                                      refreshing](const yani::SearchInput &page,
                                                  shm::SearchHandle handle) {
              const bool update = delivered->exchange(true);
              auto reply = handle ? success(id) : failure(id, "search failed");
              if (update) reply["event"] = "search.updated";
              reply["handle"] = handle_json(handle);
              reply["offset"] = page.offset.value_or(0);
              reply["limit"] = 10;
              if (!update) reply["refreshing"] = refreshing;
              if (!client->send(reply) && handle)
                Cache::deallocate_search(handle);
              return false; // One page per IPC request; caller controls
                            // pagination.
            });
        if (!accepted) client->send(failure(id, "search could not be started"));
      } else if (method == "getFeed") {
        const auto watches =
            request.value("watches", std::vector<std::string>{});
        const bool refreshing = server.feedNeedsRefresh(watches);
        auto delivered = std::make_shared<std::atomic<bool>>(false);
        const bool accepted =
            server.getFeed(watches, [client, id, delivered, refreshing](
                                        const std::vector<std::string> &,
                                        shm::HeapHandle handle) {
              const bool update = delivered->exchange(true);
              auto reply =
                  handle ? success(id) : failure(id, "feed request failed");
              if (update) reply["event"] = "feed.updated";
              else reply["refreshing"] = refreshing;
              reply["handle"] = handle_json(handle);
              if (!client->send(reply) && handle)
                Cache::deallocate_feed(handle);
            });
        if (!accepted)
          client->send(failure(id, "feed request could not be started"));
      } else if (method == "deleteAnime" || method == "deleteSearch" ||
                 method == "deleteFeed") {
        const auto handle = parse_handle(request.value("handle", Json{}));
        if (!handle) {
          client->send(failure(id, "invalid handle"));
          return;
        }
        bool deleted = false;
        if (method == "deleteAnime") deleted = server.deleteAnime(*handle);
        else if (method == "deleteSearch")
          deleted = server.deleteSearch(*handle);
        else deleted = server.deleteFeed(*handle);
        auto reply = success(id);
        reply["deleted"] = deleted;
        client->send(reply);
      } else {
        client->send(failure(id, "unknown method"));
      }
    } catch (const std::exception &error) {
      client->send(failure(id, error.what()));
    } catch (...) { client->send(failure(id, "unknown server error")); }
  }
};

IpcServer::IpcServer(Server &server,
                     std::string socket_path,
                     std::string cache_name,
                     std::string shared_memory_directory)
  : impl_(std::make_unique<Impl>(server,
                                 std::move(socket_path),
                                 std::move(cache_name),
                                 std::move(shared_memory_directory))) {}
IpcServer::~IpcServer() { stop(); }
bool IpcServer::start() { return impl_->start(); }
void IpcServer::stop() {
  if (impl_) impl_->stop();
}
