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
// BFSRouter: Unweighted Breadth-First Search (Fewest Hops Baseline)
//
// Explores the time-expanded scheduled graph level-by-level in hop order.
// Guarantees finding the route with the minimum number of flight legs (hops).
// Among routes with the minimum hops, it selects the optimal one (by price/time).
// Useful baseline to demonstrate the cost/time penalty of minimizing stops alone.
// ---------------------------------------------------------------------------
class BFSRouter {
public:
  const uint32_t INF = std::numeric_limits<uint32_t>::max();

  struct QueueState {
    uint32_t node_id;
    uint32_t current_time;
    uint32_t total_cost;
    int32_t label_id;
    uint8_t hops;
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

    // Central immutable label store for path tracing
    std::vector<Label> all_labels;
    all_labels.reserve(65536);

    // Pareto frontier per airport: (cost, arrival_time, hops)
    std::vector<std::vector<ParetoPoint>> pareto_labels(num_nodes);

    // Standard FIFO Queue for BFS (level-by-level exploration)
    std::queue<QueueState> q;

    int32_t source_label_id = static_cast<int32_t>(all_labels.size());
    all_labels.push_back(Label{source_id, 0, start_time, -1, flight{}, 0});
    pareto_labels[source_id].push_back({0, start_time, 0});

    q.push({source_id, start_time, 0, source_label_id, 0});

    uint8_t min_dest_hops = 255;
    std::vector<int32_t> dest_candidate_labels;

    while (!q.empty()) {
      QueueState current = q.front();
      q.pop();
      metrics.nodes_explored++;

      // If we already reached the destination at fewer hops, BFS can stop
      // searching deeper levels once current hop level exceeds min_dest_hops
      if (current.hops > min_dest_hops) {
        break;
      }

      if (current.node_id == dest_id) {
        if (min_dest_hops == 255) {
          min_dest_hops = current.hops;
        }
        dest_candidate_labels.push_back(current.label_id);
        continue;
      }

      if (current.hops >= max_hops) {
        continue;
      }

      for (const flight &edge : adjacency_list[current.node_id]) {
        metrics.edges_relaxed++;
        uint32_t next_node = edge.destination_id;

        uint32_t req_dep = current.current_time + nodes[current.node_id].min_transfer;
        if (current.node_id == source_id) {
          req_dep = current.current_time;
        }

        // Flight departure in unified UTC minutes
        int32_t origin_utc_offset = nodes[current.node_id].utc_offset_15m * 15;
        uint32_t abs_dep = static_cast<uint32_t>(
            static_cast<int32_t>(edge.flight_day * 1440 + edge.departure_time) -
            origin_utc_offset);

        if (abs_dep < req_dep) {
          continue; // Missed flight
        }

        uint32_t wait_time = abs_dep - current.current_time;
        if (wait_time > 1440) {
          continue; // Layover > 24 hours
        }

        uint32_t duration = edge.duration_mins;
        uint32_t next_time = abs_dep + duration;

        uint32_t new_cost = 0;
        if (mode == OptimizeFor::PRICE) {
          new_cost = current.total_cost + edge.price;
        } else {
          new_cost = next_time - start_time;
        }

        if (new_cost > std::numeric_limits<uint16_t>::max()) {
          continue;
        }

        uint8_t next_hops = current.hops + 1;

        // Pareto dominance check at next_node
        if (is_dominated(next_node, dest_id, pareto_labels[next_node], new_cost,
                         next_time, next_hops)) {
          continue;
        }

        prune_dominated(next_node, dest_id, pareto_labels[next_node], new_cost,
                        next_time, next_hops);
        pareto_labels[next_node].push_back({new_cost, next_time, next_hops});

        int32_t new_label_id = static_cast<int32_t>(all_labels.size());
        all_labels.push_back(Label{next_node, new_cost, next_time,
                                   current.label_id, edge, next_hops});

        q.push({next_node, next_time, new_cost, new_label_id, next_hops});
      }
    }

    if (dest_candidate_labels.empty()) {
      return {false, 0, 0, {}, metrics};
    }

    // Among all candidate paths with minimum hops reaching destination, pick optimal
    int32_t best_label_id = dest_candidate_labels[0];
    for (size_t i = 1; i < dest_candidate_labels.size(); ++i) {
      int32_t lid = dest_candidate_labels[i];
      if (mode == OptimizeFor::PRICE) {
        if (all_labels[lid].cost < all_labels[best_label_id].cost ||
            (all_labels[lid].cost == all_labels[best_label_id].cost &&
             all_labels[lid].hops < all_labels[best_label_id].hops) ||
            (all_labels[lid].cost == all_labels[best_label_id].cost &&
             all_labels[lid].hops == all_labels[best_label_id].hops &&
             all_labels[lid].arrival_time < all_labels[best_label_id].arrival_time)) {
          best_label_id = lid;
        }
      } else {
        if (all_labels[lid].arrival_time < all_labels[best_label_id].arrival_time ||
            (all_labels[lid].arrival_time == all_labels[best_label_id].arrival_time &&
             all_labels[lid].cost < all_labels[best_label_id].cost) ||
            (all_labels[lid].arrival_time == all_labels[best_label_id].arrival_time &&
             all_labels[lid].cost == all_labels[best_label_id].cost &&
             all_labels[lid].hops < all_labels[best_label_id].hops)) {
          best_label_id = lid;
        }
      }
    }

    return reconstruct_path(best_label_id, all_labels, start_time, metrics);
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

  void prune_dominated(uint32_t node_id, uint32_t dest_id,
                       std::vector<ParetoPoint> &labels, uint32_t cost,
                       uint32_t time, uint8_t hops) {
    labels.erase(
        std::remove_if(
            labels.begin(), labels.end(),
            [node_id, dest_id, cost, time, hops](const ParetoPoint &p) {
              return p.cost >= cost && p.arrival_time >= time &&
                     p.hops >= hops &&
                     (node_id == dest_id || (p.arrival_time - time <= 1440));
            }),
        labels.end());
  }

  RoutingResult reconstruct_path(int32_t final_label_id,
                                 const std::vector<Label> &all_labels,
                                 uint32_t start_time,
                                 RoutingMetrics metrics) const {
    RoutingResult result;
    result.success = true;
    result.metrics = metrics;
    result.total_time_mins = all_labels[final_label_id].arrival_time - start_time;
    result.total_price = 0;

    int32_t curr_id = final_label_id;
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

