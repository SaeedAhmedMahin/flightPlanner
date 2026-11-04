#pragma once
#include "../DataStructures/airport.hpp"
#include "../DataStructures/flight.hpp"
#include "SearchState.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <queue>
#include <vector>

enum class OptimizeFor { PRICE, TIME };

struct RoutingMetrics {
  uint32_t nodes_explored;
  uint32_t edges_relaxed;
};

struct RoutingResult {
  bool success;
  uint32_t total_price;
  uint32_t total_time_mins;
  std::vector<flight> path;
  RoutingMetrics metrics;
};

// ---------------------------------------------------------------------------
// ParetoPoint: A single non-dominated (cost, arrival_time) tuple stored per
// airport. A state is dominated if another state reaches the same airport with
// BOTH a lower-or-equal cost AND an earlier-or-equal arrival time, meaning the
// dominated state can never lead to a better complete route on any criterion.
// ---------------------------------------------------------------------------
struct ParetoPoint {
  uint32_t cost;
  uint32_t arrival_time;
  uint8_t hops;
};
// SearchState (with label_id), CompareCost, and Label are defined in
// SearchState.hpp and included above.

class Dijkstra {
public:
  const uint32_t INF = std::numeric_limits<uint32_t>::max();

  RoutingResult run(uint32_t source_id, uint32_t dest_id,
                    const std::vector<std::vector<flight>> &adjacency_list,
                    const std::vector<airport> &nodes, OptimizeFor mode,
                    uint32_t start_time, uint8_t max_hops = 5) {
    uint32_t num_nodes = adjacency_list.size();

    // pareto_labels[airport_id] holds all currently non-dominated
    // (cost, arrival_time) pairs at that airport. Replaces the old scalar
    // min_cost[node], which incorrectly pruned a route that costs more but
    // arrives earlier — blocking it from catching a cheaper onward connection.
    std::vector<std::vector<ParetoPoint>> pareto_labels(num_nodes);

    // Central label store: every accepted non-dominated state gets a Label.
    // Labels are immutable after creation (only appended, never mutated).
    // Path reconstruction walks the parent_label_id chain from dest to source.
    std::vector<Label> all_labels;
    all_labels.reserve(65536); // Pre-allocate to avoid frequent re-allocations

    // SearchState carries (current_time, label_id, total_cost, node_id).
    // CompareCost orders the min-heap by total_cost (defined in
    // SearchState.hpp)
    std::priority_queue<SearchState, std::vector<SearchState>, CompareCost> pq;

    // Seed: source airport with zero cost and the user-supplied query time.
    // label_id = -1 signals the chain root (no parent edge).
    int32_t source_label_id = static_cast<int32_t>(all_labels.size());
    all_labels.push_back(Label{source_id, 0, start_time, -1, flight{}, 0});
    pareto_labels[source_id].push_back({0, start_time, 0});
    pq.push({start_time,
             source_label_id,
             0,
             static_cast<uint16_t>(source_id),
             0,
             {0, 0, 0}});

    RoutingMetrics metrics = {0, 0};

    while (!pq.empty()) {
      SearchState current = pq.top();
      pq.pop();
      metrics.nodes_explored++;

      // Early exit: first time we pop the destination it is the optimal route
      // for the primary criterion (cost or time) by Dijkstra's guarantee.
      if (current.node_id == dest_id) {
        return reconstruct_path(current, all_labels, nodes, start_time,
                                metrics);
      }

      // Stale-state check: if a strictly better label (cheaper on cost AND
      // arriving earlier within the 24h layover window) has been accepted at
      // this airport since this state was enqueued, this state is now
      // dominated.
      const Label &curr_label = all_labels[current.label_id];
      if (is_strictly_dominated(current.node_id, dest_id,
                                pareto_labels[current.node_id], curr_label.cost,
                                curr_label.arrival_time, current.hops)) {
        continue;
      }

      // If we've already reached max_hops legs, cannot expand further onward
      // legs
      if (current.hops >= max_hops) {
        continue;
      }

      for (const flight &edge : adjacency_list[current.node_id]) {
        metrics.edges_relaxed++;
        uint32_t next_node = edge.destination_id;

        // Minimum time we must depart from this airport: passenger needs time
        // to clear the gate or security for a transfer. Source has no transfer
        // time (we are already at the departure terminal at query time).
        uint32_t required_departure =
            current.current_time + nodes[current.node_id].min_transfer;

        // The first node has no transfer time required
        if (current.node_id == source_id) {
          required_departure = current.current_time;
        }

        uint32_t absolute_flight_departure =
            edge.flight_day * 1440 + edge.departure_time;

        if (absolute_flight_departure < required_departure) {
          continue; // Missed the flight (or it departed in the past)
        }

        uint32_t wait_time = absolute_flight_departure - current.current_time;
        if (wait_time > 1440) {
          continue; // Transit wait is over 24 hours, skip
        }

        // Compute absolute minute we touch down at next_node.
        // arrival_time is local time so add 1440 if the flight crosses
        // midnight.
        uint32_t flight_duration =
            (edge.arrival_time +
             (edge.arrival_time < edge.departure_time ? 1440 : 0)) -
            edge.departure_time;
        uint32_t next_time = absolute_flight_departure + flight_duration;

        // Determine the optimization metric
        uint32_t new_cost = 0;
        if (mode == OptimizeFor::PRICE) {
          new_cost = current.total_cost + edge.price;
        } else {
          new_cost = next_time - start_time; // Total elapsed time from query
        }

        // Drop routes exceeding 16-bit total_cost capacity ($65,535 or ~45.5
        // days elapsed)
        if (new_cost > std::numeric_limits<uint16_t>::max()) {
          continue;
        }

        uint8_t next_hops = current.hops + 1;

        // Pareto dominance check: if an existing label at next_node is both
        // cheaper, arrives earlier, and has <= hops, check if it dominates.
        // At the final destination: earlier + cheaper + <= hops always
        // dominates. At intermediate hubs: max transit is 24h (1440m). An
        // arrival >24h earlier cannot catch onward flights available to this
        // state and does not dominate.
        if (is_dominated(next_node, dest_id, pareto_labels[next_node], new_cost,
                         next_time, next_hops)) {
          continue;
        }

        // Accept: evict any existing labels at next_node that this state
        // dominates
        prune_dominated(next_node, dest_id, pareto_labels[next_node], new_cost,
                        next_time, next_hops);
        pareto_labels[next_node].push_back({new_cost, next_time, next_hops});

        // Create an immutable Label linked to its parent for path tracing
        int32_t new_label_id = static_cast<int32_t>(all_labels.size());
        all_labels.push_back(Label{next_node, new_cost, next_time,
                                   current.label_id, edge, next_hops});

        // next_time becomes current_time for this new state so that the next
        // iteration has the correct arrival time for transfer window checks.
        pq.push({next_time,
                 new_label_id,
                 static_cast<uint16_t>(new_cost),
                 static_cast<uint16_t>(next_node),
                 next_hops,
                 {0, 0, 0}});
      }
    }

    return {false, 0, 0, {}, metrics};
  }

private:
  // Returns true if (cost, time, hops) is dominated by an existing Pareto
  // point. At destination, the journey is complete: cheaper + earlier + <= hops
  // always dominates. At intermediate hubs: max transit is 24h (1440 mins). An
  // arrival >24h earlier has already expired from the layover window and cannot
  // connect to flights available to this candidate, so it does not dominate.
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

  // Returns true if (cost, time, hops) is strictly dominated for the
  // stale-state check.
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

  // Removes any Pareto points at an airport that are strictly dominated by
  // the new (cost, time, hops) tuple within the valid layover window.
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

  // Walks the parent_label_id chain from the destination label back to the
  // source to reconstruct the exact sequence of flights taken.
  RoutingResult reconstruct_path(const SearchState &final_state,
                                 const std::vector<Label> &all_labels,
                                 const std::vector<airport> &nodes,
                                 uint32_t start_time,
                                 RoutingMetrics metrics) const {
    RoutingResult result;
    result.success = true;
    result.metrics = metrics;
    // final_state.current_time is the absolute arrival minute at the
    // destination (next_time was stored as current_time when pushed to the pq)
    result.total_time_mins = final_state.current_time - start_time;
    result.total_price = 0;

    // Walk backwards from destination label to source label via parent chain
    int32_t curr_id = final_state.label_id;
    while (curr_id != -1 && all_labels[curr_id].parent_label_id != -1) {
      const Label &lbl = all_labels[curr_id];
      result.path.push_back(lbl.edge_taken);

      int32_t parent_id = lbl.parent_label_id;
      uint32_t origin_node = all_labels[parent_id].airport_id;
      uint32_t dest_node = lbl.airport_id;

      result.total_price += lbl.edge_taken.price;

      curr_id = parent_id;
    }

    // The path is traced backwards from destination to source, so we flip it
    std::reverse(result.path.begin(), result.path.end());
    return result;
  }
};