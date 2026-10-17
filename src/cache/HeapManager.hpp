// ============================================================
// HeapManager
//
// Управляет сегментами конкретного heap:
// - создаёт первый сегмент;
// - находит место для allocation;
// - добавляет сегменты при нехватке памяти;
// - подключает существующие сегменты;
// - разрешает handle в локальный указатель.
//
// Политика распределения памяти остаётся в конкретном heap.
// ============================================================
#pragma once

#include <string>
#include <string_view>
#include <deque>
#include <mutex>
#include <atomic>
#include <vector>

#include "IShmSegmentProvider.hpp"

namespace shm {

class HeapRegistryGuard {
 public:
  explicit HeapRegistryGuard(std::atomic_flag *flag = nullptr) : flag_(flag) {
    if (flag_) while (flag_->test_and_set(std::memory_order_acquire)) {}
  }
  ~HeapRegistryGuard() { if (flag_) flag_->clear(std::memory_order_release); }
  void unlock() { if (flag_) { flag_->clear(std::memory_order_release); flag_ = nullptr; } }
  HeapRegistryGuard(HeapRegistryGuard &&other) noexcept : flag_(other.flag_) { other.flag_ = nullptr; }
  HeapRegistryGuard(const HeapRegistryGuard &) = delete;
  HeapRegistryGuard &operator=(const HeapRegistryGuard &) = delete;
 private:
  std::atomic_flag *flag_;
};

class HeapManager {
  public:
  HeapManager();

  bool create(std::string_view name,
              std::size_t initial_segment_size,
              IShmSegmentProvider &provider);

  bool open(std::string_view name, IShmSegmentProvider &provider);

  void close();

  // Discover segments created after this manager was opened.
  bool refresh();

  // Создать следующий сегмент.
  bool add_segment(std::size_t minimum_size);

  // Получить локальное отображение сегмента.
  const HeapSegment* get_segment(SegmentId id) const;

  HeapSegment* get_segment(SegmentId id);

  // Преобразовать offset в локальный адрес.
  void* resolve(HeapHandle handle) const;

  std::size_t segment_count() const noexcept;

  HeapStatistics statistics() const;

  std::vector<HeapHandle> allocated_handles() const;
  HeapRegistryGuard lock_registry() const;

  bool valid() const noexcept;

  private:
  bool open_next_segment() const;

  std::string name_;
  std::size_t initial_segment_size_ = 0;
  std::size_t next_segment_size_ = 0;

  IShmSegmentProvider* provider_ = nullptr;

  // Только локальные адреса процесса.
  mutable std::mutex segments_mutex_;
  mutable std::deque<HeapSegment> segments_;
};
} // namespace shm
