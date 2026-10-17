#include "core.hpp"
#include "PosixShmSegmentProvider.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

void print_heap_statistics(std::string_view moment) {
  const auto print = [moment](std::string_view name,
                              const shm::HeapStatistics &stats) {
    std::cout << "Heap " << moment << " — " << name << ": used_payload="
              << stats.allocated_bytes << " bytes, mapped="
              << stats.segment_bytes << " bytes, allocations="
              << stats.allocation_count << '\n';
  };

  print("AnimeHeap", Cache::anime_heap().statistics());
  print("VariableHeap", Cache::variable_heap().statistics());
}

#ifndef NDEBUG
void print_anime_dependencies(const Anime *anime, unsigned depth,
                              std::unordered_set<const Anime *> &visited) {
  if (!anime) return;
  const std::string indent(depth * 2, ' ');
  if (!visited.insert(anime).second) {
    std::cout << indent << "└─ Anime " << anime->anime_id
              << " (already shown; cycle/shared child)\n";
    return;
  }

  const auto references = std::atomic_ref<std::uint64_t>(
                              const_cast<std::uint64_t &>(anime->usage_count))
                              .load(std::memory_order_relaxed);
  const auto used_at = std::atomic_ref<std::uint64_t>(
                           const_cast<std::uint64_t &>(anime->last_used_ns))
                           .load(std::memory_order_relaxed);
  const auto now = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count());
  const auto age_ns = now >= used_at ? now - used_at : 0;
  const auto *related = anime->viewing_order.data(Cache::variable_heap());
  std::cout << indent << "├─ Anime " << anime->anime_id
            << " [references=" << references
            << ", last_used=" << age_ns / 1'000'000 << " ms ago, children="
            << anime->viewing_order.size << "]\n";
  for (std::uint64_t i = 0; related && i < anime->viewing_order.size; ++i) {
    const auto *child = static_cast<const Anime *>(
        Cache::anime_heap().resolve(related[i]));
    if (child) {
      std::cout << indent << "│  └─ child[" << i << "] ->\n";
      print_anime_dependencies(child, depth + 1, visited);
    }
  }
}

void print_anime_dependencies(const Anime *anime) {
  std::cout << "Dependency tree:\n";
  std::unordered_set<const Anime *> visited;
  print_anime_dependencies(anime, 0, visited);
}
#endif

bool test_anime(Core &core, std::int64_t anime_id) {
  using Clock = std::chrono::steady_clock;
  const auto started_at = Clock::now();
  const auto caller_thread = std::this_thread::get_id();
  std::atomic<bool> core_call_returned = false;
  std::atomic<Anime *> callback_anime = nullptr;
  std::promise<bool> update_complete;
  auto update_result = update_complete.get_future();

  std::cout << "[main] requesting anime " << anime_id << " on thread "
            << caller_thread << '\n';
  Anime *anime = core.getAnime(
      anime_id, [&update_complete, &core_call_returned, &callback_anime,
                 started_at, caller_thread](std::int64_t updated_id,
                                            Anime *updated) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                 Clock::now() - started_at)
                                 .count();
        const auto callback_thread = std::this_thread::get_id();
        const bool returned_first =
            core_call_returned.load(std::memory_order_acquire);
        const bool different_thread = callback_thread != caller_thread;
        std::cout << "[callback] after " << elapsed << " ms on thread "
                  << callback_thread << " (different_thread="
                  << std::boolalpha << different_thread
                  << ", core_returned=" << returned_first
                  << ", pointer=" << static_cast<const void *>(updated)
                  << ")\n";

        if (!updated) {
          std::cerr << "FAIL: API update failed for anime " << updated_id
                    << '\n';
          update_complete.set_value(false);
          return;
        }

        const auto name = updated->title.view(Cache::variable_heap());
        std::cout << "Anime name: " << name << '\n';
#ifndef NDEBUG
        print_anime_dependencies(updated);
#endif
        print_heap_statistics("before release");
        const bool valid = updated->anime_id == updated_id && !name.empty() &&
                           different_thread;
        callback_anime.store(updated, std::memory_order_release);
        update_complete.set_value(valid);
      });

  if (!anime) {
    core_call_returned.store(true, std::memory_order_release);
    std::cerr << "FAIL: could not allocate cache slot for anime " << anime_id
              << '\n';
    return false;
  }

  const auto returned_at = Clock::now();
  core_call_returned.store(true, std::memory_order_release);
  std::cout << "[main] Core returned a cache pointer after "
            << std::chrono::duration_cast<std::chrono::milliseconds>(
                   returned_at - started_at)
                   .count()
            << " ms (pointer=" << static_cast<const void *>(anime)
            << "); waiting for update callback\n";

  const bool callback_valid = update_result.get();
  const bool same_pointer =
      callback_anime.load(std::memory_order_acquire) == anime;
  std::cout << "[check] callback used the same cache pointer: "
            << std::boolalpha << same_pointer << '\n';
  const bool released = core.releaseAnime(anime);
  print_heap_statistics("after release");
  const bool passed = callback_valid && same_pointer && released;
  std::cout << (passed ? "PASS" : "FAIL") << ": async test for anime "
            << anime_id << '\n';
  return passed;
}

bool test_naruto_search(Core &core) {
  std::promise<std::vector<std::int64_t>> first_ids_promise;
  auto first_ids_future = first_ids_promise.get_future();
  std::promise<void> search_finished_promise;
  auto search_finished = search_finished_promise.get_future();
  bool first_page_reported = false;
  shm::SearchHandle root_search{};

  yani::SearchInput params{};
  params.q = "наруто";
  std::cout << "[search] searching for 'наруто'\n";
  const bool scheduled = core.searchTitles(
      std::move(params),
      [&core, &first_ids_promise, &search_finished_promise, &first_page_reported,
       &root_search](const yani::SearchInput &request,
                     SharedSearch *page) -> bool {
        if (!page) {
          std::cerr << "FAIL: Naruto search failed at offset "
                    << request.offset.value_or(0) << '\n';
          if (!first_page_reported) {
            first_page_reported = true;
            first_ids_promise.set_value({});
          }
          if (root_search) core.releaseSearch(root_search);
          search_finished_promise.set_value();
          return false;
        }

        if (!root_search) root_search = page->self;
        std::cout << "[search] page offset=" << request.offset.value_or(0)
                  << ", loaded=" << page->anime_count << '\n';
        if (!first_page_reported) {
          std::vector<std::int64_t> ids;
          const auto count = std::min<std::uint64_t>(page->anime_count, 3);
          for (std::uint64_t i = 0; i < count; ++i) {
            const auto *anime = Cache::resolve_anime(page->anime[i]);
            if (anime) ids.push_back(anime->anime_id);
          }
          first_page_reported = true;
          first_ids_promise.set_value(std::move(ids));
        }

        // The test only needs the first three matches, so stop after page 0.
        if (root_search) core.releaseSearch(root_search);
        root_search = {};
        search_finished_promise.set_value();
        return false;
      });
  if (!scheduled) {
    std::cerr << "FAIL: could not start Naruto search\n";
    return false;
  }

  const auto first_ids = first_ids_future.get();
  bool passed = first_ids.size() == 3;
  if (!passed)
    std::cerr << "FAIL: Naruto search returned fewer than 3 anime on its first page\n";

  for (const auto anime_id : first_ids) {
    std::promise<bool> anime_loaded_promise;
    auto anime_loaded = anime_loaded_promise.get_future();
    std::cout << "[search] loading full anime " << anime_id << '\n';
    Anime *anime = core.getAnime(
        anime_id, [&anime_loaded_promise](std::int64_t updated_id,
                                          Anime *updated) {
          if (!updated) {
            std::cerr << "FAIL: could not load anime " << updated_id << '\n';
            anime_loaded_promise.set_value(false);
            return;
          }
          const auto name = updated->title.view(Cache::variable_heap());
          std::cout << "[search result] " << updated_id << ": " << name << '\n';
          anime_loaded_promise.set_value(!name.empty());
        });
    if (!anime) {
      passed = false;
      std::cerr << "FAIL: could not allocate anime " << anime_id << '\n';
      continue;
    }
    passed = anime_loaded.get() && passed;
    core.releaseAnime(anime);
  }

  search_finished.wait();
  return passed;
}

bool test_feed(Core &core) {
  std::promise<shm::HeapHandle> feed_loaded_promise;
  auto feed_loaded = feed_loaded_promise.get_future();
  std::cout << "[feed] requesting feed\n";
  if (!core.getFeed({}, [&feed_loaded_promise](
                         const std::vector<std::string> &watches,
                         SharedFeed *feed) {
        std::cout << "[feed callback] watches=" << watches.size() << '\n';
        if (!feed) {
          std::cerr << "FAIL: feed loading failed\n";
          feed_loaded_promise.set_value({});
          return;
        }

        std::cout << "[feed] new=" << feed->new_anime.size
                  << ", announcements=" << feed->announcements.size
                  << ", recommendations=" << feed->recommendations.size
                  << ", carousel=" << feed->carousel_items.size
                  << ", schedule=" << feed->schedule_items.size
                  << ", collections=" << feed->collections.size << '\n';

        const auto *new_items =
            feed->new_anime.data(Cache::variable_heap());
        const auto count = std::min<std::uint64_t>(feed->new_anime.size, 3);
        for (std::uint64_t i = 0; new_items && i < count; ++i) {
          const auto *anime = Cache::resolve_anime(new_items[i]);
          if (anime) {
            std::cout << "[feed anime] " << anime->anime_id << ": "
                      << anime->title.view(Cache::variable_heap()) << '\n';
          }
        }
        feed_loaded_promise.set_value(feed->self);
      })) {
    std::cerr << "FAIL: could not start feed request\n";
    return false;
  }

  const auto handle = feed_loaded.get();
  if (!handle) return false;
  const auto *feed = core.resolveFeed(handle);
  const bool valid = feed && feed->self.segment_id == handle.segment_id &&
                     feed->self.offset == handle.offset;
  const bool released = core.releaseFeed(handle);
  print_heap_statistics("after feed release");
  std::cout << ((valid && released) ? "PASS" : "FAIL")
            << ": feed conversion and release verified\n";
  return valid && released;
}

} // namespace

int main() {
  constexpr std::int64_t anime_ids[] = {771, 4958};
  shm::PosixShmSegmentProvider provider;
  Core core(provider, "aniparser_cache");
  if (!core.initialize()) {
    std::cerr << "FAIL: could not initialize cache\n";
    return 1;
  }

  bool passed = true;
  for (const auto anime_id : anime_ids)
    passed = test_anime(core, anime_id) && passed;
  passed = test_naruto_search(core) && passed;
  passed = test_feed(core) && passed;
  return passed ? 0 : 1;
}
