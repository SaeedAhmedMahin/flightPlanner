#pragma once
#include "../DataStructures/Flat4AryMinHeap.hpp"
#include "../DataStructures/airport.hpp"
#include "../DataStructures/flight.hpp"
#include "Dijkstra.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <queue>
#include <random>
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

  RoutingResult run(uint32_t source_id, uint32_t dest_id,
                    const std::vector<std::vector<flight>> &adjacency_list,
                    const std::vector<airport> &nodes,
                    const std::vector<std::vector<uint32_t>> &static_graph,
                    const std::vector<std::vector<uint32_t>> &reverse_static_graph,
                    const std::vector<std::vector<uint16_t>> &static_min_price,
                    const std::vector<std::vector<uint16_t>> &static_min_duration,
                    LandmarkData &landmark_data, OptimizeFor mode,
                    uint32_t start_time, uint8_t max_hops = 5) {

    RoutingMetrics metrics = {0, 0};

    if (source_id == dest_id) {
      return {true, 0, 0, {}, metrics};
    }

    if (!landmark_data.is_preprocessed) {
      preprocess(nodes, static_graph, reverse_static_graph,
                 static_min_price, static_min_duration, landmark_data);
    }

    uint32_t num_nodes = static_cast<uint32_t>(adjacency_list.size());
    std::vector<std::vector<ParetoPoint>> pareto_labels(num_nodes);

    std::vector<Label> all_labels;
    all_labels.reserve(65536);

    Flat4AryMinHeap<ALTState, CompareALT> pq(65536);

    uint32_t h_start = compute_alt_h(source_id, dest_id, landmark_data, mode);
    int32_t source_label_id = static_cast<int32_t>(all_labels.size());
    all_labels.push_back(Label{source_id, 0, start_time, -1, flight{}, 0});
    pareto_labels[source_id].push_back({0, start_time, 0});

    pq.push({start_time, source_label_id, h_start, 0,
             static_cast<uint16_t>(source_id), 0, 0});

    while (!pq.empty()) {
      ALTState current = pq.top();
      pq.pop();
      metrics.nodes_explored++;

      if (current.node_id == dest_id) {
        // Reconstruct path
        std::vector<flight> path;
        int32_t curr_lbl_id = current.label_id;
        while (curr_lbl_id != -1) {
          const Label &lbl = all_labels[curr_lbl_id];
          if (lbl.parent_label_id != -1) {
            path.push_back(lbl.edge_taken);
          }
          curr_lbl_id = lbl.parent_label_id;
        }
        std::reverse(path.begin(), path.end());

        uint32_t total_price = all_labels[current.label_id].cost;
        uint32_t total_time = current.current_time - start_time;
        if (mode == OptimizeFor::TIME) {
          // In time mode, cost in Label is travel time
          total_price = 0;
          for (const auto &f : path) total_price += f.price;
        }
        return {true, total_price, total_time, std::move(path), metrics};
      }

      // Stale state check
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

        uint32_t req_dep =
            current.current_time + nodes[current.node_id].min_transfer;
        if (current.node_id == source_id) {
          req_dep = current.current_time;
        }

        int32_t origin_utc_offset = nodes[current.node_id].utc_offset_15m * 15;
        uint32_t abs_dep = static_cast<uint32_t>(
            static_cast<int32_t>(edge.flight_day * 1440 + edge.departure_time) -
            origin_utc_offset);

        if (abs_dep < req_dep) {
          continue;
        }
        if (abs_dep - current.current_time > 1440) {
          continue;
        }

        uint32_t next_time = abs_dep + edge.duration_mins;
        uint32_t new_g = (mode == OptimizeFor::PRICE)
                             ? (current.g_score + edge.price)
                             : (next_time - start_time);

        uint8_t next_hops = current.hops + 1;

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

        uint32_t h_next = compute_alt_h(next_node, dest_id, landmark_data, mode);
        uint32_t next_f = new_g + h_next;

        pq.push({next_time, new_label_id, next_f, new_g,
                 static_cast<uint16_t>(next_node), next_hops, 0});
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
      if (p.cost < cost && p.arrival_time < time && p.hops <= hops) {
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
