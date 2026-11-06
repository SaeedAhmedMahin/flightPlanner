#pragma once
#include "../DataStructures/Flat4AryMinHeap.hpp"
#include "../DataStructures/airport.hpp"
#include "../DataStructures/flight.hpp"
#include "Dijkstra.hpp"
#include "SearchState.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <vector>

// ---------------------------------------------------------------------------
// AStarState: Priority queue element for A* Search.
// 16 bytes:
//   uint32_t current_time (4 bytes) - Absolute arrival minute at this airport
//   int32_t label_id      (4 bytes) - Index to Label in all_labels
//   uint16_t f_score      (2 bytes) - Estimated total cost: g(n) + h(n)
//   uint16_t g_score      (2 bytes) - Exact accumulated cost: g(n)
//   uint16_t node_id      (2 bytes) - Current airport node ID
//   uint8_t hops          (1 byte)  - Number of flight legs taken so far
//   uint8_t padding       (1 byte)  - 16-byte alignment
// ---------------------------------------------------------------------------
struct AStarState {
  uint32_t current_time;
  int32_t label_id;
  uint16_t f_score;
  uint16_t g_score;
  uint16_t node_id;
  uint8_t hops;
  uint8_t padding;
};
static_assert(sizeof(AStarState) == 16, "AStarState must be 16 bytes");

// Min-heap comparator for A*: returns true if 'a' has higher priority than 'b' (lower f_score).
// On tie, breaks in favor of higher g_score (closer to destination).
struct CompareAStar {
  bool operator()(const AStarState &a, const AStarState &b) const {
    if (a.f_score != b.f_score) {
      return a.f_score < b.f_score;
    }
    return a.g_score > b.g_score; // Tie-breaker: prefer higher g_score (smaller h)
  }
};

// ---------------------------------------------------------------------------
// AStar: Heuristic-Guided Pareto Flight Router
// Uses admissible Great-Circle Haversine lower-bound heuristics to guide the
// multi-criteria search directly towards the destination, exploring
// substantially fewer nodes than uninformed Dijkstra while maintaining
// exact Pareto optimality.
// ---------------------------------------------------------------------------
class AStar {
public:
  const uint32_t INF = std::numeric_limits<uint32_t>::max();

  static inline double haversine_km(float lat1, float lon1, float lat2, float lon2) {
    const double R = 6371.0;
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    double a = std::sin(dLat / 2.0) * std::sin(dLat / 2.0) +
               std::cos(lat1 * M_PI / 180.0) * std::cos(lat2 * M_PI / 180.0) *
                   std::sin(dLon / 2.0) * std::sin(dLon / 2.0);
    double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return R * c;
  }

  // Admissible heuristic: strictly lower-bounds remaining cost/time to destination
  static inline double compute_h(uint32_t node_id, uint32_t dest_id,
                                 const std::vector<airport> &nodes,
                                 OptimizeFor mode) {
    if (node_id == dest_id) return 0.0;
    double dist = haversine_km(nodes[node_id].latitude, nodes[node_id].longitude,
                               nodes[dest_id].latitude, nodes[dest_id].longitude);
    if (mode == OptimizeFor::PRICE) {
      // Conservative lower bound heuristic for flight fare: $0.025 / km
      return dist * 0.025;
    } else {
      // Speed lower bound at 1000 km/h: ~0.060 min / km
      return dist * 0.060;
    }
  }

  RoutingResult run(uint32_t source_id, uint32_t dest_id,
                    const std::vector<std::vector<flight>> &adjacency_list,
                    const std::vector<airport> &nodes, OptimizeFor mode,
                    uint32_t start_time, uint8_t max_hops = 5) {
    RoutingMetrics metrics = {0, 0};

    if (source_id == dest_id) {
      return {true, 0, 0, {}, metrics};
    }

    uint32_t num_nodes = adjacency_list.size();
    std::vector<std::vector<ParetoPoint>> pareto_labels(num_nodes);

    std::vector<Label> all_labels;
    all_labels.reserve(65536);

    Flat4AryMinHeap<AStarState, CompareAStar> pq(65536);

    // Initial heuristic from source to dest
    double h_start = compute_h(source_id, dest_id, nodes, mode);
    uint16_t f_start = static_cast<uint16_t>(std::min<double>(h_start, 65535.0));

    int32_t source_label_id = static_cast<int32_t>(all_labels.size());
    all_labels.push_back(Label{source_id, 0, start_time, -1, flight{}, 0});
    pareto_labels[source_id].push_back({0, start_time, 0});

    pq.push({start_time,
             source_label_id,
             f_start,
             0,
             static_cast<uint16_t>(source_id),
             0,
             0});

    while (!pq.empty()) {
      AStarState current = pq.top();
      pq.pop();
      metrics.nodes_explored++;

      // Early exit: first time we pop destination is guaranteed optimal
      if (current.node_id == dest_id) {
        return reconstruct_path(current, all_labels, nodes, start_time, metrics);
      }

      // Stale-state check
      const Label &curr_label = all_labels[current.label_id];
      if (is_strictly_dominated(current.node_id, dest_id,
                                pareto_labels[current.node_id], curr_label.cost,
                                curr_label.arrival_time, current.hops)) {
        continue;
      }

      if (current.hops >= max_hops) {
        continue;
      }

      for (const flight &edge : adjacency_list[current.node_id]) {
        metrics.edges_relaxed++;
        uint32_t next_node = edge.destination_id;

        uint32_t required_departure =
            current.current_time + nodes[current.node_id].min_transfer;
        if (current.node_id == source_id) {
          required_departure = current.current_time;
        }

        uint32_t absolute_flight_departure =
            edge.flight_day * 1440 + edge.departure_time;

        if (absolute_flight_departure < required_departure) {
          continue;
        }

        uint32_t wait_time = absolute_flight_departure - current.current_time;
        if (wait_time > 1440) {
          continue; // Transit wait > 24 hours
        }

        uint32_t flight_duration =
            (edge.arrival_time +
             (edge.arrival_time < edge.departure_time ? 1440 : 0)) -
            edge.departure_time;
        uint32_t next_time = absolute_flight_departure + flight_duration;

        uint32_t new_g = 0;
        if (mode == OptimizeFor::PRICE) {
          new_g = current.g_score + edge.price;
        } else {
          new_g = next_time - start_time;
        }

        if (new_g > std::numeric_limits<uint16_t>::max()) {
          continue;
        }

        uint8_t next_hops = current.hops + 1;

        // Pareto dominance check
        if (is_dominated(next_node, dest_id, pareto_labels[next_node], new_g,
                         next_time, next_hops)) {
          continue;
        }

        prune_dominated(next_node, dest_id, pareto_labels[next_node], new_g,
                        next_time, next_hops);
        pareto_labels[next_node].push_back({new_g, next_time, next_hops});

        int32_t new_label_id = static_cast<int32_t>(all_labels.size());
        all_labels.push_back(Label{next_node, new_g, next_time,
                                   current.label_id, edge, next_hops});

        // Compute A* f_score = g(n) + h(n)
        double h_next = compute_h(next_node, dest_id, nodes, mode);
        double f_val = static_cast<double>(new_g) + h_next;
        uint16_t next_f = static_cast<uint16_t>(
            std::min<double>(f_val, static_cast<double>(std::numeric_limits<uint16_t>::max())));

        pq.push({next_time,
                 new_label_id,
                 next_f,
                 static_cast<uint16_t>(new_g),
                 static_cast<uint16_t>(next_node),
                 next_hops,
                 0});
      }
    }

    return {false, 0, 0, {}, metrics};
  }

private:
  bool is_dominated(uint32_t node_id, uint32_t dest_id,
                    const std::vector<ParetoPoint> &labels, uint32_t cost,
                    uint32_t time, uint8_t hops) const {
    for (const ParetoPoint &p : labels) {
      if (p.cost <= cost && p.arrival_time <= time && p.hops <= hops) {
        if (node_id == dest_id || (time - p.arrival_time <= 1440)) {
          return true;
        }
      }
    }
    return false;
  }

  bool is_strictly_dominated(uint32_t node_id, uint32_t dest_id,
                             const std::vector<ParetoPoint> &labels,
                             uint32_t cost, uint32_t time, uint8_t hops) const {
    for (const ParetoPoint &p : labels) {
      if (p.cost <= cost && p.arrival_time <= time && p.hops <= hops &&
          (p.cost < cost || p.arrival_time < time || p.hops < hops)) {
        if (node_id == dest_id || (time - p.arrival_time <= 1440)) {
          return true;
        }
      }
    }
    return false;
  }

  void prune_dominated(uint32_t node_id, uint32_t dest_id,
                       std::vector<ParetoPoint> &labels, uint32_t cost,
                       uint32_t time, uint8_t hops) {
    labels.erase(
        std::remove_if(
            labels.begin(), labels.end(),
            [node_id, dest_id, cost, time, hops](const ParetoPoint &p) {
              return p.cost >= cost && p.arrival_time >= time &&
                     p.hops >= hops &&
                     (node_id == dest_id || (p.arrival_time - time <= 1440));
            }),
        labels.end());
  }

  RoutingResult reconstruct_path(const AStarState &final_state,
                                 const std::vector<Label> &all_labels,
                                 const std::vector<airport> &nodes,
                                 uint32_t start_time,
                                 RoutingMetrics metrics) const {
    RoutingResult result;
    result.success = true;
    result.metrics = metrics;
    result.total_time_mins = final_state.current_time - start_time;
    result.total_price = 0;

    int32_t curr_id = final_state.label_id;
    while (curr_id != -1 && all_labels[curr_id].parent_label_id != -1) {
      const Label &lbl = all_labels[curr_id];
      result.path.push_back(lbl.edge_taken);
      result.total_price += lbl.edge_taken.price;
      curr_id = lbl.parent_label_id;
    }

    std::reverse(result.path.begin(), result.path.end());
    return result;
  }
};

