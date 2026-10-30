#include "anime_cache.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

#include "json.hpp"

namespace {
bool has_value(const nlohmann::json &value) {
  if (value.is_null()) return false;
  if (value.is_string()) return !value.get_ref<const std::string &>().empty();
  if (value.is_array() || value.is_object()) return !value.empty();
  if (value.is_number()) return value.get<double>() != 0.0;
  return true;
}

void merge_json(nlohmann::json &target, const nlohmann::json &update) {
  if (target.is_object() && update.is_object()) {
    for (auto it = update.begin(); it != update.end(); ++it) {
      if (!has_value(it.value())) continue;
      if (!target.contains(it.key())) target[it.key()] = it.value();
      else merge_json(target[it.key()], it.value());
    }
  } else if (has_value(update)) {
    target = update;
  }
}
} // namespace

void AnimeCache::Entry::subscribe(SubscriptionId id, Callback callback) {
  subscribers.emplace(id, std::move(callback));
}

bool AnimeCache::Entry::unsubscribe(SubscriptionId id) {
  return subscribers.erase(id) != 0;
}

void AnimeCache::Entry::addToSearch(const std::string &search_key) {
  search_keys.insert(search_key);
}

void AnimeCache::Entry::removeFromSearch(const std::string &search_key) {
  search_keys.erase(search_key);
}

bool AnimeCache::Entry::canErase() const {
  return !updating && subscribers.empty() && search_keys.empty();
}

AnimeCache::SubscribeResult AnimeCache::subscribe(std::int64_t anime_id,
                                                  Callback callback) {
  SubscribeResult result;
  if (anime_id <= 0) return result;
  auto* entry = findOrCreate(anime_id);
  if (!entry) return result;

  if (entry->value.anime_id != 0) result.anime = entry->value;
  const auto now = Clock::now();
  const bool expired = entry->refreshed_at == Clock::time_point{} ||
                       now - entry->refreshed_at > kRefreshInterval;
  if (callback && (expired || entry->updating)) {
    ++next_subscription_id_;
    if (next_subscription_id_ == 0) ++next_subscription_id_;
    result.subscription_id = next_subscription_id_;
    entry->subscribe(result.subscription_id, std::move(callback));
    subscriber_index_.insert_or_assign(result.subscription_id, anime_id);
  }
  if (expired && !entry->updating) {
    entry->updating = true;
    result.should_refresh = true;
  }
  if (entry->canErase()) queueIfUnused(anime_id, *entry);
  else removeCandidate(anime_id);
  return result;
}

bool AnimeCache::unsubscribe(SubscriptionId subscription_id) {
  const auto subscription = subscriber_index_.find(subscription_id);
  if (subscription == subscriber_index_.end()) return false;
  const auto anime_id = subscription->second;
  subscriber_index_.erase(subscription);
  const auto found = entries_.find(anime_id);
  if (found == entries_.end()) return false;
  found->second->unsubscribe(subscription_id);
  queueIfUnused(anime_id, *found->second);
  return true;
}

bool AnimeCache::needsFullRefresh(std::int64_t anime_id) const {
  const auto found = entries_.find(anime_id);
  return found == entries_.end() ||
         found->second->refreshed_at == Clock::time_point{} ||
         Clock::now() - found->second->refreshed_at > kRefreshInterval;
}

bool AnimeCache::isLoaded(std::int64_t anime_id) const {
  const auto found = entries_.find(anime_id);
  return found != entries_.end() && found->second->value.anime_id != 0;
}

std::optional<yani::AnimeResult> AnimeCache::get(std::int64_t anime_id) const {
  const auto found = entries_.find(anime_id);
  if (found == entries_.end() || found->second->value.anime_id == 0)
    return std::nullopt;
  return found->second->value;
}

std::vector<AnimeCache::PendingCallback>
AnimeCache::finishRefresh(std::int64_t anime_id,
                          std::optional<yani::AnimeResult> anime) {
  std::vector<PendingCallback> callbacks;
  const auto found = entries_.find(anime_id);
  if (found == entries_.end()) return callbacks;
  auto &entry = *found->second;
  entry.updating = false;
  if (anime) {
    entry.value = std::move(*anime);
    entry.refreshed_at = Clock::now();
    entry.card_refreshed_at = entry.refreshed_at;
    entry.has_full_details = true;
    checked_ids_.erase(anime_id);
  }
  callbacks.reserve(entry.subscribers.size());
  for (auto &[id, callback] : entry.subscribers) {
    subscriber_index_.erase(id);
    callbacks.push_back({id, std::move(callback)});
  }
  entry.subscribers.clear();
  queueIfUnused(anime_id, entry);
  return callbacks;
}

void AnimeCache::storeSearchResults(
    const std::string &search_key,
    const std::vector<yani::SearchResult> &results) {
  const auto refreshed_at = Clock::now();
  for (const auto &result : results) {
    if (result.anime_id <= 0) continue;
    checked_ids_.erase(result.anime_id);
    auto* entry = findOrCreate(result.anime_id);
    if (!entry) continue;
    if (!entry->has_full_details) {
      yani::AnimeResult partial;
      partial.anime_id = result.anime_id;
      partial.anime_status = result.anime_status;
      partial.anime_url = result.anime_url;
      partial.description = result.description;
      partial.min_age = result.min_age;
      partial.poster = result.poster;
      partial.rating = result.rating;
      partial.remote_ids = result.remote_ids;
      partial.season = result.season;
      partial.title = result.title;
      partial.top = result.top;
      partial.type = result.type;
      partial.views = result.views;
      partial.year = result.year;
      merge(entry->value, partial);
    }
    entry->card_refreshed_at = refreshed_at;
    entry->addToSearch(search_key);
    removeCandidate(result.anime_id);
    queueIfUnused(result.anime_id, *entry);
  }
}

void AnimeCache::mergePartial(std::int64_t anime_id,
                              const yani::AnimeResult &partial) {
  if (anime_id <= 0) return;
  auto* entry = findOrCreate(anime_id);
  if (!entry) return;
  checked_ids_.erase(anime_id);
  if (!entry->has_full_details) merge(entry->value, partial);
  entry->card_refreshed_at = Clock::now();
  queueIfUnused(anime_id, *entry);
}

std::vector<yani::SearchResult>
AnimeCache::searchResults(const std::vector<std::int64_t> &anime_ids) const {
  std::vector<yani::SearchResult> results;
  results.reserve(anime_ids.size());
  for (const auto anime_id : anime_ids) {
    const auto anime = get(anime_id);
    if (anime) results.push_back(toSearchResult(*anime));
  }
  return results;
}

void AnimeCache::addToSearch(const std::string &search_key,
                             std::int64_t anime_id) {
  const auto found = entries_.find(anime_id);
  if (found == entries_.end()) return;
  found->second->addToSearch(search_key);
  removeCandidate(anime_id);
}

void AnimeCache::removeFromSearch(const std::string &search_key,
                                  std::int64_t anime_id) {
  const auto found = entries_.find(anime_id);
  if (found == entries_.end()) return;
  found->second->removeFromSearch(search_key);
  queueIfUnused(anime_id, *found->second);
}

bool AnimeCache::cardNeedsRefresh(std::int64_t anime_id) const {
  const auto checked = checked_ids_.find(anime_id);
  if (checked != checked_ids_.end() &&
      Clock::now() - checked->second < kRefreshInterval)
    return false;
  const auto found = entries_.find(anime_id);
  return found == entries_.end() ||
         found->second->card_refreshed_at == Clock::time_point{} ||
         Clock::now() - found->second->card_refreshed_at >= kRefreshInterval;
}

void AnimeCache::markCardChecked(std::int64_t anime_id) {
  if (anime_id <= 0) return;
  const auto now = Clock::now();
  for (auto it = checked_ids_.begin(); it != checked_ids_.end();) {
    if (now - it->second >= kRefreshInterval) it = checked_ids_.erase(it);
    else ++it;
  }
  if (!checked_ids_.contains(anime_id) &&
      checked_ids_.size() >= kCheckedIdCapacity) {
    const auto oldest =
        std::min_element(checked_ids_.begin(), checked_ids_.end(),
                         [](const auto &left, const auto &right) {
                           return left.second < right.second;
                         });
    if (oldest != checked_ids_.end()) checked_ids_.erase(oldest);
  }
  checked_ids_.insert_or_assign(anime_id, now);
}

AnimeCache::Entry* AnimeCache::findOrCreate(std::int64_t anime_id) {
  if (const auto found = entries_.find(anime_id); found != entries_.end())
    return found->second.get();
  cleanCandidates();
  auto entry = std::make_unique<Entry>();
  auto* result = entry.get();
  entries_.emplace(anime_id, std::move(entry));
  return result;
}

void AnimeCache::queueIfUnused(std::int64_t anime_id, const Entry &entry) {
  if (entry.canErase() && !cleanup_candidate_index_.contains(anime_id)) {
    cleanup_candidates_.push_back(anime_id);
    cleanup_candidate_index_.emplace(anime_id,
                                     std::prev(cleanup_candidates_.end()));
  }
}

void AnimeCache::removeCandidate(std::int64_t anime_id) {
  const auto candidate = cleanup_candidate_index_.find(anime_id);
  if (candidate == cleanup_candidate_index_.end()) return;
  cleanup_candidates_.erase(candidate->second);
  cleanup_candidate_index_.erase(candidate);
}

void AnimeCache::cleanCandidates() {
  while (entries_.size() >= kCapacity && !cleanup_candidates_.empty()) {
    const auto anime_id = cleanup_candidates_.front();
    cleanup_candidates_.pop_front();
    cleanup_candidate_index_.erase(anime_id);
    const auto found = entries_.find(anime_id);
    if (found != entries_.end() && found->second->canErase())
      entries_.erase(found);
  }
}

yani::SearchResult AnimeCache::toSearchResult(const yani::AnimeResult &anime) {
  return yani::SearchResult{
      .anime_id = anime.anime_id,
      .anime_status = anime.anime_status,
      .anime_url = anime.anime_url,
      .description = anime.description,
      .min_age = anime.min_age,
      .poster = anime.poster,
      .rating = anime.rating,
      .remote_ids = anime.remote_ids,
      .season = anime.season,
      .title = anime.title,
      .top = anime.top,
      .type = anime.type,
      .views = anime.views,
      .year = anime.year,
  };
}

void AnimeCache::merge(yani::AnimeResult &target,
                       const yani::AnimeResult &partial) {
  nlohmann::json merged = target;
  merge_json(merged, nlohmann::json(partial));
  target = merged.get<yani::AnimeResult>();
}
