#include <nlohmann/json.hpp>

namespace yani {

struct AnimeStatus {
  std::string title{};
  std::string class_{};
  std::string alias{};
  std::int64_t value{};
};

struct Poster {
  std::string big{};
  std::string fullsize{};
  std::string huge{};
  std::string medium{};
  std::string small{};
};

struct Rating {
  double average{};
  double kp_rating{};
  double anidub_rating{};
  std::int64_t counters{};
  double myanimelist_rating{};
  double shikimori_rating{};
  double worldart_rating{};
};
struct AnimeType {
  std::string name{};
  std::int64_t value{};
  std::string shortname{};
};

struct MinAge {
  std::int64_t value{};
  std::string title{};
  std::string titleLong{};
};

struct RemoteIds {
  std::int64_t worldart_id{};
  std::string worldart_type{};
  std::int64_t kp_id{};
  std::int64_t anidb_id{};
  std::int64_t sr_id{};
  std::string anilibria_alias{};
  std::int64_t shikimori_id{};
  std::int64_t myanimelist_id{};
};
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

struct VideoData {
  std::string dubbing{};
  std::string player{};
};

struct VideoInfo {
  std::int64_t video_id{};
  std::string iframe_url{};
  VideoData data{};
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

struct ScreenshotSizes {
  std::string small{};
  std::string full{};
};

struct RandomScreenshot {
  ScreenshotSizes sizes{};
  std::int64_t id{};
  std::int64_t time{};
  std::int64_t episode{};
};

struct Top {
  std::int64_t category{};
  std::int64_t global{};
};

struct ViewingOrder {
  std::string title{};
  std::int64_t anime_id{};
  AnimeType type{};
  std::string anime_url{};
  AnimeStatus anime_status{};
  std::string description{};
  Poster poster{};
  std::int64_t year{};
  std::int64_t date{};
};
struct FeedRating {
  std::int64_t counters{};
  double average{};
};

struct FeedMinAge {
  std::int64_t value{};
  std::string title{};
  std::string title_long{};
};

struct FeedUserListInfo {
  std::string title{};
  std::string href{};
  std::int64_t id{};
};

struct FeedUserList {
  bool is_fav{};
  FeedUserListInfo list{};
};

struct FeedUser {
  FeedUserList list{};
  double rating{};
};

struct FeedRemoteIds {
  std::int64_t worldart_id{};
  std::string worldart_type{};
  std::int64_t kp_id{};
  std::int64_t anidub_id{};
  std::int64_t sr_id{};
  std::string anilibria_alias{};
  std::int64_t shikimori_id{};
  std::int64_t myanimelist_id{};
};
struct FeedAnimeItem {
  std::int64_t anime_id{};
  AnimeStatus anime_status{};
  std::string anime_url{};
  Poster poster{};
  FeedRating rating{};
  std::string title{};
  AnimeType type{};
  std::int64_t year{};
  std::string description{};
  std::int64_t views{};
  std::int64_t season{};
  FeedMinAge min_age{};
  FeedUser user{};
  FeedRemoteIds remote_ids{};
  Top top{};
  std::vector<std::string> blocked_in{};
};
struct FeedVideoItem {
  std::string title{};
  std::int64_t date{};
  std::string description{};
  std::string ep_title{};
  std::string player_title{};
  Poster poster{};
  std::string anime_url{};
  std::string dub_title{};
  std::int64_t anime_id{};
  std::int64_t video_id{};
};
struct FeedTopCarousel {
  std::int64_t season{};
  std::int64_t year{};
  std::vector<FeedAnimeItem> items{};
};

struct FeedLastWatchScreenshot {
  std::int64_t id{};
  std::int64_t time{};
  std::string episode{};
  ScreenshotSizes sizes{};
};

struct FeedLastWatchItem {
  std::int64_t date{};
  std::int64_t end_time{};
  std::string anime_url{};
  Poster poster{};
  std::int64_t video_id{};
  std::string description{};
  std::string title{};
  std::int64_t anime_id{};
  bool do_not_recommend{};
  std::string ep_title{};
  std::int64_t duration{};
  FeedLastWatchScreenshot screenshot{};
};
struct FeedScheduleEpisodes {
  std::int64_t aired{};
  std::int64_t count{};
  std::int64_t next_date{};
  std::int64_t prev_date{};
};

struct FeedScheduleItem {
  std::string title{};
  std::string description{};
  Poster poster{};
  std::string anime_url{};
  std::int64_t anime_id{};
  FeedScheduleEpisodes episodes{};
};
struct FeedUserIdsShikimori {
  std::int64_t id{};
  std::string nickname{};
};

struct FeedUserIds {
  FeedUserIdsShikimori shikimori{};
  std::string tg_nickname{};
  std::int64_t vk{};
};

struct FeedUserAvatars {
  std::string big{};
  std::string full{};
  std::string small{};
};

struct FeedUserBanner {
  std::string cropped{};
  std::string full{};
};

struct FeedUserTexts {
  std::int64_t color{};
  std::string left{};
  std::string right{};
};

struct FeedUserProfile {
  std::int64_t id{};
  std::string nickname{};
  std::string about{};
  bool banned{};
  FeedUserIds ids{};
  FeedUserAvatars avatars{};
  std::int64_t bdate{};
  std::int64_t last_online{};
  std::int64_t sex{};
  std::vector<std::string> roles{};
  std::int64_t register_date{};
  FeedUserTexts texts{};
  FeedUserBanner banner{};
};
struct FeedPostCategory {
  std::int64_t id{};
  std::string title{};
  std::string uri{};
};

struct FeedPostItem {
  std::string title{};
  std::string preview_image{};
  std::int64_t id{};
  std::string content_preview{};
  FeedUserProfile user{};
  FeedPostCategory category{};
  std::int64_t created_at{};
};
struct FeedPostType {
  std::string title{};
  std::int64_t id{};
  std::string uri{};
};

struct FeedPosts {
  std::vector<FeedPostItem> items{};
  std::vector<FeedPostType> types{};
};

struct FeedBloggerCategory {
  std::string id{};
  std::string title{};
};

struct FeedBloggerVideoDescriptions {
  std::string big{};
  std::string small{};
};

struct FeedBloggerVideoPreviews {
  std::string small{};
  std::string big{};
};

struct FeedBloggerVideoLikes {
  std::int64_t likes{};
  std::int64_t dislikes{};
  std::int64_t vote{};
};

struct FeedBloggerVideoItem {
  std::string title{};
  std::int64_t id{};
  std::int64_t time{};
  FeedBloggerCategory category{};
  std::int64_t publish_date{};
  std::string iframe_url{};
  bool has_spoiler{};
  FeedBloggerVideoDescriptions descriptions{};
  std::int64_t comments_count{};
  FeedUserProfile creator{};
  FeedBloggerVideoPreviews previews{};
  FeedBloggerVideoLikes likes{};
  std::int64_t views{};
  std::string language{};
};
struct FeedBloggerVideos {
  std::vector<FeedBloggerVideoItem> items{};
  std::vector<FeedBloggerCategory> categories{};
};

struct FeedBloggerPeople {
  std::vector<FeedUserProfile> items{};
  std::int64_t count{};
};

struct FeedBlogger {
  FeedBloggerVideos videos{};
  FeedBloggerPeople people{};
};

struct FeedCollectionLikes {
  std::int64_t likes{};
  std::int64_t dislikes{};
  std::int64_t vote{};
};

struct FeedCollectionOwner {
  std::int64_t id{};
  std::string nickname{};
  FeedUserAvatars avatars{};
};

struct FeedCollection {
  std::string title{};
  std::string description{};
  std::int64_t id{};
  FeedCollectionLikes likes{};
  std::string language{};
  std::vector<FeedAnimeItem> animes{};
  FeedCollectionOwner owner{};
  std::int64_t create_date{};
  std::int64_t views{};
  bool public_{};
  std::vector<Poster> poster_previews{};
};
struct FeedResponse {
  std::vector<FeedAnimeItem> new_{};
  std::vector<FeedAnimeItem> announcements{};
  std::vector<FeedAnimeItem> recommends{};
  std::vector<FeedVideoItem> new_videos{};
  FeedTopCarousel top_carousel{};
  std::vector<FeedLastWatchItem> last_watches{};
  std::vector<FeedScheduleItem> schedule{};
  FeedPosts posts{};
  FeedBlogger blogger{};
  std::vector<FeedCollection> collections{};
};
struct SearchResult {
  std::int64_t anime_id{};
  AnimeStatus anime_status{};
  std::string anime_url{};
  std::string description{};
  MinAge min_age{};
  Poster poster{};
  Rating rating{};
  RemoteIds remote_ids{};
  std::int64_t season{};
  std::string title{};
  Top top{};
  AnimeType type{};
  std::int64_t views{};
  std::int64_t year{};
};
struct AnimeResult {
  std::int64_t anime_id{};
  AnimeStatus anime_status{};
  std::string anime_url{};
  Poster poster{};
  Rating rating{};
  std::string title{};
  AnimeType type{};
  std::int64_t year{};
  std::string description{};
  std::int64_t views{};
  std::int64_t season{};
  MinAge min_age{};
  RemoteIds remote_ids{};
  std::string original{};
  std::vector<std::string> other_titles{};
  std::vector<Creator> creators{};
  std::vector<Studio> studios{};
  std::vector<VideoInfo> videos{};
  std::vector<Genre> genres{};
  std::vector<ViewingOrder> viewing_order{};
  std::vector<Translates> translates{};
  std::vector<std::string> blocked_in{};
  Episodes episodes{};
  std::int64_t comments_count{};
  std::int64_t reviews_count{};
  std::vector<RandomScreenshot> random_screenshots{};
  Top top{};
  std::int64_t posts_count{};
};
struct SearchInput {
  std::vector<std::int64_t> studio_ids{};
  std::string q{};
  std::vector<std::int64_t> director_ids{};
  std::vector<std::int64_t> worldart_ids{};
  std::vector<std::int64_t> mal_ids{};
  std::vector<std::int64_t> kp_ids{};
  std::vector<std::int64_t> shikimori_ids{};
  std::vector<std::int64_t> min_age{};
  std::int64_t max_rating_counters{};
  std::int64_t min_rating_counters{};
  std::vector<std::int64_t> ids{};
  std::vector<std::string> season{};
  std::vector<std::string> status{};
  std::vector<std::string> types{};
  std::vector<std::string> exclude_genres{};
  std::vector<std::string> genres{};
  double max_rating{};
  double min_rating{};
  std::int64_t ep_to{};
  std::int64_t ep_from{};
  std::int64_t to_year{};
  bool sort_forward{};
  std::string sort{};
  std::int64_t offset{};
  std::int64_t limit{};
};

} // namespace yani

namespace yani {

template <typename T> struct yani_null_value {
  static T get() { return T{}; }
};

template <> struct yani_null_value<std::string> {
  static std::string get() { return ""; }
};

template <> struct yani_null_value<std::int64_t> {
  static std::int64_t get() { return 1LL << 63; }
};

template <> struct yani_null_value<double> {
  static double get() { return -1.0; }
};

template <> struct yani_null_value<bool> {
  static bool get() { return true; }
};

template <typename T> struct yani_null_value<std::vector<T>> {
  static std::vector<T> get() { return {}; }
};

template <typename T> T yani_get(const nlohmann::json &j, const char *name) {
  const auto &value = j.at(name);

  if (value.is_null())
    return yani_null_value<T>::get();

  return value.get<T>();
}

inline void from_json(const nlohmann::json &j, AnimeStatus &obj) {
  obj.title = yani_get<std::string>(j, "title");
  obj.class_ = yani_get<std::string>(j, "class");
  obj.alias = yani_get<std::string>(j, "alias");
  obj.value = yani_get<std::int64_t>(j, "value");
}

inline void from_json(const nlohmann::json &j, Poster &obj) {
  obj.big = yani_get<std::string>(j, "big");
  obj.fullsize = yani_get<std::string>(j, "fullsize");
  obj.huge = yani_get<std::string>(j, "huge");
  obj.medium = yani_get<std::string>(j, "medium");
  obj.small = yani_get<std::string>(j, "small");
}

inline void from_json(const nlohmann::json &j, Rating &obj) {
  obj.average = yani_get<double>(j, "average");
  obj.kp_rating = yani_get<double>(j, "kp_rating");
  obj.anidub_rating = yani_get<double>(j, "anidub_rating");
  obj.counters = yani_get<std::int64_t>(j, "counters");
  obj.myanimelist_rating = yani_get<double>(j, "myanimelist_rating");
  obj.shikimori_rating = yani_get<double>(j, "shikimori_rating");
  obj.worldart_rating = yani_get<double>(j, "worldart_rating");
}
inline void from_json(const nlohmann::json &j, AnimeType &obj) {
  obj.name = yani_get<std::string>(j, "name");
  obj.value = yani_get<std::int64_t>(j, "value");
  obj.shortname = yani_get<std::string>(j, "shortname");
}

inline void from_json(const nlohmann::json &j, MinAge &obj) {
  obj.value = yani_get<std::int64_t>(j, "value");
  obj.title = yani_get<std::string>(j, "title");
  obj.titleLong = yani_get<std::string>(j, "titleLong");
}

inline void from_json(const nlohmann::json &j, RemoteIds &obj) {
  obj.worldart_id = yani_get<std::int64_t>(j, "worldart_id");
  obj.worldart_type = yani_get<std::string>(j, "worldart_type");
  obj.kp_id = yani_get<std::int64_t>(j, "kp_id");
  obj.anidb_id = yani_get<std::int64_t>(j, "anidb_id");
  obj.sr_id = yani_get<std::int64_t>(j, "sr_id");
  obj.anilibria_alias = yani_get<std::string>(j, "anilibria_alias");
  obj.shikimori_id = yani_get<std::int64_t>(j, "shikimori_id");
  obj.myanimelist_id = yani_get<std::int64_t>(j, "myanimelist_id");
}
inline void from_json(const nlohmann::json &j, Creator &obj) {
  obj.title = yani_get<std::string>(j, "title");
  obj.id = yani_get<std::int64_t>(j, "id");
  obj.url = yani_get<std::string>(j, "url");
}

inline void from_json(const nlohmann::json &j, Studio &obj) {
  obj.title = yani_get<std::string>(j, "title");
  obj.id = yani_get<std::int64_t>(j, "id");
  obj.url = yani_get<std::string>(j, "url");
}

inline void from_json(const nlohmann::json &j, SkipData &obj) {
  obj.time = yani_get<std::string>(j, "time");
  obj.length = yani_get<std::string>(j, "length");
}

inline void from_json(const nlohmann::json &j, Skips &obj) {
  obj.opening = yani_get<SkipData>(j, "opening");
  obj.ending = yani_get<SkipData>(j, "ending");
}

inline void from_json(const nlohmann::json &j, VideoData &obj) {
  obj.dubbing = yani_get<std::string>(j, "dubbing");
  obj.player = yani_get<std::string>(j, "player");
}

inline void from_json(const nlohmann::json &j, VideoInfo &obj) {
  obj.video_id = yani_get<std::int64_t>(j, "video_id");
  obj.iframe_url = yani_get<std::string>(j, "iframe_url");
  obj.data = yani_get<VideoData>(j, "data");
  obj.number = yani_get<std::string>(j, "number");
  obj.date = yani_get<std::int64_t>(j, "date");
  obj.index = yani_get<std::int64_t>(j, "index");
  obj.skips = yani_get<Skips>(j, "skips");
}
inline void from_json(const nlohmann::json &j, Genre &obj) {
  obj.title = yani_get<std::string>(j, "title");
  obj.id = yani_get<std::int64_t>(j, "id");
  obj.alias = yani_get<std::string>(j, "alias");
  obj.url = yani_get<std::string>(j, "url");
}

inline void from_json(const nlohmann::json &j, Translates &obj) {
  obj.title = yani_get<std::string>(j, "title");
  obj.href = yani_get<std::string>(j, "href");
  obj.value = yani_get<std::string>(j, "value");
}

inline void from_json(const nlohmann::json &j, Episodes &obj) {
  obj.aired = yani_get<std::int64_t>(j, "aired");
  obj.count = yani_get<std::int64_t>(j, "count");
  obj.next_date = yani_get<std::int64_t>(j, "next_date");
}

inline void from_json(const nlohmann::json &j, ScreenshotSizes &obj) {
  obj.small = yani_get<std::string>(j, "small");
  obj.full = yani_get<std::string>(j, "full");
}

inline void from_json(const nlohmann::json &j, RandomScreenshot &obj) {
  obj.sizes = yani_get<ScreenshotSizes>(j, "sizes");
  obj.id = yani_get<std::int64_t>(j, "id");
  obj.time = yani_get<std::int64_t>(j, "time");
  obj.episode = yani_get<std::int64_t>(j, "episode");
}

inline void from_json(const nlohmann::json &j, Top &obj) {
  obj.category = yani_get<std::int64_t>(j, "category");
  obj.global = yani_get<std::int64_t>(j, "global");
}

inline void from_json(const nlohmann::json &j, ViewingOrder &obj) {
  obj.title = yani_get<std::string>(j, "title");
  obj.anime_id = yani_get<std::int64_t>(j, "anime_id");
  obj.type = yani_get<AnimeType>(j, "type");
  obj.anime_url = yani_get<std::string>(j, "anime_url");
  obj.anime_status = yani_get<AnimeStatus>(j, "anime_status");
  obj.description = yani_get<std::string>(j, "description");
  obj.poster = yani_get<Poster>(j, "poster");
  obj.year = yani_get<std::int64_t>(j, "year");
  obj.date = yani_get<std::int64_t>(j, "date");
}
inline void from_json(const nlohmann::json &j, FeedRating &obj) {
  obj.counters = yani_get<std::int64_t>(j, "counters");
  obj.average = yani_get<double>(j, "average");
}

inline void from_json(const nlohmann::json &j, FeedMinAge &obj) {
  obj.value = yani_get<std::int64_t>(j, "value");
  obj.title = yani_get<std::string>(j, "title");
  obj.title_long = yani_get<std::string>(j, "title_long");
}

inline void from_json(const nlohmann::json &j, FeedUserListInfo &obj) {
  obj.title = yani_get<std::string>(j, "title");
  obj.href = yani_get<std::string>(j, "href");
  obj.id = yani_get<std::int64_t>(j, "id");
}

inline void from_json(const nlohmann::json &j, FeedUserList &obj) {
  obj.is_fav = yani_get<bool>(j, "is_fav");
  obj.list = yani_get<FeedUserListInfo>(j, "list");
}

inline void from_json(const nlohmann::json &j, FeedUser &obj) {
  obj.list = yani_get<FeedUserList>(j, "list");
  obj.rating = yani_get<double>(j, "rating");
}

inline void from_json(const nlohmann::json &j, FeedRemoteIds &obj) {
  obj.worldart_id = yani_get<std::int64_t>(j, "worldart_id");
  obj.worldart_type = yani_get<std::string>(j, "worldart_type");
  obj.kp_id = yani_get<std::int64_t>(j, "kp_id");
  obj.anidub_id = yani_get<std::int64_t>(j, "anidub_id");
  obj.sr_id = yani_get<std::int64_t>(j, "sr_id");
  obj.anilibria_alias = yani_get<std::string>(j, "anilibria_alias");
  obj.shikimori_id = yani_get<std::int64_t>(j, "shikimori_id");
  obj.myanimelist_id = yani_get<std::int64_t>(j, "myanimelist_id");
}
inline void from_json(const nlohmann::json &j, FeedAnimeItem &obj) {
  obj.anime_id = yani_get<std::int64_t>(j, "anime_id");
  obj.anime_status = yani_get<AnimeStatus>(j, "anime_status");
  obj.anime_url = yani_get<std::string>(j, "anime_url");
  obj.poster = yani_get<Poster>(j, "poster");
  obj.rating = yani_get<FeedRating>(j, "rating");
  obj.title = yani_get<std::string>(j, "title");
  obj.type = yani_get<AnimeType>(j, "type");
  obj.year = yani_get<std::int64_t>(j, "year");
  obj.description = yani_get<std::string>(j, "description");
  obj.views = yani_get<std::int64_t>(j, "views");
  obj.season = yani_get<std::int64_t>(j, "season");
  obj.min_age = yani_get<FeedMinAge>(j, "min_age");
  obj.user = yani_get<FeedUser>(j, "user");
  obj.remote_ids = yani_get<FeedRemoteIds>(j, "remote_ids");
  obj.top = yani_get<Top>(j, "top");
  obj.blocked_in = yani_get<std::vector<std::string>>(j, "blocked_in");
}
inline void from_json(const nlohmann::json &j, FeedVideoItem &obj) {
  obj.title = yani_get<std::string>(j, "title");
  obj.date = yani_get<std::int64_t>(j, "date");
  obj.description = yani_get<std::string>(j, "description");
  obj.ep_title = yani_get<std::string>(j, "ep_title");
  obj.player_title = yani_get<std::string>(j, "player_title");
  obj.poster = yani_get<Poster>(j, "poster");
  obj.anime_url = yani_get<std::string>(j, "anime_url");
  obj.dub_title = yani_get<std::string>(j, "dub_title");
  obj.anime_id = yani_get<std::int64_t>(j, "anime_id");
  obj.video_id = yani_get<std::int64_t>(j, "video_id");
}
inline void from_json(const nlohmann::json &j, FeedTopCarousel &obj) {
  obj.season = yani_get<std::int64_t>(j, "season");
  obj.year = yani_get<std::int64_t>(j, "year");
  obj.items = yani_get<std::vector<FeedAnimeItem>>(j, "items");
}

inline void from_json(const nlohmann::json &j, FeedLastWatchScreenshot &obj) {
  obj.id = yani_get<std::int64_t>(j, "id");
  obj.time = yani_get<std::int64_t>(j, "time");
  obj.episode = yani_get<std::string>(j, "episode");
  obj.sizes = yani_get<ScreenshotSizes>(j, "sizes");
}

inline void from_json(const nlohmann::json &j, FeedLastWatchItem &obj) {
  obj.date = yani_get<std::int64_t>(j, "date");
  obj.end_time = yani_get<std::int64_t>(j, "end_time");
  obj.anime_url = yani_get<std::string>(j, "anime_url");
  obj.poster = yani_get<Poster>(j, "poster");
  obj.video_id = yani_get<std::int64_t>(j, "video_id");
  obj.description = yani_get<std::string>(j, "description");
  obj.title = yani_get<std::string>(j, "title");
  obj.anime_id = yani_get<std::int64_t>(j, "anime_id");
  obj.do_not_recommend = yani_get<bool>(j, "do_not_recommend");
  obj.ep_title = yani_get<std::string>(j, "ep_title");
  obj.duration = yani_get<std::int64_t>(j, "duration");
  obj.screenshot = yani_get<FeedLastWatchScreenshot>(j, "screenshot");
}
inline void from_json(const nlohmann::json &j, FeedScheduleEpisodes &obj) {
  obj.aired = yani_get<std::int64_t>(j, "aired");
  obj.count = yani_get<std::int64_t>(j, "count");
  obj.next_date = yani_get<std::int64_t>(j, "next_date");
  obj.prev_date = yani_get<std::int64_t>(j, "prev_date");
}

inline void from_json(const nlohmann::json &j, FeedScheduleItem &obj) {
  obj.title = yani_get<std::string>(j, "title");
  obj.description = yani_get<std::string>(j, "description");
  obj.poster = yani_get<Poster>(j, "poster");
  obj.anime_url = yani_get<std::string>(j, "anime_url");
  obj.anime_id = yani_get<std::int64_t>(j, "anime_id");
  obj.episodes = yani_get<FeedScheduleEpisodes>(j, "episodes");
}
inline void from_json(const nlohmann::json &j, FeedUserIdsShikimori &obj) {
  obj.id = yani_get<std::int64_t>(j, "id");
  obj.nickname = yani_get<std::string>(j, "nickname");
}

inline void from_json(const nlohmann::json &j, FeedUserIds &obj) {
  obj.shikimori = yani_get<FeedUserIdsShikimori>(j, "shikimori");
  obj.tg_nickname = yani_get<std::string>(j, "tg_nickname");
  obj.vk = yani_get<std::int64_t>(j, "vk");
}

inline void from_json(const nlohmann::json &j, FeedUserAvatars &obj) {
  obj.big = yani_get<std::string>(j, "big");
  obj.full = yani_get<std::string>(j, "full");
  obj.small = yani_get<std::string>(j, "small");
}

inline void from_json(const nlohmann::json &j, FeedUserBanner &obj) {
  obj.cropped = yani_get<std::string>(j, "cropped");
  obj.full = yani_get<std::string>(j, "full");
}

inline void from_json(const nlohmann::json &j, FeedUserTexts &obj) {
  obj.color = yani_get<std::int64_t>(j, "color");
  obj.left = yani_get<std::string>(j, "left");
  obj.right = yani_get<std::string>(j, "right");
}

inline void from_json(const nlohmann::json &j, FeedUserProfile &obj) {
  obj.id = yani_get<std::int64_t>(j, "id");
  obj.nickname = yani_get<std::string>(j, "nickname");
  obj.about = yani_get<std::string>(j, "about");
  obj.banned = yani_get<bool>(j, "banned");
  obj.ids = yani_get<FeedUserIds>(j, "ids");
  obj.avatars = yani_get<FeedUserAvatars>(j, "avatars");
  obj.bdate = yani_get<std::int64_t>(j, "bdate");
  obj.last_online = yani_get<std::int64_t>(j, "last_online");
  obj.sex = yani_get<std::int64_t>(j, "sex");
  obj.roles = yani_get<std::vector<std::string>>(j, "roles");
  obj.register_date = yani_get<std::int64_t>(j, "register_date");
  obj.texts = yani_get<FeedUserTexts>(j, "texts");
  obj.banner = yani_get<FeedUserBanner>(j, "banner");
}
inline void from_json(const nlohmann::json &j, FeedPostCategory &obj) {
  obj.id = yani_get<std::int64_t>(j, "id");
  obj.title = yani_get<std::string>(j, "title");
  obj.uri = yani_get<std::string>(j, "uri");
}

inline void from_json(const nlohmann::json &j, FeedPostItem &obj) {
  obj.title = yani_get<std::string>(j, "title");
  obj.preview_image = yani_get<std::string>(j, "preview_image");
  obj.id = yani_get<std::int64_t>(j, "id");
  obj.content_preview = yani_get<std::string>(j, "content_preview");
  obj.user = yani_get<FeedUserProfile>(j, "user");
  obj.category = yani_get<FeedPostCategory>(j, "category");
  obj.created_at = yani_get<std::int64_t>(j, "created_at");
}
inline void from_json(const nlohmann::json &j, FeedPostType &obj) {
  obj.title = yani_get<std::string>(j, "title");
  obj.id = yani_get<std::int64_t>(j, "id");
  obj.uri = yani_get<std::string>(j, "uri");
}

inline void from_json(const nlohmann::json &j, FeedPosts &obj) {
  obj.items = yani_get<std::vector<FeedPostItem>>(j, "items");
  obj.types = yani_get<std::vector<FeedPostType>>(j, "types");
}

inline void from_json(const nlohmann::json &j, FeedBloggerCategory &obj) {
  obj.id = yani_get<std::string>(j, "id");
  obj.title = yani_get<std::string>(j, "title");
}

inline void from_json(const nlohmann::json &j,
                      FeedBloggerVideoDescriptions &obj) {
  obj.big = yani_get<std::string>(j, "big");
  obj.small = yani_get<std::string>(j, "small");
}

inline void from_json(const nlohmann::json &j, FeedBloggerVideoPreviews &obj) {
  obj.small = yani_get<std::string>(j, "small");
  obj.big = yani_get<std::string>(j, "big");
}

inline void from_json(const nlohmann::json &j, FeedBloggerVideoLikes &obj) {
  obj.likes = yani_get<std::int64_t>(j, "likes");
  obj.dislikes = yani_get<std::int64_t>(j, "dislikes");
  obj.vote = yani_get<std::int64_t>(j, "vote");
}

inline void from_json(const nlohmann::json &j, FeedBloggerVideoItem &obj) {
  obj.title = yani_get<std::string>(j, "title");
  obj.id = yani_get<std::int64_t>(j, "id");
  obj.time = yani_get<std::int64_t>(j, "time");
  obj.category = yani_get<FeedBloggerCategory>(j, "category");
  obj.publish_date = yani_get<std::int64_t>(j, "publish_date");
  obj.iframe_url = yani_get<std::string>(j, "iframe_url");
  obj.has_spoiler = yani_get<bool>(j, "has_spoiler");
  obj.descriptions = yani_get<FeedBloggerVideoDescriptions>(j, "descriptions");
  obj.comments_count = yani_get<std::int64_t>(j, "comments_count");
  obj.creator = yani_get<FeedUserProfile>(j, "creator");
  obj.previews = yani_get<FeedBloggerVideoPreviews>(j, "previews");
  obj.likes = yani_get<FeedBloggerVideoLikes>(j, "likes");
  obj.views = yani_get<std::int64_t>(j, "views");
  obj.language = yani_get<std::string>(j, "language");
}
inline void from_json(const nlohmann::json &j, FeedBloggerVideos &obj) {
  obj.items = yani_get<std::vector<FeedBloggerVideoItem>>(j, "items");
  obj.categories = yani_get<std::vector<FeedBloggerCategory>>(j, "categories");
}

inline void from_json(const nlohmann::json &j, FeedBloggerPeople &obj) {
  obj.items = yani_get<std::vector<FeedUserProfile>>(j, "items");
  obj.count = yani_get<std::int64_t>(j, "count");
}

inline void from_json(const nlohmann::json &j, FeedBlogger &obj) {
  obj.videos = yani_get<FeedBloggerVideos>(j, "videos");
  obj.people = yani_get<FeedBloggerPeople>(j, "people");
}

inline void from_json(const nlohmann::json &j, FeedCollectionLikes &obj) {
  obj.likes = yani_get<std::int64_t>(j, "likes");
  obj.dislikes = yani_get<std::int64_t>(j, "dislikes");
  obj.vote = yani_get<std::int64_t>(j, "vote");
}

inline void from_json(const nlohmann::json &j, FeedCollectionOwner &obj) {
  obj.id = yani_get<std::int64_t>(j, "id");
  obj.nickname = yani_get<std::string>(j, "nickname");
  obj.avatars = yani_get<FeedUserAvatars>(j, "avatars");
}

inline void from_json(const nlohmann::json &j, FeedCollection &obj) {
  obj.title = yani_get<std::string>(j, "title");
  obj.description = yani_get<std::string>(j, "description");
  obj.id = yani_get<std::int64_t>(j, "id");
  obj.likes = yani_get<FeedCollectionLikes>(j, "likes");
  obj.language = yani_get<std::string>(j, "language");
  obj.animes = yani_get<std::vector<FeedAnimeItem>>(j, "animes");
  obj.owner = yani_get<FeedCollectionOwner>(j, "owner");
  obj.create_date = yani_get<std::int64_t>(j, "create_date");
  obj.views = yani_get<std::int64_t>(j, "views");
  obj.public_ = yani_get<bool>(j, "public");
  obj.poster_previews = yani_get<std::vector<Poster>>(j, "poster_previews");
}
inline void from_json(const nlohmann::json &j, FeedResponse &obj) {
  obj.new_ = yani_get<std::vector<FeedAnimeItem>>(j, "new");
  obj.announcements = yani_get<std::vector<FeedAnimeItem>>(j, "announcements");
  obj.recommends = yani_get<std::vector<FeedAnimeItem>>(j, "recommends");
  obj.new_videos = yani_get<std::vector<FeedVideoItem>>(j, "new_videos");
  obj.top_carousel = yani_get<FeedTopCarousel>(j, "top_carousel");
  obj.last_watches =
      yani_get<std::vector<FeedLastWatchItem>>(j, "last_watches");
  obj.schedule = yani_get<std::vector<FeedScheduleItem>>(j, "schedule");
  obj.posts = yani_get<FeedPosts>(j, "posts");
  obj.blogger = yani_get<FeedBlogger>(j, "blogger");
  obj.collections = yani_get<std::vector<FeedCollection>>(j, "collections");
}
inline void from_json(const nlohmann::json &j, SearchResult &obj) {
  obj.anime_id = yani_get<std::int64_t>(j, "anime_id");
  obj.anime_status = yani_get<AnimeStatus>(j, "anime_status");
  obj.anime_url = yani_get<std::string>(j, "anime_url");
  obj.description = yani_get<std::string>(j, "description");
  obj.min_age = yani_get<MinAge>(j, "min_age");
  obj.poster = yani_get<Poster>(j, "poster");
  obj.rating = yani_get<Rating>(j, "rating");
  obj.remote_ids = yani_get<RemoteIds>(j, "remote_ids");
  obj.season = yani_get<std::int64_t>(j, "season");
  obj.title = yani_get<std::string>(j, "title");
  obj.top = yani_get<Top>(j, "top");
  obj.type = yani_get<AnimeType>(j, "type");
  obj.views = yani_get<std::int64_t>(j, "views");
  obj.year = yani_get<std::int64_t>(j, "year");
}
inline void from_json(const nlohmann::json &j, AnimeResult &obj) {
  obj.anime_id = yani_get<std::int64_t>(j, "anime_id");
  obj.anime_status = yani_get<AnimeStatus>(j, "anime_status");
  obj.anime_url = yani_get<std::string>(j, "anime_url");
  obj.poster = yani_get<Poster>(j, "poster");
  obj.rating = yani_get<Rating>(j, "rating");
  obj.title = yani_get<std::string>(j, "title");
  obj.type = yani_get<AnimeType>(j, "type");
  obj.year = yani_get<std::int64_t>(j, "year");
  obj.description = yani_get<std::string>(j, "description");
  obj.views = yani_get<std::int64_t>(j, "views");
  obj.season = yani_get<std::int64_t>(j, "season");
  obj.min_age = yani_get<MinAge>(j, "min_age");
  obj.remote_ids = yani_get<RemoteIds>(j, "remote_ids");
  obj.original = yani_get<std::string>(j, "original");
  obj.other_titles = yani_get<std::vector<std::string>>(j, "other_titles");
  obj.creators = yani_get<std::vector<Creator>>(j, "creators");
  obj.studios = yani_get<std::vector<Studio>>(j, "studios");
  obj.videos = yani_get<std::vector<VideoInfo>>(j, "videos");
  obj.genres = yani_get<std::vector<Genre>>(j, "genres");
  obj.viewing_order = yani_get<std::vector<ViewingOrder>>(j, "viewing_order");
  obj.translates = yani_get<std::vector<Translates>>(j, "translates");
  obj.blocked_in = yani_get<std::vector<std::string>>(j, "blocked_in");
  obj.episodes = yani_get<Episodes>(j, "episodes");
  obj.comments_count = yani_get<std::int64_t>(j, "comments_count");
  obj.reviews_count = yani_get<std::int64_t>(j, "reviews_count");
  obj.random_screenshots =
      yani_get<std::vector<RandomScreenshot>>(j, "random_screenshots");
  obj.top = yani_get<Top>(j, "top");
  obj.posts_count = yani_get<std::int64_t>(j, "posts_count");
}
inline void from_json(const nlohmann::json &j, SearchInput &obj) {
  obj.studio_ids = yani_get<std::vector<std::int64_t>>(j, "studio_ids");
  obj.q = yani_get<std::string>(j, "q");
  obj.director_ids = yani_get<std::vector<std::int64_t>>(j, "director_ids");
  obj.worldart_ids = yani_get<std::vector<std::int64_t>>(j, "worldart_ids");
  obj.mal_ids = yani_get<std::vector<std::int64_t>>(j, "mal_ids");
  obj.kp_ids = yani_get<std::vector<std::int64_t>>(j, "kp_ids");
  obj.shikimori_ids = yani_get<std::vector<std::int64_t>>(j, "shikimori_ids");
  obj.min_age = yani_get<std::vector<std::int64_t>>(j, "min_age");
  obj.max_rating_counters = yani_get<std::int64_t>(j, "max_rating_counters");
  obj.min_rating_counters = yani_get<std::int64_t>(j, "min_rating_counters");
  obj.ids = yani_get<std::vector<std::int64_t>>(j, "ids");
  obj.season = yani_get<std::vector<std::string>>(j, "season");
  obj.status = yani_get<std::vector<std::string>>(j, "status");
  obj.types = yani_get<std::vector<std::string>>(j, "types");
  obj.exclude_genres = yani_get<std::vector<std::string>>(j, "exclude_genres");
  obj.genres = yani_get<std::vector<std::string>>(j, "genres");
  obj.max_rating = yani_get<double>(j, "max_rating");
  obj.min_rating = yani_get<double>(j, "min_rating");
  obj.ep_to = yani_get<std::int64_t>(j, "ep_to");
  obj.ep_from = yani_get<std::int64_t>(j, "ep_from");
  obj.to_year = yani_get<std::int64_t>(j, "to_year");
  obj.sort_forward = yani_get<bool>(j, "sort_forward");
  obj.sort = yani_get<std::string>(j, "sort");
  obj.offset = yani_get<std::int64_t>(j, "offset");
  obj.limit = yani_get<std::int64_t>(j, "limit");
}

inline void to_json(nlohmann::json &j, const AnimeStatus &obj) {
  j["title"] = obj.title;
  j["class"] = obj.class_;
  j["alias"] = obj.alias;
  j["value"] = obj.value;
}

inline void to_json(nlohmann::json &j, const Poster &obj) {
  j["big"] = obj.big;
  j["fullsize"] = obj.fullsize;
  j["huge"] = obj.huge;
  j["medium"] = obj.medium;
  j["small"] = obj.small;
}

inline void to_json(nlohmann::json &j, const Rating &obj) {
  j["average"] = obj.average;
  j["kp_rating"] = obj.kp_rating;
  j["anidub_rating"] = obj.anidub_rating;
  j["counters"] = obj.counters;
  j["myanimelist_rating"] = obj.myanimelist_rating;
  j["shikimori_rating"] = obj.shikimori_rating;
  j["worldart_rating"] = obj.worldart_rating;
}
inline void to_json(nlohmann::json &j, const AnimeType &obj) {
  j["name"] = obj.name;
  j["value"] = obj.value;
  j["shortname"] = obj.shortname;
}

inline void to_json(nlohmann::json &j, const MinAge &obj) {
  j["value"] = obj.value;
  j["title"] = obj.title;
  j["titleLong"] = obj.titleLong;
}

inline void to_json(nlohmann::json &j, const RemoteIds &obj) {
  j["worldart_id"] = obj.worldart_id;
  j["worldart_type"] = obj.worldart_type;
  j["kp_id"] = obj.kp_id;
  j["anidb_id"] = obj.anidb_id;
  j["sr_id"] = obj.sr_id;
  j["anilibria_alias"] = obj.anilibria_alias;
  j["shikimori_id"] = obj.shikimori_id;
  j["myanimelist_id"] = obj.myanimelist_id;
}
inline void to_json(nlohmann::json &j, const Creator &obj) {
  j["title"] = obj.title;
  j["id"] = obj.id;
  j["url"] = obj.url;
}

inline void to_json(nlohmann::json &j, const Studio &obj) {
  j["title"] = obj.title;
  j["id"] = obj.id;
  j["url"] = obj.url;
}

inline void to_json(nlohmann::json &j, const SkipData &obj) {
  j["time"] = obj.time;
  j["length"] = obj.length;
}

inline void to_json(nlohmann::json &j, const Skips &obj) {
  j["opening"] = obj.opening;
  j["ending"] = obj.ending;
}

inline void to_json(nlohmann::json &j, const VideoData &obj) {
  j["dubbing"] = obj.dubbing;
  j["player"] = obj.player;
}

inline void to_json(nlohmann::json &j, const VideoInfo &obj) {
  j["video_id"] = obj.video_id;
  j["iframe_url"] = obj.iframe_url;
  j["data"] = obj.data;
  j["number"] = obj.number;
  j["date"] = obj.date;
  j["index"] = obj.index;
  j["skips"] = obj.skips;
}
inline void to_json(nlohmann::json &j, const Genre &obj) {
  j["title"] = obj.title;
  j["id"] = obj.id;
  j["alias"] = obj.alias;
  j["url"] = obj.url;
}

inline void to_json(nlohmann::json &j, const Translates &obj) {
  j["title"] = obj.title;
  j["href"] = obj.href;
  j["value"] = obj.value;
}

inline void to_json(nlohmann::json &j, const Episodes &obj) {
  j["aired"] = obj.aired;
  j["count"] = obj.count;
  j["next_date"] = obj.next_date;
}

inline void to_json(nlohmann::json &j, const ScreenshotSizes &obj) {
  j["small"] = obj.small;
  j["full"] = obj.full;
}

inline void to_json(nlohmann::json &j, const RandomScreenshot &obj) {
  j["sizes"] = obj.sizes;
  j["id"] = obj.id;
  j["time"] = obj.time;
  j["episode"] = obj.episode;
}

inline void to_json(nlohmann::json &j, const Top &obj) {
  j["category"] = obj.category;
  j["global"] = obj.global;
}

inline void to_json(nlohmann::json &j, const ViewingOrder &obj) {
  j["title"] = obj.title;
  j["anime_id"] = obj.anime_id;
  j["type"] = obj.type;
  j["anime_url"] = obj.anime_url;
  j["anime_status"] = obj.anime_status;
  j["description"] = obj.description;
  j["poster"] = obj.poster;
  j["year"] = obj.year;
  j["date"] = obj.date;
}
inline void to_json(nlohmann::json &j, const FeedRating &obj) {
  j["counters"] = obj.counters;
  j["average"] = obj.average;
}

inline void to_json(nlohmann::json &j, const FeedMinAge &obj) {
  j["value"] = obj.value;
  j["title"] = obj.title;
  j["title_long"] = obj.title_long;
}

inline void to_json(nlohmann::json &j, const FeedUserListInfo &obj) {
  j["title"] = obj.title;
  j["href"] = obj.href;
  j["id"] = obj.id;
}

inline void to_json(nlohmann::json &j, const FeedUserList &obj) {
  j["is_fav"] = obj.is_fav;
  j["list"] = obj.list;
}

inline void to_json(nlohmann::json &j, const FeedUser &obj) {
  j["list"] = obj.list;
  j["rating"] = obj.rating;
}

inline void to_json(nlohmann::json &j, const FeedRemoteIds &obj) {
  j["worldart_id"] = obj.worldart_id;
  j["worldart_type"] = obj.worldart_type;
  j["kp_id"] = obj.kp_id;
  j["anidub_id"] = obj.anidub_id;
  j["sr_id"] = obj.sr_id;
  j["anilibria_alias"] = obj.anilibria_alias;
  j["shikimori_id"] = obj.shikimori_id;
  j["myanimelist_id"] = obj.myanimelist_id;
}
inline void to_json(nlohmann::json &j, const FeedAnimeItem &obj) {
  j["anime_id"] = obj.anime_id;
  j["anime_status"] = obj.anime_status;
  j["anime_url"] = obj.anime_url;
  j["poster"] = obj.poster;
  j["rating"] = obj.rating;
  j["title"] = obj.title;
  j["type"] = obj.type;
  j["year"] = obj.year;
  j["description"] = obj.description;
  j["views"] = obj.views;
  j["season"] = obj.season;
  j["min_age"] = obj.min_age;
  j["user"] = obj.user;
  j["remote_ids"] = obj.remote_ids;
  j["top"] = obj.top;
  j["blocked_in"] = obj.blocked_in;
}
inline void to_json(nlohmann::json &j, const FeedVideoItem &obj) {
  j["title"] = obj.title;
  j["date"] = obj.date;
  j["description"] = obj.description;
  j["ep_title"] = obj.ep_title;
  j["player_title"] = obj.player_title;
  j["poster"] = obj.poster;
  j["anime_url"] = obj.anime_url;
  j["dub_title"] = obj.dub_title;
  j["anime_id"] = obj.anime_id;
  j["video_id"] = obj.video_id;
}
inline void to_json(nlohmann::json &j, const FeedTopCarousel &obj) {
  j["season"] = obj.season;
  j["year"] = obj.year;
  j["items"] = obj.items;
}

inline void to_json(nlohmann::json &j, const FeedLastWatchScreenshot &obj) {
  j["id"] = obj.id;
  j["time"] = obj.time;
  j["episode"] = obj.episode;
  j["sizes"] = obj.sizes;
}

inline void to_json(nlohmann::json &j, const FeedLastWatchItem &obj) {
  j["date"] = obj.date;
  j["end_time"] = obj.end_time;
  j["anime_url"] = obj.anime_url;
  j["poster"] = obj.poster;
  j["video_id"] = obj.video_id;
  j["description"] = obj.description;
  j["title"] = obj.title;
  j["anime_id"] = obj.anime_id;
  j["do_not_recommend"] = obj.do_not_recommend;
  j["ep_title"] = obj.ep_title;
  j["duration"] = obj.duration;
  j["screenshot"] = obj.screenshot;
}
inline void to_json(nlohmann::json &j, const FeedScheduleEpisodes &obj) {
  j["aired"] = obj.aired;
  j["count"] = obj.count;
  j["next_date"] = obj.next_date;
  j["prev_date"] = obj.prev_date;
}

inline void to_json(nlohmann::json &j, const FeedScheduleItem &obj) {
  j["title"] = obj.title;
  j["description"] = obj.description;
  j["poster"] = obj.poster;
  j["anime_url"] = obj.anime_url;
  j["anime_id"] = obj.anime_id;
  j["episodes"] = obj.episodes;
}
inline void to_json(nlohmann::json &j, const FeedUserIdsShikimori &obj) {
  j["id"] = obj.id;
  j["nickname"] = obj.nickname;
}

inline void to_json(nlohmann::json &j, const FeedUserIds &obj) {
  j["shikimori"] = obj.shikimori;
  j["tg_nickname"] = obj.tg_nickname;
  j["vk"] = obj.vk;
}

inline void to_json(nlohmann::json &j, const FeedUserAvatars &obj) {
  j["big"] = obj.big;
  j["full"] = obj.full;
  j["small"] = obj.small;
}

inline void to_json(nlohmann::json &j, const FeedUserBanner &obj) {
  j["cropped"] = obj.cropped;
  j["full"] = obj.full;
}

inline void to_json(nlohmann::json &j, const FeedUserTexts &obj) {
  j["color"] = obj.color;
  j["left"] = obj.left;
  j["right"] = obj.right;
}

inline void to_json(nlohmann::json &j, const FeedUserProfile &obj) {
  j["id"] = obj.id;
  j["nickname"] = obj.nickname;
  j["about"] = obj.about;
  j["banned"] = obj.banned;
  j["ids"] = obj.ids;
  j["avatars"] = obj.avatars;
  j["bdate"] = obj.bdate;
  j["last_online"] = obj.last_online;
  j["sex"] = obj.sex;
  j["roles"] = obj.roles;
  j["register_date"] = obj.register_date;
  j["texts"] = obj.texts;
  j["banner"] = obj.banner;
}
inline void to_json(nlohmann::json &j, const FeedPostCategory &obj) {
  j["id"] = obj.id;
  j["title"] = obj.title;
  j["uri"] = obj.uri;
}

inline void to_json(nlohmann::json &j, const FeedPostItem &obj) {
  j["title"] = obj.title;
  j["preview_image"] = obj.preview_image;
  j["id"] = obj.id;
  j["content_preview"] = obj.content_preview;
  j["user"] = obj.user;
  j["category"] = obj.category;
  j["created_at"] = obj.created_at;
}
inline void to_json(nlohmann::json &j, const FeedPostType &obj) {
  j["title"] = obj.title;
  j["id"] = obj.id;
  j["uri"] = obj.uri;
}

inline void to_json(nlohmann::json &j, const FeedPosts &obj) {
  j["items"] = obj.items;
  j["types"] = obj.types;
}

inline void to_json(nlohmann::json &j, const FeedBloggerCategory &obj) {
  j["id"] = obj.id;
  j["title"] = obj.title;
}

inline void to_json(nlohmann::json &j,
                    const FeedBloggerVideoDescriptions &obj) {
  j["big"] = obj.big;
  j["small"] = obj.small;
}

inline void to_json(nlohmann::json &j, const FeedBloggerVideoPreviews &obj) {
  j["small"] = obj.small;
  j["big"] = obj.big;
}

inline void to_json(nlohmann::json &j, const FeedBloggerVideoLikes &obj) {
  j["likes"] = obj.likes;
  j["dislikes"] = obj.dislikes;
  j["vote"] = obj.vote;
}

inline void to_json(nlohmann::json &j, const FeedBloggerVideoItem &obj) {
  j["title"] = obj.title;
  j["id"] = obj.id;
  j["time"] = obj.time;
  j["category"] = obj.category;
  j["publish_date"] = obj.publish_date;
  j["iframe_url"] = obj.iframe_url;
  j["has_spoiler"] = obj.has_spoiler;
  j["descriptions"] = obj.descriptions;
  j["comments_count"] = obj.comments_count;
  j["creator"] = obj.creator;
  j["previews"] = obj.previews;
  j["likes"] = obj.likes;
  j["views"] = obj.views;
  j["language"] = obj.language;
}
inline void to_json(nlohmann::json &j, const FeedBloggerVideos &obj) {
  j["items"] = obj.items;
  j["categories"] = obj.categories;
}

inline void to_json(nlohmann::json &j, const FeedBloggerPeople &obj) {
  j["items"] = obj.items;
  j["count"] = obj.count;
}

inline void to_json(nlohmann::json &j, const FeedBlogger &obj) {
  j["videos"] = obj.videos;
  j["people"] = obj.people;
}

inline void to_json(nlohmann::json &j, const FeedCollectionLikes &obj) {
  j["likes"] = obj.likes;
  j["dislikes"] = obj.dislikes;
  j["vote"] = obj.vote;
}

inline void to_json(nlohmann::json &j, const FeedCollectionOwner &obj) {
  j["id"] = obj.id;
  j["nickname"] = obj.nickname;
  j["avatars"] = obj.avatars;
}

inline void to_json(nlohmann::json &j, const FeedCollection &obj) {
  j["title"] = obj.title;
  j["description"] = obj.description;
  j["id"] = obj.id;
  j["likes"] = obj.likes;
  j["language"] = obj.language;
  j["animes"] = obj.animes;
  j["owner"] = obj.owner;
  j["create_date"] = obj.create_date;
  j["views"] = obj.views;
  j["public"] = obj.public_;
  j["poster_previews"] = obj.poster_previews;
}
inline void to_json(nlohmann::json &j, const FeedResponse &obj) {
  j["new"] = obj.new_;
  j["announcements"] = obj.announcements;
  j["recommends"] = obj.recommends;
  j["new_videos"] = obj.new_videos;
  j["top_carousel"] = obj.top_carousel;
  j["last_watches"] = obj.last_watches;
  j["schedule"] = obj.schedule;
  j["posts"] = obj.posts;
  j["blogger"] = obj.blogger;
  j["collections"] = obj.collections;
}
inline void to_json(nlohmann::json &j, const SearchResult &obj) {
  j["anime_id"] = obj.anime_id;
  j["anime_status"] = obj.anime_status;
  j["anime_url"] = obj.anime_url;
  j["description"] = obj.description;
  j["min_age"] = obj.min_age;
  j["poster"] = obj.poster;
  j["rating"] = obj.rating;
  j["remote_ids"] = obj.remote_ids;
  j["season"] = obj.season;
  j["title"] = obj.title;
  j["top"] = obj.top;
  j["type"] = obj.type;
  j["views"] = obj.views;
  j["year"] = obj.year;
}
inline void to_json(nlohmann::json &j, const AnimeResult &obj) {
  j["anime_id"] = obj.anime_id;
  j["anime_status"] = obj.anime_status;
  j["anime_url"] = obj.anime_url;
  j["poster"] = obj.poster;
  j["rating"] = obj.rating;
  j["title"] = obj.title;
  j["type"] = obj.type;
  j["year"] = obj.year;
  j["description"] = obj.description;
  j["views"] = obj.views;
  j["season"] = obj.season;
  j["min_age"] = obj.min_age;
  j["remote_ids"] = obj.remote_ids;
  j["original"] = obj.original;
  j["other_titles"] = obj.other_titles;
  j["creators"] = obj.creators;
  j["studios"] = obj.studios;
  j["videos"] = obj.videos;
  j["genres"] = obj.genres;
  j["viewing_order"] = obj.viewing_order;
  j["translates"] = obj.translates;
  j["blocked_in"] = obj.blocked_in;
  j["episodes"] = obj.episodes;
  j["comments_count"] = obj.comments_count;
  j["reviews_count"] = obj.reviews_count;
  j["random_screenshots"] = obj.random_screenshots;
  j["top"] = obj.top;
  j["posts_count"] = obj.posts_count;
}
inline void to_json(nlohmann::json &j, const SearchInput &obj) {
  j["studio_ids"] = obj.studio_ids;
  j["q"] = obj.q;
  j["director_ids"] = obj.director_ids;
  j["worldart_ids"] = obj.worldart_ids;
  j["mal_ids"] = obj.mal_ids;
  j["kp_ids"] = obj.kp_ids;
  j["shikimori_ids"] = obj.shikimori_ids;
  j["min_age"] = obj.min_age;
  j["max_rating_counters"] = obj.max_rating_counters;
  j["min_rating_counters"] = obj.min_rating_counters;
  j["ids"] = obj.ids;
  j["season"] = obj.season;
  j["status"] = obj.status;
  j["types"] = obj.types;
  j["exclude_genres"] = obj.exclude_genres;
  j["genres"] = obj.genres;
  j["max_rating"] = obj.max_rating;
  j["min_rating"] = obj.min_rating;
  j["ep_to"] = obj.ep_to;
  j["ep_from"] = obj.ep_from;
  j["to_year"] = obj.to_year;
  j["sort_forward"] = obj.sort_forward;
  j["sort"] = obj.sort;
  j["offset"] = obj.offset;
  j["limit"] = obj.limit;
}

} // namespace yani
