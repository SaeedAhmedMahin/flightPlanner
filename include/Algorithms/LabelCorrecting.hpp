#pragma once
#include "../DataStructures/airport.hpp"
#include "../DataStructures/flight.hpp"
#include "Dijkstra.hpp"
#include "SearchState.hpp"
#include <algorithm>
#include <limits>
#include <queue>
#include <vector>

// ---------------------------------------------------------------------------
// LabelCorrectingRouter:
// Shortest Path with Resource Constraints (SPPRC) via Bellman-Ford Label-Correcting.
//
// Unlike Label-Setting algorithms (Dijkstra/A*) that freeze a node once popped from
// a min-heap, Label-Correcting maintains active candidate labels across the graph.
// In each step:
// 1. A node u is dequeued from an active worklist (FIFO queue).
// 2. Outgoing flights from u are extended to next_node v.
// 3. New candidate labels are checked against the Pareto frontier at v:
//    - If dominated by an existing label, it is pruned.
//    - If non-dominated, it is inserted into v's frontier and dominates older labels.
//    - When v's frontier is updated ("corrected"), v is re-added to the worklist
//      so improvements propagate downstream.
// 4. Bounded by resource constraints: max transit window (24h) and max hops (5).
// ---------------------------------------------------------------------------
class LabelCorrectingRouter {
public:
  const uint32_t INF = std::numeric_limits<uint32_t>::max();

  struct ExtendedLabel {
    uint32_t cost;
    uint32_t arrival_time;
    uint8_t hops;
    int32_t label_id; // Pointer to global all_labels for path tracing
  };

  RoutingResult run(uint32_t source_id, uint32_t dest_id,
                    const std::vector<std::vector<flight>> &adjacency_list,
                    const std::vector<airport> &nodes, OptimizeFor mode,
                    uint32_t start_time, uint8_t max_hops = 5) {
    RoutingMetrics metrics = {0, 0};

    if (source_id == dest_id) {
      return {true, 0, 0, {}, metrics};
    }

    uint32_t num_nodes = adjacency_list.size();

    // Central immutable store for path tracing
    std::vector<Label> all_labels;
    all_labels.reserve(65536);

    // Frontier of non-dominated labels per airport node
    std::vector<std::vector<ExtendedLabel>> frontiers(num_nodes);

    // Track which labels at node u have already been expanded downstream
    // expanded_idx[u] points to the first unexpanded label in frontiers[u]
    std::vector<size_t> expanded_idx(num_nodes, 0);

    // Worklist for Bellman-Ford active node processing
    std::queue<uint32_t> worklist;
    std::vector<bool> in_worklist(num_nodes, false);

    // Initialize source
    int32_t source_label_id = static_cast<int32_t>(all_labels.size());
    all_labels.push_back(Label{source_id, 0, start_time, -1, flight{}, 0});
    frontiers[source_id].push_back({0, start_time, 0, source_label_id});

    worklist.push(source_id);
    in_worklist[source_id] = true;

    while (!worklist.empty()) {
      uint32_t u = worklist.front();
      worklist.pop();
      in_worklist[u] = false;
      metrics.nodes_explored++;

      // We expand only new/corrected labels at node u that haven't been extended yet
      size_t start_idx = expanded_idx[u];
      size_t end_idx = frontiers[u].size();
      expanded_idx[u] = end_idx;

      for (size_t l_idx = start_idx; l_idx < end_idx; ++l_idx) {
        const ExtendedLabel &cur = frontiers[u][l_idx];

        // If current label has reached the maximum hop limit, it cannot branch further
        if (cur.hops >= max_hops) {
          continue;
        }

        // Destination reached: journey terminates, do not branch outward from destination
        if (u == dest_id) {
          continue;
        }

        for (const flight &edge : adjacency_list[u]) {
          metrics.edges_relaxed++;
          uint32_t next_node = edge.destination_id;

          // Transit transfer buffer requirement
          uint32_t req_dep = cur.arrival_time + nodes[u].min_transfer;
          if (u == source_id) {
            req_dep = cur.arrival_time; // First departure has no layover penalty
          }

          uint32_t abs_dep = edge.flight_day * 1440 + edge.departure_time;
          if (abs_dep < req_dep) {
            continue; // Missed flight
          }

          uint32_t wait_time = abs_dep - cur.arrival_time;
          if (wait_time > 1440) {
            continue; // Layover > 24 hours
          }

          uint32_t duration =
              (edge.arrival_time +
               (edge.arrival_time < edge.departure_time ? 1440 : 0)) -
              edge.departure_time;
          uint32_t next_time = abs_dep + duration;

          uint32_t new_cost = 0;
          if (mode == OptimizeFor::PRICE) {
            new_cost = cur.cost + edge.price;
          } else {
            new_cost = next_time - start_time;
          }

          if (new_cost > std::numeric_limits<uint16_t>::max()) {
            continue;
          }

          uint8_t next_hops = cur.hops + 1;

          // Resource & Pareto Dominance Check at next_node
          if (is_dominated(next_node, dest_id, frontiers[next_node], new_cost,
                           next_time, next_hops)) {
            continue;
          }

          // Prune dominated labels at next_node
          prune_dominated(next_node, dest_id, frontiers[next_node], expanded_idx[next_node],
                          new_cost, next_time, next_hops);

          // Append new non-dominated label
          int32_t new_label_id = static_cast<int32_t>(all_labels.size());
          all_labels.push_back(Label{next_node, new_cost, next_time,
                                     cur.label_id, edge, next_hops});

          frontiers[next_node].push_back(
              {new_cost, next_time, next_hops, new_label_id});

          // If next_node's frontier was corrected and it's not the destination,
          // queue next_node to propagate improvements downstream
          if (next_node != dest_id && !in_worklist[next_node]) {
            worklist.push(next_node);
            in_worklist[next_node] = true;
          }
        }
      }
    }

    // Select the optimal solution from destination's Pareto frontier
    if (frontiers[dest_id].empty()) {
      return {false, 0, 0, {}, metrics};
    }

    const ExtendedLabel *best_lbl = nullptr;
    for (const auto &lbl : frontiers[dest_id]) {
      if (!best_lbl) {
        best_lbl = &lbl;
        continue;
      }
      if (mode == OptimizeFor::PRICE) {
        if (lbl.cost < best_lbl->cost ||
            (lbl.cost == best_lbl->cost && lbl.hops < best_lbl->hops) ||
            (lbl.cost == best_lbl->cost && lbl.hops == best_lbl->hops &&
             lbl.arrival_time < best_lbl->arrival_time)) {
          best_lbl = &lbl;
        }
      } else {
        if (lbl.arrival_time < best_lbl->arrival_time ||
            (lbl.arrival_time == best_lbl->arrival_time && lbl.cost < best_lbl->cost) ||
            (lbl.arrival_time == best_lbl->arrival_time && lbl.cost == best_lbl->cost &&
             lbl.hops < best_lbl->hops)) {
          best_lbl = &lbl;
        }
      }
    }

    return reconstruct_path(*best_lbl, all_labels, start_time, metrics);
  }

private:
  bool is_dominated(uint32_t node_id, uint32_t dest_id,
                    const std::vector<ExtendedLabel> &frontier, uint32_t cost,
                    uint32_t time, uint8_t hops) const {
    for (const auto &p : frontier) {
      if (p.cost <= cost && p.arrival_time <= time && p.hops <= hops) {
        if (node_id == dest_id || (time - p.arrival_time <= 1440)) {
          return true;
        }
      }
    }
    return false;
  }

  void prune_dominated(uint32_t node_id, uint32_t dest_id,
                       std::vector<ExtendedLabel> &frontier, size_t &exp_idx,
                       uint32_t cost, uint32_t time, uint8_t hops) {
    size_t write_idx = 0;
    size_t old_exp_idx = exp_idx;

    for (size_t read_idx = 0; read_idx < frontier.size(); ++read_idx) {
      const auto &p = frontier[read_idx];
      bool dominated = (p.cost >= cost && p.arrival_time >= time && p.hops >= hops &&
                        (node_id == dest_id || (p.arrival_time - time <= 1440)));

      if (!dominated) {
        if (write_idx != read_idx) {
          frontier[write_idx] = frontier[read_idx];
        }
        write_idx++;
      } else {
        // If an already-expanded label was pruned, adjust exp_idx
        if (read_idx < old_exp_idx) {
          exp_idx--;
        }
      }
    }
    frontier.resize(write_idx);
  }

  RoutingResult reconstruct_path(const ExtendedLabel &best_label,
                                 const std::vector<Label> &all_labels,
                                 uint32_t start_time,
                                 RoutingMetrics metrics) const {
    RoutingResult result;
    result.success = true;
    result.metrics = metrics;
    result.total_time_mins = best_label.arrival_time - start_time;
    result.total_price = 0;

    int32_t curr_id = best_label.label_id;
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

