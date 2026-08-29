#pragma once
#include <cstdint>

// 8 bytes
struct SearchState {
  uint32_t total_cost; // The feature we are minimizing (Price or Duration)

  uint16_t node_id;
  uint16_t current_time; // Absolute time elapsed + initial departure (for
                         // layover logic)

  // C++ std::priority_queue is a Max-Heap by default.
  // Overloading '>' flips it into a Min-Heap so the lowest total_cost pops
  // first.
  bool operator>(const SearchState &other) const {
    return total_cost > other.total_cost;
  }
};