#include <exception>
#include <iostream>
#include <string>
#include <utility>

#include "core.hpp"

namespace {
template <typename T, typename Call>
yani::Result<T> safe_api_call(Call &&call) {
  try {
    return call();
  } catch (const std::exception &exception) {
    return std::unexpected(
        yani::ApiError{.status = 0, .message = exception.what(), .data = {}});
  } catch (...) {
    return std::unexpected(yani::ApiError{
        .status = 0, .message = "Unknown API exception", .data = {}});
  }
}
} // namespace

Core::AnimeSubscriptionId Core::subscribeAnime(std::int64_t anime_id,
                                               AnimeCallback callback,
                                               bool need_videos) {
  if (anime_id <= 0) {
    if (callback)
      callback(anime_id, std::nullopt, false,
               yani::ApiError{.status = 0, .message = "Invalid anime ID"});
    return 0;
  }

  AnimeCache::SubscribeResult subscription;
  {
    std::lock_guard lock(mutex_);
    subscription = anime_cache_.subscribe(anime_id, callback);
  }
  if (callback) callback(anime_id, subscription.anime, false, std::nullopt);
  if (!subscription.should_refresh) return subscription.subscription_id;

  const bool accepted = tasks_.submit([this, anime_id, need_videos] {
    auto response = safe_api_call<yani::AnimeResult>(
        [&] { return yani::getAnime(std::to_string(anime_id), need_videos); });
    if (!response)
      std::cerr << "[api error] getAnime id=" << anime_id
                << " status=" << response.error().status
                << " message=" << response.error().message << '\n'
                << "[api error] reproduce with: curl --show-error "
                   "--connect-timeout 15 --max-time 30 "
                   "-H 'Lang: ru' --get "
                   "--data-urlencode 'need_videos=true' "
                   "'https://api.yani.tv/anime/"
                << anime_id << "'\n";
    std::vector<AnimeCache::PendingCallback> callbacks;
    std::optional<yani::AnimeResult> updated;
    std::optional<yani::ApiError> error;
    {
      std::lock_guard lock(mutex_);
      if (response) updated = *response;
      else error = response.error();
      callbacks = anime_cache_.finishRefresh(anime_id, updated);
      if (response) updated = anime_cache_.get(anime_id);
    }
    for (auto &pending : callbacks) {
      try {
        pending.callback(anime_id, updated, true, error);
      } catch (...) {}
    }
  });
  if (!accepted) {
    std::cerr << "[core] getAnime scheduling failed core=" << this
              << " pool=" << &tasks_ << " anime_id=" << anime_id << '\n';
    std::vector<AnimeCache::PendingCallback> callbacks;
    const yani::ApiError error{.status = 0,
                               .message = "API task pool rejected getAnime " +
                                          std::to_string(anime_id)};
    {
      std::lock_guard lock(mutex_);
      callbacks = anime_cache_.finishRefresh(anime_id, std::nullopt);
    }
    for (auto &pending : callbacks) {
      try {
        pending.callback(anime_id, std::nullopt, true, error);
      } catch (...) {}
    }
  }
  return subscription.subscription_id;
}

bool Core::unsubscribeAnime(AnimeSubscriptionId subscription_id) {
  if (subscription_id == 0) return false;
  std::lock_guard lock(mutex_);
  return anime_cache_.unsubscribe(subscription_id);
}

bool Core::animeNeedsRefresh(std::int64_t anime_id) const {
  std::lock_guard lock(mutex_);
  return anime_cache_.needsFullRefresh(anime_id);
}

bool Core::animeIsLoaded(std::int64_t anime_id) const {
  std::lock_guard lock(mutex_);
  return anime_cache_.isLoaded(anime_id);
}
