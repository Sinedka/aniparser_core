#pragma once

#include <cstdint>
#include <type_traits>

#include "SharedString.hpp"
#include "SharedVector.hpp"
#include "types.hpp"

struct Anime {
  std::int64_t anime_id{};
  struct AnimeStatus {
    shm::SharedString title{};
    shm::SharedString class_{};
    shm::SharedString alias{};
    std::int64_t value{};
  } anime_status{};
  shm::SharedString anime_url{};
  struct Poster {
    shm::SharedString big{};
    shm::SharedString fullsize{};
    shm::SharedString huge{};
    shm::SharedString medium{};
    shm::SharedString small{};
  } poster{};
  struct Rating {
    double average{};
    double kp_rating{};
    double anidub_rating{};
    std::int64_t counters{};
    double myanimelist_rating{};
    double shikimori_rating{};
    double worldart_rating{};
  } rating{};
  shm::SharedString title{};
  struct AnimeType {
    shm::SharedString name{};
    std::int64_t value{};
    shm::SharedString shortname{};
  } type{};
  std::int64_t year{};
  // Used for related-anime records created from API viewing_order entries.
  std::int64_t date{};
  shm::SharedString description{};
  std::int64_t views{};
  std::int64_t season{};
  struct MinAge {
    std::int64_t value{};
    shm::SharedString title{};
    shm::SharedString titleLong{};
  } min_age{};
  struct RemoteIds {
    std::int64_t worldart_id{};
    shm::SharedString worldart_type{};
    std::int64_t kp_id{};
    std::int64_t anidb_id{};
    std::int64_t anidub_id{};
    std::int64_t sr_id{};
    shm::SharedString anilibria_alias{};
    std::int64_t shikimori_id{};
    std::int64_t myanimelist_id{};
  } remote_ids{};
  shm::SharedString original{};
  shm::SharedVector<shm::SharedString> other_titles{};
  struct Creator {
    shm::SharedString title{};
    std::int64_t id{};
    shm::SharedString url{};

    void release(shm::VariableHeap &heap) {
      title.clear(heap);
      url.clear(heap);
    }
  };
  struct Studio {
    shm::SharedString title{};
    std::int64_t id{};
    shm::SharedString url{};

    void release(shm::VariableHeap &heap) {
      title.clear(heap);
      url.clear(heap);
    }
  };

  struct SkipData {
    shm::SharedString time{};
    shm::SharedString length{};

    void release(shm::VariableHeap &heap) {
      time.clear(heap);
      length.clear(heap);
    }
  };

  struct Skips {
    SkipData opening{};
    SkipData ending{};
  };

  struct VideoInfo {
    std::int64_t video_id{};
    shm::SharedString iframe_url{};
    struct {
      shm::SharedString dubbing{};
      shm::SharedString player{};
    } data{};
    shm::SharedString number{};
    std::int64_t date{};
    std::int64_t index{};
    Skips skips{};

    void release(shm::VariableHeap &heap) {
      iframe_url.clear(heap);
      data.dubbing.clear(heap);
      data.player.clear(heap);
      number.clear(heap);
      skips.opening.release(heap);
      skips.ending.release(heap);
    }
  };
  struct Genre {
    shm::SharedString title{};
    std::int64_t id{};
    shm::SharedString alias{};
    shm::SharedString url{};

    void release(shm::VariableHeap &heap) {
      title.clear(heap);
      alias.clear(heap);
      url.clear(heap);
    }
  };

  struct Translates {
    shm::SharedString title{};
    shm::SharedString href{};
    shm::SharedString value{};

    void release(shm::VariableHeap &heap) {
      title.clear(heap);
      href.clear(heap);
      value.clear(heap);
    }
  };

  struct Episodes {
    std::int64_t aired{};
    std::int64_t count{};
    std::int64_t next_date{};
  };

  shm::SharedVector<Creator> creators{};
  shm::SharedVector<Studio> studios{};
  shm::SharedVector<VideoInfo> videos{};
  shm::SharedVector<Genre> genres{};
  shm::SharedVector<shm::HeapHandle> viewing_order{};
  shm::SharedVector<Translates> translates{};
  shm::SharedVector<shm::SharedString> blocked_in{};
  Episodes episodes{};
  std::int64_t comments_count{};
  std::int64_t reviews_count{};
  struct RandomScreenshot {
    struct {
      shm::SharedString small{};
      shm::SharedString full{};
    } sizes{};
    std::int64_t id{};
    std::int64_t time{};
    std::int64_t episode{};

    void release(shm::VariableHeap &heap) {
      sizes.small.clear(heap);
      sizes.full.clear(heap);
    }
  };

  shm::SharedVector<RandomScreenshot> random_screenshots{};
  struct Top {
    std::int64_t category{};
    std::int64_t global{};
  } top{};
  std::int64_t posts_count{};

  // Server-managed ownership metadata; client readers must treat it as read-only.
  alignas(std::uint64_t) std::uint64_t usage_count{1};
  std::uint64_t last_used_ns{};
  // Stable shared-memory identity used by clients to retain/release this slot.
  shm::HeapHandle self{};

  void release_shared_storage(shm::VariableHeap &heap);
};

static_assert(std::is_trivially_copyable_v<Anime>);
static_assert(std::is_trivially_destructible_v<Anime>);
static_assert(alignof(Anime) <= 8,
              "AnimeHeap allocator supports alignments up to 8 bytes");
