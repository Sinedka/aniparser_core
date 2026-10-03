#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

class cache {
  public:
  cache() = default;
  ~cache() = default;

  private:
  struct sharedMemoryAnime {
    std::atomic<uint32_t> version;
    // some dinamic struct that have animes in a row, with good allocator logic
  };

  struct sharedMemorySearch {
    std::atomic<uint32_t> version;
    // some dinamic struct that have Search class in a row
  };

  struct stringData {
    std::atomic<uint32_t> version;
  }

  static sharedMemoryAnime Animedata;
  static sharedMemorySearch Searchdata;
  static sharedMemoryString string_data;
};

struct Search;

struct Anime {
  std::uint64_t last_update_time;
  std::uint64_t last_used_time;
  std::vector<Search> searches;   // who is using
  std::vector<Anime> from_animes; // who is using

  std::int64_t anime_id{};
  struct {
    std::string title;
    std::string class_;
    std::string alias;
    std::int64_t value;
  } anime_status;
  std::string anime_url{};
  struct {
    std::string big{};
    std::string fullsize{};
    std::string huge{};
    std::string medium{};
    std::string small{};
  } poster{};
  struct {
    double average{};
    double kp_rating{};
    double anidub_rating{};
    std::int64_t counters{};
    double myanimelist_rating{};
    double shikimori_rating{};
    double worldart_rating{};
  } rating{};
  std::string title{};
  struct {
    std::string name{};
    std::int64_t value{};
    std::string shortname{};
  } type{};
  std::int64_t year{};
  std::string description{};
  std::int64_t views{};
  std::int64_t season{};
  struct {
    std::int64_t value{};
    std::string title{};
    std::string titleLong{};
  } min_age{};
  struct {
    std::int64_t worldart_id{};
    std::string worldart_type{};
    std::int64_t kp_id{};
    std::int64_t anidb_id{};
    std::int64_t sr_id{};
    std::string anilibria_alias{};
    std::int64_t shikimori_id{};
    std::int64_t myanimelist_id{};
  } remote_ids{};
  std::string original{};
  std::vector<std::string> other_titles{};
  struct Creator {
    std::string title{};
    std::int64_t id{};
    std::string url{};
  };
  struct Studio {
    std::string title{};
    std::int64_t id{};
    std::string url{};
  };

  struct SkipData {
    std::string time{};
    std::string length{};
  };

  struct Skips {
    SkipData opening{};
    SkipData ending{};
  };

  struct VideoInfo {
    std::int64_t video_id{};
    std::string iframe_url{};
    struct {
      std::string dubbing{};
      std::string player{};
    } data{};
    std::string number{};
    std::int64_t date{};
    std::int64_t index{};
    Skips skips{};
  };
  struct Genre {
    std::string title{};
    std::int64_t id{};
    std::string alias{};
    std::string url{};
  };

  struct Translates {
    std::string title{};
    std::string href{};
    std::string value{};
  };

  struct Episodes {
    std::int64_t aired{};
    std::int64_t count{};
    std::int64_t next_date{};
  };

  std::vector<Creator> creators{};
  std::vector<Studio> studios{};
  std::vector<VideoInfo> videos{};
  std::vector<Genre> genres{};
  std::vector<Anime> viewing_order{};
  std::vector<Translates> translates{};
  std::vector<std::string> blocked_in{};
  struct {
    std::int64_t aired{};
    std::int64_t count{};
    std::int64_t next_date{};
  } episodes{};
  std::int64_t comments_count{};
  std::int64_t reviews_count{};
  struct RandomScreenshot {
    struct {
      std::string small{};
      std::string full{};
    } sizes{};
    std::int64_t id{};
    std::int64_t time{};
    std::int64_t episode{};
  };

  std::vector<RandomScreenshot> random_screenshots{};
  struct {
    std::int64_t category{};
    std::int64_t global{};
  } top{};
  std::int64_t posts_count{};
};

struct Search {
  Search(const Search &) = default;
  Search(Search &&) = default;
  Search &operator=(const Search &) = default;
  Search &operator=(Search &&) = default;
  Anime* list[10];
  Search* next;
};
