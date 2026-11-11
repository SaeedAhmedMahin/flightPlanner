#include "Algorithms/AStar.hpp"
#include "Algorithms/BFS.hpp"
#include "Algorithms/Dijkstra.hpp"
#include "Algorithms/LabelCorrecting.hpp"
#include "Algorithms/TwoPhaseRouter.hpp"
#include "Algorithms/TwoPhaseHubRouter.hpp"
#include "Algorithms/Yen.hpp"
#include "DataLoader.hpp"
#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>

struct RunStats {
  std::string algo_name;
  std::string mode_label;
  long compute_time_us;
  RoutingResult result;
};

void print_performance_summary(const std::vector<RunStats> &benchmarks) {
  std::cout << "\n======================================================\n";
  std::cout << "                 ALGORITHM BENCHMARKS                 \n";
  std::cout << "======================================================\n";

  std::string current_algo = "";
  for (const auto &bench : benchmarks) {
    if (bench.algo_name != current_algo) {
      current_algo = bench.algo_name;
      std::cout << current_algo << ":\n";
    }

    std::cout << "   " << std::left << std::setw(10) << bench.mode_label << ": "
              << std::right << std::setw(5) << (bench.compute_time_us / 1000)
              << "." << std::setw(2) << std::setfill('0')
              << ((bench.compute_time_us % 1000) / 10) << std::setfill(' ')
              << " ms";

    if (bench.result.success) {
      std::cout << "  (nodes: " << std::setw(5)
                << bench.result.metrics.nodes_explored
                << ", relaxed: " << std::setw(8)
                << bench.result.metrics.edges_relaxed << ")";

      if (bench.mode_label == "cheapest") {
        std::cout << " -> $" << bench.result.total_price;
      } else {
        std::cout << " -> " << bench.result.total_time_mins / 60 << "h "
                  << bench.result.total_time_mins % 60 << "m";
      }
    } else {
      std::cout << "  [No Route Found]";
    }
    std::cout << "\n";
  }
  std::cout << "======================================================\n";
}

void print_route_path(const RunStats &run, const DataLoader &loader,
                      uint32_t source_id) {
  std::cout << "======================================================";
  std::cout << "\n=== " << run.algo_name << " [" << run.mode_label
            << "] ROUTE ===\n";
  if (!run.result.success) {
    std::cout << "Status: No valid route found.\n";
    return;
  }

  std::cout << "Total Price: $" << run.result.total_price
            << "  |  Total Time: " << run.result.total_time_mins / 60 << "h "
            << run.result.total_time_mins % 60 << "m\n";
  std::cout << "======================================================\n";

  uint32_t current_airport_id = source_id;

  for (size_t i = 0; i < run.result.path.size(); ++i) {
    const flight &f = run.result.path[i];
    const flightDetails &f_ui = loader.ui_flights[f.flight_details_id];
    std::string origin_iata = loader.ui_nodes[current_airport_id].iata_code;
    std::string dest_iata = loader.ui_nodes[f.destination_id].iata_code;
    std::string airline_full_name = loader.get_airline_name(f_ui.airline_code);

    // Flight duration from the precomputed wall-clock duration (timezone-aware)
    uint32_t flight_dur_min = f.duration_mins;
    uint32_t dur_h = flight_dur_min / 60;
    uint32_t dur_m = flight_dur_min % 60;

    // Local departure/arrival times for display (these are correctly in local time)
    uint32_t dep_min = f.departure_time;
    uint32_t arr_min = f.arrival_time;

    std::string dep_str =
        (dep_min / 60 < 10 ? "0" : "") + std::to_string(dep_min / 60) + ":" +
        (dep_min % 60 < 10 ? "0" : "") + std::to_string(dep_min % 60);
    std::string arr_str =
        (arr_min / 60 < 10 ? "0" : "") + std::to_string(arr_min / 60) + ":" +
        (arr_min % 60 < 10 ? "0" : "") + std::to_string(arr_min % 60);
    std::string next_day = (arr_min < dep_min) ? " (+1 day)" : "";

    std::cout << airline_full_name << " - " << f_ui.airline_code << " "
              << f_ui.flight_number << "\n";
    std::cout << origin_iata << " -> " << dest_iata << "\n";
    std::cout << dep_str << " to " << arr_str << next_day << " (" << dur_h
              << " hours";
    if (dur_m > 0)
      std::cout << " " << dur_m << " mins";
    std::cout << ")\n";

    // If there is a next flight in the itinerary, display the transit banner
    if (i + 1 < run.result.path.size()) {
      const flight &next_f = run.result.path[i + 1];
      int32_t origin_utc_offset = loader.nodes[current_airport_id].utc_offset_15m * 15;
      uint32_t dep_utc = static_cast<uint32_t>(
          static_cast<int32_t>(f.flight_day * 1440 + f.departure_time) - origin_utc_offset);
      uint32_t arr_utc = dep_utc + f.duration_mins;

      int32_t dest_utc_offset = loader.nodes[f.destination_id].utc_offset_15m * 15;
      uint32_t next_dep_utc = static_cast<uint32_t>(
          static_cast<int32_t>(next_f.flight_day * 1440 + next_f.departure_time) - dest_utc_offset);

      uint32_t transit_min = (next_dep_utc >= arr_utc) ? (next_dep_utc - arr_utc) : 0;
      uint32_t lay_h = transit_min / 60;
      uint32_t lay_m = transit_min % 60;

      std::cout << "\n(";
      if (lay_h > 0) {
        std::cout << lay_h << " hour" << (lay_h != 1 ? "s" : "");
        if (lay_m > 0) std::cout << " ";
      }
      if (lay_m > 0 || lay_h == 0) {
        std::cout << lay_m << " mins";
      }
      std::cout << " TRANSIT @ " << dest_iata << ")\n\n";
    }

    current_airport_id = f.destination_id;
  }
}

int main() {
  DataLoader loader;
  std::cout << "Loading Data...\n";
  loader.load_data("data/processed/airports_clean.csv",
                   "data/processed/routes_clean.csv");

  std::string source_iata = "DAC"; // Dhaka
  std::string dest_iata = "JFK";   // New York

  std::cout << "Enter source airport (IATA): ";
  std::cin >> source_iata;
  std::cout << "Enter destination airport (IATA): ";
  std::cin >> dest_iata;

  std::string target_date;
  std::cout << "Enter target date (YYYY-MM-DD): ";
  std::cin >> target_date;

  if (loader.iata_to_id.find(source_iata) == loader.iata_to_id.end() ||
      loader.iata_to_id.find(dest_iata) == loader.iata_to_id.end()) {
    std::cerr << "Error: Unknown airport code(s).\n";
    return 1;
  }

  uint32_t source_id = loader.iata_to_id[source_iata];
  uint32_t dest_id = loader.iata_to_id[dest_iata];

  std::cout << "\nRouting: " << source_iata << " -> " << dest_iata << " on "
            << target_date << "\n";

  uint32_t start_day = DataLoader::parse_date_to_days(target_date);
  int32_t source_utc_offset = loader.nodes[source_id].utc_offset_15m * 15;
  uint32_t absolute_start_time = static_cast<uint32_t>(
      static_cast<int32_t>(start_day * 1440) - source_utc_offset); // 00:00 local in UTC minutes

  std::vector<RunStats> benchmarks;

  // 1. Classical Dijkstra
  Dijkstra dijkstra_router;
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res = dijkstra_router.run(
        source_id, dest_id, loader.adjacency_list, loader.nodes,
        OptimizeFor::PRICE, absolute_start_time);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"Dijkstra", "cheapest", duration.count(), res});
  }
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res = dijkstra_router.run(
        source_id, dest_id, loader.adjacency_list, loader.nodes,
        OptimizeFor::TIME, absolute_start_time);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"Dijkstra", "fastest", duration.count(), res});
  }

  // 2. A* Search (Heuristic-Guided Pareto Search)
  AStar astar_router;
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res =
        astar_router.run(source_id, dest_id, loader.adjacency_list,
                         loader.nodes, OptimizeFor::PRICE, absolute_start_time);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"A*", "cheapest", duration.count(), res});
  }
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res =
        astar_router.run(source_id, dest_id, loader.adjacency_list,
                         loader.nodes, OptimizeFor::TIME, absolute_start_time);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"A*", "fastest", duration.count(), res});
  }

  // 3. Label-Correcting Algorithm (SPPRC Bellman-Ford variant)
  LabelCorrectingRouter label_correcting_router;
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res = label_correcting_router.run(
        source_id, dest_id, loader.adjacency_list, loader.nodes,
        OptimizeFor::PRICE, absolute_start_time);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"Label-Corr", "cheapest", duration.count(), res});
  }
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res = label_correcting_router.run(
        source_id, dest_id, loader.adjacency_list, loader.nodes,
        OptimizeFor::TIME, absolute_start_time);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"Label-Corr", "fastest", duration.count(), res});
  }

  // 4. Breadth-First Search (Fewest Hops Baseline)
  BFSRouter bfs_router;
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res =
        bfs_router.run(source_id, dest_id, loader.adjacency_list, loader.nodes,
                       OptimizeFor::PRICE, absolute_start_time);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"BFS (Hops)", "cheapest", duration.count(), res});
  }
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res =
        bfs_router.run(source_id, dest_id, loader.adjacency_list, loader.nodes,
                       OptimizeFor::TIME, absolute_start_time);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"BFS (Hops)", "fastest", duration.count(), res});
  }

  // 5. Two-Phase Router (Topology-First, Schedule-Second)
  TwoPhaseRouter two_phase_router;
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res = two_phase_router.run(
        source_id, dest_id, loader.adjacency_list, loader.static_route_graph,
        loader.static_route_min_price, loader.static_route_min_duration,
        loader.nodes, OptimizeFor::PRICE, absolute_start_time);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"Two-Phase", "cheapest", duration.count(), res});
  }
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res = two_phase_router.run(
        source_id, dest_id, loader.adjacency_list, loader.static_route_graph,
        loader.static_route_min_price, loader.static_route_min_duration,
        loader.nodes, OptimizeFor::TIME, absolute_start_time);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"Two-Phase", "fastest", duration.count(), res});
  }

  // 6. Two-Phase Hub Router (Hierarchical Hub-and-Spoke Aviation Corridors)
  TwoPhaseHubRouter two_phase_hub_router;
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res = two_phase_hub_router.run(
        source_id, dest_id, loader.adjacency_list, loader.static_route_graph,
        loader.static_route_min_price, loader.static_route_min_duration,
        loader.nodes, OptimizeFor::PRICE, absolute_start_time);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"Two-Phase Hub", "cheapest", duration.count(), res});
  }
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res = two_phase_hub_router.run(
        source_id, dest_id, loader.adjacency_list, loader.static_route_graph,
        loader.static_route_min_price, loader.static_route_min_duration,
        loader.nodes, OptimizeFor::TIME, absolute_start_time);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"Two-Phase Hub", "fastest", duration.count(), res});
  }

  // 7. Yen's Algorithm (K-Shortest Paths & Best Path Recommendation)
  YenRouter yen_router;
  YenRouter::YenKResult yen_k_cheapest;
  {
    auto start = std::chrono::high_resolution_clock::now();
    yen_k_cheapest =
        yen_router.run(source_id, dest_id, loader.adjacency_list, loader.nodes,
                       OptimizeFor::PRICE, absolute_start_time, 5);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    RoutingResult primary_yen = yen_k_cheapest.k_paths.empty()
                                    ? RoutingResult{false, 0, 0, {}, {}}
                                    : yen_k_cheapest.k_paths[0];
    benchmarks.push_back(
        {"Yen's K-SP", "k=5 cheap", duration.count(), primary_yen});
  }

  // --- OUTPUT ALL PERFORMANCES FIRST ON TOP ---
  print_performance_summary(benchmarks);

  // --- OUTPUT DETAILED PATHS BELOW ---
  for (const auto &run : benchmarks) {
    print_route_path(run, loader, source_id);
  }

  // --- OUTPUT YEN'S TOP-K ALTERNATIVE PATHS & "BEST FLIGHT" RECOMMENDATION ---
  if (!yen_k_cheapest.k_paths.empty()) {
    std::cout << "\n======================================================\n";
    std::cout << "        YEN'S TOP-" << yen_k_cheapest.k_paths.size()
              << " ALTERNATIVE ITINERARIES (PRICE)        \n";
    std::cout << "======================================================\n";
    for (size_t i = 0; i < yen_k_cheapest.k_paths.size(); ++i) {
      const auto &res = yen_k_cheapest.k_paths[i];
      std::cout << "Alternative #" << (i + 1) << ": $" << res.total_price
                << " | Duration: " << res.total_time_mins / 60 << "h "
                << res.total_time_mins % 60 << "m | Transits: "
                << (res.path.empty() ? 0 : res.path.size() - 1) << " | Route: ";
      std::cout << loader.ui_nodes[source_id].iata_code;
      for (size_t j = 0; j < res.path.size(); ++j) {
        std::cout << " -> "
                  << loader.ui_nodes[res.path[j].destination_id].iata_code;
      }
      std::cout << "\n";
    }

    std::cout << "\n======================================================\n";
    std::cout << "       BEST FLIGHTS \n";
    std::cout << "======================================================\n";
    const auto &best = yen_k_cheapest.best_recommendation;
    std::cout << "Recommended Choice: $" << best.result.total_price << " ("
              << best.result.total_time_mins / 60 << "h "
              << best.result.total_time_mins % 60 << "m, "
              << (best.result.path.empty() ? 0 : best.result.path.size() - 1)
              << " transits)\n";
    std::cout << "Reasoning: " << best.recommendation_reason << "\n";
    std::cout << "Generalized Economic Cost: $" << std::fixed
              << std::setprecision(2) << best.generalized_cost
              << " (Value of Time: $25.00/hr, Layover Penalty: $15/stop)\n";

    RunStats best_run = {"Best-Choice", "recommended", 0, best.result};
    print_route_path(best_run, loader, source_id);
  }

  return 0;
}