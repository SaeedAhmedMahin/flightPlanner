#pragma once
#include "../DataStructures/airport.hpp"
#include "../DataStructures/flight.hpp"
#include "Dijkstra.hpp" // For OptimizeFor, RoutingMetrics, RoutingResult
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <queue>
#include <vector>

// ---------------------------------------------------------------------------
// TwoPhaseRouter: "Route-First, Schedule-Second" Routing Engine
//
// Phase 1 (Topology Discovery):
//   Explores the static, time-independent airport graph to discover candidate
//   airport corridors (e.g., DAC -> DEL -> JFK) within a maximum hop limit
//   (e.g., up to 3 layovers / 4 legs), guided by geodesic distance.
//
// Phase 2 (Timetable Sweep):
//   Evaluates the actual scheduled flights ONLY along each discovered
//   corridor using a forward time-window sweep, finding the optimal flight
//   itinerary with zero unnecessary exploration across unrelated airports.
// ---------------------------------------------------------------------------
class TwoPhaseRouter {
public:
  const uint32_t INF = std::numeric_limits<uint32_t>::max();

  struct Config {
    uint32_t max_hops;
    uint32_t max_corridors;
    Config(uint32_t hops = 5, uint32_t corridors = 150)
        : max_hops(hops), max_corridors(corridors) {}
  };

  // Haversine distance in kilometers for geographic corridor ranking
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
    // PHASE 1: DISCOVER TOPOLOGICAL AIRPORT CORRIDORS
    // =========================================================================
    // Find candidate sequences of airport IDs: [source_id, ..., dest_id]
    // using mode-aligned bounded search:
    // - For PRICE mode: cost-guided search on minimum flight fare per corridor
    // - For TIME mode: duration-guided search on minimum flight duration per
    // corridor
    std::vector<std::vector<uint32_t>> corridors;
    corridors.reserve(config.max_corridors);

    find_candidate_corridors(source_id, dest_id, static_route_graph,
                             static_route_min_price, static_route_min_duration,
                             nodes, mode, config.max_hops, config.max_corridors,
                             corridors, metrics);

    if (corridors.empty()) {
      return {false, 0, 0, {}, metrics};
    }

    // =========================================================================
    // PHASE 2: LOCALIZED TIMETABLE FORWARD SWEEP
    // =========================================================================
    // For each candidate corridor, evaluate all connecting flights along that
    // specific sequence and pick the optimal valid flight combination.
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
  // Candidate Corridor State for Priority Queue in Phase 1
  // Stack-allocated path to eliminate all malloc/heap allocations in the PQ
  struct PathCandidate {
    double estimated_total; // Heuristic estimate (cost or distance)
    double score_so_far;    // Accumulated score (cost or distance)
    uint8_t path_len;
    uint32_t path[6]; // Up to 5 hops = at most 6 airports

    bool operator>(const PathCandidate &other) const {
      return estimated_total > other.estimated_total;
    }
  };

  void find_candidate_corridors(
      uint32_t source_id, uint32_t dest_id,
      const std::vector<std::vector<uint32_t>> &static_graph,
      const std::vector<std::vector<uint16_t>> &static_min_price,
      const std::vector<std::vector<uint16_t>> &static_min_duration,
      const std::vector<airport> &nodes, OptimizeFor mode, uint32_t max_hops,
      uint32_t max_corridors, std::vector<std::vector<uint32_t>> &corridors,
      RoutingMetrics &metrics) {

    const airport &dest_apt = nodes[dest_id];

    std::priority_queue<PathCandidate, std::vector<PathCandidate>,
                        std::greater<PathCandidate>>
        pq;

    double h_start =
        haversine_km(nodes[source_id].latitude, nodes[source_id].longitude,
                     dest_apt.latitude, dest_apt.longitude);

    PathCandidate root;
    root.score_so_far = 0.0;
    root.path_len = 1;
    root.path[0] = source_id;

    if (mode == OptimizeFor::PRICE) {
      // Conservative lower bound heuristic for flight fare: $0.025 / km
      root.estimated_total = h_start * 0.025;
      pq.push(root);
    } else {
      // Flight speed lower bound (~850 km/h -> ~14.1 km/min -> ~0.070 min/km)
      root.estimated_total = h_start * 0.070;
      pq.push(root);
    }

    // Diversification tracking
    std::unordered_map<uint32_t, uint32_t> gateway_count;
    std::vector<uint8_t> expansions(nodes.size(), 0);
    std::vector<uint8_t> queued_count(nodes.size(), 0);
    std::vector<double> best_score(nodes.size(), 1e9);
    best_score[source_id] = 0.0;

    const uint32_t max_per_gateway = 5;

    while (!pq.empty() && corridors.size() < max_corridors) {
      PathCandidate current = pq.top();
      pq.pop();

      uint32_t u = current.path[current.path_len - 1];

      if (u == dest_id) {
        // Multi-hop paths (2+ layovers) share the gateway budget to ensure
        // diversity. Direct 1-stop corridors (Source -> Hub -> Dest) are always
        // included.
        if (current.path_len > 3) {
          uint32_t gw = current.path[1];
          if (gateway_count[gw] >= max_per_gateway) {
            continue;
          }
          gateway_count[gw]++;
        }
        corridors.push_back(std::vector<uint32_t>(
            current.path, current.path + current.path_len));
        continue;
      }

      if (current.path_len > max_hops) {
        continue; // Exceeded hop budget
      }

      if (expansions[u] >= 4) {
        continue;
      }
      expansions[u]++;
      metrics.nodes_explored++;

      for (size_t edge_idx = 0; edge_idx < static_graph[u].size(); ++edge_idx) {
        uint32_t v = static_graph[u][edge_idx];

        // Prevent simple cycles in the corridor
        bool visited = false;
        for (uint8_t i = 0; i < current.path_len; ++i) {
          if (current.path[i] == v) {
            visited = true;
            break;
          }
        }
        if (visited)
          continue;

        double h_v = haversine_km(nodes[v].latitude, nodes[v].longitude,
                                  dest_apt.latitude, dest_apt.longitude);

        double new_score_so_far = 0;
        double est_total = 0;

        if (mode == OptimizeFor::PRICE) {
          uint16_t min_edge_price = static_min_price[u][edge_idx];
          new_score_so_far = current.score_so_far + min_edge_price;
          est_total = new_score_so_far + (h_v * 0.025);
        } else {
          uint16_t min_edge_dur = static_min_duration[u][edge_idx];
          new_score_so_far = current.score_so_far + min_edge_dur;
          est_total = new_score_so_far + (h_v * 0.070);
        }

        // Suboptimal route pruning to intermediate nodes
        if (v != dest_id) {
          if (queued_count[v] >= 4) {
            continue;
          }
          if (new_score_so_far > best_score[v] * 1.30 + 50.0) {
            continue;
          }
          if (new_score_so_far < best_score[v]) {
            best_score[v] = new_score_so_far;
          }
          queued_count[v]++;
        }

        PathCandidate next = current;
        next.score_so_far = new_score_so_far;
        next.estimated_total = est_total;
        next.path[next.path_len++] = v;
        pq.push(next);
      }
    }
  }

  // Phase 2: Localized Timetable Forward Sweep along a single corridor
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

    // States at the current airport along the corridor
    std::vector<TimetableState> current_states;
    current_states.push_back({start_time, 0, {}});

    for (size_t hop = 0; hop + 1 < corridor.size(); ++hop) {
      uint32_t u = corridor[hop];
      uint32_t v = corridor[hop + 1];

      std::vector<TimetableState> next_states;

      for (const auto &state : current_states) {
        uint32_t req_dep = state.current_time + nodes[u].min_transfer;
        if (hop == 0) {
          req_dep =
              state.current_time; // First departure has no transfer penalty
        }

        for (const flight &f : adjacency_list[u]) {
          if (f.destination_id != v)
            continue;
          metrics.edges_relaxed++;

          uint32_t abs_dep = f.flight_day * 1440 + f.departure_time;
          if (abs_dep < req_dep)
            continue;

          uint32_t wait_time = abs_dep - state.current_time;
          if (wait_time > 1440)
            continue; // Layover over 24h

          uint32_t duration = (f.arrival_time +
                               (f.arrival_time < f.departure_time ? 1440 : 0)) -
                              f.departure_time;
          uint32_t next_time = abs_dep + duration;
          uint32_t next_price = state.total_price + f.price;

          // Pareto dominance check within this localized step:
          // If at the final destination airport, cheaper + earlier always
          // dominates. At intermediate hubs, max transit is 24h (1440m). An
          // arrival >24h earlier cannot connect to flights available to this
          // state and does not dominate.
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

          // Prune existing states dominated by this new candidate
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
        return; // Corridor broken: no feasible connection on this leg
      }
    }

    // Compare completed itineraries reaching destination
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
      } else { // OptimizeFor::TIME
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
        // If two routes have the exact same price, prefer the one with fewer transits (fewer flight legs)
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