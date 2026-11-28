#pragma once
#include "../DataStructures/flight.hpp"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

// ---------------------------------------------------------------------------
// SearchState: The priority queue element. Carries enough info to:
//   1. Drive the priority queue ordering (total_cost as the key)
//   2. Know where we are (node_id)
//   3. Know WHEN we arrived (current_time) for transfer window checks
//   4. Point back to the full Label record (label_id) for path reconstruction
//   5. Track number of flight hops taken (hops)
// 16 bytes:
//   uint32_t current_time (4 bytes)
//   int32_t label_id      (4 bytes)
//   uint16_t total_cost   (2 bytes)
//   uint16_t node_id      (2 bytes)
//   uint8_t hops          (1 byte)
//   uint8_t padding[3]    (3 bytes)
// ---------------------------------------------------------------------------
struct SearchState {
  uint32_t current_time; // Absolute arrival minute at this airport
  int32_t label_id;      // keeping track of label
  uint16_t total_cost;   // Accumulated cost up to this node
  uint16_t node_id;      // airports are our nodes
  uint8_t hops;          // Number of flight legs taken so far
  uint8_t padding[3];    // Alignment padding to 16 bytes
};
static_assert(sizeof(SearchState) == 16, "SearchState must be 16 bytes");

// Min-heap comparator: returns true if 'a' has higher priority than 'b'.
// Primary: lower total_cost.
// Secondary tie-breaker: lower number of flight hops (fewer transits).
struct CompareCost {
  bool operator()(const SearchState &a, const SearchState &b) const {
    if (a.total_cost != b.total_cost) {
      return a.total_cost < b.total_cost;
    }
    return a.hops < b.hops;
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
  uint8_t hops;            // Number of flight legs taken so far
};

struct FlightDayRange {
  std::vector<flight>::const_iterator begin;
  std::vector<flight>::const_iterator end;
};

// Returns [begin, end) covering outgoing flights in [req_dep, curr_utc + 1440]
// from day-sorted adjacency lists in O(log N) instead of scanning all 334 days.
inline FlightDayRange get_flight_day_range(const std::vector<flight> &edges,
                                           uint32_t curr_utc, uint32_t req_dep,
                                           int32_t origin_utc_offset) {
  if (edges.empty()) {
    return {edges.end(), edges.end()};
  }
  const int64_t curr_i = static_cast<int32_t>(curr_utc);
  const int64_t req_i = static_cast<int32_t>(req_dep);
  const int64_t min_local = req_i + origin_utc_offset;
  const uint16_t min_day =
      (min_local > 0) ? static_cast<uint16_t>(min_local / 1440) : 0;
  const int64_t max_day = (curr_i + 1440 + origin_utc_offset) / 1440;

  auto it = std::lower_bound(
      edges.begin(), edges.end(), min_day,
      [](const flight &f, uint16_t d) { return f.flight_day < d; });
  auto end_it = edges.end();
  if (max_day >= 0) {
    uint16_t cap_day = static_cast<uint16_t>(
        std::min<int64_t>(max_day, std::numeric_limits<uint16_t>::max()));
    end_it = std::upper_bound(
        it, edges.end(), cap_day,
        [](uint16_t d, const flight &f) { return d < f.flight_day; });
  }
  return {it, end_it};
}