#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <nlohmann/json.hpp>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "ipc_server.hpp"
#include "json.hpp"
#include "server.hpp"

namespace {
using Json = nlohmann::json;
constexpr HANDLE kInvalid = INVALID_HANDLE_VALUE;
std::mutex log_mutex;

void log_server(std::string_view kind, std::string_view message) {
  std::lock_guard lock(log_mutex);
  std::cerr << "[ipc " << kind << "] " << message << '\n';
}

void log_json(std::string_view kind, const Json &message) {
  log_server(kind, message.dump());
}

std::wstring widen(std::string_view value) {
  return std::filesystem::u8path(value).wstring();
}

template <typename T>
void read_optional(const Json &json, const char* key, std::optional<T> &out) {
  const auto it = json.find(key);
  if (it != json.end() && !it->is_null()) out = it->get<T>();
}

yani::SearchInput parse_search(const Json &json) {
  if (!json.is_object()) throw std::runtime_error("params must be an object");
  yani::SearchInput p{};
#define READ(name) read_optional(json, #name, p.name)
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

Json success(Json id) { return {{"id", std::move(id)}, {"ok", true}}; }
Json failure(Json id, std::string message) {
  return {{"id", std::move(id)}, {"ok", false}, {"error", std::move(message)}};
}

HANDLE create_pipe(const std::wstring &name) {
  return ::CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX,
                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                                PIPE_REJECT_REMOTE_CLIENTS,
                            PIPE_UNLIMITED_INSTANCES, 64 * 1024, 64 * 1024, 0,
                            nullptr);
}

struct Client {
  explicit Client(HANDLE value) : pipe(value) {}
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
      if (!::WriteFile(pipe, bytes.data() + offset, request, &written,
                       nullptr) ||
          written == 0)
        return false;
      offset += written;
    }
    return true;
  }

  void interrupt() {
    std::lock_guard lock(mutex);
    if (pipe != kInvalid) ::CancelIoEx(pipe, nullptr);
  }
  void close() {
    std::lock_guard lock(mutex);
    if (pipe != kInvalid) {
      ::CancelIoEx(pipe, nullptr);
      ::DisconnectNamedPipe(pipe);
      ::CloseHandle(pipe);
      pipe = kInvalid;
    }
  }
  HANDLE descriptor() const { return pipe; }

  mutable std::mutex mutex;
  HANDLE pipe = kInvalid;
};
} // namespace

struct IpcServer::Impl {
  Server &server;
  std::wstring pipe_name;
  std::atomic<bool> stopping{false};
  std::thread accept_thread;
  std::mutex clients_mutex;
  std::vector<std::shared_ptr<Client>> clients;
  std::vector<std::thread> workers;

  Impl(Server &owner, std::string name)
    : server(owner)
    , pipe_name(widen(name)) {}

  bool start() {
    if (pipe_name.empty() || accept_thread.joinable()) return false;
    stopping = false;
    accept_thread = std::thread([this] { accept_loop(); });
    return true;
  }

  void stop() {
    if (!accept_thread.joinable()) return;
    stopping = true;
    ::CancelSynchronousIo(accept_thread.native_handle());
    {
      std::lock_guard lock(clients_mutex);
      for (const auto &client : clients) client->interrupt();
    }
    accept_thread.join();
    for (auto &worker : workers)
      if (worker.joinable()) worker.join();
    workers.clear();
    clients.clear();
  }

  void accept_loop() {
    while (!stopping) {
      HANDLE pipe = create_pipe(pipe_name);
      if (pipe == kInvalid) return;
      const BOOL connected = ::ConnectNamedPipe(pipe, nullptr);
      if (!connected && ::GetLastError() != ERROR_PIPE_CONNECTED) {
        ::CloseHandle(pipe);
        if (stopping) return;
        continue;
      }
      if (stopping) {
        ::DisconnectNamedPipe(pipe);
        ::CloseHandle(pipe);
        return;
      }
      auto client = std::make_shared<Client>(pipe);
      std::lock_guard lock(clients_mutex);
      clients.push_back(client);
      workers.emplace_back([this, client] { client_loop(client); });
    }
  }

  void client_loop(const std::shared_ptr<Client> &client) {
    std::string pending;
    std::array<char, 8192> buffer{};
    while (!stopping) {
      DWORD count = 0;
      if (!::ReadFile(client->descriptor(), buffer.data(),
                      static_cast<DWORD>(buffer.size()), &count, nullptr) ||
          count == 0)
        break;
      pending.append(buffer.data(), count);
      if (pending.size() > 1024 * 1024) {
        client->send(failure(nullptr, "request exceeds 1 MiB"));
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
        auto reply = success(id);
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
          auto reply = success(id);
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
              auto reply = success(id);
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
              auto reply = success(id);
              if (updated) reply["event"] = "feed.updated";
              else reply["refreshing"] = owner->feedNeedsRefresh();
              reply["feed"] = std::move(feed);
              client->send(reply);
            });
        if (!scheduled)
          log_server("error", "getFeed: API task pool is stopping");
      } else if (method == "getVideo" || method == "getVideoOptions") {
        const auto iframe = request.at("iframe").get<std::string>();
        auto reply = success(id);
        if (method == "getVideo") {
          auto result = server.getVideo(
              iframe, request.value("quality", std::string{"720"}));
          if (!result) client->send(failure(id, result.error().message));
          else {
            reply["url"] = *result;
            client->send(reply);
          }
        } else {
          auto result = server.getVideoOptions(iframe);
          if (!result) client->send(failure(id, result.error().message));
          else {
            reply["qualities"] = Json::array();
            for (const auto &quality : *result)
              reply["qualities"].push_back(
                  {{"quality", quality.quality}, {"url", quality.url}});
            client->send(reply);
          }
        }
      } else {
        client->send(failure(id, "unknown method"));
      }
    } catch (const std::exception &exception) {
      log_server("error", "request id=" + id.dump() + ": " + exception.what());
      client->send(failure(id, exception.what()));
    } catch (...) {
      log_server("error", "request id=" + id.dump() + ": unknown server error");
      client->send(failure(id, "unknown server error"));
    }
  }
};

IpcServer::IpcServer(Server &server, std::string endpoint)
  : impl_(std::make_unique<Impl>(server, std::move(endpoint))) {}
IpcServer::~IpcServer() { stop(); }
bool IpcServer::start() { return impl_->start(); }
void IpcServer::stop() {
  if (impl_) impl_->stop();
}
