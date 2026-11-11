#pragma once
#include "../DataStructures/Flat4AryMinHeap.hpp"
#include "../DataStructures/airport.hpp"
#include "../DataStructures/flight.hpp"
#include "Dijkstra.hpp"
#include "SearchState.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

// ---------------------------------------------------------------------------
// RC-A* (Resource-Constrained A* with Multi-Dimensional Heuristic Look-Ahead)
//
// The Resource-Constrained Shortest Path Problem (RCSPP / SPPRC):
//   Finds the optimal path for a primary objective (e.g. ticket price)
//   subject to hard auxiliary resource budgets:
//     1. Maximum elapsed travel time budget: T_max
//     2. Maximum financial cost budget:      C_max
//     3. Maximum flight transits / hops:     H_max
//
// Heuristic Look-Ahead (Multi-Dimensional Admissible Bounds):
//   At any intermediate airport n, RC-A* computes dual admissible look-aheads:
//     - h_cost(n):   Lower bound on flight fare remaining to destination
//     - h_time(n):   Lower bound on flight duration remaining to destination
//     - h_hops(n):   Lower bound on legs remaining (0 if at dest, >= 1 otherwise)
//
// Early Look-Ahead Pruning:
//   A candidate state is pruned BEFORE pushing to the priority queue if:
//     1. Infeasible Time Look-Ahead:
//        g_time(n) + h_time(n) > T_max
//     2. Infeasible Cost Look-Ahead:
//        g_cost(n) + h_cost(n) > C_max
//     3. Infeasible Hop Look-Ahead:
//        g_hops(n) + h_hops(n) > H_max
//     4. Incumbent Upper-Bound Look-Ahead:
//        Once a feasible path to the destination is discovered, any state whose
//        look-ahead primary cost f(n) = g(n) + h(n) exceeds the incumbent is pruned.
// ---------------------------------------------------------------------------
class RCAStarRouter {
public:
  const uint32_t INF = std::numeric_limits<uint32_t>::max();

  struct ResourceConstraints {
    uint32_t max_time_mins; // e.g. 2880 mins (48 hours)
    uint32_t max_cost;      // e.g. $2000
    uint8_t max_hops;       // e.g. 5 legs

    ResourceConstraints(uint32_t time_limit = 2880, uint32_t cost_limit = 50000,
                        uint8_t hops_limit = 5)
        : max_time_mins(time_limit), max_cost(cost_limit),
          max_hops(hops_limit) {}
  };

  struct RCState {
    uint32_t current_time; // Absolute arrival minute in UTC
    int32_t label_id;      // Index in all_labels
    uint16_t f_score;      // Primary priority: g(n) + h(n)
    uint16_t g_score;      // Primary accumulated objective
    uint16_t node_id;      // Airport node ID
    uint8_t hops;          // Flight legs taken
    uint8_t padding;       // 16-byte alignment
  };
  static_assert(sizeof(RCState) == 16, "RCState must be 16 bytes");

  struct CompareRCState {
    bool operator()(const RCState &a, const RCState &b) const {
      if (a.f_score != b.f_score) {
        return a.f_score < b.f_score;
      }
      if (a.hops != b.hops) {
        return a.hops < b.hops;
      }
      return a.g_score > b.g_score;
    }
  };

  static inline double haversine_km(float lat1, float lon1, float lat2,
                                    float lon2) {
    const double R = 6371.0;
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    double a = std::sin(dLat / 2.0) * std::sin(dLat / 2.0) +
               std::cos(lat1 * M_PI / 180.0) * std::cos(lat2 * M_PI / 180.0) *
                   std::sin(dLon / 2.0) * std::sin(dLon / 2.0);
    double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return R * c;
  }

  // Dual Admissible Look-Aheads
  static inline double compute_h_cost(uint32_t u, uint32_t dest,
                                      const std::vector<airport> &nodes) {
    if (u == dest) return 0.0;
    double dist = haversine_km(nodes[u].latitude, nodes[u].longitude,
                               nodes[dest].latitude, nodes[dest].longitude);
    return dist * 0.018; // $0.018 / km lower bound
  }

  static inline double compute_h_time(uint32_t u, uint32_t dest,
                                      const std::vector<airport> &nodes) {
    if (u == dest) return 0.0;
    double dist = haversine_km(nodes[u].latitude, nodes[u].longitude,
                               nodes[dest].latitude, nodes[dest].longitude);
    return dist * 0.060; // 1000 km/h speed lower bound (~0.060 min / km)
  }

  RoutingResult run(uint32_t source_id, uint32_t dest_id,
                    const std::vector<std::vector<flight>> &adjacency_list,
                    const std::vector<airport> &nodes, OptimizeFor mode,
                    uint32_t start_time,
                    ResourceConstraints constraints = ResourceConstraints{}) {

    RoutingMetrics metrics = {0, 0};

    if (source_id == dest_id) {
      return {true, 0, 0, {}, metrics};
    }

    uint32_t num_nodes = adjacency_list.size();
    std::vector<std::vector<ParetoPoint>> pareto_labels(num_nodes);
    std::vector<Label> all_labels;
    all_labels.reserve(65536);

    Flat4AryMinHeap<RCState, CompareRCState> pq(65536);

    int32_t source_label_id = static_cast<int32_t>(all_labels.size());
    all_labels.push_back(Label{source_id, 0, start_time, -1, flight{}, 0});
    pareto_labels[source_id].push_back({0, start_time, 0});

    double h_initial = (mode == OptimizeFor::PRICE)
                           ? compute_h_cost(source_id, dest_id, nodes)
                           : compute_h_time(source_id, dest_id, nodes);

    pq.push({start_time, source_label_id, static_cast<uint16_t>(h_initial), 0,
             static_cast<uint16_t>(source_id), 0, 0});

    uint32_t incumbent_upper_bound = INF;
    int32_t best_dest_label_id = -1;

    while (!pq.empty()) {
      RCState current = pq.top();
      pq.pop();
      metrics.nodes_explored++;

      // Incumbent look-ahead pruning
      if (current.f_score >= incumbent_upper_bound) {
        continue;
      }

      if (current.node_id == dest_id) {
        if (current.g_score < incumbent_upper_bound) {
          incumbent_upper_bound = current.g_score;
          best_dest_label_id = current.label_id;
        }
        // First pop of destination in A* is the guaranteed optimal
        break;
      }

      const Label &curr_label = all_labels[current.label_id];
      if (is_strictly_dominated(current.node_id, dest_id,
                                pareto_labels[current.node_id], curr_label.cost,
                                curr_label.arrival_time, current.hops)) {
        continue;
      }

      if (current.hops >= constraints.max_hops) {
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

        int32_t origin_utc_offset = nodes[current.node_id].utc_offset_15m * 15;
        uint32_t absolute_flight_departure = static_cast<uint32_t>(
            static_cast<int32_t>(edge.flight_day * 1440 + edge.departure_time) -
            origin_utc_offset);

        if (absolute_flight_departure < required_departure) {
          continue;
        }

        uint32_t wait_time = absolute_flight_departure - current.current_time;
        if (wait_time > 1440) {
          continue; // Layover > 24 hours
        }

        uint32_t flight_duration = edge.duration_mins;
        uint32_t next_time = absolute_flight_departure + flight_duration;

        uint32_t new_price = 0;
        uint32_t new_elapsed_time = next_time - start_time;
        uint32_t new_g = 0;

        if (mode == OptimizeFor::PRICE) {
          new_price = current.g_score + edge.price;
          new_g = new_price;
        } else {
          new_price = curr_label.cost + edge.price; // Cost tracked via label
          new_g = new_elapsed_time;
        }

        if (new_g > std::numeric_limits<uint16_t>::max()) {
          continue;
        }

        uint8_t next_hops = current.hops + 1;

        // =====================================================================
        // HEURISTIC LOOK-AHEAD PRUNING
        // =====================================================================
        double h_cost_remain = compute_h_cost(next_node, dest_id, nodes);
        double h_time_remain = compute_h_time(next_node, dest_id, nodes);

        // 1. Time Resource Constraint Look-Ahead:
        // If elapsed time + minimum remaining flight time > max_time_mins, prune!
        if (new_elapsed_time + h_time_remain > constraints.max_time_mins) {
          continue;
        }

        // 2. Cost Resource Constraint Look-Ahead:
        // If accumulated price + minimum remaining fare > max_cost, prune!
        if (new_price + h_cost_remain > constraints.max_cost) {
          continue;
        }

        // 3. Hop Resource Constraint Look-Ahead:
        // If hops taken + minimum hops needed > max_hops, prune!
        uint8_t min_hops_needed = (next_node == dest_id) ? 0 : 1;
        if (next_hops + min_hops_needed > constraints.max_hops) {
          continue;
        }

        // 4. Incumbent Upper-Bound Look-Ahead:
        double h_primary = (mode == OptimizeFor::PRICE) ? h_cost_remain : h_time_remain;
        double estimated_total = static_cast<double>(new_g) + h_primary;
        if (estimated_total >= incumbent_upper_bound) {
          continue;
        }

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

        uint16_t next_f = static_cast<uint16_t>(std::min<double>(
            estimated_total, static_cast<double>(std::numeric_limits<uint16_t>::max())));

        pq.push({next_time, new_label_id, next_f, static_cast<uint16_t>(new_g),
                 static_cast<uint16_t>(next_node), next_hops, 0});
      }
    }

    if (best_dest_label_id == -1) {
      return {false, 0, 0, {}, metrics};
    }

    // Path reconstruction
    RoutingResult result;
    result.success = true;
    result.metrics = metrics;
    result.total_time_mins = all_labels[best_dest_label_id].arrival_time - start_time;
    result.total_price = 0;

    int32_t curr_id = best_dest_label_id;
    while (curr_id != -1 && all_labels[curr_id].parent_label_id != -1) {
      const Label &lbl = all_labels[curr_id];
      result.path.push_back(lbl.edge_taken);
      result.total_price += lbl.edge_taken.price;
      curr_id = lbl.parent_label_id;
    }
    std::reverse(result.path.begin(), result.path.end());
    return result;
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
        std::remove_if(labels.begin(), labels.end(),
                       [node_id, dest_id, cost, time, hops](const ParetoPoint &p) {
                         return p.cost >= cost && p.arrival_time >= time &&
                                p.hops >= hops &&
                                (node_id == dest_id ||
                                 (p.arrival_time - time <= 1440));
                       }),
        labels.end());
  }
};

