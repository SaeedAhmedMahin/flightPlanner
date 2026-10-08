#pragma once
#include <cstdint>

// 8 bytes
struct SearchState {
  uint32_t total_cost;
  uint16_t node_id;
  uint16_t current_time;
  // No operator overloading inside the struct at all
};

// Custom Comparator (Functor)
struct CompareCost {
  bool operator()(const SearchState &a, const SearchState &b) const {
    return a.total_cost > b.total_cost; // > creates a Min-Heap
  }
};