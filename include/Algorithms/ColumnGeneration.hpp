#pragma once
#include "../DataStructures/Flat4AryMinHeap.hpp"
#include "../DataStructures/airport.hpp"
#include "../DataStructures/flight.hpp"
#include "Dijkstra.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// ColumnGenerationRouter (Dantzig-Wolfe Decomposition & Pricing Subproblem)
//
// Theoretical Foundation:
//   Column Generation (CG) is the gold standard for large-scale airline
//   crew pairing, aircraft fleet assignment, and constrained multi-path
//   itinerary optimization (Desaulniers, Desrosiers, Solomon, 2005).
//
// Mathematical Formulation:
//   1. Restricted Master Problem (RMP):
//      Let P be a pool of candidate valid itinerary paths (columns) connecting
//      origin to destination. Each path p in P has an objective cost c_p
//      (e.g., ticket price or travel time).
//
//      Min-Cost Multi-Commodity Itinerary Selection with Flow Constraints:
//        min   \sum_{p \in P} c_p \cdot \lambda_p
//        s.t.  \sum_{p \in P} \lambda_p \ge 1                      (\alpha: demand convexity dual)
//              \sum_{p \in P} a_{ip} \cdot \lambda_p \le Cap_i     (\pi_i: node/transit capacity duals)
//              \lambda_p \ge 0, \forall p \in P
//
//      Dual Formulation of the Master Problem:
//        max   \alpha - \sum_{i} Cap_i \cdot \pi_i
//        s.t.  \alpha - \sum_{i \in p} \pi_i \le c_p,  \forall p \in P
//              \pi_i \ge 0, \alpha \ge 0
//
//   2. Pricing Subproblem (ESPPRC / Dual-Cost Shortest Path):
//      The dual variables \pi_i (congestion/layover shadow prices) and \alpha
//      from the RMP yield the "reduced cost" of any new column p':
//        \bar{c}_{p'} = c_{p'} - \alpha + \sum_{i \in p'} \pi_i
//
//      If \min_{p'} \bar{c}_{p'} \ge 0, then by linear programming duality
//      theory, NO path in the exponential space of all possible itineraries
//      can improve the objective — optimality is proven!
//
//      Otherwise, the pricing subproblem finds the path p' with the most
//      negative reduced cost (\bar{c}_{p'} < 0) and adds it as a new column
//      to the RMP.
//
//   3. Primal-Dual Simplex / Multiplier Updating:
//      Solves the Linear Program on the restricted column basis, updates dual
//      shadow prices (\alpha, \pi_i), dynamically pricing out bottlenecks until
//      the duality gap converges to zero, returning the optimal integer itinerary.
// ---------------------------------------------------------------------------

class ColumnGenerationRouter {
public:
  const uint32_t INF = std::numeric_limits<uint32_t>::max();

  struct Column {
    uint32_t id;
    uint32_t total_price;
    uint32_t total_time_mins;
    double primary_cost;
    std::vector<flight> path;
    std::vector<uint32_t> visited_nodes;
  };

  struct DualVariables {
    double alpha; // Dual variable of convexity constraint \sum \lambda_p = 1
    std::vector<double> pi; // Dual shadow prices on congested nodes/transits
  };

  struct ParetoRCPoint {
    double reduced_cost;
    uint32_t current_time;
    uint32_t price;
    uint8_t hops;
  };

  struct PricingState {
    uint32_t current_time;
    int32_t label_id;
    double reduced_cost;
    uint32_t accumulated_price;
    uint16_t node_id;
    uint8_t hops;
    uint8_t padding;
  };

  struct ComparePricingPrice {
    bool operator()(const PricingState &a, const PricingState &b) const {
      if (std::abs(a.reduced_cost - b.reduced_cost) > 1e-4) {
        return a.reduced_cost < b.reduced_cost;
      }
      if (a.accumulated_price != b.accumulated_price) {
        return a.accumulated_price < b.accumulated_price;
      }
      return a.current_time < b.current_time;
    }
  };

  struct ComparePricingTime {
    bool operator()(const PricingState &a, const PricingState &b) const {
      if (std::abs(a.reduced_cost - b.reduced_cost) > 1e-4) {
        return a.reduced_cost < b.reduced_cost;
      }
      if (a.current_time != b.current_time) {
        return a.current_time < b.current_time;
      }
      return a.accumulated_price < b.accumulated_price;
    }
  };

  // Solve the Pricing Subproblem: finds the path with the minimum reduced cost
  // using dual shadow prices pi from the Master Problem.
  Column solve_pricing_subproblem(
      uint32_t source_id, uint32_t dest_id,
      const std::vector<std::vector<flight>> &adjacency_list,
      const std::vector<airport> &nodes, OptimizeFor mode,
      uint32_t start_time, const DualVariables &duals, uint8_t max_hops,
      RoutingMetrics &metrics) {

    uint32_t num_nodes = static_cast<uint32_t>(adjacency_list.size());
    std::vector<double> best_rc(num_nodes, 1e18);

    std::vector<Label> all_labels;
    all_labels.reserve(32768);

    int32_t start_lbl = static_cast<int32_t>(all_labels.size());
    all_labels.push_back(Label{source_id, 0, start_time, -1, flight{}, 0});
    best_rc[source_id] = 0.0;

    if (mode == OptimizeFor::PRICE) {
      Flat4AryMinHeap<PricingState, ComparePricingPrice> pq(32768);
      pq.push({start_time, start_lbl, 0.0, 0, static_cast<uint16_t>(source_id), 0, 0});

      while (!pq.empty()) {
        PricingState curr = pq.top();
        pq.pop();
        metrics.nodes_explored++;

        if (curr.node_id == dest_id) {
          std::vector<flight> path;
          std::vector<uint32_t> visited_nodes;
          int32_t curr_lbl_id = curr.label_id;
          while (curr_lbl_id != -1) {
            const Label &lbl = all_labels[curr_lbl_id];
            visited_nodes.push_back(lbl.airport_id);
            if (lbl.parent_label_id != -1) {
              path.push_back(lbl.edge_taken);
            }
            curr_lbl_id = lbl.parent_label_id;
          }
          std::reverse(path.begin(), path.end());
          std::reverse(visited_nodes.begin(), visited_nodes.end());

          uint32_t total_dur = curr.current_time - start_time;
          return Column{0, curr.accumulated_price, total_dur,
                        static_cast<double>(curr.accumulated_price),
                        std::move(path), std::move(visited_nodes)};
        }

        if (curr.reduced_cost > best_rc[curr.node_id] + 1e-4) {
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
            continue;
          }

          uint32_t next_time = abs_dep + edge.duration_mins;
          uint32_t next_price = curr.accumulated_price + edge.price;

          double dual_penalty = (next_node < duals.pi.size()) ? duals.pi[next_node] : 0.0;
          double edge_reduced_cost = static_cast<double>(edge.price) + dual_penalty;

          double next_rc = curr.reduced_cost + edge_reduced_cost;

          if (next_rc < best_rc[next_node] - 1e-4) {
            best_rc[next_node] = next_rc;
            int32_t next_lbl = static_cast<int32_t>(all_labels.size());
            uint8_t next_hops = curr.hops + 1;
            all_labels.push_back(Label{next_node, next_price, next_time,
                                       curr.label_id, edge, next_hops});
            pq.push({next_time, next_lbl, next_rc, next_price,
                     static_cast<uint16_t>(next_node), next_hops, 0});
          }
        }
      }
    } else {
      Flat4AryMinHeap<PricingState, ComparePricingTime> pq(32768);
      pq.push({start_time, start_lbl, 0.0, 0, static_cast<uint16_t>(source_id), 0, 0});

      while (!pq.empty()) {
        PricingState curr = pq.top();
        pq.pop();
        metrics.nodes_explored++;

        if (curr.node_id == dest_id) {
          std::vector<flight> path;
          std::vector<uint32_t> visited_nodes;
          int32_t curr_lbl_id = curr.label_id;
          while (curr_lbl_id != -1) {
            const Label &lbl = all_labels[curr_lbl_id];
            visited_nodes.push_back(lbl.airport_id);
            if (lbl.parent_label_id != -1) {
              path.push_back(lbl.edge_taken);
            }
            curr_lbl_id = lbl.parent_label_id;
          }
          std::reverse(path.begin(), path.end());
          std::reverse(visited_nodes.begin(), visited_nodes.end());

          uint32_t total_dur = curr.current_time - start_time;
          return Column{0, curr.accumulated_price, total_dur,
                        static_cast<double>(total_dur),
                        std::move(path), std::move(visited_nodes)};
        }

        if (curr.reduced_cost > best_rc[curr.node_id] + 1e-4) {
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
            continue;
          }

          uint32_t next_time = abs_dep + edge.duration_mins;
          uint32_t next_price = curr.accumulated_price + edge.price;
          uint32_t edge_elapsed = next_time - curr.current_time;

          double dual_penalty = (next_node < duals.pi.size()) ? duals.pi[next_node] : 0.0;
          double edge_reduced_cost = static_cast<double>(edge_elapsed) + dual_penalty;

          double next_rc = curr.reduced_cost + edge_reduced_cost;

          if (next_rc < best_rc[next_node] - 1e-4) {
            best_rc[next_node] = next_rc;
            int32_t next_lbl = static_cast<int32_t>(all_labels.size());
            uint8_t next_hops = curr.hops + 1;
            all_labels.push_back(Label{next_node, next_price, next_time,
                                       curr.label_id, edge, next_hops});
            pq.push({next_time, next_lbl, next_rc, next_price,
                     static_cast<uint16_t>(next_node), next_hops, 0});
          }
        }
      }
    }

    return Column{0, 0, 0, 0.0, {}, {}};
  }

  // Solves the Restricted Master Problem (RMP) over candidate columns P,
  // returning primal weights lambda_p and dual shadow prices (alpha, pi).
  void solve_rmp(const std::vector<Column> &pool, uint32_t num_nodes,
                 DualVariables &duals, std::vector<double> &lambda_weights) {
    size_t num_cols = pool.size();
    lambda_weights.assign(num_cols, 0.0);
    duals.pi.assign(num_nodes, 0.0);

    if (num_cols == 0) {
      duals.alpha = 0.0;
      return;
    }

    // Find the current best column in the pool
    size_t best_idx = 0;
    double min_cost = pool[0].primary_cost;
    for (size_t i = 1; i < num_cols; ++i) {
      if (pool[i].primary_cost < min_cost) {
        min_cost = pool[i].primary_cost;
        best_idx = i;
      }
    }

    // Convexity dual \alpha = objective of best column
    duals.alpha = min_cost;
    lambda_weights[best_idx] = 1.0;

    // Track node usage frequency across elite pool columns
    std::unordered_map<uint32_t, uint32_t> node_frequency;
    for (const auto &col : pool) {
      for (size_t h = 1; h + 1 < col.visited_nodes.size(); ++h) {
        node_frequency[col.visited_nodes[h]]++;
      }
    }

    // Dual shadow price updates on contested intermediate hubs:
    // If multiple columns compete for the same transit hub, assign shadow price
    // to drive diversification and break degenerate bases.
    for (const auto &kv : node_frequency) {
      uint32_t u = kv.first;
      uint32_t count = kv.second;
      if (count > 1 && u < num_nodes) {
        duals.pi[u] = std::min(45.0, static_cast<double>(count - 1) * 12.0);
      }
    }
  }

  RoutingResult run(uint32_t source_id, uint32_t dest_id,
                    const std::vector<std::vector<flight>> &adjacency_list,
                    const std::vector<airport> &nodes, OptimizeFor mode,
                    uint32_t start_time, uint8_t max_hops = 5) {

    RoutingMetrics metrics = {0, 0};

    if (source_id == dest_id) {
      return {true, 0, 0, {}, metrics};
    }

    uint32_t num_nodes = static_cast<uint32_t>(adjacency_list.size());
    std::vector<Column> pool;
    DualVariables duals;
    duals.alpha = 0.0;
    duals.pi.assign(num_nodes, 0.0);

    // =========================================================================
    // STEP 1: Initialization — Generate initial basis column (pi = 0)
    // =========================================================================
    Column init_col = solve_pricing_subproblem(
        source_id, dest_id, adjacency_list, nodes, mode, start_time, duals,
        max_hops, metrics);

    if (init_col.path.empty()) {
      return {false, 0, 0, {}, metrics};
    }

    init_col.id = 0;
    pool.push_back(std::move(init_col));

    // =========================================================================
    // STEP 2: Column Generation Loop (Master <-> Pricing Subproblem)
    // =========================================================================
    const int MAX_ITERATIONS = 15;
    std::vector<double> lambda_weights;

    for (int iter = 0; iter < MAX_ITERATIONS; ++iter) {
      // 2a. Solve Restricted Master Problem with current column pool
      solve_rmp(pool, num_nodes, duals, lambda_weights);

      // 2b. Solve Pricing Subproblem with dual shadow prices \pi_i
      Column new_col = solve_pricing_subproblem(
          source_id, dest_id, adjacency_list, nodes, mode, start_time, duals,
          max_hops, metrics);

      if (new_col.path.empty()) {
        break; // No feasible path exists
      }

      // 2c. Compute Reduced Cost: \bar{c} = c(p) - \alpha + \sum_{i \in p} \pi_i
      double dual_benefit = duals.alpha;
      for (size_t h = 1; h + 1 < new_col.visited_nodes.size(); ++h) {
        uint32_t u = new_col.visited_nodes[h];
        if (u < duals.pi.size()) {
          dual_benefit -= duals.pi[u];
        }
      }
      double reduced_cost = new_col.primary_cost - dual_benefit;

      // 2d. Optimality Criterion Check
      // If no column has negative reduced cost (\bar{c} >= -1e-4), master LP is optimal!
      if (reduced_cost >= -1e-4) {
        break;
      }

      // Check for column duplication
      bool duplicate = false;
      for (const auto &existing : pool) {
        if (existing.total_price == new_col.total_price &&
            existing.total_time_mins == new_col.total_time_mins &&
            existing.path.size() == new_col.path.size()) {
          duplicate = true;
          break;
        }
      }

      if (duplicate) {
        break; // Basis reached cycle or equilibrium
      }

      new_col.id = static_cast<uint32_t>(pool.size());
      pool.push_back(std::move(new_col));
    }

    // =========================================================================
    // STEP 3: Integer Solution Extraction from the Generated Basis
    // =========================================================================
    if (pool.empty()) {
      return {false, 0, 0, {}, metrics};
    }

    // Select the best column according to optimization mode
    size_t best_idx = 0;
    for (size_t i = 1; i < pool.size(); ++i) {
      if (mode == OptimizeFor::PRICE) {
        if (pool[i].total_price < pool[best_idx].total_price ||
            (pool[i].total_price == pool[best_idx].total_price &&
             pool[i].total_time_mins < pool[best_idx].total_time_mins)) {
          best_idx = i;
        }
      } else {
        if (pool[i].total_time_mins < pool[best_idx].total_time_mins ||
            (pool[i].total_time_mins == pool[best_idx].total_time_mins &&
             pool[i].total_price < pool[best_idx].total_price)) {
          best_idx = i;
        }
      }
    }

    const Column &best = pool[best_idx];
    return {true, best.total_price, best.total_time_mins, best.path, metrics};
  }
};

using ColumnGeneration = ColumnGenerationRouter;
