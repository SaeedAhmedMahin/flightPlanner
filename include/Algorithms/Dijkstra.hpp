#pragma once
#include "../DataStructures/airport.hpp"
#include "../DataStructures/flight.hpp"
#include "SearchState.hpp"
#include <algorithm>
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

class Dijkstra {
public:
  const uint32_t INF = std::numeric_limits<uint32_t>::max();

  RoutingResult run(uint32_t source_id, uint32_t dest_id,
                    const std::vector<std::vector<flight>> &adjacency_list,
                    const std::vector<airport> &nodes, OptimizeFor mode,
                    uint16_t start_time = 480) {
    uint32_t num_nodes = adjacency_list.size();
    std::vector<uint32_t> min_cost(num_nodes, INF);

    // Arrays for textbook path reconstruction
    std::vector<uint32_t> parent_node(num_nodes, INF);
    std::vector<flight> edge_to(num_nodes);

    std::priority_queue<SearchState, std::vector<SearchState>,
                        std::greater<SearchState>>
        pq;

    min_cost[source_id] = 0;
    pq.push({0, static_cast<uint16_t>(source_id), start_time});

    RoutingMetrics metrics = {0, 0};

    while (!pq.empty()) {
      SearchState current = pq.top();
      pq.pop();
      metrics.nodes_explored++;

      if (current.node_id == dest_id) {
        return reconstruct_path(source_id, dest_id, current, parent_node,
                                edge_to, start_time, metrics);
      }

      if (current.total_cost > min_cost[current.node_id]) {
        continue;
      }

      for (const flight &edge : adjacency_list[current.node_id]) {
        metrics.edges_relaxed++;
        uint32_t next_node = edge.destination_id;

        uint32_t local_arrival_time = current.current_time % 1440;
        uint32_t required_departure =
            local_arrival_time + nodes[current.node_id].min_transfer;

        uint32_t wait_time = 0;
        if (edge.departure_time < required_departure) {
          wait_time = (1440 - local_arrival_time) + edge.departure_time;
        } else {
          wait_time = edge.departure_time - local_arrival_time;
        }

        uint32_t flight_duration =
            (edge.arrival_time + (edge.day_change * 1440)) -
            edge.departure_time;
        uint32_t next_time = current.current_time + wait_time + flight_duration;

        // Determine the optimization metric
        uint32_t new_cost = 0;
        if (mode == OptimizeFor::PRICE) {
          new_cost = current.total_cost + edge.price;
        } else {
          new_cost = next_time - start_time; // Total elapsed time
        }

        // Relaxation Step
        if (new_cost < min_cost[next_node]) {
          min_cost[next_node] = new_cost;

          // Track the exact edge we took to get here
          parent_node[next_node] = current.node_id;
          edge_to[next_node] = edge;

          pq.push({new_cost, static_cast<uint16_t>(next_node),
                   static_cast<uint16_t>(next_time)});
        }
      }
    }

    return {false, 0, 0, {}, metrics};
  }

private:
  RoutingResult reconstruct_path(uint32_t source_id, uint32_t dest_id,
                                 SearchState final_state,
                                 const std::vector<uint32_t> &parent_node,
                                 const std::vector<flight> &edge_to,
                                 uint16_t start_time, RoutingMetrics metrics) {
    RoutingResult result;
    result.success = true;
    result.metrics = metrics;
    result.total_time_mins = final_state.current_time - start_time;
    result.total_price = 0;

    uint32_t curr = dest_id;
    while (curr != source_id) {
      flight f = edge_to[curr];
      result.path.push_back(f);
      result.total_price += f.price;
      curr = parent_node[curr];
    }

    // The path is traced backwards from destination to source, so we flip it
    std::reverse(result.path.begin(), result.path.end());
    return result;
  }
};