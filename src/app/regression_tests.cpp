#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "../../tui/state_utils.hpp"
#include "../server/ipc/protocol.hpp"
#include "../server/ipc/request_validation.hpp"
#include "callback_queue.hpp"
#include "core.hpp"
#include "ipc_server.hpp"
#include "json.hpp"
#include "server.hpp"

using namespace std::chrono_literals;
namespace {
using Json = nlohmann::json;
int failures = 0;
void check(const char* name, bool success) {
  std::cout << (success ? "PASS " : "FAIL ") << name << '\n';
  if (!success) ++failures;
}
void data_dir(const std::filesystem::path &path) {
#if defined(_WIN32)
  _putenv_s("ANIPARSER_DATA_DIR", path.string().c_str());
#else
  ::setenv("ANIPARSER_DATA_DIR", path.c_str(), 1);
#endif
}
void write_file(const std::filesystem::path &path, const Json &document) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream out(path);
  out << document.dump();
  if (!out) throw std::runtime_error("Cannot write fixture");
}
Json read_file(const std::filesystem::path &path) {
  std::ifstream input(path);
  return Json::parse(input);
}
bool changed(yani::Result<bool> result) { return result && *result; }

Core::ApiBackend fixtures() {
  Core::ApiBackend backend;
  backend.anime = [](std::int64_t id, bool) -> yani::Result<yani::AnimeResult> {
    // Make loading observable in the manual fixture UI without network I/O.
    std::this_thread::sleep_for(50ms);
    yani::AnimeResult result;
    result.anime_id = id;
    result.title = "Fixture " + std::to_string(id);
    result.year = 2026;
    result.viewing_order.push_back(
        yani::ViewingOrder{.title = "Cold fixture 3", .anime_id = 3});
    if (id == 1) {
      yani::VideoInfo video;
      video.iframe_url = "https://kodikplayer.com/season/fixture";
      video.data.player = "Kodik";
      video.data.dubbing = "Fixture dub";
      video.number = "1";
      result.videos.push_back(video);
    }
    return result;
  };
  backend.search = [](const yani::SearchInput &params)
      -> yani::Result<std::vector<yani::SearchResult>> {
    if (params.q == "fail")
      return std::unexpected(yani::ApiError{
          .status = 503, .message = "Fixture search unavailable"});
    std::vector<yani::SearchResult> results;
    for (auto id : params.ids.value_or(std::vector<std::int64_t>{1, 2})) {
      yani::SearchResult row;
      row.anime_id = id;
      row.title = "Fixture " + std::to_string(id);
      row.year = 2026;
      results.push_back(row);
    }
    return results;
  };
  backend.feed = []() -> yani::Result<yani::FeedResponse> {
    yani::FeedResponse feed;
    for (auto id : {1, 2}) {
      yani::FeedAnimeItem item;
      item.anime_id = id;
      item.title = "Fixture " + std::to_string(id);
      item.year = 2026;
      feed.new_.push_back(item);
    }
    return feed;
  };
  return backend;
}

void cache_checks() {
  bool invalid_model = false;
  try {
    (void)Json(nullptr).get<yani::AnimeResult>();
  } catch (const std::exception &) { invalid_model = true; }
  check("primitive/null model roots are rejected", invalid_model);

  AnimeCache cache;
  yani::SearchResult first;
  first.anime_id = 1;
  first.year = 2020;
  first.rating.average = 8;
  cache.storeSearchResults("a", {first});
  auto partial = Json{{"anime_id", 1}, {"rating", Json::object()}}
                     .get<yani::SearchResult>();
  cache.storeSearchResults("b", {partial});
  check("missing/null sentinels preserve numeric card fields",
        cache.get(1)->year == 2020 && cache.get(1)->rating.average == 8);
  cache.subscribe(2, {});
  yani::AnimeResult full;
  full.anime_id = 2;
  full.title = "old";
  full.original = "preserved detail";
  full.videos.push_back({});
  cache.finishRefresh(2, full);
  first.anime_id = 2;
  first.title = "new";
  cache.storeSearchResults("fresh", {first});
  check("Search updates full-card fields without losing details/videos",
        cache.searchResults({2})[0].title == "new" &&
            cache.get(2)->original == "preserved detail" &&
            cache.get(2)->videos.size() == 1);
  check("unsubscribe releases a pending Anime subscription",
        cache.unsubscribe(
            cache.subscribe(3, [](auto, auto, auto, auto) {}).subscription_id));
  for (const auto &value : {Json(1.5), Json(1e100), Json(UINT64_MAX)}) {
    bool rejected = false;
    try {
      (void)Json{{"year", value}}.get<yani::SearchResult>();
    } catch (const std::exception &) { rejected = true; }
    check("API numeric conversion rejects fractional/out-of-range integers",
          rejected);
  }
}

void storage_checks(const std::filesystem::path &root) {
  for (int version = 1; version <= 4; ++version) {
    const auto path = root / ("v" + std::to_string(version));
    data_dir(path);
    Json document{{"version", version}, {"history", Json::array({7, 8})}};
    if (version == 1)
      document["history"] = Json::array(
          {{{"anime_id", 7}, {"player", "P"}, {"dubber", "D"}, {"time", 12}}});
    if (version >= 2)
      document["save_data"] = Json::array(
          {{{"anime_id", 7}, {"player", "P"}, {"dubber", "D"}, {"time", 12}}});
    if (version >= 3) document["favorites"] = Json::array({8});
    if (version >= 4)
      document["anime_states"] = {{"planned", Json::array({9})}};
    write_file(path / "history.json", document);
    SaveManager manager;
    const auto state = manager.loadState();
    check("load history versions 1-4 with legacy progress",
          state && state->history.front() == 7 && state->save_data.size() == 1);
    check("atomic rewrite produces readable version 4",
          state && manager.saveState(*state) &&
              read_file(path / "history.json")["version"] == 4);
  }
  for (const auto &document :
       {Json{{"version", 5}, {"history", Json::array({123})}},
        Json{{"version", 4}, {"history", "invalid"}}}) {
    const auto path =
        root / ("protected-" + std::to_string(document["version"].get<int>()));
    data_dir(path);
    write_file(path / "history.json", document);
    Core core(fixtures());
    auto result = core.addHistory(42);
    check("unsupported/corrupt storage is never overwritten",
          !result && read_file(path / "history.json") == document &&
              core.getHistory().empty());
  }
  const auto corrupt = root / "truncated";
  std::filesystem::create_directories(corrupt);
  {
    std::ofstream out(corrupt / "history.json");
    out << "{truncated";
  }
  data_dir(corrupt);
  {
    Core core(fixtures());
    check("truncated JSON blocks writes", !core.addHistory(1));
  }

  const auto malformed = root / "malformed-entry";
  const Json invalid_entry{{"version", 4}, {"history", Json::array({1, 1.5})}};
  write_file(malformed / "history.json", invalid_entry);
  data_dir(malformed);
  {
    Core core(fixtures());
    check("malformed storage entries preserve original bytes",
          !core.addHistory(2) &&
              read_file(malformed / "history.json") == invalid_entry);
  }

  const auto blocked = root / "ordinary-file";
  {
    std::ofstream out(blocked);
    out << "fixture";
  }
  data_dir(blocked);
  {
    Core core(fixtures());
    const auto result = core.updateAnimeSaveData({42, "P", "D", "1", 10});
    check("failed save returns error without publishing progress or revision",
          !result && !core.getAnimeSaveData(42) &&
              core.getHistoryRevision() == 0);
  }
  data_dir(root / "rename-failure");
  {
    Core core(fixtures());
    std::filesystem::create_directories(root / "rename-failure/history.json");
    check("rename failure rolls back live collections",
          !core.addHistory(1) && core.getHistory().empty());
  }

  const auto path = root / "transactions";
  data_dir(path);
  {
    Core core(fixtures());
    check("set planned status", changed(core.addPlanned(7)));
    check("status transition preserves exclusivity",
          changed(core.addCompleted(7)) && core.getStatus(7) == 3 &&
              !core.isPlanned(7));
    check("progress independent of history",
          changed(core.updateAnimeSaveData({42, "P", "D", "1", 10})) &&
              core.getHistory().empty());
  }
  {
    Core core(fixtures());
    check("restart restores committed status and progress",
          core.getStatus(7) == 3 && core.getAnimeSaveData(42)->time == 10);
    check("explicit progress removal persists",
          changed(core.removeAnimeSaveData(42)) && !core.getAnimeSaveData(42));
  }
  {
    Core core(fixtures());
    check("restart preserves explicit progress removal",
          !core.getAnimeSaveData(42));
  }
  const auto limit_path = root / "limit";
  data_dir(limit_path);
  auto entries = Json::array();
  for (std::int64_t id = 1; id <= 10000; ++id)
    entries.push_back({{"anime_id", id},
                       {"player", "P"},
                       {"dubber", "D"},
                       {"episode", "1"},
                       {"time", 0}});
  write_file(
      limit_path / "history.json",
      {{"version", 4}, {"history", Json::array()}, {"save_data", entries}});
  {
    Core core(fixtures());
    check("progress limit rejects new IDs without eviction",
          !core.updateAnimeSaveData({10001, "P", "D", "1", 1}) &&
              core.getAnimeSaveData(1).has_value());
    check("progress at capacity can still be updated",
          changed(core.updateAnimeSaveData({1, "P", "D", "1", 99})));
    check("explicit removal frees a progress slot",
          changed(core.removeAnimeSaveData(1)) &&
              changed(core.updateAnimeSaveData({10001, "P", "D", "1", 1})));
  }
}

void concurrency_checks(const std::filesystem::path &root) {
  data_dir(root / "callbacks");
  {
    CallbackQueue delivery;
    std::vector<int> order;
    delivery.post([&] { order.push_back(1); });
    delivery.initial([&] { order.push_back(0); });
    check("completion queued before initial snapshot is delivered afterward",
          order == std::vector<int>({0, 1}));
  }
  {
    auto backend = fixtures();
    std::promise<void> started, release, first_finished, second_finished;
    auto gate = release.get_future().share();
    auto first_done = first_finished.get_future();
    auto second_done = second_finished.get_future();
    std::atomic<int> requests = 0;
    backend.anime = [&](auto id, bool) -> yani::Result<yani::AnimeResult> {
      ++requests;
      started.set_value();
      gate.wait();
      yani::AnimeResult result;
      result.anime_id = id;
      return result;
    };
    Core core(backend);
    core.subscribeAnime(20, [&](auto, auto, bool updated, auto) {
      if (updated) first_finished.set_value();
    });
    started.get_future().wait();
    std::vector<int> order;
    bool refreshing = false;
    core.subscribeAnime(
        20,
        [&](auto, auto, bool updated, auto) {
          if (!updated) {
            release.set_value();
            (void)first_done.wait_for(1s);
            order.push_back(refreshing ? 0 : -1);
          } else {
            order.push_back(1);
            second_finished.set_value();
          }
        },
        true, &refreshing);
    check("coalesced refresh cannot overtake initial callback or clear its "
          "refresh flag",
          second_done.wait_for(2s) == std::future_status::ready &&
              order == std::vector<int>({0, 1}) && requests == 1);
  }

  {
    Core core(fixtures());
    std::vector<std::uint64_t> sequences;
    core.subscribeFavorites([&](auto snapshot, bool updated) {
      if (!updated || snapshot.update_reason != "favorites") return;
      sequences.push_back(snapshot.sequence);
      if (sequences.size() == 1) {
        auto result = core.removeFavorite(1);
        if (!changed(std::move(result)))
          throw std::runtime_error("Nested mutation failed");
      }
    });
    check("reentrant collection callback completes in sequence",
          changed(core.addFavorite(1)) && !core.isFavorite(1) &&
              sequences.size() == 2 && sequences[0] < sequences[1]);
  }
  {
    TaskPool pool(1, 1);
    std::promise<void> started, release;
    auto gate = release.get_future().share();
    std::promise<bool> rejected;
    auto result = rejected.get_future();
    pool.submit([&] {
      started.set_value();
      gate.wait();
      rejected.set_value(!pool.submit([] {}));
    });
    started.get_future().wait();
    const bool filled = pool.submit([] {});
    release.set_value();
    check("nested submit rejects full queue without blocking worker",
          filled && result.wait_for(2s) == std::future_status::ready &&
              result.get());
  }
  {
    TaskPool pool(1, 1);
    std::promise<void> started, released;
    auto release = released.get_future().share();
    pool.submit([&] {
      started.set_value();
      release.wait();
    });
    started.get_future().wait();
    auto capture = std::shared_ptr<int>(new int(0), [&](int* value) {
      delete value;
      // This would deadlock if dropped task captures were destroyed under
      // mutex.
      (void)pool.submit([] {});
      released.set_value();
    });
    pool.submit([capture] {});
    capture.reset();
    pool.shutdown(true);
    check("shutdown discards pending captures outside mutex before joining",
          true);
  }
  {
    auto backend = fixtures();
    std::atomic<bool> video_requested = false;
    backend.anime = [&](auto id,
                        bool videos) -> yani::Result<yani::AnimeResult> {
      video_requested = videos;
      yani::AnimeResult result;
      result.anime_id = id;
      return result;
    };
    Core core(backend);
    std::promise<void> done;
    auto completed = done.get_future();
    core.subscribeAnime(
        7,
        [&](auto, auto, bool updated, auto) {
          if (!updated) throw std::runtime_error("Initial callback failure");
          done.set_value();
        },
        false);
    check("throwing initial callback does not strand Anime refresh and videos "
          "are complete",
          completed.wait_for(2s) == std::future_status::ready &&
              video_requested);
  }
  {
    auto backend = fixtures();
    backend.search =
        [](const auto &) -> yani::Result<std::vector<yani::SearchResult>> {
      throw std::runtime_error("Injected search exception");
    };
    backend.feed = []() -> yani::Result<yani::FeedResponse> {
      return std::unexpected(
          yani::ApiError{.status = 503, .message = "Injected feed failure"});
    };
    Core core(backend);
    std::promise<void> search, feed;
    auto search_done = search.get_future(), feed_done = feed.get_future();
    core.subscribeSearch(
        {},
        [](auto, auto, bool) {
          throw std::runtime_error("Initial search callback failure");
        },
        [&](auto) { search.set_value(); });
    core.subscribeFeed(
        [](auto, bool) {
          throw std::runtime_error("Initial feed callback failure");
        },
        [&](auto) { feed.set_value(); });
    check("Search and Feed deliver terminal failures despite throwing initial "
          "callbacks",
          search_done.wait_for(2s) == std::future_status::ready &&
              feed_done.wait_for(2s) == std::future_status::ready);
  }
  {
    data_dir(root / "metadata-errors");
    auto backend = fixtures();
    std::atomic<int> requests = 0;
    backend.search =
        [&](const auto &) -> yani::Result<std::vector<yani::SearchResult>> {
      ++requests;
      return std::unexpected(
          yani::ApiError{.status = 503, .message = "Metadata unavailable"});
    };
    Core core(backend);
    const bool setup =
        changed(core.addHistory(10)) && changed(core.addFavorite(11)) &&
        changed(core.addPlanned(12)) && changed(core.addWatched(13)) &&
        changed(core.addCompleted(14)) && changed(core.addDropped(15));
    using Subscribe = Core::HistorySubscriptionId (Core::*)(
        Core::HistoryCallback, std::size_t, std::size_t);
    const Subscribe methods[] = {
        &Core::subscribeHistory,   &Core::subscribeFavorites,
        &Core::subscribePlanned,   &Core::subscribeWatched,
        &Core::subscribeCompleted, &Core::subscribeDropped};
    bool terminal = setup;
    for (const auto method : methods) {
      const auto done = std::make_shared<std::promise<bool>>();
      auto result = done->get_future();
      (core.*method)(
          [done](auto snapshot, bool updated) {
            if (updated && snapshot.error)
              done->set_value(snapshot.error->status == 503 &&
                              !snapshot.refreshing &&
                              snapshot.update_reason == "error");
          },
          0, 1);
      terminal = terminal && result.wait_for(2s) == std::future_status::ready &&
                 result.get();
    }
    check("all six collections terminate failed metadata refresh without retry "
          "loops",
          terminal && requests == 6);
  }
  {
    data_dir(root / "parallel");
    Core core(fixtures());
    std::vector<std::thread> threads;
    std::atomic<int> successes = 0;
    for (int i = 1; i <= 8; ++i)
      threads.emplace_back([&, i] {
        if (changed(core.addHistory(i))) ++successes;
      });
    for (auto &thread : threads) thread.join();
    check("parallel mutations commit every entry with matching revision",
          successes == 8 && core.getHistory().size() == 8 &&
              core.getHistoryRevision() == 8);
  }
}

void input_checks() {
  std::string frames(1024 * 1024, ' ');
  frames += "\nnext\n";
  int lines = 0;
  check("1 MiB line followed by another frame is valid",
        aniparser::ipc::consume_lines(frames, [&](const auto &) { ++lines; }) &&
            lines == 2 && frames.empty());
  frames.assign(1024 * 1024 + 1, 'x');
  check("oversized unterminated frame is rejected",
        !aniparser::ipc::consume_lines(frames, [](const auto &) {}));

  for (const auto &invalid :
       {Json(-1), Json(1.5), Json(1e100), Json(UINT64_MAX), Json("1")}) {
    bool rejected = false;
    try {
      aniparser::ipc::validate_request(
          {{"id", 1}, {"method", "addPlanned"}, {"anime_id", invalid}});
    } catch (const std::exception &) { rejected = true; }
    check("IPC rejects invalid numeric IDs before mutation", rejected);
  }
  std::string text = "Aя😀";
  aniparser::tui::erase_last_utf8(text);
  check("Backspace removes entire emoji", text == "Aя");
  aniparser::tui::erase_last_utf8(text);
  check("Backspace removes entire Cyrillic code point", text == "A");
  aniparser::tui::erase_last_utf8(text);
  aniparser::tui::erase_last_utf8(text);
  check("Backspace safely handles ASCII and empty strings", text.empty());
  check("empty/shrinking menu selections stay in bounds",
        aniparser::tui::bounded_selection(5, 0) == 0 &&
            aniparser::tui::bounded_selection(5, 2) == 1);
}

volatile std::sig_atomic_t stopped = 0;
void stop_fixture(int) { stopped = 1; }
} // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 3 && std::string_view(argv[1]) == "--fixture-server") {
      if (!std::getenv("ANIPARSER_DATA_DIR")) {
        const auto storage =
            std::filesystem::path("out/fixture-server") /
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(storage);
        data_dir(storage);
      }
      auto backend = fixtures();
      if (std::getenv("ANIPARSER_FIXTURE_FEED_ERROR"))
        backend.feed = []() -> yani::Result<yani::FeedResponse> {
          return std::unexpected(yani::ApiError{
              .status = 503, .message = "Fixture feed unavailable"});
        };
      Core core(backend);
      Server server(core);
      IpcServer ipc(server, argv[2]);
      if (!ipc.start()) return 2;
      std::signal(SIGINT, stop_fixture);
      std::signal(SIGTERM, stop_fixture);
      while (!stopped) std::this_thread::sleep_for(20ms);
      return 0;
    }
    const auto root =
        std::filesystem::path("out/checks") /
        std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    std::filesystem::create_directories(root);
    cache_checks();
    storage_checks(root);
    concurrency_checks(root);
    input_checks();
    std::cout << failures << " regression failure(s); fixtures: " << root
              << '\n';
    return failures == 0 ? 0 : 1;
  } catch (const std::exception &error) {
    std::cerr << "Regression check aborted: " << error.what() << '\n';
    return 2;
  }
}
