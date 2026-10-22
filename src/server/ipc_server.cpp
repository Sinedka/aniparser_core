#include "ipc_server.hpp"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

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

  std::atomic<int> fd{-1};
  std::mutex write_mutex;
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
        reply["protocol"] = 2;
        reply["transport"] = "json-lines";
        client->send(reply);
      } else if (method == "getAnime") {
        const auto anime_id = request.at("anime_id").get<std::int64_t>();
        server.getAnime(anime_id, [client, id, owner = &server](
                                      std::int64_t updated_id,
                                      std::optional<yani::AnimeResult> anime,
                                      bool updated,
                                      std::optional<yani::ApiError> error) {
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
          reply["anime"] = anime ? Server::animeToJson(*anime) : Json(nullptr);
          client->send(reply);
        });
      } else if (method == "getSearch") {
        auto params = parse_search(request.value("params", Json::object()));
        const bool scheduled = server.getSearch(
            params, [client, id, owner = &server](
                        yani::SearchInput page,
                        std::vector<yani::SearchResult> rows, bool updated) {
              auto reply = ok(id);
              if (updated) reply["event"] = "search.updated";
              else reply["refreshing"] = owner->searchPageNeedsRefresh(page);
              reply["offset"] = page.offset.value_or(0);
              reply["limit"] = page.limit.value_or(10);
              reply["results"] = std::move(rows);
              client->send(reply);
            });
        if (!scheduled)
          log_server("error", "getSearch: API task pool is stopping");
      } else if (method == "getFeed") {
        const auto owner = &server;
        const bool scheduled = server.getFeed(
            [client, id, owner](yani::FeedResponse feed, bool updated) {
              auto reply = ok(id);
              if (updated) reply["event"] = "feed.updated";
              else reply["refreshing"] = owner->feedNeedsRefresh();
              reply["feed"] = std::move(feed);
              client->send(reply);
            });
        if (!scheduled)
          log_server("error", "getFeed: API task pool is stopping");
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
      client->send(error(id, exception.what()));
    } catch (...) {
      log_server("error", "request id=" + id.dump() + ": unknown server error");
      client->send(error(id, "unknown server error"));
    }
  }
};

IpcServer::IpcServer(Server &server, std::string endpoint)
  : impl_(std::make_unique<Impl>(server, std::move(endpoint))) {}
IpcServer::~IpcServer() { stop(); }
bool IpcServer::start() { return impl_->start(); }
void IpcServer::stop() { impl_->stop(); }
