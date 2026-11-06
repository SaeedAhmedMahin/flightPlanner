#pragma once
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// Flat4AryMinHeap: High-Performance 4-ary (d=4) Min-Heap
//
// Optimized specifically for 16-byte structs (e.g., SearchState, AStarState):
// 1. 4-ary branching: 4 children = 4 * 16 bytes = exactly 64 bytes (one L1 cache line).
//    During sift_down, inspecting all 4 children touches a single contiguous cache line.
// 2. Tree height is halved compared to binary heaps: ceil(log4(N)) instead of ceil(log2(N)),
//    halving memory branch penalties during sift_down operations.
// 3. Pre-allocated flat buffer eliminates dynamic reallocation overhead during search.
// ---------------------------------------------------------------------------
template <typename T, typename Compare>
class Flat4AryMinHeap {
public:
  explicit Flat4AryMinHeap(size_t initial_capacity = 65536, Compare comp = Compare())
      : comp_(comp) {
    data_.reserve(initial_capacity);
  }

  [[nodiscard]] inline bool empty() const noexcept {
    return data_.empty();
  }

  [[nodiscard]] inline size_t size() const noexcept {
    return data_.size();
  }

  [[nodiscard]] inline const T &top() const noexcept {
    return data_[0];
  }

  inline void push(const T &value) {
    data_.push_back(value);
    sift_up(data_.size() - 1);
  }

  inline void push(T &&value) {
    data_.push_back(std::move(value));
    sift_up(data_.size() - 1);
  }

  inline void pop() {
    if (data_.empty()) return;
    data_[0] = std::move(data_.back());
    data_.pop_back();
    if (!data_.empty()) {
      sift_down(0);
    }
  }

  inline void clear() noexcept {
    data_.clear();
  }

  inline void reserve(size_t capacity) {
    data_.reserve(capacity);
  }

private:
  std::vector<T> data_;
  Compare comp_;

  inline void sift_up(size_t index) {
    T val = std::move(data_[index]);
    while (index > 0) {
      size_t parent = (index - 1) >> 2; // (index - 1) / 4
      if (comp_(val, data_[parent])) {  // val < parent in min-heap ordering
        data_[index] = std::move(data_[parent]);
        index = parent;
      } else {
        break;
      }
    }
    data_[index] = std::move(val);
  }

  inline void sift_down(size_t index) {
    const size_t n = data_.size();
    T val = std::move(data_[index]);

    while (true) {
      size_t child_first = (index << 2) + 1; // index * 4 + 1
      if (child_first >= n) break;

      // Find minimum among up to 4 children (all 4 occupy a contiguous 64-byte cache line)
      size_t best_child = child_first;
      size_t child_last = child_first + 4;
      if (child_last > n) child_last = n;

      for (size_t c = child_first + 1; c < child_last; ++c) {
        if (comp_(data_[c], data_[best_child])) {
          best_child = c;
        }
      }

      if (comp_(data_[best_child], val)) {
        data_[index] = std::move(data_[best_child]);
        index = best_child;
      } else {
        break;
      }
    }
    data_[index] = std::move(val);
  }
};

