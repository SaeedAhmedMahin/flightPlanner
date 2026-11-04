#pragma once
#include "../DataStructures/airport.hpp"
#include "../DataStructures/flight.hpp"
#include "Dijkstra.hpp" // For OptimizeFor, RoutingMetrics, RoutingResult
#include <algorithm>
#include <cmath>
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
    Config(uint32_t hops = 4, uint32_t corridors = 100)
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

  RoutingResult run(
      uint32_t source_id, uint32_t dest_id,
      const std::vector<std::vector<flight>> &adjacency_list,
      const std::vector<std::vector<uint32_t>> &static_route_graph,
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
    // using Geodesic-guided bounded search on static_route_graph.
    std::vector<std::vector<uint32_t>> corridors;
    corridors.reserve(config.max_corridors);

    find_candidate_corridors(source_id, dest_id, static_route_graph, nodes,
                             config.max_hops, config.max_corridors, corridors,
                             metrics);

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
  struct PathCandidate {
    double estimated_total_dist; // Distance so far + Haversine to dest
    double dist_so_far;
    std::vector<uint32_t> path;

    bool operator>(const PathCandidate &other) const {
      return estimated_total_dist > other.estimated_total_dist;
    }
  };

  void find_candidate_corridors(
      uint32_t source_id, uint32_t dest_id,
      const std::vector<std::vector<uint32_t>> &static_graph,
      const std::vector<airport> &nodes, uint32_t max_hops,
      uint32_t max_corridors, std::vector<std::vector<uint32_t>> &corridors,
      RoutingMetrics &metrics) {

    const airport &dest_apt = nodes[dest_id];

    // Priority queue of partial paths ordered by estimated geographic distance
    std::priority_queue<PathCandidate, std::vector<PathCandidate>,
                        std::greater<PathCandidate>>
        pq;

    double h_start = haversine_km(nodes[source_id].latitude,
                                  nodes[source_id].longitude, dest_apt.latitude,
                                  dest_apt.longitude);
    pq.push({h_start, 0.0, {source_id}});

    while (!pq.empty() && corridors.size() < max_corridors) {
      PathCandidate current = pq.top();
      pq.pop();
      metrics.nodes_explored++;

      uint32_t u = current.path.back();

      if (u == dest_id) {
        corridors.push_back(current.path);
        continue;
      }

      // current.path.size() is the number of airports visited.
      // Flight legs (hops) = current.path.size() - 1.
      // If we already have max_hops legs, we cannot expand to another intermediate node.
      if (current.path.size() > max_hops) {
        continue; // Exceeded hop budget
      }

      for (uint32_t v : static_graph[u]) {
        // Prevent simple cycles in the corridor
        bool visited = false;
        for (uint32_t visited_id : current.path) {
          if (visited_id == v) {
            visited = true;
            break;
          }
        }
        if (visited) continue;

        double leg_dist = haversine_km(nodes[u].latitude, nodes[u].longitude,
                                       nodes[v].latitude, nodes[v].longitude);
        double h_v = haversine_km(nodes[v].latitude, nodes[v].longitude,
                                  dest_apt.latitude, dest_apt.longitude);

        double new_dist_so_far = current.dist_so_far + leg_dist;
        double est_total = new_dist_so_far + h_v;

        // Pruning heuristic: don't fly more than 2.5x the direct great-circle distance
        if (est_total > h_start * 2.5 + 2000.0) {
          continue;
        }

        std::vector<uint32_t> next_path = current.path;
        next_path.push_back(v);
        pq.push({est_total, new_dist_so_far, std::move(next_path)});
      }
    }
  }

  // Phase 2: Localized Timetable Forward Sweep along a single corridor
  struct TimetableState {
    uint32_t current_time;
    uint32_t total_price;
    std::vector<flight> flights_taken;
  };

  void evaluate_corridor(
      const std::vector<uint32_t> &corridor,
      const std::vector<std::vector<flight>> &adjacency_list,
      const std::vector<airport> &nodes, OptimizeFor mode, uint32_t start_time,
      RoutingResult &best_result, RoutingMetrics &metrics) {

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
          req_dep = state.current_time; // First departure has no transfer penalty
        }

        for (const flight &f : adjacency_list[u]) {
          if (f.destination_id != v) continue;
          metrics.edges_relaxed++;

          uint32_t abs_dep = f.flight_day * 1440 + f.departure_time;
          if (abs_dep < req_dep) continue;

          uint32_t wait_time = abs_dep - state.current_time;
          if (wait_time > 1440) continue; // Layover over 24h

          uint32_t duration =
              (f.arrival_time + (f.arrival_time < f.departure_time ? 1440 : 0)) -
              f.departure_time;
          uint32_t next_time = abs_dep + duration;
          uint32_t next_price = state.total_price + f.price;

          // Pareto dominance check within this localized step:
          // If at the final destination airport, cheaper + earlier always dominates.
          // At intermediate hubs, max transit is 24h (1440m). An arrival >24h earlier
          // cannot connect to flights available to this state and does not dominate.
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
          if (dominated) continue;

          // Prune existing states dominated by this new candidate
          next_states.erase(
              std::remove_if(
                  next_states.begin(), next_states.end(),
                  [is_final_hop, next_price, next_time](const TimetableState &s) {
                    return s.total_price >= next_price &&
                           s.current_time >= next_time &&
                           (is_final_hop || (s.current_time - next_time <= 1440));
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
      }
    }
  }
};