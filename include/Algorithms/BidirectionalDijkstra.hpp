#pragma once
#include "../DataStructures/airport.hpp"
#include "../DataStructures/flight.hpp"
#include "Dijkstra.hpp" // For OptimizeFor, RoutingMetrics, RoutingResult, ParetoPoint
#include "SearchState.hpp"
#include <algorithm>
#include <limits>
#include <queue>
#include <vector>

// ---------------------------------------------------------------------------
// BackwardSearchState: Priority queue entry for the backward search frontier.
// Moves backward in time from destination towards source.
// ---------------------------------------------------------------------------
struct BackwardSearchState {
  uint32_t departure_time; // Departure minute of onward flight leaving this airport
  int32_t label_id;       // Index in all_backward_labels
  uint16_t total_cost;    // Accumulated cost from this node to destination
  uint16_t node_id;       // Current airport ID
};

struct CompareBackwardCost {
  bool operator()(const BackwardSearchState &a,
                  const BackwardSearchState &b) const {
    return a.total_cost > b.total_cost; // Min-Heap on cost
  }
};

// ---------------------------------------------------------------------------
// BackwardLabel: Stores an accepted backward path state.
// Points toward destination via parent_label_id.
// ---------------------------------------------------------------------------
struct BackwardLabel {
  uint32_t airport_id;
  uint32_t cost;
  uint32_t departure_time; // The departure time of the onward flight
  int32_t parent_label_id; // Links toward destination (-1 at destination)
  flight edge_taken;       // The forward flight taken from this airport
};

// ---------------------------------------------------------------------------
// BackwardParetoPoint: (cost, departure_time) tuple.
// In backward search, a LATER departure time is superior because it gives
// earlier predecessor flights more time to connect.
// State A dominates State B if cost_A <= cost_B AND dep_A >= dep_B.
// ---------------------------------------------------------------------------
struct BackwardParetoPoint {
  uint32_t cost;
  uint32_t departure_time;
};

class BidirectionalDijkstra {
public:
  const uint32_t INF = std::numeric_limits<uint32_t>::max();

  RoutingResult run(uint32_t source_id, uint32_t dest_id,
                    const std::vector<std::vector<flight>> &adjacency_list,
                    const std::vector<std::vector<reverse_flight>> &reverse_adjacency_list,
                    const std::vector<airport> &nodes, OptimizeFor mode,
                    uint32_t start_time) {
    uint32_t num_nodes = adjacency_list.size();
    RoutingMetrics metrics = {0, 0};

    // Edge case: same source and destination
    if (source_id == dest_id) {
      return {true, 0, 0, {}, metrics};
    }

    // --- Forward Search Structures ---
    std::vector<std::vector<ParetoPoint>> fwd_pareto(num_nodes);
    std::vector<Label> fwd_labels;
    fwd_labels.reserve(32768);
    std::priority_queue<SearchState, std::vector<SearchState>, CompareCost> pq_fwd;

    // --- Backward Search Structures ---
    std::vector<std::vector<BackwardParetoPoint>> bwd_pareto(num_nodes);
    std::vector<BackwardLabel> bwd_labels;
    bwd_labels.reserve(32768);
    std::priority_queue<BackwardSearchState, std::vector<BackwardSearchState>,
                        CompareBackwardCost>
        pq_bwd;

    // Seed Forward Search at Source
    int32_t src_label_id = static_cast<int32_t>(fwd_labels.size());
    fwd_labels.push_back(Label{source_id, 0, start_time, -1, flight{}});
    fwd_pareto[source_id].push_back({0, start_time});
    pq_fwd.push({start_time, src_label_id, 0, static_cast<uint16_t>(source_id)});

    // Seed Backward Search at Destination
    // Upper horizon: passenger can arrive anytime up to 14 days from start
    const uint32_t HORIZON_TIME = start_time + 14 * 1440;
    int32_t dst_label_id = static_cast<int32_t>(bwd_labels.size());
    bwd_labels.push_back(
        BackwardLabel{dest_id, 0, HORIZON_TIME, -1, flight{}});
    bwd_pareto[dest_id].push_back({0, HORIZON_TIME});
    pq_bwd.push({HORIZON_TIME, dst_label_id, 0, static_cast<uint16_t>(dest_id)});

    // Best meeting connection tracking
    uint32_t best_total_cost = INF;
    int32_t best_fwd_label_id = -1;
    int32_t best_bwd_label_id = -1;
    uint32_t best_meeting_airport = INF;

    // Helper lambda to test and update best complete path when frontiers meet
    auto test_connection = [&](uint32_t airport_id, const Label &fwd_lbl,
                               const BackwardLabel &bwd_lbl) {
      // Transfer feasibility check at meeting airport
      uint32_t min_layover = (airport_id == source_id || airport_id == dest_id)
                                 ? 0
                                 : nodes[airport_id].min_transfer;

      if (fwd_lbl.arrival_time + min_layover > bwd_lbl.departure_time) {
        return; // Arrived too late for onward departure
      }

      uint32_t wait = bwd_lbl.departure_time - fwd_lbl.arrival_time;
      if (airport_id != source_id && airport_id != dest_id && wait > 1440) {
        return; // Layover exceeds 24h
      }

      uint32_t total_c = 0;
      if (mode == OptimizeFor::PRICE) {
        total_c = fwd_lbl.cost + bwd_lbl.cost;
      } else {
        total_c = fwd_lbl.cost + wait + bwd_lbl.cost;
      }

      if (total_c < best_total_cost) {
        best_total_cost = total_c;
        best_fwd_label_id = fwd_lbl.airport_id == source_id && fwd_lbl.parent_label_id == -1 && airport_id == source_id
                                ? fwd_lbl.parent_label_id
                                : static_cast<int32_t>(&fwd_lbl - &fwd_labels[0]);
        best_bwd_label_id = static_cast<int32_t>(&bwd_lbl - &bwd_labels[0]);
        best_meeting_airport = airport_id;
      }
    };

    // Alternating bidirectional expansion
    while (!pq_fwd.empty() || !pq_bwd.empty()) {
      // Stopping criterion: if min possible unexpanded combined cost >= best seen, terminate
      if (!pq_fwd.empty() && !pq_bwd.empty()) {
        if (static_cast<uint32_t>(pq_fwd.top().total_cost) +
                static_cast<uint32_t>(pq_bwd.top().total_cost) >=
            best_total_cost) {
          break;
        }
      }

      // Decide which frontier to expand: balance based on top cost
      bool expand_forward = false;
      if (!pq_fwd.empty() && !pq_bwd.empty()) {
        expand_forward = (pq_fwd.top().total_cost <= pq_bwd.top().total_cost);
      } else if (!pq_fwd.empty()) {
        expand_forward = true;
      } else {
        expand_forward = false;
      }

      if (expand_forward) {
        // ==================== FORWARD STEP ====================
        SearchState current = pq_fwd.top();
        pq_fwd.pop();
        metrics.nodes_explored++;

        const Label &curr_label = fwd_labels[current.label_id];
        if (is_fwd_strictly_dominated(fwd_pareto[current.node_id],
                                      curr_label.cost, curr_label.arrival_time)) {
          continue;
        }

        // Check if this newly popped forward state can connect with any backward label at this airport
        for (const auto &bwd_lbl : bwd_labels) {
          if (bwd_lbl.airport_id == current.node_id) {
            test_connection(current.node_id, curr_label, bwd_lbl);
          }
        }

        for (const flight &edge : adjacency_list[current.node_id]) {
          metrics.edges_relaxed++;
          uint32_t next_node = edge.destination_id;

          uint32_t req_dep =
              current.current_time + nodes[current.node_id].min_transfer;
          if (current.node_id == source_id) {
            req_dep = current.current_time;
          }

          uint32_t abs_dep = edge.flight_day * 1440 + edge.departure_time;
          if (abs_dep < req_dep) continue;

          uint32_t wait_time = abs_dep - current.current_time;
          if (wait_time > 1440) continue;

          uint32_t duration =
              (edge.arrival_time +
               (edge.arrival_time < edge.departure_time ? 1440 : 0)) -
              edge.departure_time;
          uint32_t next_time = abs_dep + duration;

          uint32_t new_cost = 0;
          if (mode == OptimizeFor::PRICE) {
            new_cost = current.total_cost + edge.price;
          } else {
            new_cost = next_time - start_time;
          }

          if (new_cost > std::numeric_limits<uint16_t>::max() ||
              new_cost >= best_total_cost) {
            continue;
          }

          if (is_fwd_dominated(fwd_pareto[next_node], new_cost, next_time)) {
            continue;
          }

          prune_fwd_dominated(fwd_pareto[next_node], new_cost, next_time);
          fwd_pareto[next_node].push_back({new_cost, next_time});

          int32_t new_lbl_id = static_cast<int32_t>(fwd_labels.size());
          fwd_labels.push_back(
              Label{next_node, new_cost, next_time, current.label_id, edge});

          pq_fwd.push({next_time, new_lbl_id, static_cast<uint16_t>(new_cost),
                       static_cast<uint16_t>(next_node)});

          // Check connection with backward frontier at next_node
          for (const auto &bwd_lbl : bwd_labels) {
            if (bwd_lbl.airport_id == next_node) {
              test_connection(next_node, fwd_labels.back(), bwd_lbl);
            }
          }
        }
      } else {
        // ==================== BACKWARD STEP ====================
        BackwardSearchState current = pq_bwd.top();
        pq_bwd.pop();
        metrics.nodes_explored++;

        const BackwardLabel &curr_label = bwd_labels[current.label_id];
        if (is_bwd_strictly_dominated(bwd_pareto[current.node_id],
                                      curr_label.cost,
                                      curr_label.departure_time)) {
          continue;
        }

        // Check if this backward state connects with any forward label at this airport
        for (const auto &fwd_lbl : fwd_labels) {
          if (fwd_lbl.airport_id == current.node_id) {
            test_connection(current.node_id, fwd_lbl, curr_label);
          }
        }

        // Traverse incoming reverse flights into current.node_id
        for (const reverse_flight &rev : reverse_adjacency_list[current.node_id]) {
          metrics.edges_relaxed++;
          const flight &edge = rev.edge;
          uint32_t prev_node = rev.origin_id;

          uint32_t abs_dep = edge.flight_day * 1440 + edge.departure_time;
          if (abs_dep < start_time) continue;

          uint32_t duration =
              (edge.arrival_time +
               (edge.arrival_time < edge.departure_time ? 1440 : 0)) -
              edge.departure_time;
          uint32_t abs_arr = abs_dep + duration;

          // Layover feasibility at current.node_id (transfer to onward flight)
          uint32_t req_transfer = (current.node_id == dest_id)
                                      ? 0
                                      : nodes[current.node_id].min_transfer;

          if (abs_arr + req_transfer > current.departure_time) {
            continue; // Lands too late for onward flight
          }

          uint32_t wait_time = current.departure_time - abs_arr;
          if (current.node_id != dest_id && wait_time > 1440) {
            continue; // Layover exceeds 24h
          }

          uint32_t new_cost = 0;
          if (mode == OptimizeFor::PRICE) {
            new_cost = current.total_cost + edge.price;
          } else {
            new_cost = current.total_cost + duration + (current.node_id == dest_id ? 0 : wait_time);
          }

          if (new_cost > std::numeric_limits<uint16_t>::max() ||
              new_cost >= best_total_cost) {
            continue;
          }

          // In backward search, earlier departure at prev_node is abs_dep
          if (is_bwd_dominated(bwd_pareto[prev_node], new_cost, abs_dep)) {
            continue;
          }

          prune_bwd_dominated(bwd_pareto[prev_node], new_cost, abs_dep);
          bwd_pareto[prev_node].push_back({new_cost, abs_dep});

          int32_t new_lbl_id = static_cast<int32_t>(bwd_labels.size());
          bwd_labels.push_back(
              BackwardLabel{prev_node, new_cost, abs_dep, current.label_id, edge});

          pq_bwd.push({abs_dep, new_lbl_id, static_cast<uint16_t>(new_cost),
                       static_cast<uint16_t>(prev_node)});

          // Check connection with forward frontier at prev_node
          for (const auto &fwd_lbl : fwd_labels) {
            if (fwd_lbl.airport_id == prev_node) {
              test_connection(prev_node, fwd_lbl, bwd_labels.back());
            }
          }
        }
      }
    }

    if (best_total_cost == INF || best_bwd_label_id == -1) {
      return {false, 0, 0, {}, metrics};
    }

    return reconstruct_bidirectional_path(
        best_fwd_label_id, best_bwd_label_id, fwd_labels, bwd_labels,
        start_time, metrics);
  }

private:
  // --- Forward Dominance Helpers ---
  bool is_fwd_dominated(const std::vector<ParetoPoint> &labels, uint32_t cost,
                        uint32_t time) const {
    for (const auto &p : labels) {
      if (p.cost <= cost && p.arrival_time <= time) return true;
    }
    return false;
  }

  bool is_fwd_strictly_dominated(const std::vector<ParetoPoint> &labels,
                                 uint32_t cost, uint32_t time) const {
    for (const auto &p : labels) {
      if (p.cost <= cost && p.arrival_time <= time &&
          (p.cost < cost || p.arrival_time < time)) {
        return true;
      }
    }
    return false;
  }

  void prune_fwd_dominated(std::vector<ParetoPoint> &labels, uint32_t cost,
                           uint32_t time) {
    labels.erase(
        std::remove_if(labels.begin(), labels.end(),
                       [cost, time](const ParetoPoint &p) {
                         return p.cost >= cost && p.arrival_time >= time;
                       }),
        labels.end());
  }

  // --- Backward Dominance Helpers ---
  // In backward search, higher departure_time is strictly preferred (earlier flight can connect).
  bool is_bwd_dominated(const std::vector<BackwardParetoPoint> &labels,
                        uint32_t cost, uint32_t dep_time) const {
    for (const auto &p : labels) {
      if (p.cost <= cost && p.departure_time >= dep_time) return true;
    }
    return false;
  }

  bool is_bwd_strictly_dominated(const std::vector<BackwardParetoPoint> &labels,
                                 uint32_t cost, uint32_t dep_time) const {
    for (const auto &p : labels) {
      if (p.cost <= cost && p.departure_time >= dep_time &&
          (p.cost < cost || p.departure_time > dep_time)) {
        return true;
      }
    }
    return false;
  }

  void prune_bwd_dominated(std::vector<BackwardParetoPoint> &labels,
                           uint32_t cost, uint32_t dep_time) {
    labels.erase(
        std::remove_if(labels.begin(), labels.end(),
                       [cost, dep_time](const BackwardParetoPoint &p) {
                         return p.cost >= cost && p.departure_time <= dep_time;
                       }),
        labels.end());
  }

  // --- Path Stitching ---
  RoutingResult reconstruct_bidirectional_path(
      int32_t fwd_lbl_id, int32_t bwd_lbl_id,
      const std::vector<Label> &fwd_labels,
      const std::vector<BackwardLabel> &bwd_labels, uint32_t start_time,
      RoutingMetrics metrics) const {
    RoutingResult result;
    result.success = true;
    result.metrics = metrics;
    result.total_price = 0;

    // 1. Trace forward path from meeting node back to source
    std::vector<flight> fwd_segment;
    int32_t curr_f = fwd_lbl_id;
    while (curr_f != -1 && fwd_labels[curr_f].parent_label_id != -1) {
      fwd_segment.push_back(fwd_labels[curr_f].edge_taken);
      curr_f = fwd_labels[curr_f].parent_label_id;
    }
    std::reverse(fwd_segment.begin(), fwd_segment.end());

    // 2. Trace backward path from meeting node forward to destination
    std::vector<flight> bwd_segment;
    int32_t curr_b = bwd_lbl_id;
    while (curr_b != -1 && bwd_labels[curr_b].parent_label_id != -1) {
      bwd_segment.push_back(bwd_labels[curr_b].edge_taken);
      curr_b = bwd_labels[curr_b].parent_label_id;
    }

    // 3. Combine both halves
    result.path = std::move(fwd_segment);
    for (const auto &f : bwd_segment) {
      result.path.push_back(f);
    }

    // Calculate total price and total travel time
    for (const auto &f : result.path) {
      result.total_price += f.price;
    }

    if (!result.path.empty()) {
      const flight &last = result.path.back();
      uint32_t last_dep = last.flight_day * 1440 + last.departure_time;
      uint32_t last_dur =
          (last.arrival_time +
           (last.arrival_time < last.departure_time ? 1440 : 0)) -
          last.departure_time;
      uint32_t final_arrival = last_dep + last_dur;
      result.total_time_mins = final_arrival - start_time;
    } else {
      result.total_time_mins = 0;
    }

    return result;
  }
};

