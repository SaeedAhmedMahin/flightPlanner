#pragma once
#include "../DataStructures/airport.hpp"
#include "../DataStructures/flight.hpp"
#include "Dijkstra.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <unordered_set>
#include <vector>

// ---------------------------------------------------------------------------
// YenRouter: Yen's K-Shortest Loopless Paths Algorithm with "Best" Recommendation
//
// 1. K-Shortest Paths (Yen's Algorithm):
//    - Discovers the top-K alternative flight itineraries (e.g. K=5).
//    - Uses deviation spurring: along previous path A[k-1], tests deviations
//      from each node (spur node) by masking preceding nodes to guarantee loopless
//      simple paths and masking outgoing edges sharing the same root.
//
// 2. Internationally Credited "Best Flight" Scoring Logic:
//    - Standard industry model (Google Flights, Skyscanner, Kayak, IATA):
//      Generalized Cost / Utility Function balancing Financial Cost against Value of Time:
//
//      VOT (Value of Time) ≈ $25.00/hour ($0.4167/min)
//      Layover Penalty ≈ $15.00/layover (friction of transfer, customs, security)
//      Red-Eye Schedule Penalty ≈ $30.00 for awkward 23:00 - 05:00 flights
//
//      Generalized_Cost = Ticket_Price + (Duration_Mins * VOT_Rate)
//                         + (Layover_Count * Layover_Penalty)
//                         + Schedule_Penalties
//
//    - Evaluates all K discovered alternatives and recommends the optimal "Best"
//      trade-off itinerary (saving hours of travel time for minimal price increase).
// ---------------------------------------------------------------------------
class YenRouter {
public:
  const uint32_t INF = std::numeric_limits<uint32_t>::max();

  struct BestRecommendation {
    RoutingResult result;
    double generalized_cost;
    double time_saved_vs_cheapest_hours;
    double price_diff_vs_cheapest;
    double price_increase_pct;
    std::string recommendation_reason;
  };

  struct YenKResult {
    std::vector<RoutingResult> k_paths;
    BestRecommendation best_recommendation;
  };

  // Helper struct for internal Yen's deviation state
  struct SubPathState {
    uint32_t current_time;
    uint32_t total_cost;
    uint8_t hops;
    std::vector<flight> path;
  };

  YenKResult run(uint32_t source_id, uint32_t dest_id,
                 const std::vector<std::vector<flight>> &adjacency_list,
                 const std::vector<airport> &nodes, OptimizeFor mode,
                 uint32_t start_time, uint32_t K = 5, uint8_t max_hops = 5) {
    YenKResult out;
    RoutingMetrics total_metrics = {0, 0};

    if (source_id == dest_id) {
      RoutingResult r = {true, 0, 0, {}, total_metrics};
      out.k_paths.push_back(r);
      out.best_recommendation = {r, 0.0, 0.0, 0.0, 0.0, "Source is destination"};
      return out;
    }

    // A holds the finalized K shortest paths
    std::vector<RoutingResult> A;

    // B holds the candidate paths in a min-heap ordered by primary metric
    auto comp = [mode](const RoutingResult &a, const RoutingResult &b) {
      if (mode == OptimizeFor::PRICE) {
        if (a.total_price != b.total_price) return a.total_price > b.total_price;
        if (a.path.size() != b.path.size()) return a.path.size() > b.path.size();
        return a.total_time_mins > b.total_time_mins;
      } else {
        if (a.total_time_mins != b.total_time_mins) return a.total_time_mins > b.total_time_mins;
        if (a.total_price != b.total_price) return a.total_price > b.total_price;
        return a.path.size() > b.path.size();
      }
    };
    std::priority_queue<RoutingResult, std::vector<RoutingResult>, decltype(comp)> B(comp);

    // Step 1: Find the 1st shortest path using constrained timetable search
    Dijkstra dijkstra_base;
    RoutingResult first_path = dijkstra_base.run(source_id, dest_id, adjacency_list,
                                                 nodes, mode, start_time, max_hops);
    total_metrics.nodes_explored += first_path.metrics.nodes_explored;
    total_metrics.edges_relaxed += first_path.metrics.edges_relaxed;

    if (!first_path.success || first_path.path.empty()) {
      out.best_recommendation = {first_path, 0.0, 0.0, 0.0, 0.0, "No route found"};
      return out;
    }

    first_path.metrics = total_metrics;
    A.push_back(first_path);

    // Keep track of serialized paths in B and A to prevent duplicates
    std::unordered_set<std::string> seen_paths;
    seen_paths.insert(serialize_path(first_path.path));

    // Step 2: Iterate to find k-th shortest path (from k=1 to K-1)
    for (uint32_t k = 1; k < K; ++k) {
      const RoutingResult &prev_path = A[k - 1];

      // Reconstruct sequence of airports in previous path
      // airports[0] = source, airports[1] = first layover, ..., airports[m] = dest
      std::vector<uint32_t> apt_seq;
      apt_seq.push_back(source_id);
      for (const flight &f : prev_path.path) {
        apt_seq.push_back(f.destination_id);
      }

      // Iterate through spur nodes (from source up to airport before destination)
      for (size_t i = 0; i + 1 < apt_seq.size(); ++i) {
        uint32_t spur_node = apt_seq[i];

        // Root path is from 0 to i (flights 0 to i-1)
        std::vector<flight> root_path(prev_path.path.begin(), prev_path.path.begin() + i);

        // Calculate time and cost at the spur node
        uint32_t spur_time = start_time;
        uint32_t spur_cost = 0;
        if (!root_path.empty()) {
          const flight &last_f = root_path.back();
          uint32_t abs_dep = last_f.flight_day * 1440 + last_f.departure_time;
          uint32_t dur = last_f.duration_mins;
          spur_time = abs_dep + dur;
          for (const flight &f : root_path) {
            spur_cost += f.price;
          }
        }

        // 1. Blocked nodes: All nodes in the root path except the spur node (prevents loops)
        std::vector<bool> blocked_nodes(nodes.size(), false);
        for (size_t r = 0; r < i; ++r) {
          blocked_nodes[apt_seq[r]] = true;
        }

        // 2. Blocked edges: Outgoing edges from spur_node that coincide with any path in A
        // that shares the exact same root path
        std::unordered_set<uint32_t> blocked_flight_ids;
        for (const auto &p : A) {
          if (p.path.size() > i) {
            bool matches = true;
            for (size_t r = 0; r < i; ++r) {
              if (p.path[r].flight_details_id != root_path[r].flight_details_id) {
                matches = false;
                break;
              }
            }
            if (matches) {
              blocked_flight_ids.insert(p.path[i].flight_details_id);
            }
          }
        }

        // Find spur path from spur_node to dest_id with restricted graph
        uint8_t remaining_hops = max_hops - static_cast<uint8_t>(root_path.size());
        if (remaining_hops == 0) continue;

        RoutingResult spur_res = run_spur_search(spur_node, dest_id, adjacency_list, nodes,
                                                 mode, spur_time, blocked_nodes,
                                                 blocked_flight_ids, (i == 0), remaining_hops);

        total_metrics.nodes_explored += spur_res.metrics.nodes_explored;
        total_metrics.edges_relaxed += spur_res.metrics.edges_relaxed;

        if (spur_res.success && !spur_res.path.empty()) {
          // Combine root path + spur path
          std::vector<flight> total_flights = root_path;
          total_flights.insert(total_flights.end(), spur_res.path.begin(), spur_res.path.end());

          std::string path_key = serialize_path(total_flights);
          if (seen_paths.find(path_key) == seen_paths.end()) {
            seen_paths.insert(path_key);

            RoutingResult candidate;
            candidate.success = true;
            candidate.path = std::move(total_flights);

            // Recompute exact total price and arrival time
            candidate.total_price = 0;
            for (const auto &f : candidate.path) {
              candidate.total_price += f.price;
            }
            const flight &last_f = candidate.path.back();
            uint32_t abs_dep = last_f.flight_day * 1440 + last_f.departure_time;
            uint32_t dur = last_f.duration_mins;
            candidate.total_time_mins = (abs_dep + dur) - start_time;
            candidate.metrics = total_metrics;

            B.push(candidate);
          }
        }
      }

      if (B.empty()) {
        break; // No more alternative loopless paths available
      }

      // Best candidate from B becomes A[k]
      A.push_back(B.top());
      B.pop();
    }

    out.k_paths = A;
    out.best_recommendation = compute_best_recommendation(A, mode);
    return out;
  }

  // International standard Generalized Cost Function / "Best Flight" Scoring Model
  static double calculate_generalized_cost(const RoutingResult &r) {
    if (!r.success || r.path.empty()) return 1e12;

    // 1. Base Ticket Price ($)
    double price = static_cast<double>(r.total_price);

    // 2. Value of Time (VOT): $25.00/hour = $0.4167/minute
    constexpr double VOT_PER_MIN = 25.0 / 60.0;
    double time_cost = static_cast<double>(r.total_time_mins) * VOT_PER_MIN;

    // 3. Layover friction penalty ($15 per transfer for airport navigation, customs, stress)
    double num_transits = (r.path.size() > 1) ? static_cast<double>(r.path.size() - 1) : 0.0;
    double layover_penalty = num_transits * 15.0;

    // 4. Red-eye / Schedule disruption penalty ($30 penalty for departing 23:00 - 05:00)
    double schedule_penalty = 0.0;
    for (const flight &f : r.path) {
      if (f.departure_time >= 1380 || f.departure_time < 300) {
        schedule_penalty += 20.0; // $20 inconvenience cost for each red-eye leg
      }
    }

    return price + time_cost + layover_penalty + schedule_penalty;
  }

private:
  static std::string serialize_path(const std::vector<flight> &path) {
    std::string s;
    for (const auto &f : path) {
      s += std::to_string(f.flight_details_id) + ";";
    }
    return s;
  }

  BestRecommendation compute_best_recommendation(const std::vector<RoutingResult> &paths,
                                                 OptimizeFor mode) {
    if (paths.empty()) {
      return {RoutingResult{false, 0, 0, {}, {0, 0}}, 0, 0, 0, 0, "No paths found"};
    }

    // Find baseline cheapest path among results
    const RoutingResult *cheapest = &paths[0];
    const RoutingResult *fastest = &paths[0];
    for (const auto &p : paths) {
      if (p.total_price < cheapest->total_price) {
        cheapest = &p;
      }
      if (p.total_time_mins < fastest->total_time_mins) {
        fastest = &p;
      }
    }

    // Score every path with the internationally credited Generalized Cost Function
    const RoutingResult *best_p = nullptr;
    double best_gen_cost = 1e15;

    for (const auto &p : paths) {
      double gc = calculate_generalized_cost(p);
      if (gc < best_gen_cost) {
        best_gen_cost = gc;
        best_p = &p;
      }
    }

    double time_saved_hours = 0.0;
    if (cheapest->total_time_mins > best_p->total_time_mins) {
      time_saved_hours = (cheapest->total_time_mins - best_p->total_time_mins) / 60.0;
    }

    double price_diff = static_cast<double>(best_p->total_price) - static_cast<double>(cheapest->total_price);
    double price_pct = (cheapest->total_price > 0) ? (price_diff / cheapest->total_price) * 100.0 : 0.0;

    std::string reason;
    if (best_p == cheapest && best_p == fastest) {
      reason = "Optimal on both price and time (Dominant choice).";
    } else if (best_p == cheapest) {
      reason = "Cheapest flight with best value; faster options do not justify their price premium.";
    } else {
      char buf[256];
      snprintf(buf, sizeof(buf),
               "Saves %.1fh of travel time for just +$%.0f (+%.1f%%) vs cheapest (Value of Time trade-off).",
               time_saved_hours, price_diff, price_pct);
      reason = buf;
    }

    return {*best_p, best_gen_cost, time_saved_hours, price_diff, price_pct, reason};
  }

  // Timetable search for spur subpath using Dijkstra with blocked nodes/edges
  RoutingResult run_spur_search(uint32_t spur_node, uint32_t dest_id,
                                const std::vector<std::vector<flight>> &adjacency_list,
                                const std::vector<airport> &nodes, OptimizeFor mode,
                                uint32_t spur_start_time,
                                const std::vector<bool> &blocked_nodes,
                                const std::unordered_set<uint32_t> &blocked_flight_ids,
                                bool is_initial_leg, uint8_t max_hops) {
    RoutingMetrics metrics = {0, 0};
    uint32_t num_nodes = adjacency_list.size();

    std::vector<std::vector<ParetoPoint>> pareto_labels(num_nodes);
    std::vector<Label> all_labels;
    all_labels.reserve(4096);

    // Flat 4-ary Min-Heap
    Flat4AryMinHeap<SearchState, CompareCost> pq(4096);

    int32_t root_id = static_cast<int32_t>(all_labels.size());
    all_labels.push_back(Label{spur_node, 0, spur_start_time, -1, flight{}, 0});
    pareto_labels[spur_node].push_back({0, spur_start_time, 0});

    pq.push({spur_start_time, root_id, 0, static_cast<uint16_t>(spur_node), 0, {0, 0, 0}});

    while (!pq.empty()) {
      SearchState current = pq.top();
      pq.pop();
      metrics.nodes_explored++;

      if (current.node_id == dest_id) {
        RoutingResult res;
        res.success = true;
        res.metrics = metrics;
        res.total_time_mins = current.current_time - spur_start_time;
        res.total_price = 0;

        int32_t curr_id = current.label_id;
        while (curr_id != -1 && all_labels[curr_id].parent_label_id != -1) {
          const Label &lbl = all_labels[curr_id];
          res.path.push_back(lbl.edge_taken);
          res.total_price += lbl.edge_taken.price;
          curr_id = lbl.parent_label_id;
        }
        std::reverse(res.path.begin(), res.path.end());
        return res;
      }

      if (current.hops >= max_hops) {
        continue;
      }

      for (const flight &edge : adjacency_list[current.node_id]) {
        metrics.edges_relaxed++;
        uint32_t next_node = edge.destination_id;

        // Skip blocked nodes (loopless constraint)
        if (blocked_nodes[next_node]) {
          continue;
        }

        // Skip blocked deviation edges from spur node
        if (current.node_id == spur_node && blocked_flight_ids.count(edge.flight_details_id)) {
          continue;
        }

        uint32_t req_dep = current.current_time + nodes[current.node_id].min_transfer;
        if (current.node_id == spur_node && is_initial_leg) {
          req_dep = current.current_time;
        }

        uint32_t abs_dep = edge.flight_day * 1440 + edge.departure_time;
        if (abs_dep < req_dep) {
          continue;
        }

        uint32_t wait_time = abs_dep - current.current_time;
        if (wait_time > 1440) {
          continue; // Layover > 24h
        }

        uint32_t dur = edge.duration_mins;
        uint32_t next_time = abs_dep + dur;

        uint32_t new_cost = 0;
        if (mode == OptimizeFor::PRICE) {
          new_cost = current.total_cost + edge.price;
        } else {
          new_cost = next_time - spur_start_time;
        }

        if (new_cost > std::numeric_limits<uint16_t>::max()) {
          continue;
        }

        uint8_t next_hops = current.hops + 1;

        // Pareto dominance check
        bool dominated = false;
        for (const auto &p : pareto_labels[next_node]) {
          if (p.cost <= new_cost && p.arrival_time <= next_time && p.hops <= next_hops) {
            if (next_node == dest_id || (next_time - p.arrival_time <= 1440)) {
              dominated = true;
              break;
            }
          }
        }
        if (dominated) continue;

        // Prune dominated
        pareto_labels[next_node].erase(
            std::remove_if(pareto_labels[next_node].begin(), pareto_labels[next_node].end(),
                           [next_node, dest_id, new_cost, next_time, next_hops](const ParetoPoint &p) {
                             return p.cost >= new_cost && p.arrival_time >= next_time &&
                                    p.hops >= next_hops &&
                                    (next_node == dest_id || (p.arrival_time - next_time <= 1440));
                           }),
            pareto_labels[next_node].end());

        pareto_labels[next_node].push_back({new_cost, next_time, next_hops});

        int32_t new_id = static_cast<int32_t>(all_labels.size());
        all_labels.push_back(Label{next_node, new_cost, next_time, current.label_id, edge, next_hops});

        pq.push({next_time, new_id, static_cast<uint16_t>(new_cost), static_cast<uint16_t>(next_node), next_hops, {0, 0, 0}});
      }
    }

    return {false, 0, 0, {}, metrics};
  }
};

