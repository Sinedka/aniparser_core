#pragma once
#include <algorithm>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "json.hpp"
#include "request_validation.hpp"
#include "server.hpp"

namespace aniparser::ipc {
using Json = nlohmann::json;
class OperationError : public std::runtime_error {
  public:
  explicit OperationError(yani::ApiError failure)
    : std::runtime_error(failure.message)
    , failure(std::move(failure)) {}
  yani::ApiError failure;
};
inline bool mutation_value(yani::Result<bool> result) {
  if (!result) throw OperationError(result.error());
  return *result;
}

inline std::mutex log_mutex;

inline void log_server(std::string_view kind, std::string_view message) {
  std::lock_guard lock(log_mutex);
  std::cerr << "[ipc " << kind << "] " << message << '\n';
}

inline void log_json(std::string_view kind, const Json &message) {
  log_server(kind, message.dump());
}

template <typename T>
void read_optional(const Json &json, const char* key, std::optional<T> &out) {
  const auto it = json.find(key);
  if (it != json.end() && !it->is_null()) out = it->get<T>();
}

inline yani::SearchInput parse_search(const Json &json) {
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

inline Json ok(Json id) { return {{"id", std::move(id)}, {"ok", true}}; }
inline Json error(Json id, std::string message) {
  return {{"id", std::move(id)}, {"ok", false}, {"error", std::move(message)}};
}

// Bound each JSON line, rather than rejecting a valid large line merely
// because the same recv also contained the beginning of the next command.
template <typename Call> bool consume_lines(std::string &pending, Call &&call) {
  constexpr std::size_t limit = 1024 * 1024;
  std::size_t consumed = 0;
  for (;;) {
    const auto newline = pending.find('\n', consumed);
    if (newline == std::string::npos) break;
    if (newline - consumed > limit) return false;
    if (newline != consumed) call(pending.substr(consumed, newline - consumed));
    consumed = newline + 1;
  }
  pending.erase(0, consumed);
  return pending.size() <= limit;
}

template <typename Client>
void process(Server &server,
             const std::shared_ptr<Client> &client,
             const std::string &line) {
  Json id = nullptr;
  try {
    const auto request = Json::parse(line);
    id = request.value("id", Json(nullptr));
    aniparser::ipc::validate_request(request);
    const auto method = request.at("method").get<std::string>();
    log_json("command", request);
    if (method == "hello") {
      auto reply = ok(id);
      reply["protocol"] = 3;
      reply["transport"] = "json-lines";
      client->send(reply);
    } else if (method == "subscribeHistory") {
      const auto offset = request.value("offset", std::size_t{0});
      const auto limit = static_cast<std::size_t>(std::min<std::int64_t>(
          request.value("limit", std::int64_t{20}), Core::kHistoryPageLimit));
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
            if (snapshot.error) {
              reply["ok"] = false;
              reply["error"] = snapshot.error->message;
              reply["status"] = snapshot.error->status;
            }
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
      const auto limit = static_cast<std::size_t>(std::min<std::int64_t>(
          request.value("limit", std::int64_t{20}), Core::kHistoryPageLimit));
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
            if (snapshot.error) {
              reply["ok"] = false;
              reply["error"] = snapshot.error->message;
              reply["status"] = snapshot.error->status;
            }
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
      if (!mutation_value(server.addFavorite(anime_id))) {
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
      reply["removed"] = mutation_value(server.removeFavorite(anime_id));
      reply["anime_id"] = anime_id;
      reply["revision"] = server.getFavoritesRevision();
      client->send(reply);
    } else if (method == "subscribePlanned") {
      const auto offset = request.value("offset", std::size_t{0});
      const auto limit = static_cast<std::size_t>(std::min<std::int64_t>(
          request.value("limit", std::int64_t{20}), Core::kHistoryPageLimit));
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
            if (snapshot.error) {
              reply["ok"] = false;
              reply["error"] = snapshot.error->message;
              reply["status"] = snapshot.error->status;
            }
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
      if (!mutation_value(server.addPlanned(anime_id))) {
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
      reply["removed"] = mutation_value(server.removePlanned(anime_id));
      reply["anime_id"] = anime_id;
      reply["revision"] = server.getPlannedRevision();
      client->send(reply);
    } else if (method == "subscribeWatched") {
      const auto offset = request.value("offset", std::size_t{0});
      const auto limit = static_cast<std::size_t>(std::min<std::int64_t>(
          request.value("limit", std::int64_t{20}), Core::kHistoryPageLimit));
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
            if (snapshot.error) {
              reply["ok"] = false;
              reply["error"] = snapshot.error->message;
              reply["status"] = snapshot.error->status;
            }
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
      if (!mutation_value(server.addWatched(anime_id))) {
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
      reply["removed"] = mutation_value(server.removeWatched(anime_id));
      reply["anime_id"] = anime_id;
      reply["revision"] = server.getWatchedRevision();
      client->send(reply);
    } else if (method == "subscribeCompleted") {
      const auto offset = request.value("offset", std::size_t{0});
      const auto limit = static_cast<std::size_t>(std::min<std::int64_t>(
          request.value("limit", std::int64_t{20}), Core::kHistoryPageLimit));
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
            if (snapshot.error) {
              reply["ok"] = false;
              reply["error"] = snapshot.error->message;
              reply["status"] = snapshot.error->status;
            }
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
      if (!mutation_value(server.addCompleted(anime_id))) {
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
      reply["removed"] = mutation_value(server.removeCompleted(anime_id));
      reply["anime_id"] = anime_id;
      reply["revision"] = server.getCompletedRevision();
      client->send(reply);
    } else if (method == "subscribeDropped") {
      const auto offset = request.value("offset", std::size_t{0});
      const auto limit = static_cast<std::size_t>(std::min<std::int64_t>(
          request.value("limit", std::int64_t{20}), Core::kHistoryPageLimit));
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
            if (snapshot.error) {
              reply["ok"] = false;
              reply["error"] = snapshot.error->message;
              reply["status"] = snapshot.error->status;
            }
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
      if (!mutation_value(server.addDropped(anime_id))) {
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
      reply["removed"] = mutation_value(server.removeDropped(anime_id));
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
      const bool added = mutation_value(server.addHistory(anime_id));
      if (!added) {
        client->send(error(id, "invalid history entry"));
      } else {
        auto reply = ok(id);
        reply["added"] = true;
        reply["anime_id"] = anime_id;
        reply["revision"] = server.getHistoryRevision();
        client->send(reply);
      }
    } else if (method == "removeAnimeSaveData") {
      const auto anime_id = request.at("anime_id").get<std::int64_t>();
      auto reply = ok(id);
      reply["removed"] = mutation_value(server.removeAnimeSaveData(anime_id));
      reply["anime_id"] = anime_id;
      client->send(reply);
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
      if (!mutation_value(server.updateAnimeSaveData(save_data))) {
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
      reply["removed"] = mutation_value(server.removeHistory(anime_id));
      reply["anime_id"] = anime_id;
      reply["revision"] = server.getHistoryRevision();
      client->send(reply);
    } else if (method == "unsubscribeAnime" || method == "unsubscribeSearch" ||
               method == "unsubscribeFeed") {
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
      if (!client->begin_stream_subscription("subscribeAnime", id)) {
        client->send(error(
            id, "Duplicate active request ID or stream limit reached (256)"));
        return;
      }
      const auto anime_id = request.at("anime_id").get<std::int64_t>();
      std::weak_ptr<Client> weak_client = client;
      const auto refreshing = std::make_shared<bool>(false);
      const auto subscription_id = server.subscribeAnime(
          anime_id,
          [weak_client, id, refreshing](
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
              reply["refreshing"] = *refreshing;
              reply["loaded"] = anime.has_value();
            }
            reply["anime"] =
                anime ? Server::animeToJson(*anime) : Json(nullptr);
            const bool keep_active = reply.value("refreshing", false);
            client->send_stream_subscription("subscribeAnime", id, reply,
                                             !updated && keep_active);
          },
          refreshing.get());
      client->bind_stream_subscription("subscribeAnime", id, subscription_id);
    } else if (method == "subscribeSearch") {
      if (!client->begin_stream_subscription("subscribeSearch", id)) {
        client->send(error(
            id, "Duplicate active request ID or stream limit reached (256)"));
        return;
      }
      auto params = parse_search(request.value("params", Json::object()));
      std::weak_ptr<Client> weak_client = client;
      const auto refreshing = std::make_shared<bool>(false);
      const bool scheduled = server.subscribeSearch(
          params,
          [weak_client, id, refreshing](yani::SearchInput page,
                                        std::vector<yani::SearchResult> rows,
                                        bool updated) {
            const auto client = weak_client.lock();
            if (!client) return;
            auto reply = ok(id);
            if (updated) reply["event"] = "search.updated";
            else reply["refreshing"] = *refreshing;
            reply["offset"] = page.offset.value_or(0);
            reply["limit"] = page.limit.value_or(10);
            reply["results"] = std::move(rows);
            const bool keep_active = reply.value("refreshing", false);
            client->send_stream_subscription("subscribeSearch", id, reply,
                                             !updated && keep_active);
          },
          [weak_client, id](yani::ApiError failure_info) {
            if (const auto client = weak_client.lock()) {
              auto reply = error(id, failure_info.message);
              reply["event"] = "search.updated";
              reply["status"] = failure_info.status;
              reply["refreshing"] = false;
              client->send_stream_subscription("subscribeSearch", id, reply,
                                               false);
            }
          },
          refreshing.get());
      if (!scheduled) {
        client->cancel_stream_subscription("subscribeSearch", id);
        log_server("error", "subscribeSearch: API task pool is stopping");
      }
    } else if (method == "subscribeFeed") {
      if (!client->begin_stream_subscription("subscribeFeed", id)) {
        client->send(error(
            id, "Duplicate active request ID or stream limit reached (256)"));
        return;
      }
      std::weak_ptr<Client> weak_client = client;
      const auto refreshing = std::make_shared<bool>(false);
      const bool scheduled = server.subscribeFeed(
          [weak_client, id, refreshing](yani::FeedResponse feed, bool updated) {
            const auto client = weak_client.lock();
            if (!client) return;
            auto reply = ok(id);
            if (updated) reply["event"] = "feed.updated";
            else reply["refreshing"] = *refreshing;
            reply["feed"] = std::move(feed);
            const bool keep_active = reply.value("refreshing", false);
            client->send_stream_subscription("subscribeFeed", id, reply,
                                             !updated && keep_active);
          },
          [weak_client, id](yani::ApiError failure_info) {
            if (const auto client = weak_client.lock()) {
              auto reply = error(id, failure_info.message);
              reply["event"] = "feed.updated";
              reply["status"] = failure_info.status;
              reply["refreshing"] = false;
              client->send_stream_subscription("subscribeFeed", id, reply,
                                               false);
            }
          },
          refreshing.get());
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
    } else if (method == "getAnilibriaVideos") {
      const auto anime_id = request.at("anime_id").get<std::int64_t>();
      const auto result = server.getAnilibriaVideos(anime_id);
      if (!result) {
        auto reply = error(id, result.error().message);
        reply["status"] = result.error().status;
        reply["data"] = result.error().data;
        client->send(reply);
      } else {
        auto reply = ok(id);
        reply["episodes"] = result->at("episodes");
        client->send(reply);
      }
    } else {
      client->send(error(id, "unknown method"));
    }
  } catch (const OperationError &exception) {
    auto reply = error(id, exception.what());
    reply["status"] = exception.failure.status;
    reply["data"] = exception.failure.data;
    client->send(reply);
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
} // namespace aniparser::ipc
