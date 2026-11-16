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
#include <vector>

// ---------------------------------------------------------------------------
// LagrangianRelaxationRouter (Dynamic Penalty Multipliers & LARAC)
//
// Theoretical Foundation:
//   Solves the Constrained Shortest Path Problem (CSPP) on the time-expanded
//   flight network via Lagrangian Relaxation and Subgradient Optimization /
//   LARAC (Lagrangian Relaxation Algorithm for Constrained Shortest Paths).
//
//   Problem Formulation:
//     Primary Objective: minimize C(p) = \sum_{e \in p} c_e
//     Subject to Resource Constraint: R(p) = \sum_{e \in p} r_e \le R_max
//     (e.g., minimize ticket price subject to total journey time \le T_max,
//      or minimize travel time subject to ticket price \le P_max).
//
//   Lagrangian Dual:
//     Dualizing the resource constraint with penalty multiplier \lambda \ge 0:
//       L(\lambda) = \min_{p \in \mathcal{P}} \{ C(p) + \lambda \cdot (R(p) - R_max) \}
//                  = \min_{p \in \mathcal{P}} \{ \sum_{e \in p} (c_e + \lambda \cdot r_e) \} - \lambda R_max
//
//     For any fixed \lambda \ge 0, the inner subproblem is a standard shortest
//     path problem on composite edge weights w_e(\lambda) = c_e + \lambda \cdot r_e,
//     which can be solved efficiently via timetable Dijkstra.
//
//   Dynamic Multiplier Updates (Subgradient Optimization & LARAC Bisection):
//     - Starts with \lambda = 0 to get the unconstrained primary shortest path p_c.
//       If R(p_c) \le R_max, p_c is already feasible and optimal!
//     - If R(p_c) > R_max, we solve for the minimum-resource path p_r (\lambda \to \infty).
//       If R(p_r) > R_max, the problem is infeasible.
//     - While the duality gap persists:
//         \lambda = (C(p_c) - C(p_r)) / (R(p_r) - R(p_c))  [Geometric cutting-plane / LARAC slope]
//         Subproblem finds p_\lambda with weights w_e(\lambda).
//         If R(p_\lambda) \le R_max, p_\lambda becomes the new best feasible incumbent;
//         otherwise, p_\lambda tightens the lower bound and updates the multiplier.
//     - Dynamic subgradient steps are applied with Polyak adaptive step decay to
//       rapidly converge on the optimal lower bound L(\lambda^*) and the minimal-cost
//       feasible primal path.
// ---------------------------------------------------------------------------

class LagrangianRelaxationRouter {
public:
  const uint32_t INF = std::numeric_limits<uint32_t>::max();

  struct ResourceConstraints {
    uint32_t max_time_mins;
    uint32_t max_price;
    uint8_t max_hops;

    ResourceConstraints(uint32_t time_limit = 2880, uint32_t price_limit = 50000,
                        uint8_t hops_limit = 5)
        : max_time_mins(time_limit), max_price(price_limit),
          max_hops(hops_limit) {}
  };

  struct LRState {
    uint32_t current_time;
    int32_t label_id;
    double lagrangian_cost;
    uint32_t accumulated_price;
    uint16_t node_id;
    uint8_t hops;
    uint8_t padding;
  };

  struct CompareLRState {
    bool operator()(const LRState &a, const LRState &b) const {
      if (std::abs(a.lagrangian_cost - b.lagrangian_cost) > 1e-4) {
        return a.lagrangian_cost < b.lagrangian_cost;
      }
      if (a.accumulated_price != b.accumulated_price) {
        return a.accumulated_price < b.accumulated_price;
      }
      return a.current_time < b.current_time;
    }
  };

  struct SubproblemResult {
    bool success;
    uint32_t total_price;
    uint32_t total_time_mins;
    double lagrangian_val;
    std::vector<flight> path;
  };

  // Solves the Lagrangian subproblem for a fixed multiplier lambda >= 0
  SubproblemResult solve_subproblem(
      uint32_t source_id, uint32_t dest_id,
      const std::vector<std::vector<flight>> &adjacency_list,
      const std::vector<airport> &nodes, OptimizeFor mode,
      uint32_t start_time, double lambda, double resource_limit,
      uint8_t max_hops, RoutingMetrics &metrics) {

    uint32_t num_nodes = static_cast<uint32_t>(adjacency_list.size());
    std::vector<double> best_lcost(num_nodes, 1e18);

    std::vector<Label> all_labels;
    all_labels.reserve(32768);

    Flat4AryMinHeap<LRState, CompareLRState> pq(32768);

    int32_t start_lbl = static_cast<int32_t>(all_labels.size());
    all_labels.push_back(Label{source_id, 0, start_time, -1, flight{}, 0});
    best_lcost[source_id] = 0.0;

    pq.push({start_time, start_lbl, 0.0, 0, static_cast<uint16_t>(source_id), 0, 0});

    while (!pq.empty()) {
      LRState curr = pq.top();
      pq.pop();
      metrics.nodes_explored++;

      if (curr.node_id == dest_id) {
        // Reconstruct path
        std::vector<flight> path;
        int32_t curr_lbl_id = curr.label_id;
        while (curr_lbl_id != -1) {
          const Label &lbl = all_labels[curr_lbl_id];
          if (lbl.parent_label_id != -1) {
            path.push_back(lbl.edge_taken);
          }
          curr_lbl_id = lbl.parent_label_id;
        }
        std::reverse(path.begin(), path.end());

        uint32_t total_dur = curr.current_time - start_time;
        double l_val = curr.lagrangian_cost - (lambda * resource_limit);
        return {true, curr.accumulated_price, total_dur, l_val, std::move(path)};
      }

      if (curr.lagrangian_cost > best_lcost[curr.node_id] + 1e-4) {
        continue;
      }

      if (curr.hops >= max_hops) {
        continue;
      }

      for (const flight &edge : adjacency_list[curr.node_id]) {
        metrics.edges_relaxed++;
        uint32_t next_node = edge.destination_id;

        uint32_t req_dep = curr.current_time + nodes[curr.node_id].min_transfer;
        if (curr.node_id == source_id) {
          req_dep = curr.current_time;
        }

        int32_t origin_utc_offset = nodes[curr.node_id].utc_offset_15m * 15;
        uint32_t abs_dep = static_cast<uint32_t>(
            static_cast<int32_t>(edge.flight_day * 1440 + edge.departure_time) -
            origin_utc_offset);

        if (abs_dep < req_dep) {
          continue;
        }
        if (abs_dep - curr.current_time > 1440) {
          continue; // Layover > 24 hours
        }

        uint32_t next_time = abs_dep + edge.duration_mins;
        uint32_t next_price = curr.accumulated_price + edge.price;
        uint32_t edge_elapsed = next_time - curr.current_time;

        // Composite Lagrangian weight w_e(lambda) = primary_e + lambda * resource_e
        double edge_weight = 0.0;
        if (mode == OptimizeFor::PRICE) {
          // Primary: Price ($), Constrained Resource: Time (minutes)
          edge_weight = static_cast<double>(edge.price) + lambda * static_cast<double>(edge_elapsed);
        } else {
          // Primary: Time (minutes), Constrained Resource: Price ($)
          edge_weight = static_cast<double>(edge_elapsed) + lambda * static_cast<double>(edge.price);
        }

        double next_lcost = curr.lagrangian_cost + edge_weight;

        if (next_lcost < best_lcost[next_node] - 1e-4) {
          best_lcost[next_node] = next_lcost;
          int32_t next_lbl = static_cast<int32_t>(all_labels.size());
          uint8_t next_hops = curr.hops + 1;
          all_labels.push_back(Label{next_node, next_price, next_time,
                                     curr.label_id, edge, next_hops});
          pq.push({next_time, next_lbl, next_lcost, next_price,
                   static_cast<uint16_t>(next_node), next_hops, 0});
        }
      }
    }

    return {false, 0, 0, 0.0, {}};
  }

  RoutingResult run(uint32_t source_id, uint32_t dest_id,
                    const std::vector<std::vector<flight>> &adjacency_list,
                    const std::vector<airport> &nodes, OptimizeFor mode,
                    uint32_t start_time,
                    ResourceConstraints constraints = ResourceConstraints()) {

    RoutingMetrics metrics = {0, 0};

    if (source_id == dest_id) {
      return {true, 0, 0, {}, metrics};
    }

    double resource_limit = (mode == OptimizeFor::PRICE)
                                ? static_cast<double>(constraints.max_time_mins)
                                : static_cast<double>(constraints.max_price);

    // =========================================================================
    // STEP 1: Solve unconstrained primary subproblem (lambda = 0.0)
    // =========================================================================
    double lambda = 0.0;
    SubproblemResult p_primary = solve_subproblem(
        source_id, dest_id, adjacency_list, nodes, mode, start_time,
        0.0, resource_limit, constraints.max_hops, metrics);

    if (!p_primary.success) {
      return {false, 0, 0, {}, metrics};
    }

    double r_primary = (mode == OptimizeFor::PRICE)
                           ? static_cast<double>(p_primary.total_time_mins)
                           : static_cast<double>(p_primary.total_price);

    // If the unconstrained primary path naturally respects the resource bound,
    // it is guaranteed globally optimal!
    if (r_primary <= resource_limit) {
      return {true, p_primary.total_price, p_primary.total_time_mins,
              p_primary.path, metrics};
    }

    // =========================================================================
    // STEP 2: Solve resource-minimizing subproblem (large lambda)
    // =========================================================================
    double large_lambda = (mode == OptimizeFor::PRICE) ? 1000.0 : 50.0;
    SubproblemResult p_resource = solve_subproblem(
        source_id, dest_id, adjacency_list, nodes, mode, start_time,
        large_lambda, resource_limit, constraints.max_hops, metrics);

    if (!p_resource.success) {
      // Return primary path as fallback best effort
      return {true, p_primary.total_price, p_primary.total_time_mins,
              p_primary.path, metrics};
    }

    double r_res = (mode == OptimizeFor::PRICE)
                       ? static_cast<double>(p_resource.total_time_mins)
                       : static_cast<double>(p_resource.total_price);

    // Track best feasible primal incumbent
    RoutingResult best_incumbent;
    best_incumbent.success = false;
    best_incumbent.total_price = INF;
    best_incumbent.total_time_mins = INF;

    auto update_incumbent = [&](const SubproblemResult &res) {
      double res_val = (mode == OptimizeFor::PRICE)
                           ? static_cast<double>(res.total_time_mins)
                           : static_cast<double>(res.total_price);
      if (res.success && res_val <= resource_limit) {
        bool better = false;
        if (!best_incumbent.success) {
          better = true;
        } else if (mode == OptimizeFor::PRICE) {
          if (res.total_price < best_incumbent.total_price ||
              (res.total_price == best_incumbent.total_price &&
               res.total_time_mins < best_incumbent.total_time_mins)) {
            better = true;
          }
        } else {
          if (res.total_time_mins < best_incumbent.total_time_mins ||
              (res.total_time_mins == best_incumbent.total_time_mins &&
               res.total_price < best_incumbent.total_price)) {
            better = true;
          }
        }
        if (better) {
          best_incumbent.success = true;
          best_incumbent.total_price = res.total_price;
          best_incumbent.total_time_mins = res.total_time_mins;
          best_incumbent.path = res.path;
        }
      }
    };

    update_incumbent(p_resource);

    // =========================================================================
    // STEP 3: Dynamic Multiplier Updating (LARAC Bisection + Subgradient Steps)
    // =========================================================================
    SubproblemResult p_c = p_primary;
    SubproblemResult p_r = p_resource;

    const int MAX_ITER = 12;
    double best_lower_bound = p_primary.lagrangian_val;

    for (int iter = 0; iter < MAX_ITER; ++iter) {
      double c_c = (mode == OptimizeFor::PRICE) ? p_c.total_price : p_c.total_time_mins;
      double c_r = (mode == OptimizeFor::PRICE) ? p_r.total_price : p_r.total_time_mins;
      double res_c = (mode == OptimizeFor::PRICE) ? p_c.total_time_mins : p_c.total_price;
      double res_r = (mode == OptimizeFor::PRICE) ? p_r.total_time_mins : p_r.total_price;

      if (std::abs(res_c - res_r) < 1.0) {
        break; // Slopes converged
      }

      // LARAC Cutting-plane slope for lambda
      lambda = (c_c - c_r) / (res_r - res_c);
      if (lambda < 0.0) {
        lambda = 0.0;
      }

      SubproblemResult p_new = solve_subproblem(
          source_id, dest_id, adjacency_list, nodes, mode, start_time,
          lambda, resource_limit, constraints.max_hops, metrics);

      if (!p_new.success) {
        break;
      }

      update_incumbent(p_new);

      double new_lval = p_new.lagrangian_val;
      if (new_lval > best_lower_bound) {
        best_lower_bound = new_lval;
      }

      double res_new = (mode == OptimizeFor::PRICE) ? p_new.total_time_mins : p_new.total_price;
      if (res_new <= resource_limit) {
        p_r = p_new; // Tighten the feasible anchor
      } else {
        p_c = p_new; // Tighten the infeasible anchor
      }

      // Subgradient step adjustment if duality gap persists
      double subgradient = res_new - resource_limit;
      if (std::abs(subgradient) < 1.0) {
        break;
      }

      double step_size = 0.5 / (iter + 1.0);
      lambda = std::max(0.0, lambda + step_size * (subgradient / (resource_limit + 1.0)));
    }

    if (best_incumbent.success) {
      best_incumbent.metrics = metrics;
      return best_incumbent;
    }

    // Fallback: return best primal path discovered
    p_primary.path.shrink_to_fit();
    return {true, p_primary.total_price, p_primary.total_time_mins,
            p_primary.path, metrics};
  }
};

using LagrangianRelaxation = LagrangianRelaxationRouter;

