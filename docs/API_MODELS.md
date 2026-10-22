# Справочник API-моделей

В этом файле описаны все 56 структур, объявленные через `YANI_MODEL` в
`src/api/models.def`. Он описывает форму внешнего API и C++-модели в `src/api/`.
Это **не** полная схема каждого IPC-ответа: перед отправкой Anime сервер
заменяет `videos` на сгруппированные `players` (см. ниже и
[IPC-спецификацию](../src/server/IPC.md#anime-и-video-json)).

Для каждого поля таблица показывает C++-имя, C++-тип и имя JSON API. В обычном
случае имя одинаковое; явное отличие задано через `YANI_FIELD_JSON` в
`models.def`. Все поля создаются со значением по умолчанию. Если поле отсутствует
или равно null, применяются правила `yani_null_value<T>` из `src/api/json.hpp`.

## Содержание

- [AnimeStatus](#animestatus)
- [Poster](#poster)
- [Rating](#rating)
- [AnimeType](#animetype)
- [MinAge](#minage)
- [RemoteIds](#remoteids)
- [Creator](#creator)
- [Studio](#studio)
- [SkipData](#skipdata)
- [Skips](#skips)
- [VideoData](#videodata)
- [VideoInfo](#videoinfo)
- [Genre](#genre)
- [Translates](#translates)
- [Episodes](#episodes)
- [ScreenshotSizes](#screenshotsizes)
- [RandomScreenshot](#randomscreenshot)
- [Top](#top)
- [ViewingOrder](#viewingorder)

- [FeedRating](#feedrating)
- [FeedMinAge](#feedminage)
- [FeedUserListInfo](#feeduserlistinfo)
- [FeedUserList](#feeduserlist)
- [FeedUser](#feeduser)
- [FeedRemoteIds](#feedremoteids)
- [FeedAnimeItem](#feedanimeitem)
- [FeedVideoItem](#feedvideoitem)
- [FeedTopCarousel](#feedtopcarousel)
- [FeedLastWatchScreenshot](#feedlastwatchscreenshot)
- [FeedLastWatchItem](#feedlastwatchitem)
- [FeedScheduleEpisodes](#feedscheduleepisodes)
- [FeedScheduleItem](#feedscheduleitem)
- [FeedUserIdsShikimori](#feeduseridsshikimori)
- [FeedUserIds](#feeduserids)
- [FeedUserAvatars](#feeduseravatars)
- [FeedUserBanner](#feeduserbanner)
- [FeedUserTexts](#feedusertexts)
- [FeedUserProfile](#feeduserprofile)
- [FeedPostCategory](#feedpostcategory)
- [FeedPostItem](#feedpostitem)
- [FeedPostType](#feedposttype)
- [FeedPosts](#feedposts)
- [FeedBloggerCategory](#feedbloggercategory)
- [FeedBloggerVideoDescriptions](#feedbloggervideodescriptions)
- [FeedBloggerVideoPreviews](#feedbloggervideopreviews)
- [FeedBloggerVideoLikes](#feedbloggervideolikes)
- [FeedBloggerVideoItem](#feedbloggervideoitem)
- [FeedBloggerVideos](#feedbloggervideos)
- [FeedBloggerPeople](#feedbloggerpeople)
- [FeedBlogger](#feedblogger)
- [FeedCollectionLikes](#feedcollectionlikes)
- [FeedCollectionOwner](#feedcollectionowner)
- [FeedCollection](#feedcollection)
- [FeedResponse](#feedresponse)

- [SearchResult](#searchresult)

- [AnimeResult](#animeresult)

## Base models

### `AnimeStatus`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `title` | `std::string` | `title` |
| `class_` | `std::string` | `"class"` |
| `alias` | `std::string` | `alias` |
| `value` | `std::int64_t` | `value` |

### `Poster`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `big` | `std::string` | `big` |
| `fullsize` | `std::string` | `fullsize` |
| `huge` | `std::string` | `huge` |
| `medium` | `std::string` | `medium` |
| `small` | `std::string` | `small` |

### `Rating`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `average` | `double` | `average` |
| `kp_rating` | `double` | `kp_rating` |
| `anidub_rating` | `double` | `anidub_rating` |
| `counters` | `std::int64_t` | `counters` |
| `myanimelist_rating` | `double` | `myanimelist_rating` |
| `shikimori_rating` | `double` | `shikimori_rating` |
| `worldart_rating` | `double` | `worldart_rating` |

### `AnimeType`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `name` | `std::string` | `name` |
| `value` | `std::int64_t` | `value` |
| `shortname` | `std::string` | `shortname` |

### `MinAge`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `value` | `std::int64_t` | `value` |
| `title` | `std::string` | `title` |
| `titleLong` | `std::string` | `titleLong` |

### `RemoteIds`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `worldart_id` | `std::int64_t` | `worldart_id` |
| `worldart_type` | `std::string` | `worldart_type` |
| `kp_id` | `std::int64_t` | `kp_id` |
| `anidb_id` | `std::int64_t` | `anidb_id` |
| `sr_id` | `std::int64_t` | `sr_id` |
| `anilibria_alias` | `std::string` | `anilibria_alias` |
| `shikimori_id` | `std::int64_t` | `shikimori_id` |
| `myanimelist_id` | `std::int64_t` | `myanimelist_id` |

### `Creator`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `title` | `std::string` | `title` |
| `id` | `std::int64_t` | `id` |
| `url` | `std::string` | `url` |

### `Studio`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `title` | `std::string` | `title` |
| `id` | `std::int64_t` | `id` |
| `url` | `std::string` | `url` |

### `SkipData`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `time` | `std::string` | `time` |
| `length` | `std::string` | `length` |

### `Skips`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `opening` | `SkipData` | `opening` |
| `ending` | `SkipData` | `ending` |

### `VideoData`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `dubbing` | `std::string` | `dubbing` |
| `player` | `std::string` | `player` |

### `VideoInfo`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `video_id` | `std::int64_t` | `video_id` |
| `iframe_url` | `std::string` | `iframe_url` |
| `data` | `VideoData` | `data` |
| `number` | `std::string` | `number` |
| `date` | `std::int64_t` | `date` |
| `index` | `std::int64_t` | `index` |
| `skips` | `Skips` | `skips` |

### `Genre`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `title` | `std::string` | `title` |
| `id` | `std::int64_t` | `id` |
| `alias` | `std::string` | `alias` |
| `url` | `std::string` | `url` |

### `Translates`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `title` | `std::string` | `title` |
| `href` | `std::string` | `href` |
| `value` | `std::string` | `value` |

### `Episodes`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `aired` | `std::int64_t` | `aired` |
| `count` | `std::int64_t` | `count` |
| `next_date` | `std::int64_t` | `next_date` |

### `ScreenshotSizes`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `small` | `std::string` | `small` |
| `full` | `std::string` | `full` |

### `RandomScreenshot`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `sizes` | `ScreenshotSizes` | `sizes` |
| `id` | `std::int64_t` | `id` |
| `time` | `std::int64_t` | `time` |
| `episode` | `std::int64_t` | `episode` |

### `Top`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `category` | `std::int64_t` | `category` |
| `global` | `std::int64_t` | `global` |

### `ViewingOrder`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `title` | `std::string` | `title` |
| `anime_id` | `std::int64_t` | `anime_id` |
| `type` | `AnimeType` | `type` |
| `anime_url` | `std::string` | `anime_url` |
| `anime_status` | `AnimeStatus` | `anime_status` |
| `description` | `std::string` | `description` |
| `poster` | `Poster` | `poster` |
| `year` | `std::int64_t` | `year` |
| `date` | `std::int64_t` | `date` |

## Feed models

### `FeedRating`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `counters` | `std::int64_t` | `counters` |
| `average` | `double` | `average` |

### `FeedMinAge`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `value` | `std::int64_t` | `value` |
| `title` | `std::string` | `title` |
| `title_long` | `std::string` | `"title_long"` |

### `FeedUserListInfo`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `title` | `std::string` | `title` |
| `href` | `std::string` | `href` |
| `id` | `std::int64_t` | `id` |

### `FeedUserList`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `is_fav` | `bool` | `is_fav` |
| `list` | `FeedUserListInfo` | `list` |

### `FeedUser`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `list` | `FeedUserList` | `list` |
| `rating` | `double` | `rating` |

### `FeedRemoteIds`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `worldart_id` | `std::int64_t` | `worldart_id` |
| `worldart_type` | `std::string` | `worldart_type` |
| `kp_id` | `std::int64_t` | `kp_id` |
| `anidub_id` | `std::int64_t` | `anidub_id` |
| `sr_id` | `std::int64_t` | `sr_id` |
| `anilibria_alias` | `std::string` | `anilibria_alias` |
| `shikimori_id` | `std::int64_t` | `shikimori_id` |
| `myanimelist_id` | `std::int64_t` | `myanimelist_id` |

### `FeedAnimeItem`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `anime_id` | `std::int64_t` | `anime_id` |
| `anime_status` | `AnimeStatus` | `anime_status` |
| `anime_url` | `std::string` | `anime_url` |
| `poster` | `Poster` | `poster` |
| `rating` | `FeedRating` | `rating` |
| `title` | `std::string` | `title` |
| `type` | `AnimeType` | `type` |
| `year` | `std::int64_t` | `year` |
| `description` | `std::string` | `description` |
| `views` | `std::int64_t` | `views` |
| `season` | `std::int64_t` | `season` |
| `min_age` | `FeedMinAge` | `min_age` |
| `user` | `FeedUser` | `user` |
| `remote_ids` | `FeedRemoteIds` | `remote_ids` |
| `top` | `Top` | `top` |
| `blocked_in` | `std::vector<std::string>` | `blocked_in` |

### `FeedVideoItem`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `title` | `std::string` | `title` |
| `date` | `std::int64_t` | `date` |
| `description` | `std::string` | `description` |
| `ep_title` | `std::string` | `ep_title` |
| `player_title` | `std::string` | `player_title` |
| `poster` | `Poster` | `poster` |
| `anime_url` | `std::string` | `anime_url` |
| `dub_title` | `std::string` | `dub_title` |
| `anime_id` | `std::int64_t` | `anime_id` |
| `video_id` | `std::int64_t` | `video_id` |

### `FeedTopCarousel`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `season` | `std::int64_t` | `season` |
| `year` | `std::int64_t` | `year` |
| `items` | `std::vector<FeedAnimeItem>` | `items` |

### `FeedLastWatchScreenshot`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `id` | `std::int64_t` | `id` |
| `time` | `std::int64_t` | `time` |
| `episode` | `std::string` | `episode` |
| `sizes` | `ScreenshotSizes` | `sizes` |

### `FeedLastWatchItem`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `date` | `std::int64_t` | `date` |
| `end_time` | `std::int64_t` | `end_time` |
| `anime_url` | `std::string` | `anime_url` |
| `poster` | `Poster` | `poster` |
| `video_id` | `std::int64_t` | `video_id` |
| `description` | `std::string` | `description` |
| `title` | `std::string` | `title` |
| `anime_id` | `std::int64_t` | `anime_id` |
| `do_not_recommend` | `bool` | `do_not_recommend` |
| `ep_title` | `std::string` | `ep_title` |
| `duration` | `std::int64_t` | `duration` |
| `screenshot` | `FeedLastWatchScreenshot` | `screenshot` |

### `FeedScheduleEpisodes`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `aired` | `std::int64_t` | `aired` |
| `count` | `std::int64_t` | `count` |
| `next_date` | `std::int64_t` | `next_date` |
| `prev_date` | `std::int64_t` | `prev_date` |

### `FeedScheduleItem`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `title` | `std::string` | `title` |
| `description` | `std::string` | `description` |
| `poster` | `Poster` | `poster` |
| `anime_url` | `std::string` | `anime_url` |
| `anime_id` | `std::int64_t` | `anime_id` |
| `episodes` | `FeedScheduleEpisodes` | `episodes` |

### `FeedUserIdsShikimori`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `id` | `std::int64_t` | `id` |
| `nickname` | `std::string` | `nickname` |

### `FeedUserIds`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `shikimori` | `FeedUserIdsShikimori` | `shikimori` |
| `tg_nickname` | `std::string` | `tg_nickname` |
| `vk` | `std::int64_t` | `vk` |

### `FeedUserAvatars`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `big` | `std::string` | `big` |
| `full` | `std::string` | `full` |
| `small` | `std::string` | `small` |

### `FeedUserBanner`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `cropped` | `std::string` | `cropped` |
| `full` | `std::string` | `full` |

### `FeedUserTexts`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `color` | `std::int64_t` | `color` |
| `left` | `std::string` | `left` |
| `right` | `std::string` | `right` |

### `FeedUserProfile`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `id` | `std::int64_t` | `id` |
| `nickname` | `std::string` | `nickname` |
| `about` | `std::string` | `about` |
| `banned` | `bool` | `banned` |
| `ids` | `FeedUserIds` | `ids` |
| `avatars` | `FeedUserAvatars` | `avatars` |
| `bdate` | `std::int64_t` | `bdate` |
| `last_online` | `std::int64_t` | `last_online` |
| `sex` | `std::int64_t` | `sex` |
| `roles` | `std::vector<std::string>` | `roles` |
| `register_date` | `std::int64_t` | `register_date` |
| `texts` | `FeedUserTexts` | `texts` |
| `banner` | `FeedUserBanner` | `banner` |

### `FeedPostCategory`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `id` | `std::int64_t` | `id` |
| `title` | `std::string` | `title` |
| `uri` | `std::string` | `uri` |

### `FeedPostItem`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `title` | `std::string` | `title` |
| `preview_image` | `std::string` | `preview_image` |
| `id` | `std::int64_t` | `id` |
| `content_preview` | `std::string` | `content_preview` |
| `user` | `FeedUserProfile` | `user` |
| `category` | `FeedPostCategory` | `category` |
| `created_at` | `std::int64_t` | `created_at` |

### `FeedPostType`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `title` | `std::string` | `title` |
| `id` | `std::int64_t` | `id` |
| `uri` | `std::string` | `uri` |

### `FeedPosts`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `items` | `std::vector<FeedPostItem>` | `items` |
| `types` | `std::vector<FeedPostType>` | `types` |

### `FeedBloggerCategory`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `id` | `std::string` | `id` |
| `title` | `std::string` | `title` |

### `FeedBloggerVideoDescriptions`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `big` | `std::string` | `big` |
| `small` | `std::string` | `small` |

### `FeedBloggerVideoPreviews`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `small` | `std::string` | `small` |
| `big` | `std::string` | `big` |

### `FeedBloggerVideoLikes`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `likes` | `std::int64_t` | `likes` |
| `dislikes` | `std::int64_t` | `dislikes` |
| `vote` | `std::int64_t` | `vote` |

### `FeedBloggerVideoItem`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `title` | `std::string` | `title` |
| `id` | `std::int64_t` | `id` |
| `time` | `std::int64_t` | `time` |
| `category` | `FeedBloggerCategory` | `category` |
| `publish_date` | `std::int64_t` | `publish_date` |
| `iframe_url` | `std::string` | `iframe_url` |
| `has_spoiler` | `bool` | `has_spoiler` |
| `descriptions` | `FeedBloggerVideoDescriptions` | `descriptions` |
| `comments_count` | `std::int64_t` | `comments_count` |
| `creator` | `FeedUserProfile` | `creator` |
| `previews` | `FeedBloggerVideoPreviews` | `previews` |
| `likes` | `FeedBloggerVideoLikes` | `likes` |
| `views` | `std::int64_t` | `views` |
| `language` | `std::string` | `language` |

### `FeedBloggerVideos`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `items` | `std::vector<FeedBloggerVideoItem>` | `items` |
| `categories` | `std::vector<FeedBloggerCategory>` | `categories` |

### `FeedBloggerPeople`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `items` | `std::vector<FeedUserProfile>` | `items` |
| `count` | `std::int64_t` | `count` |

### `FeedBlogger`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `videos` | `FeedBloggerVideos` | `videos` |
| `people` | `FeedBloggerPeople` | `people` |

### `FeedCollectionLikes`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `likes` | `std::int64_t` | `likes` |
| `dislikes` | `std::int64_t` | `dislikes` |
| `vote` | `std::int64_t` | `vote` |

### `FeedCollectionOwner`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `id` | `std::int64_t` | `id` |
| `nickname` | `std::string` | `nickname` |
| `avatars` | `FeedUserAvatars` | `avatars` |

### `FeedCollection`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `title` | `std::string` | `title` |
| `description` | `std::string` | `description` |
| `id` | `std::int64_t` | `id` |
| `likes` | `FeedCollectionLikes` | `likes` |
| `language` | `std::string` | `language` |
| `animes` | `std::vector<FeedAnimeItem>` | `animes` |
| `owner` | `FeedCollectionOwner` | `owner` |
| `create_date` | `std::int64_t` | `create_date` |
| `views` | `std::int64_t` | `views` |
| `public_` | `bool` | `"public"` |
| `poster_previews` | `std::vector<Poster>` | `poster_previews` |

### `FeedResponse`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `new_` | `std::vector<FeedAnimeItem>` | `"new"` |
| `announcements` | `std::vector<FeedAnimeItem>` | `announcements` |
| `recommends` | `std::vector<FeedAnimeItem>` | `recommends` |
| `new_videos` | `std::vector<FeedVideoItem>` | `new_videos` |
| `top_carousel` | `FeedTopCarousel` | `top_carousel` |
| `last_watches` | `std::vector<FeedLastWatchItem>` | `last_watches` |
| `schedule` | `std::vector<FeedScheduleItem>` | `schedule` |
| `posts` | `FeedPosts` | `posts` |
| `blogger` | `FeedBlogger` | `blogger` |
| `collections` | `std::vector<FeedCollection>` | `collections` |

## Search

### `SearchResult`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `anime_id` | `std::int64_t` | `anime_id` |
| `anime_status` | `AnimeStatus` | `anime_status` |
| `anime_url` | `std::string` | `anime_url` |
| `description` | `std::string` | `description` |
| `min_age` | `MinAge` | `min_age` |
| `poster` | `Poster` | `poster` |
| `rating` | `Rating` | `rating` |
| `remote_ids` | `RemoteIds` | `remote_ids` |
| `season` | `std::int64_t` | `season` |
| `title` | `std::string` | `title` |
| `top` | `Top` | `top` |
| `type` | `AnimeType` | `type` |
| `views` | `std::int64_t` | `views` |
| `year` | `std::int64_t` | `year` |

## Anime

### `AnimeResult`

| Поле C++ | Тип | Имя в JSON |
| --- | --- | --- |
| `anime_id` | `std::int64_t` | `anime_id` |
| `anime_status` | `AnimeStatus` | `anime_status` |
| `anime_url` | `std::string` | `anime_url` |
| `poster` | `Poster` | `poster` |
| `rating` | `Rating` | `rating` |
| `title` | `std::string` | `title` |
| `type` | `AnimeType` | `type` |
| `year` | `std::int64_t` | `year` |
| `description` | `std::string` | `description` |
| `views` | `std::int64_t` | `views` |
| `season` | `std::int64_t` | `season` |
| `min_age` | `MinAge` | `min_age` |
| `remote_ids` | `RemoteIds` | `remote_ids` |
| `original` | `std::string` | `original` |
| `other_titles` | `std::vector<std::string>` | `other_titles` |
| `creators` | `std::vector<Creator>` | `creators` |
| `studios` | `std::vector<Studio>` | `studios` |
| `videos` | `std::vector<VideoInfo>` | `videos` |
| `genres` | `std::vector<Genre>` | `genres` |
| `viewing_order` | `std::vector<ViewingOrder>` | `viewing_order` |
| `translates` | `std::vector<Translates>` | `translates` |
| `blocked_in` | `std::vector<std::string>` | `blocked_in` |
| `episodes` | `Episodes` | `episodes` |
| `comments_count` | `std::int64_t` | `comments_count` |
| `reviews_count` | `std::int64_t` | `reviews_count` |
| `random_screenshots` | `std::vector<RandomScreenshot>` | `random_screenshots` |
| `top` | `Top` | `top` |
| `posts_count` | `std::int64_t` | `posts_count` |

## Правила чтения каталога

`YANI_FIELD_JSON(type, member, json_name)` задаёт явное имя ключа JSON;
например `class_` читается из `class`, `new_` из `new`, `public_` из `public`.
`YANI_FIELD(type, member)` использует имя C++ члена как имя API JSON. Допуски
типов и специальные null defaults описаны в [API](API.md).

### Важное отличие Anime в IPC

Таблица `AnimeResult` выше — API-модель. Её сырые видео находятся в
`videos: VideoInfo[]`. IPC-сервер в `Server::animeToJson` группирует их как
`players[].dubbers[].episodes[]`, сортирует эпизоды и удаляет `videos` из
клиентского JSON. Поля episode остаются `video_id`, `iframe_url`, `number`,
`date`, `index`, `skips`. Это преобразование описано в
[SERVER.md](SERVER.md#anime-json) и [IPC.md](../src/server/IPC.md).

При изменении `models.def` обновляйте этот справочник. При изменении группировки
IPC обновляйте protocol-документы и клиентские модели; править API `AnimeResult`
ради одного IPC представления не нужно.
