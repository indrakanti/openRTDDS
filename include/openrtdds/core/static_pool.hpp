#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace openrtdds::core {

// Fixed-capacity object storage. This type is intentionally single-owner;
// synchronization belongs at the subsystem boundary where priority and
// blocking behavior can be analyzed explicitly.
template <typename T, std::size_t Capacity>
class StaticPool final {
  static_assert(Capacity > 0U, "StaticPool capacity must be nonzero");

 public:
  StaticPool() noexcept = default;
  StaticPool(const StaticPool&) = delete;
  StaticPool& operator=(const StaticPool&) = delete;

  ~StaticPool() noexcept { clear(); }

  template <typename... Args>
  [[nodiscard]] T* acquire(Args&&... args) noexcept(
      std::is_nothrow_constructible_v<T, Args...>) {
    for (std::size_t index = 0U; index < Capacity; ++index) {
      if (!used_[index]) {
        T* const object = ::new (static_cast<void*>(&storage_[index]))
            T(std::forward<Args>(args)...);
        used_[index] = true;
        ++size_;
        return object;
      }
    }
    return nullptr;
  }

  [[nodiscard]] bool release(T* object) noexcept {
    if (object == nullptr) {
      return false;
    }
    for (std::size_t index = 0U; index < Capacity; ++index) {
      if (used_[index] && (pointer_at(index) == object)) {
        object->~T();
        used_[index] = false;
        --size_;
        return true;
      }
    }
    return false;
  }

  void clear() noexcept {
    for (std::size_t index = 0U; index < Capacity; ++index) {
      if (used_[index]) {
        pointer_at(index)->~T();
        used_[index] = false;
      }
    }
    size_ = 0U;
  }

  [[nodiscard]] constexpr std::size_t capacity() const noexcept {
    return Capacity;
  }

  [[nodiscard]] std::size_t size() const noexcept { return size_; }
  [[nodiscard]] bool empty() const noexcept { return size_ == 0U; }
  [[nodiscard]] bool full() const noexcept { return size_ == Capacity; }

 private:
  using Storage = std::aligned_storage_t<sizeof(T), alignof(T)>;

  [[nodiscard]] T* pointer_at(std::size_t index) noexcept {
    return std::launder(reinterpret_cast<T*>(&storage_[index]));
  }

  std::array<Storage, Capacity> storage_{};
  std::array<bool, Capacity> used_{};
  std::size_t size_{0U};
};

}  // namespace openrtdds::core

