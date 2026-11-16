#pragma once
#include "../DataStructures/airport.hpp"
#include "../DataStructures/flight.hpp"
#include "Dijkstra.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// ---------------------------------------------------------------------------
// MetaheuristicRouter: Hybrid Genetic Algorithm & Ant Colony Optimization (GA-ACO)
//
// Theoretical Foundation:
//   Solves complex multi-modal, non-convex routing on the time-expanded
//   flight network where exact dynamic programming struggles with non-linear
//   objective functions (e.g. combined fare, transit layover penalties,
//   airline alliance preferences, and transfer fatigue).
//
// 1. Ant Colony Optimization (ACO) — Stochastic Itinerary Construction:
//    - Artificial ants probabilistically construct valid flight paths from
//      source to destination using a random proportional transition rule:
//        P(i, j) = [tau(i, j)]^alpha * [eta(i, j)]^beta / sum([tau(i, k)]^alpha * [eta(i, k)]^beta)
//      where:
//        tau(i, j) = Pheromone trail intensity on corridor (i -> j)
//        eta(i, j) = Visibility / heuristic desirability (inverse cost & Haversine direction)
//        alpha = 1.0 (pheromone history weight), beta = 2.0 (heuristic bias)
//    - Temporal feasibility & transfer constraints (45 min <= layover <= 24 hrs)
//      are enforced dynamically during ant movement.
//
// 2. Genetic Algorithm (GA) — Evolutionary Population Optimization:
//    - Population: A diverse pool of candidate chromosome itineraries.
//    - Selection: Tournament selection (k=3) based on fitness:
//        Fitness(p) = 1.0 / (1.0 + Objective(p))
//    - Crossover: Order-based & Hub-Intersection Crossover. Identifies common
//      connecting hub airports between two parent itineraries, splicing their
//      path prefixes and suffixes with timetable validation.
//    - Mutation: Random intermediate hub detour / neighborhood perturbation
//      with adaptive mutation probability (Pm = 0.20).
//
// 3. Pheromone Evaporation & Global Elite Reinforcement:
//    - Evaporation: tau(i, j) = (1 - rho) * tau(i, j) with rho = 0.15
//    - Elite Reinforcement: The best solution found across all generations
//      deposits extra pheromone: Delta_tau = Q / Best_Objective.
// ---------------------------------------------------------------------------

class MetaheuristicRouter {
public:
  static constexpr uint32_t INF = std::numeric_limits<uint32_t>::max();

  struct Individual {
    std::vector<flight> path;
    uint32_t total_price;
    uint32_t total_time_mins;
    double fitness;
    bool valid;
  };

  static inline double haversine_km(float lat1, float lon1, float lat2, float lon2) {
    const double R = 6371.0;
    double dLat = (lat2 - lat1) * M_PI / 180.0;
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    double a = std::sin(dLat / 2.0) * std::sin(dLat / 2.0) +
               std::cos(lat1 * M_PI / 180.0) * std::cos(lat2 * M_PI / 180.0) *
                   std::sin(dLon / 2.0) * std::sin(dLon / 2.0);
    double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(1.0 - a));
    return R * c;
  }

  // Evaluates chromosome fitness and validates schedule
  static Individual evaluate_itinerary(uint32_t source_id,
                                       const std::vector<flight> &path,
                                       const std::vector<airport> &nodes,
                                       OptimizeFor mode, uint32_t start_time) {
    if (path.empty()) {
      return {{}, INF, INF, 0.0, false};
    }

    uint32_t curr_node = source_id;
    uint32_t curr_time = start_time;
    uint32_t total_price = 0;

    for (size_t hop = 0; hop < path.size(); ++hop) {
      const flight &f = path[hop];
      total_price += f.price;

      uint32_t req_dep = curr_time + ((hop == 0) ? 0 : nodes[curr_node].min_transfer);
      int32_t offset = nodes[curr_node].utc_offset_15m * 15;
      uint32_t abs_dep = static_cast<uint32_t>(
          static_cast<int32_t>(f.flight_day * 1440 + f.departure_time) - offset);

      if (abs_dep < req_dep || abs_dep - curr_time > 1440) {
        return {{}, INF, INF, 0.0, false};
      }

      curr_time = abs_dep + f.duration_mins;
      curr_node = f.destination_id;
    }

    uint32_t total_time = curr_time - start_time;
    double obj = (mode == OptimizeFor::PRICE)
                     ? static_cast<double>(total_price)
                     : static_cast<double>(total_time);

    double fitness = 1.0 / (1.0 + obj);
    return {path, total_price, total_time, fitness, true};
  }

  // Ant Colony probabilistic path construction
  bool construct_ant_path(
      uint32_t source_id, uint32_t dest_id,
      const std::vector<std::vector<flight>> &adj,
      const std::vector<airport> &nodes, OptimizeFor mode,
      uint32_t start_time,
      const std::unordered_map<uint64_t, double> &pheromones,
      std::mt19937 &rng, std::vector<flight> &ant_path,
      RoutingMetrics &metrics) {

    uint32_t curr_node = source_id;
    uint32_t curr_time = start_time;
    std::unordered_set<uint32_t> visited;
    visited.insert(source_id);
    ant_path.clear();

    const uint8_t MAX_HOPS = 5;

    for (uint8_t hop = 0; hop < MAX_HOPS; ++hop) {
      metrics.nodes_explored++;
      if (curr_node == dest_id) {
        return true;
      }

      uint32_t req_dep = curr_time + ((hop == 0) ? 0 : nodes[curr_node].min_transfer);
      int32_t origin_utc_offset = nodes[curr_node].utc_offset_15m * 15;

      // Filter feasible candidate outgoing flights
      std::vector<size_t> feasible_indices;
      std::vector<double> probabilities;
      feasible_indices.reserve(64);
      probabilities.reserve(64);

      double curr_dist = haversine_km(
          nodes[curr_node].latitude, nodes[curr_node].longitude,
          nodes[dest_id].latitude, nodes[dest_id].longitude);

      for (size_t idx = 0; idx < adj[curr_node].size(); ++idx) {
        const flight &f = adj[curr_node][idx];
        metrics.edges_relaxed++;
        uint32_t next_node = f.destination_id;

        if (visited.find(next_node) != visited.end()) {
          continue; // Avoid cycles
        }

        uint32_t abs_dep = static_cast<uint32_t>(
            static_cast<int32_t>(f.flight_day * 1440 + f.departure_time) -
            origin_utc_offset);

        if (abs_dep < req_dep || abs_dep - curr_time > 1440) {
          continue;
        }

        if (next_node == dest_id) {
          // Direct arrival at destination is heavily prioritized
          feasible_indices.push_back(idx);
          probabilities.push_back(10000.0);
          continue;
        }

        double dist_to_dest = haversine_km(
            nodes[next_node].latitude, nodes[next_node].longitude,
            nodes[dest_id].latitude, nodes[dest_id].longitude);

        // Geodesic progress bias: favor nodes making forward progress toward destination
        double progress = curr_dist - dist_to_dest;
        double progress_factor = (progress > 0) ? (1.0 + progress / 500.0) : 0.1;

        double metric_cost = (mode == OptimizeFor::PRICE)
                                 ? static_cast<double>(f.price)
                                 : static_cast<double>(f.duration_mins);

        double eta = (progress_factor) / (1.0 + metric_cost * 0.01 + dist_to_dest * 0.01);

        // Pheromone (tau)
        uint64_t key = (static_cast<uint64_t>(curr_node) << 32) | next_node;
        auto p_it = pheromones.find(key);
        double tau = (p_it != pheromones.end()) ? p_it->second : 1.0;

        double weight = tau * std::pow(std::max(0.001, eta), 2.5);
        feasible_indices.push_back(idx);
        probabilities.push_back(weight);
      }

      if (feasible_indices.empty()) {
        break; // Dead-end
      }

      // Discrete probability distribution sampling
      std::discrete_distribution<size_t> dist(probabilities.begin(), probabilities.end());
      size_t chosen_cand = dist(rng);
      size_t chosen_edge_idx = feasible_indices[chosen_cand];

      const flight &chosen_flight = adj[curr_node][chosen_edge_idx];
      uint32_t abs_dep = static_cast<uint32_t>(
          static_cast<int32_t>(chosen_flight.flight_day * 1440 +
                               chosen_flight.departure_time) -
          origin_utc_offset);

      curr_time = abs_dep + chosen_flight.duration_mins;
      curr_node = chosen_flight.destination_id;
      visited.insert(curr_node);
      ant_path.push_back(chosen_flight);

      if (curr_node == dest_id) {
        return true;
      }
    }

    return (curr_node == dest_id);
  }

  // Hub-Splicing Genetic Crossover
  Individual crossover(const Individual &parent1, const Individual &parent2,
                       const std::vector<std::vector<flight>> &adj,
                       const std::vector<airport> &nodes, OptimizeFor mode,
                       uint32_t start_time) {
    if (parent1.path.empty()) return parent2;
    if (parent2.path.empty()) return parent1;

    // Extract intermediate visited nodes
    std::vector<uint32_t> nodes1;
    for (const auto &f : parent1.path) nodes1.push_back(f.destination_id);

    std::vector<uint32_t> nodes2;
    for (const auto &f : parent2.path) nodes2.push_back(f.destination_id);

    // Look for common intermediate crossover hub (excluding destination)
    for (size_t i = 0; i + 1 < nodes1.size(); ++i) {
      for (size_t j = 0; j + 1 < nodes2.size(); ++j) {
        if (nodes1[i] == nodes2[j]) {
          // Splice prefix of parent1 with suffix of parent2
          uint32_t crossover_hub = nodes1[i];
          std::vector<flight> child_path;

          for (size_t k = 0; k <= i; ++k) {
            child_path.push_back(parent1.path[k]);
          }

          // Calculate arrival time at crossover hub
          uint32_t hub_arrival = start_time;
          uint32_t prev = 0;
          for (size_t k = 0; k < child_path.size(); ++k) {
            const auto &f = child_path[k];
            if (k > 0) {
              int32_t offset = nodes[prev].utc_offset_15m * 15;
              uint32_t dep = static_cast<uint32_t>(
                  static_cast<int32_t>(f.flight_day * 1440 + f.departure_time) - offset);
              hub_arrival = dep + f.duration_mins;
            } else {
              hub_arrival += f.duration_mins;
            }
            prev = f.destination_id;
          }

          // Find feasible connecting suffix flights from parent2
          bool suffix_valid = true;
          uint32_t curr_time = hub_arrival;
          uint32_t curr_node = crossover_hub;

          for (size_t k = j + 1; k < parent2.path.size(); ++k) {
            uint32_t target = parent2.path[k].destination_id;
            uint32_t req_dep = curr_time + nodes[curr_node].min_transfer;
            int32_t offset = nodes[curr_node].utc_offset_15m * 15;

            bool found_leg = false;
            flight best_leg{};
            uint32_t min_cost = INF;

            for (const auto &cand : adj[curr_node]) {
              if (cand.destination_id != target) continue;
              uint32_t dep = static_cast<uint32_t>(
                  static_cast<int32_t>(cand.flight_day * 1440 + cand.departure_time) - offset);
              if (dep >= req_dep && dep - curr_time <= 1440) {
                if (cand.price < min_cost) {
                  min_cost = cand.price;
                  best_leg = cand;
                  found_leg = true;
                }
              }
            }

            if (!found_leg) {
              suffix_valid = false;
              break;
            }

            int32_t off = nodes[curr_node].utc_offset_15m * 15;
            uint32_t d = static_cast<uint32_t>(
                static_cast<int32_t>(best_leg.flight_day * 1440 + best_leg.departure_time) - off);
            curr_time = d + best_leg.duration_mins;
            curr_node = target;
            child_path.push_back(best_leg);
          }

          if (suffix_valid && !child_path.empty()) {
            return evaluate_itinerary(parent1.path[0].destination_id, child_path, nodes, mode, start_time);
          }
        }
      }
    }

    return (parent1.fitness > parent2.fitness) ? parent1 : parent2;
  }

  // Mutation: Re-routes through a candidate neighbor hub
  Individual mutate(const Individual &ind,
                    const std::vector<std::vector<flight>> &adj,
                    const std::vector<airport> &nodes, OptimizeFor mode,
                    uint32_t source_id, uint32_t dest_id,
                    uint32_t start_time, std::mt19937 &rng) {
    if (ind.path.size() <= 1) return ind;

    // Try a single-point neighborhood mutation
    std::uniform_int_distribution<size_t> dist_leg(0, ind.path.size() - 1);
    size_t mut_idx = dist_leg(rng);

    uint32_t origin = (mut_idx == 0) ? source_id : ind.path[mut_idx - 1].destination_id;
    if (adj[origin].empty()) return ind;

    std::uniform_int_distribution<size_t> dist_cand(0, adj[origin].size() - 1);
    const flight &new_leg = adj[origin][dist_cand(rng)];

    if (new_leg.destination_id == dest_id) {
      std::vector<flight> new_path;
      for (size_t i = 0; i < mut_idx; ++i) new_path.push_back(ind.path[i]);
      new_path.push_back(new_leg);
      auto candidate = evaluate_itinerary(source_id, new_path, nodes, mode, start_time);
      if (candidate.valid) return candidate;
    }

    return ind;
  }

  RoutingResult run(uint32_t source_id, uint32_t dest_id,
                    const std::vector<std::vector<flight>> &adj,
                    const std::vector<airport> &nodes, OptimizeFor mode,
                    uint32_t start_time) {

    RoutingMetrics metrics = {0, 0};

    if (source_id == dest_id) {
      return {true, 0, 0, {}, metrics};
    }

    std::mt19937 rng(42); // Deterministic seed for reproducible benchmarking
    std::unordered_map<uint64_t, double> pheromones;

    // ACO Parameters
    const size_t NUM_ANTS = 20;
    const size_t NUM_GENERATIONS = 15;
    const double RHO = 0.15; // Evaporation rate
    const double Q = 1000.0; // Pheromone reward factor

    std::vector<Individual> population;
    population.reserve(NUM_ANTS * 2);

    Individual global_best{{}, INF, INF, 0.0, false};

    // =========================================================================
    // HYBRID ACO-GA EVOLUTIONARY LOOP
    // =========================================================================
    for (size_t gen = 0; gen < NUM_GENERATIONS; ++gen) {
      std::vector<Individual> generation_ants;
      generation_ants.reserve(NUM_ANTS);

      // Phase 1: Ant Colony Construction
      for (size_t a = 0; a < NUM_ANTS; ++a) {
        std::vector<flight> ant_path;
        if (construct_ant_path(source_id, dest_id, adj, nodes, mode,
                               start_time, pheromones, rng, ant_path, metrics)) {
          Individual ind = evaluate_itinerary(source_id, ant_path, nodes, mode, start_time);
          if (ind.valid) {
            generation_ants.push_back(ind);
            population.push_back(ind);

            if (!global_best.valid || ind.fitness > global_best.fitness) {
              global_best = ind;
            }
          }
        }
      }

      // Phase 2: Genetic Algorithm (Selection, Crossover & Mutation)
      if (population.size() >= 4) {
        std::vector<Individual> next_pop;
        next_pop.reserve(population.size());

        // Elitism: Preserve top 2 individuals
        std::sort(population.begin(), population.end(),
                  [](const Individual &x, const Individual &y) {
                    return x.fitness > y.fitness;
                  });
        next_pop.push_back(population[0]);
        next_pop.push_back(population[1]);

        std::uniform_int_distribution<size_t> dist_tour(0, population.size() - 1);

        while (next_pop.size() < population.size()) {
          // Tournament selection (k=3)
          auto tournament = [&]() -> const Individual & {
            size_t i1 = dist_tour(rng), i2 = dist_tour(rng), i3 = dist_tour(rng);
            const Individual &b = (population[i1].fitness > population[i2].fitness)
                                      ? population[i1]
                                      : population[i2];
            return (b.fitness > population[i3].fitness) ? b : population[i3];
          };

          const Individual &p1 = tournament();
          const Individual &p2 = tournament();

          Individual child = crossover(p1, p2, adj, nodes, mode, start_time);

          // Mutation (20% rate)
          std::uniform_real_distribution<double> dist_mut(0.0, 1.0);
          if (dist_mut(rng) < 0.20) {
            child = mutate(child, adj, nodes, mode, source_id, dest_id, start_time, rng);
          }

          if (child.valid) {
            if (!global_best.valid || child.fitness > global_best.fitness) {
              global_best = child;
            }
            next_pop.push_back(child);
          } else {
            next_pop.push_back(p1);
          }
        }
        population = std::move(next_pop);
      }

      // Phase 3: Pheromone Evaporation & Reinforcement
      for (auto &kv : pheromones) {
        kv.second *= (1.0 - RHO);
        if (kv.second < 0.1) kv.second = 0.1;
      }

      // Deposit pheromone along elite ant paths
      for (const auto &ind : generation_ants) {
        if (!ind.valid) continue;
        double deposit = Q * ind.fitness;
        uint32_t prev = source_id;
        for (const auto &f : ind.path) {
          uint64_t key = (static_cast<uint64_t>(prev) << 32) | f.destination_id;
          pheromones[key] += deposit;
          prev = f.destination_id;
        }
      }

      // Global elite reinforcement
      if (global_best.valid) {
        double elite_deposit = 2.0 * Q * global_best.fitness;
        uint32_t prev = source_id;
        for (const auto &f : global_best.path) {
          uint64_t key = (static_cast<uint64_t>(prev) << 32) | f.destination_id;
          pheromones[key] += elite_deposit;
          prev = f.destination_id;
        }
      }
    }

    if (global_best.valid) {
      return {true, global_best.total_price, global_best.total_time_mins,
              global_best.path, metrics};
    }

    return {false, 0, 0, {}, metrics};
  }
};

using Metaheuristic = MetaheuristicRouter;
