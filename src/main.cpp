#include "Algorithms/Dijkstra.hpp"
#include "Algorithms/AStar.hpp"
#include "Algorithms/TwoPhaseRouter.hpp"
#include "Algorithms/LabelCorrecting.hpp"
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
              << std::right << std::setw(5) << (bench.compute_time_us / 1000) << "."
              << std::setw(2) << std::setfill('0') << ((bench.compute_time_us % 1000) / 10)
              << std::setfill(' ') << " ms";

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

void print_route_path(const RunStats &run, const DataLoader &loader, uint32_t source_id) {
  std::cout << "\n=== " << run.algo_name << " [" << run.mode_label << "] ROUTE ===\n";
  if (!run.result.success) {
    std::cout << "Status: No valid route found.\n";
    return;
  }

  std::cout << "Total Price: $" << run.result.total_price
            << "  |  Total Time: "
            << run.result.total_time_mins / 60 << "h "
            << run.result.total_time_mins % 60 << "m\n";
  std::cout << "------------------------------------------------------\n";

  uint32_t prev_apt = source_id;

  for (size_t i = 0; i < run.result.path.size(); ++i) {
    const flight &f = run.result.path[i];
    const flightDetails &f_ui = loader.ui_flights[f.flight_details_id];
    const airportDetails &dest_apt = loader.ui_nodes[f.destination_id];

    bool is_dom = (loader.nodes[prev_apt].country_id == loader.nodes[f.destination_id].country_id);
    prev_apt = f.destination_id;

    std::string dep_time = std::to_string(f.departure_time / 60) + ":" +
                           (f.departure_time % 60 < 10 ? "0" : "") +
                           std::to_string(f.departure_time % 60);
    std::string arr_time = std::to_string(f.arrival_time / 60) + ":" +
                           (f.arrival_time % 60 < 10 ? "0" : "") +
                           std::to_string(f.arrival_time % 60);
    std::string day_shift =
        (f.arrival_time < f.departure_time) ? " (+1)" : "";

    std::cout << "Flight " << f_ui.airline_code << " " << f_ui.flight_number
              << " -> " << dest_apt.iata_code << "  |  Dep: " << std::setw(5)
              << dep_time << "  |  Arr: " << std::setw(5) << arr_time
              << day_shift << "  |  $" << f.price
              << (is_dom ? " (incl. $40 dom tax)" : " (incl. $160 intl tax)") << "\n";
  }
}

int main() {
  DataLoader loader;
  std::cout << "Loading AeroGraph Data...\n";
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
  uint32_t absolute_start_time = start_day * 1440; // 00:00 (Midnight)

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
    RoutingResult res = astar_router.run(
        source_id, dest_id, loader.adjacency_list, loader.nodes,
        OptimizeFor::PRICE, absolute_start_time);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"A*", "cheapest", duration.count(), res});
  }
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res = astar_router.run(
        source_id, dest_id, loader.adjacency_list, loader.nodes,
        OptimizeFor::TIME, absolute_start_time);
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

  // 4. Two-Phase Router (Topology-First, Schedule-Second)
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

  // --- OUTPUT ALL PERFORMANCES FIRST ON TOP ---
  print_performance_summary(benchmarks);

  // --- OUTPUT DETAILED PATHS BELOW ---
  for (const auto &run : benchmarks) {
    print_route_path(run, loader, source_id);
  }

  return 0;
}