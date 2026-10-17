#define NOMINMAX
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <nlohmann/json.hpp>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "ipc_server.hpp"
#include "server.hpp"

namespace {
using Json = nlohmann::json;
constexpr HANDLE invalid_handle = INVALID_HANDLE_VALUE;

std::wstring widen(std::string_view value) {
  const auto path = std::filesystem::u8path(value).wstring();
  return path;
}

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
  yani::SearchInput p{};
  read_optional(json, "studio_ids", p.studio_ids);
  read_optional(json, "q", p.q);
  read_optional(json, "director_ids", p.director_ids);
  read_optional(json, "worldart_ids", p.worldart_ids);
  read_optional(json, "mal_ids", p.mal_ids);
  read_optional(json, "kp_ids", p.kp_ids);
  read_optional(json, "shikimori_ids", p.shikimori_ids);
  read_optional(json, "min_age", p.min_age);
  read_optional(json, "max_rating_counters", p.max_rating_counters);
  read_optional(json, "min_rating_counters", p.min_rating_counters);
  read_optional(json, "ids", p.ids);
  read_optional(json, "season", p.season);
  read_optional(json, "status", p.status);
  read_optional(json, "types", p.types);
  read_optional(json, "exclude_genres", p.exclude_genres);
  read_optional(json, "genres", p.genres);
  read_optional(json, "max_rating", p.max_rating);
  read_optional(json, "min_rating", p.min_rating);
  read_optional(json, "ep_to", p.ep_to);
  read_optional(json, "ep_from", p.ep_from);
  read_optional(json, "to_year", p.to_year);
  read_optional(json, "sort_forward", p.sort_forward);
  read_optional(json, "sort", p.sort);
  read_optional(json, "offset", p.offset);
  read_optional(json, "limit", p.limit);
  return p;
}

Json success(Json id) { return {{"id", std::move(id)}, {"ok", true}}; }
Json failure(Json id, std::string message) {
  return {{"id", std::move(id)}, {"ok", false}, {"error", std::move(message)}};
}

HANDLE create_pipe(const std::wstring &name, bool first) {
  const DWORD open_mode =
      PIPE_ACCESS_DUPLEX | (first ? FILE_FLAG_FIRST_PIPE_INSTANCE : 0);
  return ::CreateNamedPipeW(name.c_str(), open_mode,
                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT |
                                PIPE_REJECT_REMOTE_CLIENTS,
                            PIPE_UNLIMITED_INSTANCES, 64 * 1024, 64 * 1024, 0,
                            nullptr);
}

struct Client {
  explicit Client(HANDLE handle) : pipe(handle) {}
  ~Client() { close(); }

  bool send(const Json &message) {
    auto bytes = message.dump();
    bytes.push_back('\n');
    std::lock_guard lock(write_mutex);
    if (pipe == invalid_handle) return false;
    std::size_t sent = 0;
    while (sent < bytes.size()) {
      const DWORD chunk = static_cast<DWORD>(
          std::min<std::size_t>(bytes.size() - sent, MAXDWORD));
      DWORD written = 0;
      if (!::WriteFile(pipe, bytes.data() + sent, chunk, &written, nullptr) ||
          !written)
        return false;
      sent += written;
    }
    return true;
  }

  void close() {
    std::lock_guard lock(write_mutex);
    if (pipe != invalid_handle) {
      ::DisconnectNamedPipe(pipe);
      ::CloseHandle(pipe);
      pipe = invalid_handle;
    }
  }

  HANDLE descriptor() const { return pipe; }
  std::mutex write_mutex;
  HANDLE pipe = invalid_handle;
};
} // namespace

struct IpcServer::Impl {
  Server &server;
  std::wstring pipe_name;
  std::string cache_name;
  std::string shared_memory_directory;
  std::atomic<bool> stopping{false};
  std::thread accept_thread;
  std::mutex clients_mutex;
  std::vector<std::shared_ptr<Client>> clients;
  std::vector<std::thread> client_threads;

  Impl(Server &s, std::string name, std::string cache, std::string directory)
    : server(s)
    , pipe_name(widen(name))
    , cache_name(std::move(cache))
    , shared_memory_directory(std::move(directory)) {}

  bool start() {
    if (pipe_name.empty() || accept_thread.joinable()) return false;
    HANDLE first = create_pipe(pipe_name, true);
    if (first == invalid_handle) return false;
    stopping.store(false);
    accept_thread = std::thread([this, first] { accept_loop(first); });
    return true;
  }

  void stop() {
    if (!accept_thread.joinable()) return;
    stopping.store(true);
    while (::WaitForSingleObject(accept_thread.native_handle(), 0) ==
           WAIT_TIMEOUT) {
      ::CancelSynchronousIo(accept_thread.native_handle());
      ::Sleep(10);
    }
    accept_thread.join();
    bool workers_running = true;
    while (workers_running) {
      workers_running = false;
      for (auto &thread : client_threads) {
        if (!thread.joinable()) continue;
        if (::WaitForSingleObject(thread.native_handle(), 0) == WAIT_TIMEOUT) {
          workers_running = true;
          ::CancelSynchronousIo(thread.native_handle());
        }
      }
      if (workers_running) ::Sleep(10);
    }
    for (auto &thread : client_threads)
      if (thread.joinable()) thread.join();
    client_threads.clear();
    clients.clear();
  }

  void accept_loop(HANDLE pipe) {
    while (!stopping.load()) {
      const BOOL connected = ::ConnectNamedPipe(pipe, nullptr);
      if (!connected && ::GetLastError() != ERROR_PIPE_CONNECTED) {
        ::CloseHandle(pipe);
        if (stopping.load()) break;
        pipe = create_pipe(pipe_name, false);
        if (pipe == invalid_handle) break;
        continue;
      }
      if (stopping.load()) {
        ::DisconnectNamedPipe(pipe);
        ::CloseHandle(pipe);
        break;
      }
      auto client = std::make_shared<Client>(pipe);
      {
        std::lock_guard lock(clients_mutex);
        clients.push_back(client);
        client_threads.emplace_back([this, client] { client_loop(client); });
      }
      pipe = create_pipe(pipe_name, false);
      if (pipe == invalid_handle) break;
    }
  }

  void client_loop(const std::shared_ptr<Client> &client) {
    std::string pending;
    std::array<char, 4096> buffer{};
    while (!stopping.load()) {
      DWORD read = 0;
      if (!::ReadFile(client->descriptor(), buffer.data(),
                      static_cast<DWORD>(buffer.size()), &read, nullptr) ||
          !read)
        break;
      pending.append(buffer.data(), read);
      if (pending.size() > 1024 * 1024) {
        client->send(failure(nullptr, "request exceeds 1 MiB"));
        break;
      }
      std::size_t newline = 0;
      while ((newline = pending.find('\n')) != std::string::npos) {
        auto line = pending.substr(0, newline);
        pending.erase(0, newline + 1);
        if (!line.empty()) process(client, line);
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
              client->send(Json{{"event", "anime.updated"},
                                {"id", id},
                                {"anime_id", updated_id},
                                {"handle", handle_json(updated)},
                                {"ok", ok}});
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
              return false;
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
        bool deleted = method == "deleteAnime"    ? server.deleteAnime(*handle)
                       : method == "deleteSearch" ? server.deleteSearch(*handle)
                                                  : server.deleteFeed(*handle);
        auto reply = success(id);
        reply["deleted"] = deleted;
        client->send(reply);
      } else client->send(failure(id, "unknown method"));
    } catch (const std::exception &error) {
      client->send(failure(id, error.what()));
    } catch (...) { client->send(failure(id, "unknown server error")); }
  }
};

IpcServer::IpcServer(Server &server,
                     std::string pipe_name,
                     std::string cache_name,
                     std::string shared_memory_directory)
  : impl_(std::make_unique<Impl>(server,
                                 std::move(pipe_name),
                                 std::move(cache_name),
                                 std::move(shared_memory_directory))) {}
IpcServer::~IpcServer() { stop(); }
bool IpcServer::start() { return impl_->start(); }
void IpcServer::stop() {
  if (impl_) impl_->stop();
}
