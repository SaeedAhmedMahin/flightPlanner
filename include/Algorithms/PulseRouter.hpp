#pragma once
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
// The Pulse Algorithm (DFS with Bounding and Rollback)
//
// Theoretical Foundation:
//   Developed by Leonardo Lozano and Andrés L. Medaglia (2013, Computers &
//   Operations Research; 2016, Transportation Science) for the Constrained
//   Shortest Path Problem (CSPP) and Elementary Shortest Path Problem with
//   Resource Constraints (ESPPRC).
//
// Key Architectural Principles:
//   1. Pulse Propagation (Recursive Depth-First Search):
//      Unlike label-setting (Dijkstra/A*) or label-correcting (Bellman-Ford)
//      algorithms that maintain massive priority queues or frontiers across all
//      nodes, the Pulse Algorithm explores paths using recursive DFS probes
//      ("pulses") with O(depth) stack space and zero heap allocations during
//      traversal.
//
//   2. Infeasibility Pruning (Resource Bounding):
//      Admissible lower bounds on travel time and financial fare to destination
//      are precomputed (via Haversine great-circle distance). A pulse is pruned
//      immediately if accumulated resources + remaining lower bounds violate
//      hard constraints (max_time_mins, max_cost, max_hops).
//
//   3. Bound Pruning (Objective Bounding against Incumbent):
//      Maintains a global incumbent upper bound of the best primal feasible
//      solution found so far. Any partial path whose accumulated objective +
//      admissible heuristic lower bound exceeds or matches the incumbent is
//      pruned immediately.
//
//   4. Rollback Pruning (Local Shortcut & Triangular Dominance):
//      Evaluates sequences of recent routing decisions. When reaching node v
//      from predecessor p whose predecessor was q (q -> p -> v), the algorithm
//      checks whether a direct connection q -> v exists within the valid
//      schedule window that arrives <= current arrival and costs <= the 2-hop
//      connection. If so, routing through p is dominated and rolled back.
//
//   5. Node Dominance Memoization:
//      Maintains non-dominated Pareto tuples (cost, time, hops) per airport.
//      Subsequent pulses reaching a node with dominated attributes are pruned.
//
//   6. Best-First Edge Ordering:
//      At each node, candidate outgoing edges are sorted by estimated total
//      cost f = g + h ascending, ensuring that the first pulse to reach the
//      destination discovers a near-optimal incumbent rapidly, maximizing bound
//      pruning for the remainder of the search space.
// ---------------------------------------------------------------------------

class PulseRouter {
public:
  const uint32_t INF = std::numeric_limits<uint32_t>::max();

  struct ResourceConstraints {
    uint32_t max_time_mins; // Elapsed travel time budget (UTC minutes)
    uint32_t max_cost;      // Financial fare budget
    uint8_t max_hops;       // Maximum flight legs / transits

    ResourceConstraints(uint32_t time_limit = 10080, uint32_t cost_limit = 50000,
                        uint8_t hops_limit = 5)
        : max_time_mins(time_limit), max_cost(cost_limit),
          max_hops(hops_limit) {}
  };

  struct PulsePoint {
    uint32_t price;
    uint32_t arrival_time;
    uint8_t hops;
  };

  struct CandidatePulse {
    const flight *edge;
    uint32_t next_time;
    uint32_t new_price;
    uint32_t new_g;
    float f_score;
  };

  struct CompareCandidate {
    bool operator()(const CandidatePulse &a, const CandidatePulse &b) const {
      if (a.f_score != b.f_score) {
        return a.f_score < b.f_score;
      }
      if (a.new_price != b.new_price) {
        return a.new_price < b.new_price;
      }
      return a.next_time < b.next_time;
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

  // Primary Run Interface
  RoutingResult run(
      uint32_t source_id, uint32_t dest_id,
      const std::vector<std::vector<flight>> &adjacency_list,
      const std::vector<airport> &nodes, OptimizeFor mode, uint32_t start_time,
      ResourceConstraints constraints = ResourceConstraints{},
      const std::vector<std::vector<uint32_t>> *static_route_graph = nullptr,
      const std::vector<std::vector<uint16_t>> *static_route_min_price = nullptr) {

    RoutingMetrics metrics = {0, 0};

    if (source_id == dest_id) {
      return {true, 0, 0, {}, metrics};
    }

    uint32_t num_nodes = static_cast<uint32_t>(adjacency_list.size());

    // 1. Precompute Admissible Lower-Bound Distances from all nodes to dest_id
    // Runs in O(V) time (~50 microseconds for 5,868 airports)
    std::vector<float> h_dist(num_nodes);
    float dest_lat = nodes[dest_id].latitude;
    float dest_lon = nodes[dest_id].longitude;
    for (uint32_t i = 0; i < num_nodes; ++i) {
      h_dist[i] = static_cast<float>(
          haversine_km(nodes[i].latitude, nodes[i].longitude, dest_lat, dest_lon));
    }

    // 2. Pre-allocate DFS Data Structures
    std::vector<std::vector<PulsePoint>> memo_pulses(num_nodes);
    std::vector<uint8_t> in_path(num_nodes, 0);
    std::vector<flight> current_path;
    current_path.reserve(constraints.max_hops + 1);

    // Reusable candidate buffer per depth level to avoid dynamic allocations
    std::vector<std::vector<CandidatePulse>> candidate_pool(constraints.max_hops + 1);
    for (auto &pool : candidate_pool) {
      pool.reserve(64);
    }

    // Best incumbent state tracking
    uint32_t incumbent_upper_bound = INF;
    uint32_t best_total_price = INF;
    uint32_t best_total_time = INF;
    std::vector<flight> best_path;

    // Mark source in active DFS path
    in_path[source_id] = 1;

    // 3. Launch Pulse (DFS Exploration)
    pulse(source_id, start_time, 0, 0,
          /* prev_node */ INF, /* prev_arrival */ start_time, /* prev_edge_price */ 0,
          /* depth */ 0,
          dest_id, start_time, mode, constraints,
          adjacency_list, nodes, h_dist, static_route_graph, static_route_min_price,
          in_path, current_path, candidate_pool, memo_pulses,
          incumbent_upper_bound, best_total_price, best_total_time, best_path,
          metrics);

    in_path[source_id] = 0; // Rollback source

    if (incumbent_upper_bound == INF) {
      return {false, 0, 0, {}, metrics};
    }

    RoutingResult result;
    result.success = true;
    result.total_price = best_total_price;
    result.total_time_mins = best_total_time;
    result.path = std::move(best_path);
    result.metrics = metrics;
    return result;
  }

  // Overload supporting static route graph for accelerated rollback lookups
  RoutingResult run(
      uint32_t source_id, uint32_t dest_id,
      const std::vector<std::vector<flight>> &adjacency_list,
      const std::vector<std::vector<uint32_t>> &static_route_graph,
      const std::vector<std::vector<uint16_t>> &static_route_min_price,
      const std::vector<airport> &nodes, OptimizeFor mode, uint32_t start_time,
      ResourceConstraints constraints = ResourceConstraints{}) {
    return run(source_id, dest_id, adjacency_list, nodes, mode, start_time,
               constraints, &static_route_graph, &static_route_min_price);
  }

  // Overload matching standard router signature (max_hops)
  RoutingResult run(
      uint32_t source_id, uint32_t dest_id,
      const std::vector<std::vector<flight>> &adjacency_list,
      const std::vector<airport> &nodes, OptimizeFor mode, uint32_t start_time,
      uint8_t max_hops) {
    return run(source_id, dest_id, adjacency_list, nodes, mode, start_time,
               ResourceConstraints{10080, 50000, max_hops});
  }

private:
  void pulse(
      uint32_t u, uint32_t current_time, uint32_t current_price, uint8_t current_hops,
      uint32_t prev_node, uint32_t prev_arrival, uint32_t prev_edge_price,
      uint8_t depth,
      uint32_t dest_id, uint32_t start_time, OptimizeFor mode,
      const ResourceConstraints &constraints,
      const std::vector<std::vector<flight>> &adjacency_list,
      const std::vector<airport> &nodes,
      const std::vector<float> &h_dist,
      const std::vector<std::vector<uint32_t>> *static_graph,
      const std::vector<std::vector<uint16_t>> *static_min_price,
      std::vector<uint8_t> &in_path,
      std::vector<flight> &current_path,
      std::vector<std::vector<CandidatePulse>> &candidate_pool,
      std::vector<std::vector<PulsePoint>> &memo_pulses,
      uint32_t &incumbent_upper_bound,
      uint32_t &best_total_price,
      uint32_t &best_total_time,
      std::vector<flight> &best_path,
      RoutingMetrics &metrics) {

    metrics.nodes_explored++;

    // Reached max hops capacity
    if (current_hops >= constraints.max_hops) {
      return;
    }

    uint32_t required_departure = current_time + nodes[u].min_transfer;
    if (current_path.empty()) {
      required_departure = current_time; // First departure has no layover transfer requirement
    }

    int32_t origin_utc_offset = nodes[u].utc_offset_15m * 15;
    auto &candidates = candidate_pool[depth];
    candidates.clear();

    // -----------------------------------------------------------------------
    // CANDIDATE EXPANSION & MULTI-OBJECTIVE LOOK-AHEAD PRUNING
    // -----------------------------------------------------------------------
    for (const flight &edge : adjacency_list[u]) {
      metrics.edges_relaxed++;
      uint32_t next_node = edge.destination_id;

      // Cycle prevention (elementarity constraint)
      if (in_path[next_node]) {
        continue;
      }

      // Schedule alignment in unified UTC minutes
      uint32_t abs_dep = static_cast<uint32_t>(
          static_cast<int32_t>(edge.flight_day * 1440 + edge.departure_time) -
          origin_utc_offset);

      if (abs_dep < required_departure) {
        continue; // Flight has already departed
      }

      uint32_t wait_time = abs_dep - current_time;
      if (wait_time > 1440) {
        continue; // Layover exceeds 24-hour window
      }

      uint32_t next_time = abs_dep + edge.duration_mins;
      uint32_t new_elapsed = next_time - start_time;
      uint32_t new_price = current_price + edge.price;
      uint8_t next_hops = current_hops + 1;

      // Admissible remaining bounds from next_node to dest_id
      float h_cost_remain = h_dist[next_node] * 0.018f;
      float h_time_remain = h_dist[next_node] * 0.060f;

      // 1. Infeasibility Pruning: Time resource
      if (new_elapsed + static_cast<uint32_t>(h_time_remain) > constraints.max_time_mins) {
        continue;
      }

      // 2. Infeasibility Pruning: Cost resource
      if (new_price + static_cast<uint32_t>(h_cost_remain) > constraints.max_cost) {
        continue;
      }

      // 3. Infeasibility Pruning: Hop resource
      uint8_t min_hops_remain = (next_node == dest_id) ? 0 : 1;
      if (next_hops + min_hops_remain > constraints.max_hops) {
        continue;
      }

      // 4. Bound Pruning against Incumbent
      uint32_t new_g = (mode == OptimizeFor::PRICE) ? new_price : new_elapsed;
      float h_primary = (mode == OptimizeFor::PRICE) ? h_cost_remain : h_time_remain;
      float estimated_total = static_cast<float>(new_g) + h_primary;

      if (estimated_total >= static_cast<float>(incumbent_upper_bound)) {
        continue;
      }

      // 5. Rollback Pruning (Triangular Predecessor Shortcut Check)
      // If we have a predecessor (prev_node -> u -> next_node), check whether
      // prev_node had a direct connection to next_node that dominates this 2-hop sequence
      if (prev_node != INF && !current_path.empty()) {
        if (is_rollback_dominated(prev_node, prev_arrival, prev_edge_price,
                                  next_node, next_time, edge.price,
                                  adjacency_list, nodes,
                                  static_graph, static_min_price)) {
          continue; // Pruned by Rollback!
        }
      }

      // 6. Node Dominance Check
      if (is_dominated(next_node, dest_id, memo_pulses[next_node], new_price,
                       next_time, next_hops)) {
        continue;
      }

      candidates.push_back({&edge, next_time, new_price, new_g, estimated_total});
    }

    if (candidates.empty()) {
      return;
    }

    // -----------------------------------------------------------------------
    // BEST-FIRST SORTING: Guide DFS towards the most promising branch first
    // -----------------------------------------------------------------------
    std::sort(candidates.begin(), candidates.end(), CompareCandidate{});

    // -----------------------------------------------------------------------
    // RECURSIVE PULSE PROPAGATION & ROLLBACK (BACKTRACKING)
    // -----------------------------------------------------------------------
    for (const auto &cand : candidates) {
      // Bound Pruning: Once an incumbent is tightened, subsequent candidates
      // with higher f_score can be broken out of immediately.
      if (cand.f_score >= static_cast<float>(incumbent_upper_bound)) {
        break;
      }

      uint32_t next_node = cand.edge->destination_id;

      // BASE CASE: Destination reached!
      if (next_node == dest_id) {
        bool is_better = false;
        if (cand.new_g < incumbent_upper_bound) {
          is_better = true;
        } else if (cand.new_g == incumbent_upper_bound) {
          // Tie-breakers consistent with Dijkstra / A*
          if (mode == OptimizeFor::PRICE) {
            uint32_t elapsed = cand.next_time - start_time;
            if (current_path.size() + 1 < best_path.size() ||
                (current_path.size() + 1 == best_path.size() && elapsed < best_total_time)) {
              is_better = true;
            }
          } else {
            if (cand.new_price < best_total_price ||
                (cand.new_price == best_total_price && current_path.size() + 1 < best_path.size())) {
              is_better = true;
            }
          }
        }

        if (is_better) {
          incumbent_upper_bound = cand.new_g;
          best_total_price = cand.new_price;
          best_total_time = cand.next_time - start_time;
          best_path = current_path;
          best_path.push_back(*(cand.edge));
        }
        continue;
      }

      // Update Node Pareto Memoization
      prune_dominated(next_node, dest_id, memo_pulses[next_node], cand.new_price,
                      cand.next_time, current_hops + 1);
      memo_pulses[next_node].push_back(
          {cand.new_price, cand.next_time, static_cast<uint8_t>(current_hops + 1)});

      // Push state
      current_path.push_back(*(cand.edge));
      in_path[next_node] = 1;

      // Recursive DFS descent
      pulse(next_node, cand.next_time, cand.new_price, current_hops + 1,
            u, current_time, cand.edge->price,
            depth + 1,
            dest_id, start_time, mode, constraints,
            adjacency_list, nodes, h_dist, static_graph, static_min_price,
            in_path, current_path, candidate_pool, memo_pulses,
            incumbent_upper_bound, best_total_price, best_total_time, best_path,
            metrics);

      // STATE ROLLBACK (Backtracking)
      in_path[next_node] = 0;
      current_path.pop_back();
    }
  }

  // Check if a 2-hop sequence (prev_origin -> intermediate -> curr_dest)
  // is dominated by an existing direct flight from prev_origin to curr_dest
  bool is_rollback_dominated(
      uint32_t prev_origin, uint32_t prev_arrival, uint32_t prev_edge_price,
      uint32_t curr_dest, uint32_t curr_arrival, uint32_t curr_edge_price,
      const std::vector<std::vector<flight>> &adj,
      const std::vector<airport> &nodes,
      const std::vector<std::vector<uint32_t>> *static_graph,
      const std::vector<std::vector<uint16_t>> *static_min_price) const {

    // Fast O(log(deg)) corridor lookup via static route graph if provided
    if (static_graph && static_min_price) {
      const auto &neighbors = (*static_graph)[prev_origin];
      auto it = std::lower_bound(neighbors.begin(), neighbors.end(), curr_dest);
      if (it == neighbors.end() || *it != curr_dest) {
        return false; // No direct corridor exists between prev_origin and curr_dest
      }
      size_t idx = std::distance(neighbors.begin(), it);
      uint32_t min_direct_price = (*static_min_price)[prev_origin][idx];
      if (min_direct_price > prev_edge_price + curr_edge_price) {
        return false; // Even cheapest direct flight cannot beat the combined fare
      }
    }

    uint32_t req_dep = prev_arrival + nodes[prev_origin].min_transfer;
    int32_t origin_utc_offset = nodes[prev_origin].utc_offset_15m * 15;
    uint32_t two_leg_price = prev_edge_price + curr_edge_price;

    for (const flight &df : adj[prev_origin]) {
      if (df.destination_id != curr_dest) {
        continue;
      }

      uint32_t abs_dep = static_cast<uint32_t>(
          static_cast<int32_t>(df.flight_day * 1440 + df.departure_time) -
          origin_utc_offset);

      if (abs_dep < req_dep) {
        continue;
      }
      if (abs_dep - prev_arrival > 1440) {
        continue;
      }

      uint32_t direct_arrival = abs_dep + df.duration_mins;
      if (direct_arrival <= curr_arrival && df.price <= two_leg_price) {
        // Direct flight arrives earlier/equal, costs less/equal, and saves 1 transit leg.
        // The 2-hop path is strictly dominated.
        return true;
      }
    }
    return false;
  }

  // Pareto Dominance Check
  bool is_dominated(uint32_t node_id, uint32_t dest_id,
                    const std::vector<PulsePoint> &labels, uint32_t price,
                    uint32_t time, uint8_t hops) const {
    for (const auto &p : labels) {
      if (p.price <= price && p.arrival_time <= time && p.hops <= hops) {
        if (node_id == dest_id || (time - p.arrival_time <= 1440)) {
          return true;
        }
      }
    }
    return false;
  }

  // Pareto Frontier Pruning
  void prune_dominated(uint32_t node_id, uint32_t dest_id,
                       std::vector<PulsePoint> &labels, uint32_t price,
                       uint32_t time, uint8_t hops) {
    labels.erase(
        std::remove_if(labels.begin(), labels.end(),
                       [node_id, dest_id, price, time, hops](const PulsePoint &p) {
                         return p.price >= price && p.arrival_time >= time &&
                                p.hops >= hops &&
                                (node_id == dest_id ||
                                 (p.arrival_time - time <= 1440));
                       }),
        labels.end());
  }
};

using Pulse = PulseRouter;
