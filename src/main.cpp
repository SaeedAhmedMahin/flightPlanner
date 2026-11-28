#include "Algorithms/ALTRouter.hpp"
#include "Algorithms/AStar.hpp"
#include "Algorithms/BFS.hpp"
#include "Algorithms/ColumnGeneration.hpp"
#include "Algorithms/ContractionHierarchies.hpp"
#include "Algorithms/Dijkstra.hpp"
#include "Algorithms/LagrangianRelaxation.hpp"
#include "Algorithms/RCAStar.hpp"
#include "Algorithms/TwoPhaseHubRouter.hpp"
#include "Algorithms/TwoPhaseRouter.hpp"
#include "DataLoader.hpp"
#include "DataStructures/AirportSearch.hpp"
#include "TUI/InteractiveApp.hpp"
#include "TUI/TUIViews.hpp"
#include <chrono>
#include <exception>
#include <iomanip>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

using RunStats = AlgorithmBenchmark;

// Helper to serialize path for unique deduplication
std::string serialize_flight_path(const std::vector<flight> &path) {
  std::string key = "";
  for (const auto &f : path) {
    key += std::to_string(f.flight_details_id) + ";" +
           std::to_string(f.destination_id) + ";" +
           std::to_string(f.flight_day) + ";" +
           std::to_string(f.departure_time) + "|";
  }
  return key;
}

void print_itinerary_details(const std::string &title,
                             const RoutingResult &result,
                             const DataLoader &loader, uint32_t source_id) {
  std::cout << "\n======================================================\n";
  std::cout << "=== " << title << " ===\n";
  if (!result.success) {
    std::cout << "Status: No valid route found.\n";
    std::cout << "======================================================\n";
    return;
  }

  std::cout << "Total Price: $" << result.total_price
            << "  |  Total Time: " << result.total_time_mins / 60 << "h "
            << result.total_time_mins % 60 << "m\n";
  std::cout << "======================================================\n";

  uint32_t current_airport_id = source_id;

  for (size_t i = 0; i < result.path.size(); ++i) {
    const flight &f = result.path[i];
    const flightDetails &f_ui = loader.ui_flights[f.flight_details_id];
    std::string origin_iata = loader.ui_nodes[current_airport_id].iata_code;
    std::string dest_iata = loader.ui_nodes[f.destination_id].iata_code;
    std::string airline_full_name = loader.get_airline_name(f_ui.airline_code);

    uint32_t flight_dur_min = f.duration_mins;
    uint32_t dur_h = flight_dur_min / 60;
    uint32_t dur_m = flight_dur_min % 60;

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

    if (i + 1 < result.path.size()) {
      const flight &next_f = result.path[i + 1];
      int32_t origin_utc_offset =
          loader.nodes[current_airport_id].utc_offset_15m * 15;
      uint32_t dep_utc = static_cast<uint32_t>(
          static_cast<int32_t>(f.flight_day * 1440 + f.departure_time) -
          origin_utc_offset);
      uint32_t arr_utc = dep_utc + f.duration_mins;

      int32_t dest_utc_offset =
          loader.nodes[f.destination_id].utc_offset_15m * 15;
      uint32_t next_dep_utc =
          static_cast<uint32_t>(static_cast<int32_t>(next_f.flight_day * 1440 +
                                                     next_f.departure_time) -
                                dest_utc_offset);

      uint32_t transit_min =
          (next_dep_utc >= arr_utc) ? (next_dep_utc - arr_utc) : 0;
      uint32_t lay_h = transit_min / 60;
      uint32_t lay_m = transit_min % 60;

      std::cout << "\n(";
      if (lay_h > 0) {
        std::cout << lay_h << " hour" << (lay_h != 1 ? "s" : "");
        if (lay_m > 0)
          std::cout << " ";
      }
      if (lay_m > 0 || lay_h == 0) {
        std::cout << lay_m << " mins";
      }
      std::cout << " TRANSIT @ " << dest_iata << ")\n\n";
    }

    current_airport_id = f.destination_id;
  }
}

void print_unique_routes(const std::vector<RunStats> &benchmarks,
                         const DataLoader &loader, uint32_t source_id) {
  struct UniqueGroup {
    RoutingResult result;
    std::vector<std::pair<std::string, std::string>> algo_versions;
  };

  std::vector<std::string> order;
  std::unordered_map<std::string, UniqueGroup> groups;

  for (const auto &bench : benchmarks) {
    if (!bench.result.success)
      continue;
    std::string key = serialize_flight_path(bench.result.path);
    if (groups.find(key) == groups.end()) {
      order.push_back(key);
      groups[key] = {bench.result, {}};
    }
    groups[key].algo_versions.push_back({bench.algo_name, bench.mode_label});
  }

  for (const auto &key : order) {
    const auto &grp = groups[key];

    std::vector<std::string> algo_order;
    std::unordered_map<std::string, std::vector<std::string>> algo_map;
    for (const auto &av : grp.algo_versions) {
      if (algo_map.find(av.first) == algo_map.end()) {
        algo_order.push_back(av.first);
      }
      algo_map[av.first].push_back(av.second);
    }

    std::string title = "";
    for (size_t i = 0; i < algo_order.size(); ++i) {
      if (i > 0)
        title += ", ";
      title += algo_order[i] + " [";
      const auto &vers = algo_map[algo_order[i]];
      for (size_t v = 0; v < vers.size(); ++v) {
        if (v > 0)
          title += ", ";
        title += vers[v];
      }
      title += "]";
    }

    print_itinerary_details(title, grp.result, loader, source_id);
  }
}

int main(int argc, char *argv[]) {
  // Mode selection defaults to CUSTOMER mode
  TUIViews::UIMode ui_mode = TUIViews::UIMode::CUSTOMER;
  bool mode_specified_cli = false;

  std::string source_iata = "";
  std::string dest_iata = "";
  std::string target_date = "";

  // Parse command line arguments
  std::vector<std::string> positional_args;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--researcher" || arg == "-r" || arg == "researcher") {
      ui_mode = TUIViews::UIMode::RESEARCHER;
      mode_specified_cli = true;
    } else if (arg == "--customer" || arg == "-c" || arg == "customer") {
      ui_mode = TUIViews::UIMode::CUSTOMER;
      mode_specified_cli = true;
    } else if (arg.rfind("--mode=", 0) == 0) {
      std::string mode_val = arg.substr(7);
      if (mode_val == "researcher" || mode_val == "r") {
        ui_mode = TUIViews::UIMode::RESEARCHER;
      } else {
        ui_mode = TUIViews::UIMode::CUSTOMER;
      }
      mode_specified_cli = true;
    } else {
      positional_args.push_back(arg);
    }
  }

  if (positional_args.size() >= 1)
    source_iata = positional_args[0];
  if (positional_args.size() >= 2)
    dest_iata = positional_args[1];
  if (positional_args.size() >= 3)
    target_date = positional_args[2];

  DataLoader loader;
  std::cout << "Loading Data...\n";
  loader.load_data("data/processed/airports_clean.csv",
                   "data/processed/routes_clean.csv");

  // If running in interactive terminal: launch full mouse-driven interactive UI!
  if (isatty(STDIN_FILENO)) {
    std::string init_src = source_iata.empty() ? "DAC" : source_iata;
    std::string init_dst = dest_iata.empty() ? "JFK" : dest_iata;
    std::string init_date = target_date.empty() ? "2027-02-15" : target_date;
    bool start_research = (ui_mode == TUIViews::UIMode::RESEARCHER);

    InteractiveApp app(loader, init_src, init_dst, init_date, start_research);
    app.run_interactive();
    return 0;
  }

  // Non-interactive / piped fallback:
  if (source_iata.empty()) {
    std::cin >> source_iata;
  }
  if (dest_iata.empty()) {
    std::cin >> dest_iata;
  }
  if (target_date.empty()) {
    std::cin >> target_date;
  }

  if (!mode_specified_cli) {
    std::string mode_choice;
    if (std::cin >> mode_choice) {
      if (mode_choice == "2" || mode_choice == "r" || mode_choice == "researcher") {
        ui_mode = TUIViews::UIMode::RESEARCHER;
      }
    }
  }

  std::string resolved_src = AirportSearch::resolve_airport_code(source_iata, loader);
  std::string resolved_dst = AirportSearch::resolve_airport_code(dest_iata, loader);

  if (resolved_src.empty() || loader.iata_to_id.find(resolved_src) == loader.iata_to_id.end() ||
      resolved_dst.empty() || loader.iata_to_id.find(resolved_dst) == loader.iata_to_id.end()) {
    std::cerr << "Error: Unknown airport code(s) or city/country not found ('"
              << source_iata << "', '" << dest_iata << "').\n";
    return 1;
  }

  if (resolved_src != source_iata || resolved_dst != dest_iata) {
    std::cout << "Resolved: '" << source_iata << "' -> " << resolved_src << " ("
              << loader.ui_nodes[loader.iata_to_id[resolved_src]].city << "), '"
              << dest_iata << "' -> " << resolved_dst << " ("
              << loader.ui_nodes[loader.iata_to_id[resolved_dst]].city << ")\n";
  }

  source_iata = resolved_src;
  dest_iata = resolved_dst;

  uint32_t source_id = loader.iata_to_id[source_iata];
  uint32_t dest_id = loader.iata_to_id[dest_iata];

  uint32_t start_day = DataLoader::parse_date_to_days(target_date);
  int32_t source_utc_offset = loader.nodes[source_id].utc_offset_15m * 15;
  uint32_t absolute_start_time =
      static_cast<uint32_t>(static_cast<int32_t>(start_day * 1440) -
                            source_utc_offset); // 00:00 local in UTC minutes

  // =========================================================================
  // 1. CUSTOMER MODE: FAST, CLEAN, ITINERARY-FOCUSED (MULTI-RUN ALT)
  // =========================================================================
  if (ui_mode == TUIViews::UIMode::CUSTOMER) {
    static ALTRouter::LandmarkData customer_alt_landmarks;
    ALTRouter alt_router;
    ALTRouter::DiverseKResult alt_diverse = alt_router.run_k_diverse(
        source_id, dest_id, loader.adjacency_list, loader.nodes,
        loader.static_route_graph, loader.reverse_static_graph,
        loader.static_route_min_price, loader.static_route_min_duration,
        customer_alt_landmarks, absolute_start_time, 10, 5,
        absolute_start_time);

    RoutingResult cheapest_res{false, 0, 0, {}, {}};
    RoutingResult fastest_res{false, 0, 0, {}, {}};
    if (!alt_diverse.paths.empty()) {
      cheapest_res = alt_diverse.paths[0];
      fastest_res = alt_diverse.paths[0];
      for (const auto &p : alt_diverse.paths) {
        if (p.total_price < cheapest_res.total_price)
          cheapest_res = p;
        if (p.total_time_mins < fastest_res.total_time_mins)
          fastest_res = p;
      }
    }

    std::string best_key = serialize_flight_path(alt_diverse.best_recommendation.path);
    std::vector<RoutingResult> alternatives;
    for (const auto &p : alt_diverse.paths) {
      if (serialize_flight_path(p.path) != best_key) {
        alternatives.push_back(p);
      }
    }

    // Render FTXUI Customer View
    TUIViews::render_customer_mode(
        source_iata, dest_iata, target_date, loader, source_id, dest_id,
        alt_diverse.best_recommendation,
        alt_diverse.recommendation_reason,
        cheapest_res, fastest_res, alternatives);

    return 0;
  }

  // =========================================================================
  // 2. RESEARCHER MODE: COMPREHENSIVE BENCHMARKING & ALGORITHM PROFILING
  // =========================================================================
  std::cout << "\n[RESEARCHER MODE] Routing: " << source_iata << " -> "
            << dest_iata << " on " << target_date << "\n";
  std::cout << "Initializing Algorithmic Suite (Contraction Hierarchies, Multiple ALT, Exact Solvers, & Hierarchies)...\n";

  // Offline preprocessing for Contraction Hierarchies
  static ContractionHierarchiesRouter::CHGraph ch_graph_price;
  static ContractionHierarchiesRouter::CHGraph ch_graph_time;
  ContractionHierarchiesRouter ch_router;
  if (!ch_graph_price.is_preprocessed) {
    ch_router.preprocess(
        loader.static_route_graph, loader.static_route_min_price,
        loader.static_route_min_duration, ch_graph_price, OptimizeFor::PRICE);
  }
  if (!ch_graph_time.is_preprocessed) {
    ch_router.preprocess(
        loader.static_route_graph, loader.static_route_min_price,
        loader.static_route_min_duration, ch_graph_time, OptimizeFor::TIME);
  }

  // Offline preprocessing for ALT (Landmark Selection & Distance Matrices)
  static ALTRouter::LandmarkData alt_landmark_data;
  ALTRouter alt_router;
  if (!alt_landmark_data.is_preprocessed) {
    ALTRouter::preprocess(
        loader.nodes, loader.static_route_graph, loader.reverse_static_graph,
        loader.static_route_min_price, loader.static_route_min_duration,
        alt_landmark_data, 16);
  }

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

  // 3. ALT Algorithm (A* with Landmarks & Triangle Inequality Lower Bounds)
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res = alt_router.run(
        source_id, dest_id, loader.adjacency_list, loader.nodes,
        loader.static_route_graph, loader.reverse_static_graph,
        loader.static_route_min_price, loader.static_route_min_duration,
        alt_landmark_data, OptimizeFor::PRICE, absolute_start_time);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"ALT", "cheapest", duration.count(), res});
  }
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res = alt_router.run(
        source_id, dest_id, loader.adjacency_list, loader.nodes,
        loader.static_route_graph, loader.reverse_static_graph,
        loader.static_route_min_price, loader.static_route_min_duration,
        alt_landmark_data, OptimizeFor::TIME, absolute_start_time);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"ALT", "fastest", duration.count(), res});
  }

  // 4. Resource-Constrained A* (RC-A*)
  RCAStarRouter rcastar_router;
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res = rcastar_router.run(
        source_id, dest_id, loader.adjacency_list, loader.nodes,
        OptimizeFor::PRICE, absolute_start_time,
        RCAStarRouter::ResourceConstraints{2880, 50000, 5});
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"RC-A*", "cheapest", duration.count(), res});
  }
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res = rcastar_router.run(
        source_id, dest_id, loader.adjacency_list, loader.nodes,
        OptimizeFor::TIME, absolute_start_time,
        RCAStarRouter::ResourceConstraints{2880, 50000, 5});
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"RC-A*", "fastest", duration.count(), res});
  }

  // 5. Contraction Hierarchies (Hierarchical Node Contraction)
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res = ch_router.run(
        source_id, dest_id, loader.adjacency_list, loader.static_route_graph,
        loader.static_route_min_price, loader.static_route_min_duration,
        loader.nodes, OptimizeFor::PRICE, absolute_start_time, ch_graph_price);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back(
        {"Contraction H.", "cheapest", duration.count(), res});
  }
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res = ch_router.run(
        source_id, dest_id, loader.adjacency_list, loader.static_route_graph,
        loader.static_route_min_price, loader.static_route_min_duration,
        loader.nodes, OptimizeFor::TIME, absolute_start_time, ch_graph_time);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"Contraction H.", "fastest", duration.count(), res});
  }

  // 6. Lagrangian Relaxation Router
  LagrangianRelaxationRouter lr_router;
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res = lr_router.run(
        source_id, dest_id, loader.adjacency_list, loader.nodes,
        OptimizeFor::PRICE, absolute_start_time,
        LagrangianRelaxationRouter::ResourceConstraints{2880, 50000, 5});
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"Lagrangian", "cheapest", duration.count(), res});
  }
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res = lr_router.run(
        source_id, dest_id, loader.adjacency_list, loader.nodes,
        OptimizeFor::TIME, absolute_start_time,
        LagrangianRelaxationRouter::ResourceConstraints{2880, 50000, 5});
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"Lagrangian", "fastest", duration.count(), res});
  }

  // 7. Column Generation Router (Master RMP & Subproblem)
  ColumnGenerationRouter cg_router;
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res = cg_router.run(
        source_id, dest_id, loader.adjacency_list, loader.nodes,
        OptimizeFor::PRICE, absolute_start_time);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"Column-Gen", "cheapest", duration.count(), res});
  }
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res = cg_router.run(
        source_id, dest_id, loader.adjacency_list, loader.nodes,
        OptimizeFor::TIME, absolute_start_time);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"Column-Gen", "fastest", duration.count(), res});
  }

  // 8. BFS Router (Minimum Transfers)
  BFSRouter bfs_router;
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res = bfs_router.run(
        source_id, dest_id, loader.adjacency_list, loader.nodes,
        OptimizeFor::PRICE, absolute_start_time);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"BFS (Hops)", "cheapest", duration.count(), res});
  }
  {
    auto start = std::chrono::high_resolution_clock::now();
    RoutingResult res = bfs_router.run(
        source_id, dest_id, loader.adjacency_list, loader.nodes,
        OptimizeFor::TIME, absolute_start_time);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    benchmarks.push_back({"BFS (Hops)", "fastest", duration.count(), res});
  }

  // 12. Two-Phase Router (Topology Corridor + Schedule Sweep)
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

  // 13. Two-Phase Hub Router (Hierarchical Aviation Hub Corridors)
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

  // 11. Multiple ALT (Diversified K-Best Paths & Best Path Recommendation)
  ALTRouter::DiverseKResult multi_alt_res;
  {
    auto start = std::chrono::high_resolution_clock::now();
    multi_alt_res = alt_router.run_k_diverse(
        source_id, dest_id, loader.adjacency_list, loader.nodes,
        loader.static_route_graph, loader.reverse_static_graph,
        loader.static_route_min_price, loader.static_route_min_duration,
        alt_landmark_data, absolute_start_time, 10, 5, absolute_start_time);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start);
    RoutingResult primary_alt =
        multi_alt_res.best_recommendation.success
            ? multi_alt_res.best_recommendation
            : (multi_alt_res.paths.empty()
                   ? RoutingResult{false, 0, 0, {}, {}}
                   : multi_alt_res.paths[0]);
    benchmarks.push_back(
        {"Multiple ALT", "diverse", duration.count(), primary_alt});
  }

  // Render Researcher TUI with FTXUI (Benchmark table, visual profiler, Multiple ALT analysis)
  TUIViews::render_researcher_mode(
      source_iata, dest_iata, target_date, loader, source_id, dest_id,
      benchmarks, multi_alt_res.best_recommendation,
      multi_alt_res.recommendation_reason,
      multi_alt_res.generalized_cost,
      multi_alt_res.paths);

  // Consolidated path equivalence breakdown
  std::cout << "\n--- DETAILED FLIGHT ITINERARIES (ALGORITHM CONSENSUS GROUPS) ---\n";
  print_unique_routes(benchmarks, loader, source_id);

  return 0;
}
