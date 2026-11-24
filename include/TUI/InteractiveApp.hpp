#pragma once

#include "Algorithms/ALTRouter.hpp"
#include "Algorithms/AStar.hpp"
#include "Algorithms/BFS.hpp"
#include "Algorithms/ColumnGeneration.hpp"
#include "Algorithms/ContractionHierarchies.hpp"
#include "Algorithms/Dijkstra.hpp"
#include "Algorithms/LabelCorrecting.hpp"
#include "Algorithms/LagrangianRelaxation.hpp"
#include "Algorithms/MetaheuristicRouter.hpp"
#include "Algorithms/PulseRouter.hpp"
#include "Algorithms/RCAStar.hpp"
#include "Algorithms/TwoPhaseHubRouter.hpp"
#include "Algorithms/TwoPhaseRouter.hpp"
#include "Algorithms/Yen.hpp"
#include "DataLoader.hpp"
#include "DataStructures/AirportSearch.hpp"
#include "DataStructures/FilterParser.hpp"
#include "TUI/TUIViews.hpp"
#include "TUI/WorldMap.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>
#include <ftxui/screen/box.hpp>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class InteractiveApp {
public:
  DataLoader &loader;

  // Search parameters
  std::string source_str;
  std::string dest_str;
  std::string target_date_str;

  // Live Airport Search & Suggestion Options
  std::vector<AirportSearch::AirportMatch> src_matches;
  std::vector<AirportSearch::AirportMatch> dst_matches;
  constexpr static size_t MAX_SUGGESTIONS = 4;

  void update_suggestions() {
    src_matches = AirportSearch::search_airports(source_str, loader, MAX_SUGGESTIONS);
    dst_matches = AirportSearch::search_airports(dest_str, loader, MAX_SUGGESTIONS);
  }

  // Date Tool State
  int year_idx = 0;
  std::vector<std::string> year_entries = {"2026", "2027"};

  int month_idx = 10; // 11 - Nov (0-indexed: 10)
  std::vector<std::string> month_entries = {
      "01 - Jan", "02 - Feb", "03 - Mar", "04 - Apr", "05 - May", "06 - Jun",
      "07 - Jul", "08 - Aug", "09 - Sep", "10 - Oct", "11 - Nov", "12 - Dec"};

  int day_idx = 14; // 15th (0-indexed: 14)
  std::vector<std::string> day_entries;

  // Flight Picker Preference (0: Best Recommended, 1: Cheapest, 2: Fastest)
  int selected_preference = 0;
  int last_preference = 0;
  int selected_flight_idx = 0;
  std::vector<ftxui::Box> option_boxes;
  ftxui::Box cheapest_summary_box;
  ftxui::Box fastest_summary_box;
  std::vector<std::string> preference_entries = {
      " ⭐ Best Value ", " 🏷️  Cheapest ", " ⚡ Fastest "};

  // World Map Visualization State
  bool show_world_map = true;
  std::string world_map_btn_label = " 🗺️  World Map: ON ";

  // Typed Flight Needs & Filters State
  std::string typed_filter_str;
  FilterSpec active_spec;
  std::vector<std::string> airline_entries = {"All Airlines"};
  std::vector<std::string> airline_codes = {"ALL"};

  // Candidate pool & filtered itineraries
  std::vector<RoutingResult> all_candidates;
  std::vector<RoutingResult> filtered_itineraries;

  bool matches_filters(const RoutingResult &res) const {
    if (!res.success || res.path.empty()) return false;

    // 1. Stops / Transits Filter
    size_t stops = res.path.size() - 1;
    if (active_spec.max_stops != -1 && stops > static_cast<size_t>(active_spec.max_stops)) {
      return false;
    }

    // 2. Airline Filter
    if (!active_spec.target_airline_code.empty()) {
      bool found = false;
      for (const auto &f : res.path) {
        if (f.flight_details_id < loader.ui_flights.size()) {
          const std::string &code = loader.ui_flights[f.flight_details_id].airline_code;
          if (code == active_spec.target_airline_code ||
              (!active_spec.target_airline_name.empty() &&
               loader.get_airline_name(code) == active_spec.target_airline_name)) {
            found = true;
            break;
          }
        }
      }
      if (!found) return false;
    }

    // 3. Price Filter
    if (active_spec.max_price != -1 && res.total_price > active_spec.max_price) {
      return false;
    }

    // 4. Departure Time Filter (Local departure minute: 0..1439)
    if (active_spec.dep_time_start_min != -1 && active_spec.dep_time_end_min != -1) {
      uint16_t dep_time = res.path.front().departure_time;
      if (dep_time < active_spec.dep_time_start_min || dep_time >= active_spec.dep_time_end_min) {
        return false;
      }
    }

    // 5. Max Duration Filter
    if (active_spec.max_duration_mins != -1 && res.total_time_mins > active_spec.max_duration_mins) {
      return false;
    }

    return true;
  }

  void apply_filters() {
    // 1. Gather available route airlines for matching
    std::vector<std::pair<std::string, std::string>> available_airlines;
    for (const auto &code : airline_codes) {
      if (code != "ALL") {
        available_airlines.push_back({code, loader.get_airline_name(code)});
      }
    }
    for (const auto &entry : loader.airline_names) {
      available_airlines.push_back({entry.first, entry.second});
    }

    active_spec = FilterParser::parse_typed_filter(typed_filter_str, available_airlines);

    // If preference is typed (e.g. "cheapest", "fastest", "best")
    if (active_spec.preference_override != -1) {
      selected_preference = active_spec.preference_override;
    }

    filtered_itineraries.clear();
    for (const auto &res : all_candidates) {
      if (matches_filters(res)) {
        filtered_itineraries.push_back(res);
      }
    }

    if (!filtered_itineraries.empty()) {
      // 1. Identify cheapest among filtered
      cheapest_flight = filtered_itineraries[0];
      for (const auto &res : filtered_itineraries) {
        if (res.total_price < cheapest_flight.total_price) {
          cheapest_flight = res;
        }
      }

      // 2. Identify fastest among filtered
      fastest_flight = filtered_itineraries[0];
      for (const auto &res : filtered_itineraries) {
        if (res.total_time_mins < fastest_flight.total_time_mins) {
          fastest_flight = res;
        }
      }

      // 3. Identify best recommended among filtered (generalized economic balance)
      best_flight = filtered_itineraries[0];
      double best_score = std::numeric_limits<double>::max();
      for (const auto &res : filtered_itineraries) {
        double score = res.total_price + (res.total_time_mins * 0.3) +
                       ((res.path.size() - 1) * 75.0);
        if (score < best_score) {
          best_score = score;
          best_flight = res;
        }
      }
      best_reason =
          (best_flight.path.size() <= 1)
              ? "Non-stop direct flight matching your filters."
              : "Optimal balance between ticket fare and journey duration.";

      // 4. Populate alternative itineraries (excluding the chosen flight)
      yen_alternatives.clear();
      std::string chosen_key = serialize_path_key(
          (selected_preference == 0)
              ? best_flight.path
              : (selected_preference == 1) ? cheapest_flight.path
                                           : fastest_flight.path);

      for (const auto &res : filtered_itineraries) {
        if (serialize_path_key(res.path) != chosen_key) {
          yen_alternatives.push_back(res);
        }
      }

      // Keep selected_flight_idx within valid range
      if (selected_flight_idx < 0 ||
          selected_flight_idx >= static_cast<int>(filtered_itineraries.size())) {
        sync_selected_flight_from_preference();
      }
    } else {
      cheapest_flight = RoutingResult{false, 0, 0, {}, {}};
      fastest_flight = RoutingResult{false, 0, 0, {}, {}};
      best_flight = RoutingResult{false, 0, 0, {}, {}};
      yen_alternatives.clear();
      selected_flight_idx = -1;
    }
  }

  void sync_selected_flight_from_preference() {
    if (filtered_itineraries.empty()) {
      selected_flight_idx = -1;
      return;
    }
    std::string target_key;
    if (selected_preference == 0 && best_flight.success) {
      target_key = serialize_path_key(best_flight.path);
    } else if (selected_preference == 1 && cheapest_flight.success) {
      target_key = serialize_path_key(cheapest_flight.path);
    } else if (selected_preference == 2 && fastest_flight.success) {
      target_key = serialize_path_key(fastest_flight.path);
    }

    if (!target_key.empty()) {
      for (size_t i = 0; i < filtered_itineraries.size(); ++i) {
        if (serialize_path_key(filtered_itineraries[i].path) == target_key) {
          selected_flight_idx = static_cast<int>(i);
          return;
        }
      }
    }
    selected_flight_idx = 0;
  }

  void reset_filters() {
    typed_filter_str.clear();
    active_spec = FilterSpec{};
    apply_filters();
  }

  std::string get_active_filters_description() const {
    std::vector<std::string> parts;
    if (active_spec.max_stops == 0) {
      parts.push_back("Stops: Non-stop only");
    } else if (active_spec.max_stops == 1) {
      parts.push_back("Stops: ≤ 1 Stop");
    } else if (active_spec.max_stops == 2) {
      parts.push_back("Stops: ≤ 2 Stops");
    }

    if (!active_spec.target_airline_name.empty()) {
      parts.push_back("Airline: " + active_spec.target_airline_name + " (" + active_spec.target_airline_code + ")");
    } else if (!active_spec.target_airline_code.empty()) {
      parts.push_back("Airline: " + active_spec.target_airline_code);
    }

    if (active_spec.max_price != -1) {
      parts.push_back("Max Price: $" + std::to_string(active_spec.max_price));
    }

    if (!active_spec.dep_time_label.empty()) {
      parts.push_back("Departure: " + active_spec.dep_time_label);
    }

    if (active_spec.max_duration_mins != -1) {
      int hrs = active_spec.max_duration_mins / 60;
      parts.push_back("Max Duration: " + std::to_string(hrs) + "h");
    }

    if (parts.empty()) {
      return typed_filter_str.empty() ? "None" : typed_filter_str;
    }

    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
      if (i > 0)
        out += " │ ";
      out += parts[i];
    }
    return out;
  }

  // Mode state: starts in Customer Mode
  bool show_research_mode = false;
  std::string research_btn_label = " 🔬 Research Mode: OFF ";
  std::string status_message = "Ready. Click SEARCH FLIGHTS to query.";

  // Scroll state for the independent results viewport
  int scroll_line = 0;
  int content_height_ = 0;
  int customer_height_ = 0;
  ftxui::Box viewport_box;

  // Precomputed offline graphs for research mode
  ContractionHierarchiesRouter::CHGraph ch_graph_price;
  ContractionHierarchiesRouter::CHGraph ch_graph_time;
  ALTRouter::LandmarkData alt_landmark_data;

  // Routing Results Cache
  bool has_results = false;
  uint32_t active_source_id = 0;
  uint32_t active_dest_id = 0;
  RoutingResult best_flight;
  std::string best_reason;
  double best_gen_cost = 0.0;
  RoutingResult cheapest_flight;
  RoutingResult fastest_flight;
  std::vector<RoutingResult> yen_k_paths;
  std::vector<RoutingResult> yen_alternatives;
  long yen_compute_us = 0;
  std::vector<AlgorithmBenchmark> researcher_benchmarks;

  InteractiveApp(DataLoader &dl, const std::string &init_src = "DAC",
                 const std::string &init_dst = "JFK",
                 const std::string &init_date = "2026-11-15",
                 bool start_research_mode = false)
      : loader(dl), source_str(init_src), dest_str(init_dst),
        target_date_str(init_date), show_research_mode(start_research_mode) {

    for (int d = 1; d <= 31; ++d) {
      std::string s = (d < 10 ? "0" : "") + std::to_string(d);
      day_entries.push_back(s);
    }

    parse_initial_date(init_date);
    update_research_btn_label();
    update_suggestions();
  }

  void update_research_btn_label() {
    research_btn_label = show_research_mode ? " 🧪 Research Mode: ON  "
                                            : " 🔬 Research Mode: OFF ";
  }

  static std::string to_upper_trimmed(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
      if (!std::isspace(c)) {
        out.push_back(static_cast<char>(std::toupper(c)));
      }
    }
    return out;
  }

  static std::string serialize_path_key(const std::vector<flight> &path) {
    std::string key;
    for (const auto &f : path) {
      key += std::to_string(f.flight_details_id) + ";" +
             std::to_string(f.destination_id) + ";" +
             std::to_string(f.flight_day) + ";" +
             std::to_string(f.departure_time) + "|";
    }
    return key;
  }

  void parse_initial_date(const std::string &date_str) {
    if (date_str.size() >= 10 && date_str[4] == '-' && date_str[7] == '-') {
      try {
        std::string y = date_str.substr(0, 4);
        int m = std::stoi(date_str.substr(5, 2)) - 1;
        int d = std::stoi(date_str.substr(8, 2)) - 1;

        if (y == "2027")
          year_idx = 1;
        else
          year_idx = 0;

        if (m >= 0 && m < 12)
          month_idx = m;
        if (d >= 0 && d < 31)
          day_idx = d;
      } catch (...) {
      }
    }
  }

  void update_target_date_from_tool() {
    std::string y = year_entries[year_idx];
    std::string m = month_entries[month_idx].substr(0, 2);
    std::string d = day_entries[day_idx];
    target_date_str = y + "-" + m + "-" + d;
  }

  void ensure_offline_preprocessed() {
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
    if (!alt_landmark_data.is_preprocessed) {
      ALTRouter::preprocess(
          loader.nodes, loader.static_route_graph, loader.reverse_static_graph,
          loader.static_route_min_price, loader.static_route_min_duration,
          alt_landmark_data, 16);
    }
  }

  void run_research_suite() {
    if (!has_results)
      return;

    ensure_offline_preprocessed();
    researcher_benchmarks.clear();

    uint32_t start_day = DataLoader::parse_date_to_days(target_date_str);
    int32_t source_utc_offset =
        loader.nodes[active_source_id].utc_offset_15m * 15;
    uint32_t absolute_start_time = static_cast<uint32_t>(
        static_cast<int32_t>(start_day * 1440) - source_utc_offset);

    // Dijkstra
    Dijkstra dijkstra_router;
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = dijkstra_router.run(
          active_source_id, active_dest_id, loader.adjacency_list, loader.nodes,
          OptimizeFor::PRICE, absolute_start_time);
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back(
          {"Dijkstra", "cheapest", dur.count(), res});
    }
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = dijkstra_router.run(
          active_source_id, active_dest_id, loader.adjacency_list, loader.nodes,
          OptimizeFor::TIME, absolute_start_time);
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back(
          {"Dijkstra", "fastest", dur.count(), res});
    }

    // A*
    AStar astar_router;
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = astar_router.run(
          active_source_id, active_dest_id, loader.adjacency_list, loader.nodes,
          OptimizeFor::PRICE, absolute_start_time);
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back({"A*", "cheapest", dur.count(), res});
    }
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = astar_router.run(
          active_source_id, active_dest_id, loader.adjacency_list, loader.nodes,
          OptimizeFor::TIME, absolute_start_time);
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back({"A*", "fastest", dur.count(), res});
    }

    // ALT
    ALTRouter alt_router;
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = alt_router.run(
          active_source_id, active_dest_id, loader.adjacency_list, loader.nodes,
          loader.static_route_graph, loader.reverse_static_graph,
          loader.static_route_min_price, loader.static_route_min_duration,
          alt_landmark_data, OptimizeFor::PRICE, absolute_start_time);
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back({"ALT", "cheapest", dur.count(), res});
    }
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = alt_router.run(
          active_source_id, active_dest_id, loader.adjacency_list, loader.nodes,
          loader.static_route_graph, loader.reverse_static_graph,
          loader.static_route_min_price, loader.static_route_min_duration,
          alt_landmark_data, OptimizeFor::TIME, absolute_start_time);
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back({"ALT", "fastest", dur.count(), res});
    }

    // RC-A*
    RCAStarRouter rcastar_router;
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = rcastar_router.run(
          active_source_id, active_dest_id, loader.adjacency_list, loader.nodes,
          OptimizeFor::PRICE, absolute_start_time,
          RCAStarRouter::ResourceConstraints{2880, 50000, 5});
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back({"RC-A*", "cheapest", dur.count(), res});
    }
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = rcastar_router.run(
          active_source_id, active_dest_id, loader.adjacency_list, loader.nodes,
          OptimizeFor::TIME, absolute_start_time,
          RCAStarRouter::ResourceConstraints{2880, 50000, 5});
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back({"RC-A*", "fastest", dur.count(), res});
    }

    // Contraction Hierarchies
    ContractionHierarchiesRouter ch_router;
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = ch_router.run(
          active_source_id, active_dest_id, loader.adjacency_list,
          loader.static_route_graph, loader.static_route_min_price,
          loader.static_route_min_duration, loader.nodes, OptimizeFor::PRICE,
          absolute_start_time, ch_graph_price);
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back(
          {"Contraction H.", "cheapest", dur.count(), res});
    }
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = ch_router.run(
          active_source_id, active_dest_id, loader.adjacency_list,
          loader.static_route_graph, loader.static_route_min_price,
          loader.static_route_min_duration, loader.nodes, OptimizeFor::TIME,
          absolute_start_time, ch_graph_time);
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back(
          {"Contraction H.", "fastest", dur.count(), res});
    }

    // Label Correcting
    LabelCorrectingRouter label_correcting_router;
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = label_correcting_router.run(
          active_source_id, active_dest_id, loader.adjacency_list, loader.nodes,
          OptimizeFor::PRICE, absolute_start_time);
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back(
          {"Label-Corr", "cheapest", dur.count(), res});
    }
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = label_correcting_router.run(
          active_source_id, active_dest_id, loader.adjacency_list, loader.nodes,
          OptimizeFor::TIME, absolute_start_time);
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back(
          {"Label-Corr", "fastest", dur.count(), res});
    }

    // Pulse
    PulseRouter pulse_router;
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = pulse_router.run(
          active_source_id, active_dest_id, loader.adjacency_list,
          loader.static_route_graph, loader.static_route_min_price,
          loader.nodes, OptimizeFor::PRICE, absolute_start_time);
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back({"Pulse", "cheapest", dur.count(), res});
    }
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = pulse_router.run(
          active_source_id, active_dest_id, loader.adjacency_list,
          loader.static_route_graph, loader.static_route_min_price,
          loader.nodes, OptimizeFor::TIME, absolute_start_time);
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back({"Pulse", "fastest", dur.count(), res});
    }

    // Lagrangian Relaxation
    LagrangianRelaxationRouter lr_router;
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = lr_router.run(
          active_source_id, active_dest_id, loader.adjacency_list, loader.nodes,
          OptimizeFor::PRICE, absolute_start_time,
          LagrangianRelaxationRouter::ResourceConstraints{2880, 50000, 5});
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back(
          {"Lagrangian", "cheapest", dur.count(), res});
    }
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = lr_router.run(
          active_source_id, active_dest_id, loader.adjacency_list, loader.nodes,
          OptimizeFor::TIME, absolute_start_time,
          LagrangianRelaxationRouter::ResourceConstraints{2880, 50000, 5});
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back(
          {"Lagrangian", "fastest", dur.count(), res});
    }

    // Column Generation
    ColumnGenerationRouter cg_router;
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = cg_router.run(
          active_source_id, active_dest_id, loader.adjacency_list, loader.nodes,
          OptimizeFor::PRICE, absolute_start_time);
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back(
          {"Column-Gen", "cheapest", dur.count(), res});
    }
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = cg_router.run(
          active_source_id, active_dest_id, loader.adjacency_list, loader.nodes,
          OptimizeFor::TIME, absolute_start_time);
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back(
          {"Column-Gen", "fastest", dur.count(), res});
    }

    // Metaheuristics
    MetaheuristicRouter meta_router;
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = meta_router.run(
          active_source_id, active_dest_id, loader.adjacency_list, loader.nodes,
          OptimizeFor::PRICE, absolute_start_time);
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back(
          {"GA-ACO Meta", "cheapest", dur.count(), res});
    }
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = meta_router.run(
          active_source_id, active_dest_id, loader.adjacency_list, loader.nodes,
          OptimizeFor::TIME, absolute_start_time);
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back(
          {"GA-ACO Meta", "fastest", dur.count(), res});
    }

    // BFS (Hops)
    BFSRouter bfs_router;
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = bfs_router.run(
          active_source_id, active_dest_id, loader.adjacency_list, loader.nodes,
          OptimizeFor::PRICE, absolute_start_time);
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back(
          {"BFS (Hops)", "cheapest", dur.count(), res});
    }
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = bfs_router.run(
          active_source_id, active_dest_id, loader.adjacency_list, loader.nodes,
          OptimizeFor::TIME, absolute_start_time);
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back(
          {"BFS (Hops)", "fastest", dur.count(), res});
    }

    // Two-Phase
    TwoPhaseRouter two_phase_router;
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = two_phase_router.run(
          active_source_id, active_dest_id, loader.adjacency_list,
          loader.static_route_graph, loader.static_route_min_price,
          loader.static_route_min_duration, loader.nodes, OptimizeFor::PRICE,
          absolute_start_time);
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back(
          {"Two-Phase", "cheapest", dur.count(), res});
    }
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = two_phase_router.run(
          active_source_id, active_dest_id, loader.adjacency_list,
          loader.static_route_graph, loader.static_route_min_price,
          loader.static_route_min_duration, loader.nodes, OptimizeFor::TIME,
          absolute_start_time);
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back(
          {"Two-Phase", "fastest", dur.count(), res});
    }

    // Two-Phase Hub
    TwoPhaseHubRouter two_phase_hub_router;
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = two_phase_hub_router.run(
          active_source_id, active_dest_id, loader.adjacency_list,
          loader.static_route_graph, loader.static_route_min_price,
          loader.static_route_min_duration, loader.nodes, OptimizeFor::PRICE,
          absolute_start_time);
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back(
          {"Two-Phase Hub", "cheapest", dur.count(), res});
    }
    {
      auto start = std::chrono::high_resolution_clock::now();
      RoutingResult res = two_phase_hub_router.run(
          active_source_id, active_dest_id, loader.adjacency_list,
          loader.static_route_graph, loader.static_route_min_price,
          loader.static_route_min_duration, loader.nodes, OptimizeFor::TIME,
          absolute_start_time);
      auto dur = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - start);
      researcher_benchmarks.push_back(
          {"Two-Phase Hub", "fastest", dur.count(), res});
    }

    // Yen's K-SP
    RoutingResult primary_yen = yen_k_paths.empty()
                                    ? RoutingResult{false, 0, 0, {}, {}}
                                    : yen_k_paths[0];
    researcher_benchmarks.push_back(
        {"Yen's K-SP", "cheapest", yen_compute_us, primary_yen});
  }

  void execute_search(bool run_all_research = false) {
    update_target_date_from_tool();
    researcher_benchmarks.clear(); // Always invalidate stale benchmarks

    std::string orig_src = source_str;
    std::string orig_dst = dest_str;

    std::string resolved_src = AirportSearch::resolve_airport_code(source_str, loader);
    std::string resolved_dst = AirportSearch::resolve_airport_code(dest_str, loader);

    if (resolved_src.empty() || loader.iata_to_id.find(resolved_src) == loader.iata_to_id.end()) {
      status_message = "❌ Origin airport not found for '" + source_str + "'. Try city, country or IATA code.";
      has_results = false;
      return;
    }
    if (resolved_dst.empty() || loader.iata_to_id.find(resolved_dst) == loader.iata_to_id.end()) {
      status_message = "❌ Destination airport not found for '" + dest_str + "'. Try city, country or IATA code.";
      has_results = false;
      return;
    }

    source_str = resolved_src;
    dest_str = resolved_dst;
    update_suggestions();

    active_source_id = loader.iata_to_id[source_str];
    active_dest_id = loader.iata_to_id[dest_str];

    uint32_t start_day = DataLoader::parse_date_to_days(target_date_str);
    int32_t source_utc_offset =
        loader.nodes[active_source_id].utc_offset_15m * 15;
    uint32_t absolute_start_time = static_cast<uint32_t>(
        static_cast<int32_t>(start_day * 1440) - source_utc_offset);

    // 1. Customer Mode Quick Search (Yen's K-SP & A*)
    YenRouter yen_router;
    auto yen_start = std::chrono::high_resolution_clock::now();
    YenRouter::YenKResult yen_k = yen_router.run(
        active_source_id, active_dest_id, loader.adjacency_list, loader.nodes,
        OptimizeFor::PRICE, absolute_start_time, 15);
    yen_compute_us = std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::high_resolution_clock::now() - yen_start)
                         .count();

    AStar astar_router;
    RoutingResult absolute_fastest = astar_router.run(
        active_source_id, active_dest_id, loader.adjacency_list, loader.nodes,
        OptimizeFor::TIME, absolute_start_time);

    all_candidates.clear();
    std::unordered_set<std::string> seen_keys;
    for (const auto &p : yen_k.k_paths) {
      if (p.success && !p.path.empty()) {
        std::string k = serialize_path_key(p.path);
        if (seen_keys.find(k) == seen_keys.end()) {
          seen_keys.insert(k);
          all_candidates.push_back(p);
        }
      }
    }

    if (absolute_fastest.success && !absolute_fastest.path.empty()) {
      std::string k = serialize_path_key(absolute_fastest.path);
      if (seen_keys.find(k) == seen_keys.end()) {
        seen_keys.insert(k);
        all_candidates.push_back(absolute_fastest);
      }
    }

    yen_k_paths = all_candidates;

    // Dynamically populate airlines from discovered routes
    std::set<std::string> unique_airline_codes;
    for (const auto &res : all_candidates) {
      for (const auto &f : res.path) {
        if (f.flight_details_id < loader.ui_flights.size()) {
          unique_airline_codes.insert(
              loader.ui_flights[f.flight_details_id].airline_code);
        }
      }
    }

    airline_entries = {"All Airlines"};
    airline_codes = {"ALL"};
    for (const auto &code : unique_airline_codes) {
      airline_codes.push_back(code);
      std::string name = loader.get_airline_name(code);
      if (name.size() > 14)
        name = name.substr(0, 13) + "…";
      airline_entries.push_back(code + " - " + name);
    }

    // Apply active filters to candidate pool
    apply_filters();
    sync_selected_flight_from_preference();

    has_results = !all_candidates.empty();
    scroll_line = 0;

    // 2. Researcher Suite (if Research Mode is enabled or requested)
    if (show_research_mode || run_all_research) {
      run_research_suite();
    }

    if (orig_src != source_str || orig_dst != dest_str) {
      status_message = "✅ Resolved: '" + orig_src + "' ➔ " + source_str +
                       ", '" + orig_dst + "' ➔ " + dest_str + "  |  " +
                       std::to_string(yen_k_paths.size()) + " itineraries found";
    } else {
      status_message = "✅ " + source_str + " ──▶ " + dest_str + " (" +
                       target_date_str + ")  |  " +
                       std::to_string(yen_k_paths.size()) + " itineraries found";
    }
  }

  int get_external_dimy() const {
    return std::max(0, viewport_box.y_max - viewport_box.y_min);
  }

  int get_internal_dimy() const { return std::max(0, content_height_); }

  int get_max_scroll() const {
    int ext = get_external_dimy();
    int intl = get_internal_dimy();
    return std::max(0, intl - ext - 1);
  }

  void run_interactive() {
    using namespace ftxui;

    auto screen = ScreenInteractive::Fullscreen();
    screen.TrackMouse(true);

    // Initial search execution
    execute_search(show_research_mode);

    // 1. Text Inputs (Mouse Clickable, Single-line, Enter triggers search)
    InputOption src_opt;
    src_opt.multiline = false;
    src_opt.placeholder = "e.g. New York, USA, JFK";
    src_opt.on_change = [&] { update_suggestions(); };
    src_opt.on_enter = [&] { execute_search(show_research_mode); };
    Component source_input = Input(&source_str, "e.g. New York, JFK", src_opt);

    InputOption dst_opt;
    dst_opt.multiline = false;
    dst_opt.placeholder = "e.g. Thailand, London, BKK";
    dst_opt.on_change = [&] { update_suggestions(); };
    dst_opt.on_enter = [&] { execute_search(show_research_mode); };
    Component dest_input = Input(&dest_str, "e.g. Thailand, BKK", dst_opt);

    // 1b. Interactive Suggestion Buttons (Clickable options as you type)
    std::vector<std::string> src_sugg_labels(MAX_SUGGESTIONS);
    std::vector<std::string> dst_sugg_labels(MAX_SUGGESTIONS);
    Components src_buttons;
    Components dst_buttons;

    auto make_sugg_opt = [](Color main_color) {
      ButtonOption opt;
      opt.transform = [main_color](const EntryState &s) {
        auto el = text(s.label);
        if (s.focused) {
          return hbox({
              text("[") | bold | color(Color::White),
              el | bold | inverted | color(Color::White),
              text("]") | bold | color(Color::White),
          });
        }
        return hbox({
            text("[") | dim | color(main_color),
            el | color(main_color),
            text("]") | dim | color(main_color),
        });
      };
      return opt;
    };

    for (size_t i = 0; i < MAX_SUGGESTIONS; ++i) {
      auto btn_src = Button(
          &src_sugg_labels[i],
          [&, i] {
            if (i < src_matches.size()) {
              source_str = src_matches[i].iata;
              update_suggestions();
              execute_search(show_research_mode);
            }
          },
          make_sugg_opt(Color::CyanLight));
      src_buttons.push_back(
          Maybe(btn_src, [&, i] { return i < src_matches.size(); }));

      auto btn_dst = Button(
          &dst_sugg_labels[i],
          [&, i] {
            if (i < dst_matches.size()) {
              dest_str = dst_matches[i].iata;
              update_suggestions();
              execute_search(show_research_mode);
            }
          },
          make_sugg_opt(Color::YellowLight));
      dst_buttons.push_back(
          Maybe(btn_dst, [&, i] { return i < dst_matches.size(); }));
    }

    auto src_sugg_container = Container::Horizontal(src_buttons);
    auto dst_sugg_container = Container::Horizontal(dst_buttons);

    // 2. Date Tool Components (Dropdowns)
    Component year_dropdown = Dropdown(&year_entries, &year_idx);
    Component month_dropdown = Dropdown(&month_entries, &month_idx);
    Component day_dropdown = Dropdown(&day_entries, &day_idx);

    // 3. Flight Preference Picker (Toggle Buttons)
    Component preference_toggle =
        Toggle(&preference_entries, &selected_preference);

    // 4. Action Buttons
    Component search_btn = Button(
        " 🔍 SEARCH ", [&] { execute_search(show_research_mode); },
        ButtonOption::Border());

    Component research_toggle_btn = Button(
        &research_btn_label,
        [&] {
          show_research_mode = !show_research_mode;
          update_research_btn_label();
          if (show_research_mode) {
            if (has_results && researcher_benchmarks.empty()) {
              run_research_suite();
            }
            // Jump smoothly to the newly revealed Research Mode section
            scroll_line = customer_height_;
          } else {
            scroll_line = 0;
          }
        },
        ButtonOption::Border());

    Component exit_btn =
        Button(" ❌ Exit ", [&] { screen.Exit(); }, ButtonOption::Border());

    Component map_toggle_btn = Button(
        &world_map_btn_label,
        [&] {
          show_world_map = !show_world_map;
          world_map_btn_label = show_world_map ? " 🗺️  World Map: ON "
                                               : " 🗺️  World Map: OFF ";
        },
        ButtonOption::Border());

    // 5. Bottom Scroll Control Buttons (Always Accessible!)
    Component scroll_top_btn = Button(
        " ⤒ Top ", [&] { scroll_line = 0; }, ButtonOption::Ascii());
    Component scroll_up_btn = Button(
        " ▲ Up ", [&] { scroll_line = std::max(0, scroll_line - 6); },
        ButtonOption::Ascii());
    Component scroll_down_btn = Button(
        " ▼ Down ",
        [&] {
          scroll_line = std::min(get_max_scroll(), scroll_line + 6);
        },
        ButtonOption::Ascii());
    Component scroll_bottom_btn = Button(
        " ⤓ Bottom ", [&] { scroll_line = get_max_scroll(); },
        ButtonOption::Ascii());
    Component jump_research_btn = Button(
        " 🔬 Jump to Research ",
        [&] {
          if (!show_research_mode) {
            show_research_mode = true;
            update_research_btn_label();
            if (has_results && researcher_benchmarks.empty()) {
              run_research_suite();
            }
          }
          scroll_line = customer_height_;
        },
        ButtonOption::Ascii());

    // 3b. Typed Flight Needs Filter Component & Chips
    InputOption filter_opt;
    filter_opt.multiline = false;
    filter_opt.placeholder = "e.g. non-stop, under $800, Emirates, morning, < 24h";
    filter_opt.on_change = [&] { apply_filters(); };
    filter_opt.on_enter = [&] { apply_filters(); };
    Component filter_input =
        Input(&typed_filter_str, "e.g. non-stop, under $800, Emirates, morning, < 24h", filter_opt);

    Component clear_filter_btn = Button(
        " ↺ Clear ", [&] { reset_filters(); }, ButtonOption::Ascii());

    auto add_need_token = [&](const std::string &token) {
      if (typed_filter_str.empty()) {
        typed_filter_str = token;
      } else if (typed_filter_str.find(token) == std::string::npos) {
        typed_filter_str += " " + token;
      }
      apply_filters();
    };

    Component chip_nonstop = Button(
        " Non-stop ", [&] { add_need_token("non-stop"); }, ButtonOption::Ascii());
    Component chip_1stop = Button(
        " ≤ 1 Stop ", [&] { add_need_token("1 stop"); }, ButtonOption::Ascii());
    Component chip_p800 = Button(
        " Under $800 ", [&] { add_need_token("under $800"); }, ButtonOption::Ascii());
    Component chip_p1000 = Button(
        " Under $1,000 ", [&] { add_need_token("under $1000"); }, ButtonOption::Ascii());
    Component chip_morning = Button(
        " Morning ", [&] { add_need_token("morning"); }, ButtonOption::Ascii());
    Component chip_dur24 = Button(
        " < 24h ", [&] { add_need_token("< 24h"); }, ButtonOption::Ascii());

    auto quick_chips_container = Container::Horizontal({
        chip_nonstop,
        chip_1stop,
        chip_p800,
        chip_p1000,
        chip_morning,
        chip_dur24,
    });

    auto filters_container = Container::Vertical({
        Container::Horizontal({
            filter_input,
            clear_filter_btn,
        }),
        quick_chips_container,
    });

    // 3c. Interactive Flight Option Buttons Pool (Clickable options)
    constexpr static size_t MAX_OPTION_BUTTONS = 40;
    std::vector<std::string> opt_btn_labels(MAX_OPTION_BUTTONS);
    Components opt_buttons;
    for (size_t i = 0; i < MAX_OPTION_BUTTONS; ++i) {
      auto btn = Button(
          &opt_btn_labels[i],
          [&, i] {
            if (i < filtered_itineraries.size()) {
              selected_flight_idx = static_cast<int>(i);
            }
          },
          ButtonOption::Ascii());
      opt_buttons.push_back(
          Maybe(btn, [&, i] { return i < filtered_itineraries.size(); }));
    }
    auto opt_buttons_container = Container::Vertical(opt_buttons);

    // Assemble interactive component hierarchy
    auto top_header_buttons = Container::Horizontal({
        map_toggle_btn,
        research_toggle_btn,
        exit_btn,
    });

    auto controls_container = Container::Vertical({
        Container::Horizontal({
            source_input,
            search_btn,
        }),
        src_sugg_container,
        dest_input,
        dst_sugg_container,
        Container::Horizontal({
            year_dropdown,
            month_dropdown,
            day_dropdown,
            preference_toggle,
        }),
        filters_container,
        opt_buttons_container,
    });

    auto footer_controls = Container::Horizontal({
        scroll_top_btn,
        scroll_up_btn,
        scroll_down_btn,
        scroll_bottom_btn,
        jump_research_btn,
    });

    int main_focus_idx = 1; // Start with focus on search controls
    auto main_container = Container::Vertical({
        top_header_buttons,
        controls_container,
        footer_controls,
    }, &main_focus_idx);

    // Renderer lambda
    auto renderer = Renderer(main_container, [&] {
      // Keep date synced live whenever dropdown changes
      update_target_date_from_tool();

      // If preference changed from toggle
      if (selected_preference != last_preference) {
        last_preference = selected_preference;
        sync_selected_flight_from_preference();
      }

      // Keep filters synced live
      apply_filters();

      // Update option button labels
      for (size_t i = 0; i < MAX_OPTION_BUTTONS; ++i) {
        if (i < filtered_itineraries.size()) {
          if (static_cast<int>(i) == selected_flight_idx) {
            opt_btn_labels[i] = " ▶ [✓ Selected] ";
          } else {
            opt_btn_labels[i] = "   [Inspect]    ";
          }
        } else {
          opt_btn_labels[i] = "";
        }
      }

      // Update suggestion labels for any matches
      for (size_t i = 0; i < MAX_SUGGESTIONS; ++i) {
        if (i < src_matches.size()) {
          const auto &m = src_matches[i];
          std::string city_label = m.city.empty() ? m.name : m.city;
          if (city_label.size() > 11)
            city_label = city_label.substr(0, 10) + "…";
          src_sugg_labels[i] = " ✈ " + m.iata + " · " + city_label + " ";
        } else {
          src_sugg_labels[i] = "";
        }

        if (i < dst_matches.size()) {
          const auto &m = dst_matches[i];
          std::string city_label = m.city.empty() ? m.name : m.city;
          if (city_label.size() > 11)
            city_label = city_label.substr(0, 10) + "…";
          dst_sugg_labels[i] = " ✈ " + m.iata + " · " + city_label + " ";
        } else {
          dst_sugg_labels[i] = "";
        }
      }

      // Live resolution preview next to input boxes
      std::string live_src_code =
          AirportSearch::resolve_airport_code(source_str, loader);
      std::string live_dst_code =
          AirportSearch::resolve_airport_code(dest_str, loader);

      std::string origin_desc;
      bool origin_valid = false;
      if (!live_src_code.empty() && loader.iata_to_id.count(live_src_code)) {
        uint32_t id = loader.iata_to_id.at(live_src_code);
        const auto &apt = loader.ui_nodes[id];
        origin_desc = "➔ " + live_src_code + " (" + apt.city + ")";
        origin_valid = true;
      } else if (!source_str.empty()) {
        origin_desc = "⚠️ Not found";
      } else {
        origin_desc = "Type city, country or code";
      }

      std::string dest_desc;
      bool dest_valid = false;
      if (!live_dst_code.empty() && loader.iata_to_id.count(live_dst_code)) {
        uint32_t id = loader.iata_to_id.at(live_dst_code);
        const auto &apt = loader.ui_nodes[id];
        dest_desc = "➔ " + live_dst_code + " (" + apt.city + ")";
        dest_valid = true;
      } else if (!dest_str.empty()) {
        dest_desc = "⚠️ Not found";
      } else {
        dest_desc = "Type city, country or code";
      }

      // ---------------------------------------------------------------------
      // FIXED TOP BAR (BRANDING + TOP-RIGHT RESEARCH MODE BUTTON + EXIT)
      // ---------------------------------------------------------------------
      auto top_bar = hbox({
          vbox({
              text(" ✈️  FLIGHT ROUTE EXPLORER ") | bold |
                  color(Color::CyanLight),
              text(show_research_mode
                       ? " Customer + Research Benchmark Mode Active "
                       : " Customer Mode  (Click top-right button for Research "
                         "Analytics) ") |
                  dim,
          }),
          filler(),
          hbox({
              map_toggle_btn->Render() |
                  (show_world_map ? color(Color::CyanLight)
                                  : color(Color::GrayLight)),
              text(" "),
              research_toggle_btn->Render() |
                  (show_research_mode ? color(Color::GreenLight)
                                      : color(Color::YellowLight)),
              text(" "),
              exit_btn->Render() | color(Color::RedLight),
          }),
      });

      // ---------------------------------------------------------------------
      // FIXED COMPACT SEARCH, DATE TOOL & FLIGHT PREFERENCE FORM
      // ---------------------------------------------------------------------
      std::vector<Element> form_lines;

      // Row 1: Origin Input, live resolution, and Search button
      form_lines.push_back(hbox({
          text(" Origin: ") | bold | vcenter,
          source_input->Render() | size(WIDTH, EQUAL, 20) |
              color(Color::White) | borderLight,
          text(" " + origin_desc) |
              color(origin_valid ? Color::GreenLight : Color::YellowLight) |
              vcenter,
          filler(),
          search_btn->Render() | bold | color(Color::GreenLight),
      }));

      // Row 2: Live Origin suggestions (as user types)
      if (!src_matches.empty()) {
        std::vector<Element> s_items;
        s_items.push_back(text("  ↳ Origin options: ") |
                          color(Color::CyanLight) | dim | vcenter);
        for (size_t i = 0; i < MAX_SUGGESTIONS; ++i) {
          if (i < src_matches.size()) {
            s_items.push_back(src_buttons[i]->Render());
            s_items.push_back(text(" "));
          }
        }
        form_lines.push_back(hbox(std::move(s_items)));
      }

      // Row 3: Destination Input, live resolution
      form_lines.push_back(hbox({
          text(" Dest:   ") | bold | vcenter,
          dest_input->Render() | size(WIDTH, EQUAL, 20) | color(Color::White) |
              borderLight,
          text(" " + dest_desc) |
              color(dest_valid ? Color::GreenLight : Color::YellowLight) |
              vcenter,
          filler(),
      }));

      // Row 4: Live Dest suggestions (as user types)
      if (!dst_matches.empty()) {
        std::vector<Element> d_items;
        d_items.push_back(text("  ↳ Dest options:   ") |
                          color(Color::YellowLight) | dim | vcenter);
        for (size_t i = 0; i < MAX_SUGGESTIONS; ++i) {
          if (i < dst_matches.size()) {
            d_items.push_back(dst_buttons[i]->Render());
            d_items.push_back(text(" "));
          }
        }
        form_lines.push_back(hbox(std::move(d_items)));
      }

      // Row 5: Date Tool, Flight Preference Picker, Status Message
      form_lines.push_back(hbox({
          text(" Date: ") | bold | vcenter,
          year_dropdown->Render() | size(WIDTH, EQUAL, 9),
          text(" "),
          month_dropdown->Render() | size(WIDTH, EQUAL, 13),
          text(" "),
          day_dropdown->Render() | size(WIDTH, EQUAL, 7),
          text("   View: ") | bold | color(Color::YellowLight) | vcenter,
          preference_toggle->Render() | color(Color::White) | vcenter,
          filler(),
          text(status_message + " ") | vcenter |
              (has_results ? color(Color::Green) : color(Color::RedLight)),
      }));

      auto form_window = window(
          text(" SEARCH & FLIGHT PREFERENCES ") | bold | color(Color::CyanLight),
          vbox(std::move(form_lines))) |
          color(Color::Cyan);

      // ---------------------------------------------------------------------
      // FIXED COMPACT TYPED FLIGHT NEEDS FILTER BAR
      // ---------------------------------------------------------------------
      bool has_active_filter = active_spec.has_any_filter() || !typed_filter_str.empty();

      std::string filter_badge =
          has_active_filter
              ? ("🔍 " + std::to_string(filtered_itineraries.size()) + "/" +
                 std::to_string(all_candidates.size()) + " match needs")
              : ("✈ All " + std::to_string(all_candidates.size()) + " flights shown");

      // Build live badge elements for recognized criteria
      std::vector<Element> parsed_badges;
      if (active_spec.max_stops == 0) {
        parsed_badges.push_back(text("[ 🛑 Non-stop ]") | bold | color(Color::GreenLight));
        parsed_badges.push_back(text(" "));
      } else if (active_spec.max_stops == 1) {
        parsed_badges.push_back(text("[ 🛑 ≤ 1 Stop ]") | bold | color(Color::YellowLight));
        parsed_badges.push_back(text(" "));
      } else if (active_spec.max_stops == 2) {
        parsed_badges.push_back(text("[ 🛑 ≤ 2 Stops ]") | bold | color(Color::YellowLight));
        parsed_badges.push_back(text(" "));
      }

      if (active_spec.max_price != -1) {
        parsed_badges.push_back(text("[ 🏷️ Max $" + std::to_string(active_spec.max_price) + " ]") | bold | color(Color::YellowLight));
        parsed_badges.push_back(text(" "));
      }

      if (!active_spec.target_airline_code.empty()) {
        std::string air_label = active_spec.target_airline_name.empty() ? active_spec.target_airline_code : active_spec.target_airline_name;
        if (air_label.size() > 14) air_label = air_label.substr(0, 13) + "…";
        parsed_badges.push_back(text("[ ✈ " + air_label + " ]") | bold | color(Color::CyanLight));
        parsed_badges.push_back(text(" "));
      }

      if (!active_spec.dep_time_label.empty()) {
        parsed_badges.push_back(text("[ 🌅 " + active_spec.dep_time_label + " ]") | bold | color(Color::YellowLight));
        parsed_badges.push_back(text(" "));
      }

      if (active_spec.max_duration_mins != -1) {
        int hrs = active_spec.max_duration_mins / 60;
        parsed_badges.push_back(text("[ ⏱ Under " + std::to_string(hrs) + "h ]") | bold | color(Color::MagentaLight));
        parsed_badges.push_back(text(" "));
      }

      Element badges_row;
      if (!parsed_badges.empty()) {
        badges_row = hbox({
            text("  Active Needs: ") | bold | color(Color::CyanLight) | vcenter,
            hbox(std::move(parsed_badges)) | vcenter,
        });
      } else if (!typed_filter_str.empty()) {
        badges_row = text("  ℹ️  No specific flight criteria recognized in '" + typed_filter_str + "'. Showing all flights.") | dim | color(Color::YellowLight);
      } else {
        badges_row = text("  💡 Tip: Type your needs (e.g. 'non-stop', 'under $800', 'Emirates', 'morning', '< 24h') or click a chip below") | dim;
      }

      auto filters_window = window(
          hbox({
              text(" 🎛️  TYPE YOUR NEEDS (FILTERS) ") | bold |
                  (has_active_filter ? color(Color::YellowLight)
                                     : color(Color::CyanLight)),
              filler(),
              text(" " + filter_badge + " ") |
                  (has_active_filter ? color(Color::YellowLight)
                                     : color(Color::GreenLight)) |
                  bold,
          }),
          vbox({
              hbox({
                  text(" Your Needs: ") | bold | vcenter,
                  filter_input->Render() | size(WIDTH, EQUAL, 52) | color(Color::White) | borderLight,
                  text(" "),
                  clear_filter_btn->Render() |
                      (has_active_filter ? color(Color::RedLight)
                                         : color(Color::GrayLight)),
                  filler(),
              }),
              badges_row,
              hbox({
                  text(" Quick Needs: ") | dim | vcenter,
                  chip_nonstop->Render(),
                  text(" "),
                  chip_1stop->Render(),
                  text(" "),
                  chip_p800->Render(),
                  text(" "),
                  chip_p1000->Render(),
                  text(" "),
                  chip_morning->Render(),
                  text(" "),
                  chip_dur24->Render(),
                  filler(),
              }),
          })) |
          color(has_active_filter ? Color::Yellow : Color::Cyan);

      // ---------------------------------------------------------------------
      // SCROLLABLE BODY: CUSTOMER SECTION + (OPTIONAL) RESEARCH SECTION
      // ---------------------------------------------------------------------
      std::vector<Element> customer_elements;
      std::vector<Element> research_elements;

      if (has_results) {
        if (!filtered_itineraries.empty()) {
          std::string best_key = serialize_path_key(best_flight.path);
          std::string cheap_key = serialize_path_key(cheapest_flight.path);
          std::string fast_key = serialize_path_key(fastest_flight.path);

          // 1. Dedicated Selected Flight Details Space (Immediately visible!)
          RoutingResult chosen_flight;
          if (selected_flight_idx >= 0 &&
              selected_flight_idx < (int)filtered_itineraries.size()) {
            chosen_flight = filtered_itineraries[selected_flight_idx];
          }

          if (chosen_flight.success) {
            std::string chosen_title;
            std::string chosen_reason;
            Color chosen_color;

            std::string pkey = serialize_path_key(chosen_flight.path);
            if (selected_preference == 1 && pkey == cheap_key) {
              chosen_title = "🏷️  CHEAPEST FLIGHT ITINERARY (OPTION #" +
                             std::to_string(selected_flight_idx + 1) +
                             " SELECTED)";
              chosen_reason = "Lowest ticket fare available on the network.";
              chosen_color = Color::YellowLight;
            } else if (selected_preference == 2 && pkey == fast_key) {
              chosen_title = "⚡ FASTEST FLIGHT ITINERARY (OPTION #" +
                             std::to_string(selected_flight_idx + 1) +
                             " SELECTED)";
              chosen_reason = "Absolute minimum elapsed travel duration.";
              chosen_color = Color::MagentaLight;
            } else if (pkey == best_key) {
              chosen_title = "⭐ RECOMMENDED BEST FLIGHT (OPTION #" +
                             std::to_string(selected_flight_idx + 1) +
                             " SELECTED)";
              chosen_reason = best_reason;
              chosen_color = Color::GreenLight;
            } else if (pkey == cheap_key) {
              chosen_title = "🏷️  CHEAPEST FLIGHT ITINERARY (OPTION #" +
                             std::to_string(selected_flight_idx + 1) +
                             " SELECTED)";
              chosen_reason = "Lowest ticket fare available on the network.";
              chosen_color = Color::YellowLight;
            } else if (pkey == fast_key) {
              chosen_title = "⚡ FASTEST FLIGHT ITINERARY (OPTION #" +
                             std::to_string(selected_flight_idx + 1) +
                             " SELECTED)";
              chosen_reason = "Absolute minimum elapsed travel duration.";
              chosen_color = Color::MagentaLight;
            } else {
              chosen_title = "✈ SELECTED FLIGHT ITINERARY — OPTION #" +
                             std::to_string(selected_flight_idx + 1) + " OF " +
                             std::to_string(filtered_itineraries.size());
              chosen_reason = "Selected custom flight choice. Full leg-by-leg "
                              "schedule breakdown below.";
              chosen_color = Color::CyanLight;
            }

            customer_elements.push_back(TUIViews::build_itinerary_card(
                chosen_title, chosen_flight, chosen_reason, loader,
                active_source_id, chosen_color, chosen_color, true));

            if (show_world_map) {
              customer_elements.push_back(
                  FlightPlanner::TUI::WorldMap::build_route_map(
                      chosen_flight, loader, active_source_id));
            }
          }

          // 2. Quick Summary Comparison Cards (Cheapest & Fastest)
          std::vector<Element> summary_cards;
          if (cheapest_flight.success) {
            summary_cards.push_back(
                window(
                    hbox({
                        text(" 🏷️  CHEAPEST ITINERARY ") | bold |
                            color(Color::Yellow),
                        filler(),
                        text("[Click to Inspect]") | dim |
                            color(Color::YellowLight),
                    }),
                    vbox({
                        hbox({
                            text(" Fare: ") | bold,
                            text("$" +
                                 std::to_string(cheapest_flight.total_price)) |
                                bold | color(Color::Yellow),
                            text("  │  Duration: ") | bold,
                            text(TUIViews::format_duration(
                                cheapest_flight.total_time_mins)) |
                                color(Color::CyanLight),
                            text("  │  Stops: ") | bold,
                            text(std::to_string(cheapest_flight.path.size() -
                                                1)) |
                                color(Color::White),
                        }),
                        separatorLight(),
                        text(" Route: " + TUIViews::build_route_summary(
                                              cheapest_flight, loader,
                                              active_source_id)) |
                            color(Color::GrayLight),
                    })) |
                color(Color::Yellow) | flex | reflect(cheapest_summary_box));
          }

          if (fastest_flight.success) {
            summary_cards.push_back(
                window(
                    hbox({
                        text(" ⚡ FASTEST ITINERARY ") | bold |
                            color(Color::MagentaLight),
                        filler(),
                        text("[Click to Inspect]") | dim |
                            color(Color::MagentaLight),
                    }),
                    vbox({
                        hbox({
                            text(" Fare: ") | bold,
                            text("$" +
                                 std::to_string(fastest_flight.total_price)) |
                                bold | color(Color::Yellow),
                            text("  │  Duration: ") | bold,
                            text(TUIViews::format_duration(
                                fastest_flight.total_time_mins)) |
                                bold | color(Color::MagentaLight),
                            text("  │  Stops: ") | bold,
                            text(std::to_string(fastest_flight.path.size() -
                                                1)) |
                                color(Color::White),
                        }),
                        separatorLight(),
                        text(" Route: " + TUIViews::build_route_summary(
                                              fastest_flight, loader,
                                              active_source_id)) |
                            color(Color::GrayLight),
                    })) |
                color(Color::MagentaLight) | flex |
                reflect(fastest_summary_box));
          }

          if (!summary_cards.empty()) {
            customer_elements.push_back(hbox(std::move(summary_cards)));
          }

          // 3. Clickable Flight Options List Window
          std::vector<Element> option_rows;
          if (option_boxes.size() < filtered_itineraries.size()) {
            option_boxes.resize(filtered_itineraries.size());
          }

          for (size_t i = 0; i < filtered_itineraries.size(); ++i) {
            const auto &res = filtered_itineraries[i];
            bool is_selected = (static_cast<int>(i) == selected_flight_idx);
            std::string pkey = serialize_path_key(res.path);

            std::string badge_text;
            Color badge_color = Color::White;
            if (pkey == best_key) {
              badge_text = " ⭐ Best Value ";
              badge_color = Color::GreenLight;
            } else if (pkey == cheap_key) {
              badge_text = " 🏷️ Cheapest   ";
              badge_color = Color::YellowLight;
            } else if (pkey == fast_key) {
              badge_text = " ⚡ Fastest    ";
              badge_color = Color::MagentaLight;
            } else {
              badge_text = " Option #" + std::to_string(i + 1) + "     ";
              badge_color = Color::CyanLight;
            }

            std::string stops_str =
                (res.path.size() <= 1)
                    ? "Direct"
                    : (std::to_string(res.path.size() - 1) +
                       (res.path.size() == 2 ? " Stop " : " Stops"));

            std::string route_str =
                TUIViews::build_route_summary(res, loader, active_source_id);

            std::string airlines_str;
            std::set<std::string> leg_airlines;
            for (const auto &f : res.path) {
              if (f.flight_details_id < loader.ui_flights.size()) {
                leg_airlines.insert(
                    loader.ui_flights[f.flight_details_id].airline_code);
              }
            }
            for (const auto &code : leg_airlines) {
              if (!airlines_str.empty())
                airlines_str += "/";
              airlines_str += code;
            }
            if (!airlines_str.empty()) {
              airlines_str = " (" + airlines_str + ")";
            }

            auto row_content = hbox({
                opt_buttons[i]->Render() |
                    (is_selected ? (bold | color(Color::GreenLight))
                                 : color(Color::CyanLight)),
                text(" "),
                text(badge_text) | bold | color(badge_color),
                text("  │  ") | dim,
                text("$" + std::to_string(res.total_price)) | bold |
                    (is_selected ? color(Color::YellowLight)
                                 : color(Color::Yellow)),
                text("  │  ") | dim,
                text(TUIViews::format_duration(res.total_time_mins)) | bold |
                    (is_selected ? color(Color::CyanLight) : color(Color::Cyan)),
                text("  │  ") | dim,
                text(stops_str) |
                    (is_selected ? (bold | color(Color::White))
                                 : color(Color::GrayLight)),
                text("  │  ") | dim,
                text(route_str + airlines_str) |
                    (is_selected ? (bold | color(Color::White))
                                 : color(Color::GrayLight)),
                filler(),
            });

            Element decorated_row;
            if (is_selected) {
              decorated_row = row_content | bgcolor(Color::RGB(15, 35, 45)) |
                              reflect(option_boxes[i]);
            } else {
              decorated_row = row_content | reflect(option_boxes[i]);
            }

            option_rows.push_back(decorated_row);
            if (i + 1 < filtered_itineraries.size()) {
              option_rows.push_back(separatorLight());
            }
          }

          customer_elements.push_back(
              window(
                  hbox({
                      text(" ✈ ALL FLIGHT OPTIONS (" +
                           std::to_string(filtered_itineraries.size()) +
                           " Available — Click any option to view full details) ") |
                          bold | color(Color::CyanLight),
                      filler(),
                      text(" Click any option to inspect ") | dim,
                  }),
                  vbox(std::move(option_rows))) |
              color(Color::Cyan));
        } else {
        customer_elements.push_back(
            window(
                text(" ⚠️  NO FLIGHTS MATCH CURRENT FILTERS ") | bold |
                    color(Color::YellowLight),
                vbox({
                    text(" None of the " +
                         std::to_string(all_candidates.size()) +
                         " available itineraries match your active filter criteria:") |
                        color(Color::White),
                    text("   • " + get_active_filters_description()) |
                        color(Color::YellowLight) | bold,
                    text(" "),
                    text(" Tip: Click [ ↺ Clear ] on the filter bar above or edit your needs.") |
                        color(Color::GrayLight),
                })) |
            color(Color::YellowLight));
        }

        // ===================================================================
        // RESEARCHER MODE SECTION (REVEALED AT THE BOTTOM WHEN TOGGLED ON)
        // ===================================================================
        if (show_research_mode && !researcher_benchmarks.empty()) {
          research_elements.push_back(text(" "));
          research_elements.push_back(
              hbox({
                  text(" ═══════════ 🔬 RESEARCHER MODE: ALGORITHMIC BENCHMARK "
                       "LAB ═══════════ ") |
                      bold | color(Color::CyanLight),
              }) |
              center);

          // 1. Compact-Width FTXUI Benchmark Table
          std::vector<std::vector<std::string>> table_data;
          table_data.push_back(
              {"Algorithm", "Mode", "Time", "Nodes", "Edges", "Price", "Dur."});

          std::string last_algo = "";
          long max_time_us = 1;

          for (const auto &bench : researcher_benchmarks) {
            if (bench.compute_time_us > max_time_us)
              max_time_us = bench.compute_time_us;

            std::string display_algo = "";
            if (bench.algo_name != last_algo) {
              display_algo = bench.algo_name;
              last_algo = bench.algo_name;
            }

            std::string time_str;
            if (bench.compute_time_us >= 1000000) {
              std::ostringstream oss;
              oss << std::fixed << std::setprecision(2)
                  << (bench.compute_time_us / 1000000.0) << " s";
              time_str = oss.str();
            } else {
              std::ostringstream oss;
              oss << std::fixed << std::setprecision(2)
                  << (bench.compute_time_us / 1000.0) << " ms";
              time_str = oss.str();
            }

            std::string cost_str =
                bench.result.success
                    ? ("$" + std::to_string(bench.result.total_price))
                    : "N/A";
            std::string journey_str =
                bench.result.success
                    ? TUIViews::format_duration(bench.result.total_time_mins)
                    : "N/A";
            std::string nodes_str =
                bench.result.success
                    ? std::to_string(bench.result.metrics.nodes_explored)
                    : "N/A";
            std::string edges_str =
                bench.result.success
                    ? std::to_string(bench.result.metrics.edges_relaxed)
                    : "N/A";

            table_data.push_back({display_algo, bench.mode_label, time_str,
                                  nodes_str, edges_str, cost_str, journey_str});
          }

          auto ftx_table = Table(table_data);
          ftx_table.SelectAll().Border(LIGHT);
          ftx_table.SelectAll().SeparatorVertical(LIGHT);
          ftx_table.SelectRow(0).Decorate(bold);
          ftx_table.SelectRow(0).Border(DOUBLE);
          ftx_table.SelectRow(0).DecorateCells(color(Color::CyanLight));

          for (size_t r = 1; r < table_data.size(); ++r) {
            ftx_table.SelectCell(0, r).Decorate(bold);
            ftx_table.SelectCell(2, r).Decorate(color(Color::GreenLight));
            ftx_table.SelectCell(5, r).Decorate(color(Color::YellowLight));
            ftx_table.SelectCell(6, r).Decorate(color(Color::MagentaLight));
          }

          research_elements.push_back(
              window(text(" ALGORITHM BENCHMARK EXECUTION MATRIX ") | bold |
                         color(Color::GreenLight),
                     ftx_table.Render() | hcenter) |
              color(Color::GreenLight));

          // 2. Geographic Route Trajectory on World Map
          if (show_world_map && best_flight.success) {
            research_elements.push_back(
                FlightPlanner::TUI::WorldMap::build_route_map(
                    best_flight, loader, active_source_id));
          }

          // 3. Visual Speed Profiler Gauges
          std::vector<Element> visual_gauges;
          for (const auto &bench : researcher_benchmarks) {
            float ratio = (max_time_us > 0)
                              ? (float)bench.compute_time_us / (float)max_time_us
                              : 0.0f;
            if (ratio < 0.02f)
              ratio = 0.02f;

            std::string label = bench.algo_name + " [" + bench.mode_label + "]";
            if (label.size() < 26)
              label += std::string(26 - label.size(), ' ');
            else
              label = label.substr(0, 26);

            std::string time_str;
            if (bench.compute_time_us >= 1000000) {
              std::ostringstream oss;
              oss << std::fixed << std::setprecision(2)
                  << (bench.compute_time_us / 1000000.0) << " s";
              time_str = oss.str();
            } else {
              std::ostringstream oss;
              oss << std::fixed << std::setprecision(2)
                  << (bench.compute_time_us / 1000.0) << " ms";
              time_str = oss.str();
            }

            visual_gauges.push_back(hbox({
                text(" " + label) | color(Color::GrayLight),
                gauge(ratio) | color(Color::Cyan) | flex,
                text(" " + time_str) | bold | color(Color::GreenLight) |
                    size(WIDTH, EQUAL, 11),
            }));
          }

          research_elements.push_back(
              window(text(" VISUAL RUNTIME PROFILER (RELATIVE SPEED) ") | bold |
                         color(Color::YellowLight),
                     vbox(std::move(visual_gauges))) |
              color(Color::YellowLight));

          // 3. Yen's K-SP & Economic Analysis
          if (best_flight.success) {
            std::ostringstream oss_rec;
            oss_rec << " Recommended: $" << best_flight.total_price << " ("
                    << TUIViews::format_duration(best_flight.total_time_mins)
                    << ", "
                    << (best_flight.path.empty() ? 0
                                                 : best_flight.path.size() - 1)
                    << " transits) | " << best_reason;

            std::ostringstream oss_gen;
            oss_gen << " Generalized Economic Cost: $" << std::fixed
                    << std::setprecision(2) << best_gen_cost
                    << " (Value of Time: $25.00/hr, Layover Penalty: $15/stop)";

            std::vector<Element> yen_rows;
            yen_rows.push_back(text(oss_rec.str()) | bold |
                               color(Color::GreenLight));
            yen_rows.push_back(text(oss_gen.str()) | color(Color::YellowLight));
            yen_rows.push_back(separatorLight());

            for (size_t i = 0; i < yen_k_paths.size(); ++i) {
              const auto &res = yen_k_paths[i];
              yen_rows.push_back(hbox({
                  text(" Alt #" + std::to_string(i + 1) + ": ") | bold |
                      color(Color::White),
                  text("$" + std::to_string(res.total_price)) | bold |
                      color(Color::Yellow),
                  text(" │ " + TUIViews::format_duration(res.total_time_mins)) |
                      color(Color::CyanLight),
                  text(" │ " + std::to_string(res.path.size() - 1) + " stops") |
                      color(Color::GrayLight),
                  text(" │ " + TUIViews::build_route_summary(
                                   res, loader, active_source_id)) |
                      color(Color::White),
              }));
            }

            research_elements.push_back(
                window(text(" YEN'S K-SP & ECONOMIC TRADE-OFF ANALYSIS ") |
                           bold | color(Color::MagentaLight),
                       vbox(std::move(yen_rows))) |
                color(Color::MagentaLight));
          }

          // 4. Algorithm Consensus Equivalence Groups
          {
            struct ConsensusGroup {
              RoutingResult res;
              std::vector<std::string> algos;
            };
            std::vector<std::string> order;
            std::unordered_map<std::string, ConsensusGroup> groups;

            for (const auto &bench : researcher_benchmarks) {
              if (!bench.result.success)
                continue;
              std::string key = serialize_path_key(bench.result.path);
              if (groups.find(key) == groups.end()) {
                order.push_back(key);
                groups[key] = {bench.result, {}};
              }
              groups[key].algos.push_back(bench.algo_name + "[" +
                                          bench.mode_label + "]");
            }

            std::vector<Element> cons_rows;
            for (size_t g = 0; g < order.size(); ++g) {
              const auto &grp = groups[order[g]];
              std::string route_s = TUIViews::build_route_summary(
                  grp.res, loader, active_source_id);

              // Wrap matched algorithms into fixed-width text lines so
              // ComputeRequirement().min_y reports exact row count
              std::vector<Element> group_lines;
              group_lines.push_back(hbox({
                  text(" Route Group #" + std::to_string(g + 1) + ": ") | bold |
                      color(Color::CyanLight),
                  text("$" + std::to_string(grp.res.total_price)) | bold |
                      color(Color::Yellow),
                  text(" │ " +
                       TUIViews::format_duration(grp.res.total_time_mins)) |
                      color(Color::GreenLight),
                  text(" │ " + route_s) | color(Color::White),
              }));

              std::string curr_line = "   Matched by: ";
              for (size_t a = 0; a < grp.algos.size(); ++a) {
                std::string token = (a > 0 ? ", " : "") + grp.algos[a];
                if (curr_line.size() + token.size() > 92 &&
                    curr_line.size() > 16) {
                  group_lines.push_back(text(curr_line) |
                                        color(Color::GrayLight));
                  curr_line = "               " + grp.algos[a];
                } else {
                  curr_line += token;
                }
              }
              if (!curr_line.empty()) {
                group_lines.push_back(text(curr_line) |
                                      color(Color::GrayLight));
              }
              if (g + 1 < order.size()) {
                group_lines.push_back(separatorLight());
              }
              cons_rows.push_back(vbox(std::move(group_lines)));
            }

            research_elements.push_back(
                window(text(" ALGORITHM CONSENSUS GROUPS ") | bold |
                           color(Color::Cyan),
                       vbox(std::move(cons_rows))) |
                color(Color::Cyan));
          }

          // Bottom padding so last row is clearly visible
          research_elements.push_back(
              text(" ─── End of Research Report ─── ") | dim | hcenter);
        }
      } else {
        if (show_world_map && active_source_id != (uint32_t)-1 &&
            active_dest_id != (uint32_t)-1) {
          customer_elements.push_back(
              FlightPlanner::TUI::WorldMap::build_preview_map(
                  active_source_id, active_dest_id, loader));
        }
        customer_elements.push_back(
            window(text(" NO ROUTES FOUND ") | bold | color(Color::RedLight),
                   vbox({
                       text(" No scheduled flight connections found for " +
                            source_str + " ➔ " + dest_str + " on " + target_date_str) |
                           color(Color::White),
                       text(" Tip: Try adjusting the travel date or airport code.") |
                           color(Color::GrayLight),
                   })) |
            color(Color::Red));
      }

      Element customer_block = vbox(std::move(customer_elements));
      customer_block->ComputeRequirement();
      customer_height_ = customer_block->requirement().min_y;

      std::vector<Element> scroll_body;
      scroll_body.push_back(customer_block);
      for (auto &el : research_elements) {
        scroll_body.push_back(std::move(el));
      }

      Element scroll_content = vbox(std::move(scroll_body));
      scroll_content->ComputeRequirement();
      content_height_ = scroll_content->requirement().min_y;

      // Exact zero-dead-zone focus positioning for yframe:
      // Frame::SetBox computes dy = target_y - external_dimy / 2.
      // Setting target_y = scroll_line + external_dimy / 2 gives dy == scroll_line!
      int ext_dimy = get_external_dimy();
      int target_focus_y = scroll_line + ext_dimy / 2;

      Element scrollable_viewport =
          scroll_content |
          focusPosition(0, target_focus_y) |
          vscroll_indicator |
          yframe |
          flex |
          reflect(viewport_box);

      // ---------------------------------------------------------------------
      // FIXED BOTTOM SCROLL NAVIGATION FOOTER
      // ---------------------------------------------------------------------
      int max_sc = get_max_scroll();
      int clamped_sc = std::clamp(scroll_line, 0, max_sc);
      int pct = (max_sc > 0) ? (clamped_sc * 100 / max_sc) : 100;

      auto footer_bar = hbox({
          scroll_top_btn->Render() | color(Color::CyanLight),
          text(" "),
          scroll_up_btn->Render() | color(Color::CyanLight),
          text(" "),
          scroll_down_btn->Render() | color(Color::CyanLight),
          text(" "),
          scroll_bottom_btn->Render() | color(Color::CyanLight),
          text("  "),
          jump_research_btn->Render() | color(Color::YellowLight),
          filler(),
          text(" Scroll: " + std::to_string(pct) + "%  │  Mouse Wheel / PgUp / "
               "PgDn  │  Esc: Exit ") |
              dim,
      });

      return vbox({
          top_bar,
          form_window,
          filters_window,
          scrollable_viewport,
          separatorLight(),
          footer_bar,
      });
    });

    // Global event handler for smooth mouse wheel, scrollbar click/drag, & keys
    auto event_handler = CatchEvent(renderer, [&](Event event) {
      if (event == Event::Escape) {
        screen.Exit();
        return true;
      }

      // Mouse wheel & scrollbar dragging inside the results viewport
      if (event.is_mouse()) {
        auto &m = event.mouse();
        if (m.button == Mouse::WheelDown) {
          if (viewport_box.Contain(m.x, m.y)) {
            scroll_line = std::min(get_max_scroll(), scroll_line + 3);
            return true;
          }
        }
        if (m.button == Mouse::WheelUp) {
          if (viewport_box.Contain(m.x, m.y)) {
            scroll_line = std::max(0, scroll_line - 3);
            return true;
          }
        }
        // Click or drag directly on the right-edge scrollbar
        if (m.button == Mouse::Left && m.motion == Mouse::Pressed &&
            m.x >= viewport_box.x_max - 1 && viewport_box.Contain(m.x, m.y)) {
          int vp_h = std::max(1, viewport_box.y_max - viewport_box.y_min);
          float frac = float(m.y - viewport_box.y_min) / float(vp_h);
          scroll_line =
              int(std::clamp(frac, 0.0f, 1.0f) * float(get_max_scroll()));
          return true;
        }

        // Left click on summary comparison cards or any flight option row
        if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
          if (cheapest_summary_box.Contain(m.x, m.y)) {
            selected_preference = 1;
            last_preference = 1;
            sync_selected_flight_from_preference();
            return true;
          }
          if (fastest_summary_box.Contain(m.x, m.y)) {
            selected_preference = 2;
            last_preference = 2;
            sync_selected_flight_from_preference();
            return true;
          }
          for (size_t i = 0; i < option_boxes.size() && i < filtered_itineraries.size(); ++i) {
            if (option_boxes[i].Contain(m.x, m.y)) {
              selected_flight_idx = static_cast<int>(i);
              return true;
            }
          }
        }
      }

      // PageUp / PageDown / Home / End always scroll the results viewport
      int page_step = std::max(5, get_external_dimy() - 2);
      if (event == Event::PageDown) {
        scroll_line = std::min(get_max_scroll(), scroll_line + page_step);
        return true;
      }
      if (event == Event::PageUp) {
        scroll_line = std::max(0, scroll_line - page_step);
        return true;
      }
      if (event == Event::Home && !source_input->Focused() &&
          !dest_input->Focused() && !filter_input->Focused()) {
        scroll_line = 0;
        return true;
      }
      if (event == Event::End && !source_input->Focused() &&
          !dest_input->Focused() && !filter_input->Focused()) {
        scroll_line = get_max_scroll();
        return true;
      }

      // 'j' / 'k' vim-style scroll & 'q' exit ONLY when not typing in an input
      if (!source_input->Focused() && !dest_input->Focused() && !filter_input->Focused()) {
        if (event == Event::Character('j')) {
          scroll_line = std::min(get_max_scroll(), scroll_line + 3);
          return true;
        }
        if (event == Event::Character('k')) {
          scroll_line = std::max(0, scroll_line - 3);
          return true;
        }
        if (event == Event::Character('q')) {
          screen.Exit();
          return true;
        }
      }

      return false;
    });

    screen.Loop(event_handler);
  }
};
