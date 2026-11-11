#pragma once
#include "../DataStructures/airport.hpp"
#include "../DataStructures/flight.hpp"
#include "Dijkstra.hpp" // For OptimizeFor, RoutingMetrics, RoutingResult
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <queue>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// TwoPhaseHubRouter: Hub-and-Spoke Hierarchical Two-Phase Routing Engine
//
// Classical Hub-and-Spoke Aviation Model:
//   In commercial air transport, the vast majority of long-haul and regional
//   itineraries do not traverse arbitrary random networks. Instead, they flow
//   through high-degree, high-connectivity international and continental hubs:
//
//       Source (Spoke) -> Hub_1 -> [Hub_2] -> Destination (Spoke)
//
// Architecture:
//   Phase 1 (Hierarchical Hub-and-Spoke Topology Generation):
//     - Classifies the global airport graph into "Hubs" based on degree / connectivity
//       (degree >= hub_threshold, typically >= 40 unique destinations).
//     - Rapidly constructs corridors strictly adhering to aviation hub topology:
//       1. Direct non-stop: [Source, Dest] (if direct corridor exists)
//       2. 1-Stop via Hub:  [Source, H, Dest] for each H in Hubs
//       3. 2-Stop via Hubs: [Source, H1, H2, Dest] for each (H1, H2) in Hubs
//       4. Feeder Spokes:   If Source or Dest is a regional spoke, connects through
//          their top gateway hubs into the inter-hub trunk network.
//     - Prunes corridors using lower-bound flight costs / haversine bounds.
//
//   Phase 2 (Timetable Sweep along Hub Corridors):
//     - Evaluates candidate scheduled flights strictly along candidate hub
//       corridors with Pareto dominance and transfer friction.
// ---------------------------------------------------------------------------
class TwoPhaseHubRouter {
public:
  const uint32_t INF = std::numeric_limits<uint32_t>::max();

  struct Config {
    uint32_t hub_degree_threshold;
    uint32_t max_corridors;
    Config(uint32_t hub_deg = 35, uint32_t corridors = 150)
        : hub_degree_threshold(hub_deg), max_corridors(corridors) {}
  };

  static double haversine_km(float lat1, float lon1, float lat2, float lon2) {
    constexpr double R = 6371.0;
    constexpr double DEG_TO_RAD = 3.14159265358979323846 / 180.0;

    double dlat = (lat2 - lat1) * DEG_TO_RAD;
    double dlon = (lon2 - lon1) * DEG_TO_RAD;
    double rlat1 = lat1 * DEG_TO_RAD;
    double rlat2 = lat2 * DEG_TO_RAD;

    double a = std::sin(dlat / 2.0) * std::sin(dlat / 2.0) +
               std::cos(rlat1) * std::cos(rlat2) * std::sin(dlon / 2.0) *
                   std::sin(dlon / 2.0);
    double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return R * c;
  }

  RoutingResult
  run(uint32_t source_id, uint32_t dest_id,
      const std::vector<std::vector<flight>> &adjacency_list,
      const std::vector<std::vector<uint32_t>> &static_route_graph,
      const std::vector<std::vector<uint16_t>> &static_route_min_price,
      const std::vector<std::vector<uint16_t>> &static_route_min_duration,
      const std::vector<airport> &nodes, OptimizeFor mode, uint32_t start_time,
      Config config = Config{}) {

    RoutingMetrics metrics = {0, 0};

    if (source_id == dest_id) {
      return {true, 0, 0, {}, metrics};
    }

    // =========================================================================
    // PHASE 1: HIERARCHICAL HUB-AND-SPOKE CORRIDOR GENERATION
    // =========================================================================
    std::vector<std::vector<uint32_t>> corridors;
    corridors.reserve(config.max_corridors);

    find_hub_corridors(source_id, dest_id, static_route_graph,
                       static_route_min_price, static_route_min_duration,
                       nodes, mode, config.hub_degree_threshold,
                       config.max_corridors, corridors, metrics);

    if (corridors.empty()) {
      return {false, 0, 0, {}, metrics};
    }

    // =========================================================================
    // PHASE 2: LOCALIZED TIMETABLE FORWARD SWEEP
    // =========================================================================
    RoutingResult best_result;
    best_result.success = false;
    best_result.total_price = INF;
    best_result.total_time_mins = INF;

    for (const auto &corridor : corridors) {
      evaluate_corridor(corridor, adjacency_list, nodes, mode, start_time,
                        best_result, metrics);
    }

    best_result.metrics = metrics;
    return best_result;
  }

private:
  struct RatedCorridor {
    double estimated_total;
    std::vector<uint32_t> path;

    bool operator>(const RatedCorridor &other) const {
      return estimated_total > other.estimated_total;
    }
  };

  // Helper to lookup static edge metrics
  static bool get_edge_metrics(uint32_t u, uint32_t v,
                               const std::vector<std::vector<uint32_t>> &static_graph,
                               const std::vector<std::vector<uint16_t>> &static_min_price,
                               const std::vector<std::vector<uint16_t>> &static_min_duration,
                               uint16_t &price, uint16_t &dur) {
    const auto &neighbors = static_graph[u];
    // Binary search because static_graph neighbors are sorted
    auto it = std::lower_bound(neighbors.begin(), neighbors.end(), v);
    if (it != neighbors.end() && *it == v) {
      size_t idx = std::distance(neighbors.begin(), it);
      price = static_min_price[u][idx];
      dur = static_min_duration[u][idx];
      return true;
    }
    return false;
  }

  void find_hub_corridors(
      uint32_t source_id, uint32_t dest_id,
      const std::vector<std::vector<uint32_t>> &static_graph,
      const std::vector<std::vector<uint16_t>> &static_min_price,
      const std::vector<std::vector<uint16_t>> &static_min_duration,
      const std::vector<airport> &nodes, OptimizeFor mode,
      uint32_t hub_degree_threshold, uint32_t max_corridors,
      std::vector<std::vector<uint32_t>> &out_corridors,
      RoutingMetrics &metrics) {

    const size_t num_nodes = nodes.size();
    std::vector<bool> is_hub(num_nodes, false);
    for (size_t i = 0; i < num_nodes; ++i) {
      if (static_graph[i].size() >= hub_degree_threshold) {
        is_hub[i] = true;
      }
    }

    // Min-heap to rank candidate hub corridors by estimated lower bound cost/time
    std::priority_queue<RatedCorridor, std::vector<RatedCorridor>,
                        std::greater<RatedCorridor>>
        pq;

    // Helper to evaluate corridor estimate and push to PQ
    auto consider_corridor = [&](const std::vector<uint32_t> &p) {
      double acc_score = 0;
      for (size_t i = 0; i + 1 < p.size(); ++i) {
        uint16_t price = 0, dur = 0;
        if (!get_edge_metrics(p[i], p[i + 1], static_graph, static_min_price,
                             static_min_duration, price, dur)) {
          return;
        }
        acc_score += (mode == OptimizeFor::PRICE) ? price : dur;
      }
      pq.push(RatedCorridor{acc_score, p});
    };

    // 1. Direct Non-stop Corridor: Source -> Dest
    uint16_t direct_p = 0, direct_d = 0;
    if (get_edge_metrics(source_id, dest_id, static_graph, static_min_price,
                         static_min_duration, direct_p, direct_d)) {
      consider_corridor({source_id, dest_id});
    }

    // 2. Identify Hubs directly reachable from Source, and Hubs that directly reach Dest
    // In addition, if Source itself is a small spoke, we also allow 1 hop feeder spoke -> Hub
    std::vector<uint32_t> source_hubs;
    std::vector<uint32_t> dest_in_hubs;

    for (uint32_t v : static_graph[source_id]) {
      metrics.nodes_explored++;
      if (is_hub[v] && v != dest_id) {
        source_hubs.push_back(v);
      }
    }

    // Incoming connections to Dest: check all hubs
    for (size_t h = 0; h < num_nodes; ++h) {
      if (is_hub[h] && h != source_id && h != dest_id) {
        uint16_t p = 0, d = 0;
        if (get_edge_metrics(static_cast<uint32_t>(h), dest_id, static_graph,
                             static_min_price, static_min_duration, p, d)) {
          dest_in_hubs.push_back(static_cast<uint32_t>(h));
        }
      }
    }

    // 3. 1-Stop Hub Routes: Source -> H -> Dest
    for (uint32_t h : source_hubs) {
      uint16_t p = 0, d = 0;
      if (get_edge_metrics(h, dest_id, static_graph, static_min_price,
                           static_min_duration, p, d)) {
        consider_corridor({source_id, h, dest_id});
      }
    }

    // 4. 2-Stop Inter-Hub Trunk Routes: Source -> H1 -> H2 -> Dest
    // For each H1 reachable from Source, and H2 that can reach Dest:
    // If H1 == H2, it's 1-stop (already handled).
    // If H1 != H2, check if direct flight corridor H1 -> H2 exists.
    for (uint32_t h1 : source_hubs) {
      for (uint32_t h2 : dest_in_hubs) {
        if (h1 == h2) continue;
        uint16_t p12 = 0, d12 = 0;
        if (get_edge_metrics(h1, h2, static_graph, static_min_price,
                             static_min_duration, p12, d12)) {
          consider_corridor({source_id, h1, h2, dest_id});
        }
      }
    }

    // 5. Spoke Feeder Fallback:
    // If Source is not a hub and has few direct hub connections, expand 1 spoke layer
    if (source_hubs.size() < 3) {
      for (uint32_t spoke : static_graph[source_id]) {
        if (!is_hub[spoke] && spoke != dest_id) {
          for (uint32_t h : static_graph[spoke]) {
            if (is_hub[h] && h != dest_id) {
              uint16_t p = 0, d = 0;
              if (get_edge_metrics(h, dest_id, static_graph, static_min_price,
                                   static_min_duration, p, d)) {
                consider_corridor({source_id, spoke, h, dest_id});
              }
            }
          }
        }
      }
    }

    // 6. Select Top Corridors from PQ up to max_corridors
    std::unordered_map<uint32_t, uint32_t> gateway_usage;
    while (!pq.empty() && out_corridors.size() < max_corridors) {
      RatedCorridor top = pq.top();
      pq.pop();

      // Ensure gateway diversity for multi-stop routes
      if (top.path.size() > 3) {
        uint32_t gw = top.path[1];
        if (gateway_usage[gw] >= 8) {
          continue;
        }
        gateway_usage[gw]++;
      }

      out_corridors.push_back(std::move(top.path));
    }
  }

  // Phase 2: Localized Timetable Forward Sweep
  struct TimetableState {
    uint32_t current_time;
    uint32_t total_price;
    std::vector<flight> flights_taken;
  };

  void evaluate_corridor(const std::vector<uint32_t> &corridor,
                         const std::vector<std::vector<flight>> &adjacency_list,
                         const std::vector<airport> &nodes, OptimizeFor mode,
                         uint32_t start_time, RoutingResult &best_result,
                         RoutingMetrics &metrics) {

    std::vector<TimetableState> current_states;
    current_states.push_back({start_time, 0, {}});

    for (size_t hop = 0; hop + 1 < corridor.size(); ++hop) {
      uint32_t u = corridor[hop];
      uint32_t v = corridor[hop + 1];

      std::vector<TimetableState> next_states;

      for (const auto &state : current_states) {
        uint32_t req_dep = state.current_time + nodes[u].min_transfer;
        if (hop == 0) {
          req_dep = state.current_time;
        }

        for (const flight &f : adjacency_list[u]) {
          if (f.destination_id != v)
            continue;
          metrics.edges_relaxed++;

          int32_t origin_utc_offset = nodes[u].utc_offset_15m * 15;
          uint32_t abs_dep = static_cast<uint32_t>(
              static_cast<int32_t>(f.flight_day * 1440 + f.departure_time) -
              origin_utc_offset);

          if (abs_dep < req_dep)
            continue;

          uint32_t wait_time = abs_dep - state.current_time;
          if (wait_time > 1440)
            continue;

          uint32_t dur = f.duration_mins;
          uint32_t next_time = abs_dep + dur;
          uint32_t next_price = state.total_price + f.price;

          bool is_final_hop = (hop + 1 == corridor.size() - 1);
          bool dominated = false;
          for (const auto &existing : next_states) {
            if (existing.total_price <= next_price &&
                existing.current_time <= next_time) {
              if (is_final_hop || (next_time - existing.current_time <= 1440)) {
                dominated = true;
                break;
              }
            }
          }
          if (dominated)
            continue;

          next_states.erase(
              std::remove_if(next_states.begin(), next_states.end(),
                             [is_final_hop, next_price,
                              next_time](const TimetableState &s) {
                               return s.total_price >= next_price &&
                                      s.current_time >= next_time &&
                                      (is_final_hop ||
                                       (s.current_time - next_time <= 1440));
                             }),
              next_states.end());

          std::vector<flight> new_path = state.flights_taken;
          new_path.push_back(f);
          next_states.push_back({next_time, next_price, std::move(new_path)});
        }
      }

      current_states = std::move(next_states);
      if (current_states.empty()) {
        return;
      }
    }

    for (const auto &final_state : current_states) {
      uint32_t journey_time = final_state.current_time - start_time;

      bool is_better = false;
      if (!best_result.success) {
        is_better = true;
      } else if (mode == OptimizeFor::PRICE) {
        if (final_state.total_price < best_result.total_price ||
            (final_state.total_price == best_result.total_price &&
             journey_time < best_result.total_time_mins)) {
          is_better = true;
        }
      } else {
        if (journey_time < best_result.total_time_mins ||
            (journey_time == best_result.total_time_mins &&
             final_state.total_price < best_result.total_price)) {
          is_better = true;
        }
      }

      if (is_better) {
        best_result.success = true;
        best_result.total_price = final_state.total_price;
        best_result.total_time_mins = journey_time;
        best_result.path = final_state.flights_taken;
      } else if (best_result.success && final_state.total_price == best_result.total_price) {
        if (final_state.flights_taken.size() < best_result.path.size() ||
            (final_state.flights_taken.size() == best_result.path.size() &&
             journey_time < best_result.total_time_mins)) {
          best_result.total_time_mins = journey_time;
          best_result.path = final_state.flights_taken;
        }
      }
    }
  }
};

