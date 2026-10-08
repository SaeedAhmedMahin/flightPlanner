#pragma once
#include <cstdint>

// helps keep track of total cost and other info for more than 1 flight
// 8 bytes
struct SearchState {
  uint32_t current_time;
  uint16_t total_cost;
  uint16_t node_id;
  // No operator overloading inside the struct at all
};
static_assert(sizeof(SearchState) == 8, "SearchState must be 8 bytes");

// Custom Comparator (Functor)
struct CompareCost {
  bool operator()(const SearchState &a, const SearchState &b) const {
    return a.total_cost > b.total_cost; // > creates a Min-Heap
  }
};