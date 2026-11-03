#pragma once
#include "../DataStructures/flight.hpp"
#include <cstdint>

// ---------------------------------------------------------------------------
// SearchState: The priority queue element. Carries enough info to:
//   1. Drive the priority queue ordering (total_cost as the key)
//   2. Know where we are (node_id)
//   3. Know WHEN we arrived (current_time) for transfer window checks
//   4. Point back to the full Label record (label_id) for path reconstruction
// 12 bytes (4 + 4 + 2 + 2)
// ---------------------------------------------------------------------------
struct SearchState {
  uint32_t current_time; // Absolute arrival minute at this airport
  int32_t label_id;      // keeping track of label
  uint16_t total_cost;   // Accumulated cost up to this node
  uint16_t node_id;      // airports are our nodes
  // No operator overloading inside the struct at all
};
static_assert(sizeof(SearchState) == 12, "SearchState must be 12 bytes");

// Min-heap comparator: lowest total_cost is explored first
struct CompareCost {
  bool operator()(const SearchState &a, const SearchState &b) const {
    return a.total_cost > b.total_cost; // > creates a Min-Heap
  }
};

// ---------------------------------------------------------------------------
// Label: Immutable record of a discovered non-dominated state. Created once
// when a state is accepted into the Pareto frontier of an airport, never
// mutated. Linked via parent_label_id to form a chain from destination back
// to source, enabling exact path reconstruction at any point.
// ---------------------------------------------------------------------------
struct Label {
  uint32_t airport_id;
  uint32_t cost;
  uint32_t arrival_time;
  int32_t parent_label_id; // Index in all_labels store; -1 for the source
  flight edge_taken;       // The flight used to arrive at airport_id
};