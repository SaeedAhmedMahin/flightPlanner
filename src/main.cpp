#include "Algorithms/Dijkstra.hpp"
#include "DataLoader.hpp"
#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>

void print_result(const RoutingResult &res, const DataLoader &loader,
                  const std::string &label, long compute_time_us) {
  std::cout << "\n=== " << label << " ROUTE ===\n";
  std::cout << "Compute Time:   " << compute_time_us << " microseconds\n";
  std::cout << "Nodes Explored: " << res.metrics.nodes_explored << "\n";
  std::cout << "Edges Relaxed:  " << res.metrics.edges_relaxed << "\n";

  if (!res.success) {
    std::cout << "Status: No valid route found.\n";
    return;
  }

  std::cout << "\nTotal Price: $" << res.total_price
            << "  |  Total Time: " << res.total_time_mins / 60 << "h "
            << res.total_time_mins % 60 << "m\n";
  std::cout << "------------------------------------------------------\n";

  for (const flight &f : res.path) {
    const flightDetails &f_ui = loader.ui_flights[f.flight_details_id];
    const airportDetails &dest_apt = loader.ui_nodes[f.destination_id];

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
              << day_shift << "  |  $" << f.price << "\n";
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

  uint32_t source_id = loader.iata_to_id[source_iata];
  uint32_t dest_id = loader.iata_to_id[dest_iata];

  std::cout << "\nRouting: " << source_iata << " -> " << dest_iata << " on " << target_date << "\n";
  Dijkstra router;
  
  uint32_t start_day = DataLoader::parse_date_to_days(target_date);
  uint32_t absolute_start_time = start_day * 1440; // 00:00 (Midnight) of target day

  // --- 1. CHEAPEST ROUTE ---
  auto start = std::chrono::high_resolution_clock::now();
  RoutingResult cheapest = router.run(source_id, dest_id, loader.adjacency_list,
                                      loader.nodes, OptimizeFor::PRICE, absolute_start_time);
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::high_resolution_clock::now() - start);

  print_result(cheapest, loader, "CHEAPEST", duration.count());

  // --- 2. FASTEST ROUTE ---
  start = std::chrono::high_resolution_clock::now();
  RoutingResult fastest = router.run(source_id, dest_id, loader.adjacency_list,
                                     loader.nodes, OptimizeFor::TIME, absolute_start_time);
  duration = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::high_resolution_clock::now() - start);

  print_result(fastest, loader, "FASTEST", duration.count());

  return 0;
}