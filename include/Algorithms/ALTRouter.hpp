#pragma once
#include "../DataStructures/Flat4AryMinHeap.hpp"
#include "../DataStructures/airport.hpp"
#include "../DataStructures/flight.hpp"
#include "Dijkstra.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <limits>
#include <queue>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

// ---------------------------------------------------------------------------
// ALTRouter: A* Search with Landmarks and Triangle Inequality (ALT)
//
// Theoretical Foundation:
//   Developed by Andrew V. Goldberg and Chris Harrelson (2005, SODA;
//   "Computing the Shortest Path: A* Search Meets Watkins and Farthest Landmarks").
//
// 1. Offline Landmark Selection & Preprocessing:
//    - Selects a strategic set of K landmark airports L = {L_1, L_2, ..., L_k}
//      using the Farthest-Point / Max-Min Geodesic Sampling technique.
//      This guarantees landmarks surround the boundary and major hub cores
//      of the global flight graph.
//    - For every landmark L_i, runs standard forward and backward Dijkstra to
//      precompute two distance matrices:
//        dist_to_landmark[L_i][u] = Shortest distance from u to L_i
//        dist_from_landmark[L_i][u] = Shortest distance from L_i to u
//
// 2. Triangle Inequality Lower Bounds:
//    For any landmark L and any pair of nodes (u, v):
//      dist(u, v) >= dist(u, L) - dist(v, L)    (Triangle inequality type 1)
//      dist(u, v) >= dist(L, v) - dist(L, u)    (Triangle inequality type 2)
//
//    The ALT heuristic takes the tightest lower bound across all landmarks:
//      h_ALT(u, v) = max_{L \in \mathcal{L}} \max \{ 0, dist(u, L) - dist(v, L), dist(L, v) - dist(L, u) \}
//
//    Properties:
//    - Strictly admissible (never overestimates).
//    - Strictly consistent (monotonic), guaranteeing zero re-expansions.
//    - Unlike Euclidean / Haversine distance which assumes straight-line travel,
//      ALT captures actual network connectivity, flight costs, detours, and
//      geographical barriers.
// ---------------------------------------------------------------------------

class ALTRouter {
public:
  static constexpr uint32_t INF = std::numeric_limits<uint32_t>::max();

  struct LandmarkData {
    std::vector<uint32_t> landmark_ids;
    // dist_from_landmark[l_idx][node]
    std::vector<std::vector<uint32_t>> dist_from_lm_price;
    // dist_to_landmark[l_idx][node]
    std::vector<std::vector<uint32_t>> dist_to_lm_price;

    std::vector<std::vector<uint32_t>> dist_from_lm_time;
    std::vector<std::vector<uint32_t>> dist_to_lm_time;

    bool is_preprocessed = false;
  };

  struct ALTState {
    uint32_t current_time;
    int32_t label_id;
    uint32_t f_score;
    uint32_t g_score;
    uint16_t node_id;
    uint8_t hops;
    uint8_t padding;
  };

  struct CompareALT {
    bool operator()(const ALTState &a, const ALTState &b) const {
      if (a.f_score != b.f_score) {
        return a.f_score < b.f_score;
      }
      if (a.hops != b.hops) {
        return a.hops < b.hops;
      }
      return a.g_score > b.g_score;
    }
  };

  // Select landmarks using Farthest-Point / Max-Min heuristic
  static std::vector<uint32_t> select_landmarks_farthest(
      const std::vector<airport> &nodes,
      const std::vector<std::vector<uint32_t>> &static_graph,
      size_t num_landmarks = 16) {

    size_t n = nodes.size();
    if (n == 0) return {};

    std::vector<uint32_t> landmarks;
    landmarks.reserve(num_landmarks);

    // Pick first landmark as node with highest degree (major global hub)
    uint32_t first_lm = 0;
    size_t max_deg = 0;
    for (size_t i = 0; i < n; ++i) {
      if (static_graph[i].size() > max_deg) {
        max_deg = static_graph[i].size();
        first_lm = static_cast<uint32_t>(i);
      }
    }
    landmarks.push_back(first_lm);

    auto haversine_dist = [](const airport &a, const airport &b) -> double {
      const double R = 6371.0;
      double dLat = (b.latitude - a.latitude) * M_PI / 180.0;
      double dLon = (b.longitude - a.longitude) * M_PI / 180.0;
      double x = std::sin(dLat / 2.0) * std::sin(dLat / 2.0) +
                 std::cos(a.latitude * M_PI / 180.0) * std::cos(b.latitude * M_PI / 180.0) *
                     std::sin(dLon / 2.0) * std::sin(dLon / 2.0);
      double c = 2.0 * std::atan2(std::sqrt(x), std::sqrt(1.0 - x));
      return R * c;
    };

    std::vector<double> min_dist_to_lm(n, 1e18);

    while (landmarks.size() < num_landmarks) {
      uint32_t last_lm = landmarks.back();
      uint32_t best_next = 0;
      double max_dist = -1.0;

      for (size_t i = 0; i < n; ++i) {
        if (static_graph[i].empty()) continue; // skip isolated airports
        double d = haversine_dist(nodes[i], nodes[last_lm]);
        if (d < min_dist_to_lm[i]) {
          min_dist_to_lm[i] = d;
        }
        if (min_dist_to_lm[i] > max_dist) {
          max_dist = min_dist_to_lm[i];
          best_next = static_cast<uint32_t>(i);
        }
      }

      landmarks.push_back(best_next);
    }

    return landmarks;
  }

  // Preprocess landmark distances using static route graph
  static void preprocess(
      const std::vector<airport> &nodes,
      const std::vector<std::vector<uint32_t>> &static_graph,
      const std::vector<std::vector<uint32_t>> &reverse_static_graph,
      const std::vector<std::vector<uint16_t>> &static_min_price,
      const std::vector<std::vector<uint16_t>> &static_min_duration,
      LandmarkData &data, size_t num_landmarks = 16) {

    size_t n = nodes.size();
    data.landmark_ids = select_landmarks_farthest(nodes, static_graph, num_landmarks);
    size_t k = data.landmark_ids.size();

    data.dist_from_lm_price.assign(k, std::vector<uint32_t>(n, INF));
    data.dist_to_lm_price.assign(k, std::vector<uint32_t>(n, INF));

    data.dist_from_lm_time.assign(k, std::vector<uint32_t>(n, INF));
    data.dist_to_lm_time.assign(k, std::vector<uint32_t>(n, INF));

    // Dijkstra helper on static graph
    auto run_dijkstra = [&](uint32_t start_node,
                            const std::vector<std::vector<uint32_t>> &graph,
                            const std::vector<std::vector<uint16_t>> &weights,
                            std::vector<uint32_t> &dist) {
      dist[start_node] = 0;
      std::priority_queue<std::pair<uint32_t, uint32_t>,
                          std::vector<std::pair<uint32_t, uint32_t>>,
                          std::greater<std::pair<uint32_t, uint32_t>>> pq;
      pq.push({0, start_node});

      while (!pq.empty()) {
        auto top = pq.top();
        pq.pop();
        uint32_t d = top.first;
        uint32_t u = top.second;

        if (d > dist[u]) continue;

        for (size_t i = 0; i < graph[u].size(); ++i) {
          uint32_t v = graph[u][i];
          uint32_t w = weights[u][i];
          if (dist[u] + w < dist[v]) {
            dist[v] = dist[u] + w;
            pq.push({dist[v], v});
          }
        }
      }
    };

    // Precompute reverse static graph weights for backward Dijkstra
    std::vector<std::vector<uint16_t>> reverse_min_price(n);
    std::vector<std::vector<uint16_t>> reverse_min_duration(n);
    for (size_t u = 0; u < n; ++u) {
      for (size_t i = 0; i < static_graph[u].size(); ++i) {
        uint32_t v = static_graph[u][i];
        reverse_min_price[v].push_back(static_min_price[u][i]);
        reverse_min_duration[v].push_back(static_min_duration[u][i]);
      }
    }

    for (size_t l = 0; l < k; ++l) {
      uint32_t lm = data.landmark_ids[l];
      // Price forward & backward
      run_dijkstra(lm, static_graph, static_min_price, data.dist_from_lm_price[l]);
      run_dijkstra(lm, reverse_static_graph, reverse_min_price, data.dist_to_lm_price[l]);

      // Time forward & backward
      run_dijkstra(lm, static_graph, static_min_duration, data.dist_from_lm_time[l]);
      run_dijkstra(lm, reverse_static_graph, reverse_min_duration, data.dist_to_lm_time[l]);
    }

    data.is_preprocessed = true;
  }

  // Compute tight ALT triangle inequality lower bound
  static inline uint32_t compute_alt_h(uint32_t u, uint32_t dest,
                                       const LandmarkData &data,
                                       OptimizeFor mode) {
    if (u == dest) return 0;
    uint32_t max_h = 0;
    size_t k = data.landmark_ids.size();

    const auto &dist_from = (mode == OptimizeFor::PRICE) ? data.dist_from_lm_price : data.dist_from_lm_time;
    const auto &dist_to = (mode == OptimizeFor::PRICE) ? data.dist_to_lm_price : data.dist_to_lm_time;

    for (size_t l = 0; l < k; ++l) {
      // Bound 1: dist(u, dest) >= dist(u, L) - dist(dest, L)
      uint32_t u_to_l = dist_to[l][u];
      uint32_t dest_to_l = dist_to[l][dest];
      if (u_to_l != INF && dest_to_l != INF && u_to_l > dest_to_l) {
        uint32_t h1 = u_to_l - dest_to_l;
        if (h1 > max_h) max_h = h1;
      }

      // Bound 2: dist(u, dest) >= dist(L, dest) - dist(L, u)
      uint32_t l_to_dest = dist_from[l][dest];
      uint32_t l_to_u = dist_from[l][u];
      if (l_to_dest != INF && l_to_u != INF && l_to_dest > l_to_u) {
        uint32_t h2 = l_to_dest - l_to_u;
        if (h2 > max_h) max_h = h2;
      }
    }

    return max_h;
  }

  enum class ObjectiveMode { PRICE, TIME, BALANCED };

  struct SearchPenalties {
    const std::unordered_set<uint32_t> *penalized_flights = nullptr;
    const std::unordered_set<uint32_t> *blocked_first_flights = nullptr;
    const std::vector<uint16_t> *hub_penalties = nullptr;
    const std::array<uint16_t, 256> *airline_penalties = nullptr;
    uint32_t flight_penalty = 450;
  };

  struct SearchWorkspace {
    std::vector<std::vector<ParetoPoint>> pareto_labels;
    std::vector<uint32_t> touched_nodes;
    std::vector<Label> all_labels;
    Flat4AryMinHeap<ALTState, CompareALT> pq{65536};

    void prepare(size_t num_nodes) {
      if (pareto_labels.size() < num_nodes) {
        pareto_labels.resize(num_nodes);
      } else {
        for (uint32_t u : touched_nodes) {
          pareto_labels[u].clear();
        }
      }
      touched_nodes.clear();
      all_labels.clear();
      if (all_labels.capacity() < 16384) {
        all_labels.reserve(16384);
      }
      pq.clear();
    }

    inline void add_pareto(uint32_t node_id, const ParetoPoint &pt) {
      if (pareto_labels[node_id].empty()) {
        touched_nodes.push_back(node_id);
      }
      pareto_labels[node_id].push_back(pt);
    }
  };

  struct DiverseKResult {
    std::vector<RoutingResult> paths;
    RoutingResult best_recommendation;
    double generalized_cost = 0.0;
    std::string recommendation_reason;
    RoutingMetrics total_metrics = {0, 0};
  };

  static inline uint32_t compute_objective_h(uint32_t u, uint32_t dest,
                                             const LandmarkData &data,
                                             ObjectiveMode obj) {
    if (obj == ObjectiveMode::PRICE) {
      return compute_alt_h(u, dest, data, OptimizeFor::PRICE);
    }
    if (obj == ObjectiveMode::TIME) {
      return compute_alt_h(u, dest, data, OptimizeFor::TIME);
    }
    // Admissible lower bound for price + 0.35 * elapsed_mins
    uint32_t h_p = compute_alt_h(u, dest, data, OptimizeFor::PRICE);
    uint32_t h_t = compute_alt_h(u, dest, data, OptimizeFor::TIME);
    return h_p + (h_t * 35u) / 100u;
  }

  static double calculate_generalized_cost(const RoutingResult &r) {
    if (!r.success || r.path.empty()) return 1e12;
    double price = static_cast<double>(r.total_price);
    constexpr double VOT_PER_MIN = 25.0 / 60.0;
    double time_cost = static_cast<double>(r.total_time_mins) * VOT_PER_MIN;
    double num_transits =
        (r.path.size() > 1) ? static_cast<double>(r.path.size() - 1) : 0.0;
    double layover_penalty = num_transits * 15.0;
    double schedule_penalty = 0.0;
    for (const flight &f : r.path) {
      if (f.departure_time >= 1380 || f.departure_time < 300) {
        schedule_penalty += 20.0;
      }
    }
    return price + time_cost + layover_penalty + schedule_penalty;
  }

  RoutingResult run(uint32_t source_id, uint32_t dest_id,
                    const std::vector<std::vector<flight>> &adjacency_list,
                    const std::vector<airport> &nodes,
                    const std::vector<std::vector<uint32_t>> &static_graph,
                    const std::vector<std::vector<uint32_t>> &reverse_static_graph,
                    const std::vector<std::vector<uint16_t>> &static_min_price,
                    const std::vector<std::vector<uint16_t>> &static_min_duration,
                    LandmarkData &landmark_data, OptimizeFor mode,
                    uint32_t start_time, uint8_t max_hops = 5) {
    if (!landmark_data.is_preprocessed) {
      preprocess(nodes, static_graph, reverse_static_graph,
                 static_min_price, static_min_duration, landmark_data);
    }
    SearchWorkspace ws;
    SearchPenalties no_penalties;
    ObjectiveMode obj =
        (mode == OptimizeFor::PRICE) ? ObjectiveMode::PRICE : ObjectiveMode::TIME;
    return run_with_config(source_id, dest_id, adjacency_list, nodes,
                           landmark_data, obj, start_time, max_hops,
                           no_penalties, ws, false, true);
  }

  // Runs multiple diversified ALT searches (Price, Time, Balanced, departure
  // windows, and non-negative edge/hub/airline penalties) to produce top
  // alternative itineraries orders of magnitude faster than Yen's K-SP.
  DiverseKResult run_k_diverse(
      uint32_t source_id, uint32_t dest_id,
      const std::vector<std::vector<flight>> &adjacency_list,
      const std::vector<airport> &nodes,
      const std::vector<std::vector<uint32_t>> &static_graph,
      const std::vector<std::vector<uint32_t>> &reverse_static_graph,
      const std::vector<std::vector<uint16_t>> &static_min_price,
      const std::vector<std::vector<uint16_t>> &static_min_duration,
      LandmarkData &landmark_data, uint32_t start_time, uint32_t K = 12,
      uint8_t max_hops = 5, uint32_t date_start_utc = 0) {

    DiverseKResult out;
    if (source_id == dest_id) {
      RoutingResult r = {true, 0, 0, {}, {0, 0}};
      out.paths.push_back(r);
      out.best_recommendation = r;
      out.recommendation_reason = "Source is destination";
      return out;
    }

    if (!landmark_data.is_preprocessed) {
      preprocess(nodes, static_graph, reverse_static_graph,
                 static_min_price, static_min_duration, landmark_data);
    }

    if (date_start_utc == 0) {
      date_start_utc = start_time;
    }

    const size_t num_nodes = adjacency_list.size();
    SearchWorkspace ws;
    std::unordered_set<std::string> seen_paths;
    std::unordered_set<uint32_t> penalized_flights;
    std::unordered_set<uint32_t> blocked_first_flights;
    std::vector<uint16_t> hub_penalties(num_nodes, 0);
    std::array<uint16_t, 256> airline_penalties{};

    auto serialize_path = [](const std::vector<flight> &path) {
      std::string s;
      s.reserve(path.size() * 12);
      for (const auto &f : path) {
        s += std::to_string(f.flight_details_id);
        s.push_back(';');
      }
      return s;
    };

    auto record_result = [&](RoutingResult &res) -> bool {
      out.total_metrics.nodes_explored += res.metrics.nodes_explored;
      out.total_metrics.edges_relaxed += res.metrics.edges_relaxed;
      if (!res.success || res.path.empty()) return false;

      std::string key = serialize_path(res.path);
      if (!seen_paths.insert(key).second) {
        // Collision on an earlier path: block its departing flight for later passes
        blocked_first_flights.insert(res.path.front().flight_details_id);
        return false;
      }

      for (size_t idx = 0; idx < res.path.size(); ++idx) {
        const flight &f = res.path[idx];
        penalized_flights.insert(f.flight_details_id);
        uint16_t &ap = airline_penalties[f.airline_id];
        if (ap < 400) ap = static_cast<uint16_t>(ap + 65);
        if (idx + 1 < res.path.size() && f.destination_id < num_nodes) {
          uint16_t &hp = hub_penalties[f.destination_id];
          if (hp < 800) hp = static_cast<uint16_t>(hp + 160);
        }
      }

      out.paths.push_back(std::move(res));
      return true;
    };

    // Build departure-window probe timestamps anchored within the selected day
    std::vector<uint32_t> window_starts;
    window_starts.push_back(start_time);
    const uint32_t max_window_start =
        std::max(start_time, date_start_utc + 1200);
    for (uint32_t offset : {360u, 660u, 960u, 1200u}) {
      uint32_t cand = date_start_utc + offset;
      if (cand > start_time && cand <= max_window_start) {
        if (cand - window_starts.back() >= 180) {
          window_starts.push_back(cand);
        }
      }
    }

    SearchPenalties no_penalties;
    SearchPenalties div_penalties{&penalized_flights, &blocked_first_flights,
                                  &hub_penalties, &airline_penalties, 450};

    // Phase 1: Pure optimal baselines (Cheapest, Fastest, Balanced Best-Value)
    RoutingResult r_price = run_with_config(
        source_id, dest_id, adjacency_list, nodes, landmark_data,
        ObjectiveMode::PRICE, start_time, max_hops, no_penalties, ws, true,
        true);
    if (!r_price.success) {
      out.total_metrics = r_price.metrics;
      out.best_recommendation = {false, 0, 0, {}, out.total_metrics};
      out.recommendation_reason = "No route found";
      return out;
    }
    record_result(r_price);

    for (ObjectiveMode obj : {ObjectiveMode::TIME, ObjectiveMode::BALANCED}) {
      RoutingResult r = run_with_config(
          source_id, dest_id, adjacency_list, nodes, landmark_data, obj,
          start_time, max_hops, no_penalties, ws, true, true);
      record_result(r);
    }

    // Phase 2: Departure-window sweeps to capture morning/afternoon/evening options
    for (size_t wi = 1; wi < window_starts.size() && out.paths.size() < K; ++wi) {
      RoutingResult r = run_with_config(
          source_id, dest_id, adjacency_list, nodes, landmark_data,
          ObjectiveMode::BALANCED, window_starts[wi], max_hops, div_penalties,
          ws, true, true);
      record_result(r);
    }

    // Phase 3: Iterative penalized ALT across objectives & windows until K paths
    const uint32_t max_extra_passes = std::max<uint32_t>(K + 4, 14);
    uint32_t consecutive_misses = 0;
    for (uint32_t p = 0;
         p < max_extra_passes && out.paths.size() < K && consecutive_misses < 3;
         ++p) {
      ObjectiveMode obj = (p % 3 == 0)
                              ? ObjectiveMode::PRICE
                              : (p % 3 == 1 ? ObjectiveMode::BALANCED
                                            : ObjectiveMode::TIME);
      uint32_t pass_start = window_starts[p % window_starts.size()];
      div_penalties.flight_penalty = 450 + p * 150;

      RoutingResult r = run_with_config(
          source_id, dest_id, adjacency_list, nodes, landmark_data, obj,
          pass_start, max_hops, div_penalties, ws, true, true);
      if (record_result(r)) {
        consecutive_misses = 0;
      } else {
        consecutive_misses++;
      }
    }

    // Order itineraries with Google-Flights-style Best Value first
    std::sort(out.paths.begin(), out.paths.end(),
              [](const RoutingResult &a, const RoutingResult &b) {
                double ga = calculate_generalized_cost(a);
                double gb = calculate_generalized_cost(b);
                if (std::abs(ga - gb) > 1e-6) return ga < gb;
                if (a.total_price != b.total_price)
                  return a.total_price < b.total_price;
                return a.total_time_mins < b.total_time_mins;
              });

    if (!out.paths.empty()) {
      const RoutingResult *cheapest = &out.paths[0];
      const RoutingResult *fastest = &out.paths[0];
      const RoutingResult *best_p = &out.paths[0];
      double best_gc = calculate_generalized_cost(out.paths[0]);

      for (const auto &p : out.paths) {
        if (p.total_price < cheapest->total_price) cheapest = &p;
        if (p.total_time_mins < fastest->total_time_mins) fastest = &p;
        double gc = calculate_generalized_cost(p);
        if (gc < best_gc) {
          best_gc = gc;
          best_p = &p;
        }
      }

      out.best_recommendation = *best_p;
      out.generalized_cost = best_gc;

      double time_saved_hours =
          (cheapest->total_time_mins > best_p->total_time_mins)
              ? (cheapest->total_time_mins - best_p->total_time_mins) / 60.0
              : 0.0;
      double price_diff = static_cast<double>(best_p->total_price) -
                          static_cast<double>(cheapest->total_price);
      double price_pct =
          (cheapest->total_price > 0)
              ? (price_diff / static_cast<double>(cheapest->total_price)) * 100.0
              : 0.0;

      if (best_p == cheapest && best_p == fastest) {
        out.recommendation_reason =
            "Optimal on both price and time (Dominant choice).";
      } else if (best_p == cheapest) {
        out.recommendation_reason =
            "Cheapest flight with best value; faster options do not justify their price premium.";
      } else {
        char buf[256];
        std::snprintf(
            buf, sizeof(buf),
            "Saves %.1fh of travel time for just +$%.0f (+%.1f%%) vs cheapest (Value of Time trade-off).",
            time_saved_hours, price_diff, price_pct);
        out.recommendation_reason = buf;
      }
    } else {
      out.best_recommendation = {false, 0, 0, {}, out.total_metrics};
      out.recommendation_reason = "No route found";
    }

    return out;
  }

private:
  RoutingResult run_with_config(
      uint32_t source_id, uint32_t dest_id,
      const std::vector<std::vector<flight>> &adjacency_list,
      const std::vector<airport> &nodes, const LandmarkData &landmark_data,
      ObjectiveMode obj, uint32_t start_time, uint8_t max_hops,
      const SearchPenalties &penalties, SearchWorkspace &ws,
      bool actual_journey_duration, bool sorted_by_day) {

    RoutingMetrics metrics = {0, 0};

    if (source_id == dest_id) {
      return {true, 0, 0, {}, metrics};
    }

    uint32_t num_nodes = static_cast<uint32_t>(adjacency_list.size());
    ws.prepare(num_nodes);

    uint32_t h_start = compute_objective_h(source_id, dest_id, landmark_data, obj);
    int32_t source_label_id = static_cast<int32_t>(ws.all_labels.size());
    ws.all_labels.push_back(Label{source_id, 0, start_time, -1, flight{}, 0});
    ws.add_pareto(source_id, {0, start_time, 0});

    ws.pq.push({start_time, source_label_id, h_start, 0,
                static_cast<uint16_t>(source_id), 0, 0});

    while (!ws.pq.empty()) {
      ALTState current = ws.pq.top();
      ws.pq.pop();
      metrics.nodes_explored++;

      if (current.node_id == dest_id) {
        std::vector<flight> path;
        int32_t curr_lbl_id = current.label_id;
        while (curr_lbl_id != -1) {
          const Label &lbl = ws.all_labels[curr_lbl_id];
          if (lbl.parent_label_id != -1) {
            path.push_back(lbl.edge_taken);
          }
          curr_lbl_id = lbl.parent_label_id;
        }
        std::reverse(path.begin(), path.end());

        uint32_t total_price = 0;
        for (const auto &f : path) total_price += f.price;

        uint32_t total_time = current.current_time - start_time;
        if (actual_journey_duration && !path.empty()) {
          int32_t src_utc_offset = nodes[source_id].utc_offset_15m * 15;
          uint32_t first_dep_utc = static_cast<uint32_t>(
              static_cast<int32_t>(path.front().flight_day * 1440 +
                                   path.front().departure_time) -
              src_utc_offset);
          total_time = current.current_time - first_dep_utc;
        }
        return {true, total_price, total_time, std::move(path), metrics};
      }

      const Label &curr_label = ws.all_labels[current.label_id];
      if (is_strictly_dominated(current.node_id, dest_id,
                                ws.pareto_labels[current.node_id],
                                curr_label.cost, curr_label.arrival_time,
                                current.hops)) {
        continue;
      }

      if (current.hops >= max_hops) {
        continue;
      }

      const int32_t origin_utc_offset = nodes[current.node_id].utc_offset_15m * 15;
      const int64_t curr_utc = static_cast<int32_t>(current.current_time);
      const int64_t req_dep_i =
          (current.node_id == source_id)
              ? curr_utc
              : (curr_utc + nodes[current.node_id].min_transfer);

      const auto &edges = adjacency_list[current.node_id];
      auto it = edges.begin();
      int64_t max_day_i = (curr_utc + 1440 + origin_utc_offset) / 1440;
      if (sorted_by_day) {
        int64_t min_local = req_dep_i + origin_utc_offset;
        uint16_t min_day =
            (min_local > 0) ? static_cast<uint16_t>(min_local / 1440) : 0;
        it = std::lower_bound(
            edges.begin(), edges.end(), min_day,
            [](const flight &f, uint16_t d) { return f.flight_day < d; });
      }

      for (; it != edges.end(); ++it) {
        const flight &edge = *it;
        if (sorted_by_day && max_day_i >= 0 &&
            edge.flight_day > static_cast<uint16_t>(max_day_i)) {
          break;
        }
        metrics.edges_relaxed++;

        if (current.node_id == source_id &&
            penalties.blocked_first_flights &&
            penalties.blocked_first_flights->count(edge.flight_details_id)) {
          continue;
        }

        int64_t abs_dep_i = static_cast<int64_t>(edge.flight_day) * 1440 +
                            edge.departure_time - origin_utc_offset;
        if (abs_dep_i < req_dep_i) {
          continue;
        }
        int64_t wait_i = abs_dep_i - curr_utc;
        if (wait_i > 1440) {
          continue;
        }

        uint32_t next_node = edge.destination_id;
        uint32_t next_time = static_cast<uint32_t>(abs_dep_i + edge.duration_mins);
        uint32_t step_elapsed = next_time - current.current_time;

        uint32_t penalty = 0;
        if (penalties.penalized_flights &&
            penalties.penalized_flights->count(edge.flight_details_id)) {
          penalty += penalties.flight_penalty;
        }
        if (penalties.hub_penalties && next_node != dest_id &&
            next_node < penalties.hub_penalties->size()) {
          penalty += (*penalties.hub_penalties)[next_node];
        }
        if (penalties.airline_penalties) {
          penalty += (*penalties.airline_penalties)[edge.airline_id];
        }

        uint32_t new_g = 0;
        if (obj == ObjectiveMode::PRICE) {
          new_g = current.g_score + edge.price + penalty;
        } else if (obj == ObjectiveMode::TIME) {
          new_g = current.g_score + step_elapsed + penalty;
        } else {
          uint32_t layover_pen = (current.hops > 0) ? 65u : 0u;
          new_g = current.g_score + edge.price + (step_elapsed * 35u) / 100u +
                  layover_pen + penalty;
        }

        uint8_t next_hops = current.hops + 1;

        if (is_dominated(next_node, dest_id, ws.pareto_labels[next_node], new_g,
                         next_time, next_hops)) {
          continue;
        }

        prune_dominated(next_node, dest_id, ws.pareto_labels[next_node], new_g,
                        next_time, next_hops);
        ws.add_pareto(next_node, {new_g, next_time, next_hops});

        int32_t new_label_id = static_cast<int32_t>(ws.all_labels.size());
        ws.all_labels.push_back(Label{next_node, new_g, next_time,
                                      current.label_id, edge, next_hops});

        uint32_t h_next =
            compute_objective_h(next_node, dest_id, landmark_data, obj);
        uint32_t next_f = new_g + h_next;

        ws.pq.push({next_time, new_label_id, next_f, new_g,
                    static_cast<uint16_t>(next_node), next_hops, 0});
      }
    }

    return {false, 0, 0, {}, metrics};
  }

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

using ALT = ALTRouter;
