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
#include "TUI/CalendarPicker.hpp"
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

  // Trip Types
  enum class TripType { ROUND_TRIP = 0, ONE_WAY = 1, MULTI_CITY = 2 };
  int selected_trip_type = 0; // Default: Round trip (0: Round trip, 1: One-way, 2: Multi-city)
  int last_trip_type = 0;
  std::vector<std::string> trip_type_entries = {
      " ⇄ Round trip ", " ➔ One-way ", " 🔀 Multi-city "};

  // Search parameters (One-way / Round-trip primary)
  std::string source_str;
  std::string dest_str;
  std::string target_date_str;

  // Live Airport Search & Suggestion Options
  std::vector<AirportSearch::AirportMatch> src_matches;
  std::vector<AirportSearch::AirportMatch> dst_matches;
  constexpr static size_t MAX_SUGGESTIONS = 4;

  void update_suggestions() {
    src_matches =
        AirportSearch::search_airports(source_str, loader, MAX_SUGGESTIONS);
    dst_matches =
        AirportSearch::search_airports(dest_str, loader, MAX_SUGGESTIONS);
  }

  // Calendar Picker Integration
  FlightPlanner::TUI::CalendarPicker calendar_picker;
  std::string dep_date_str = "2026-11-15";
  std::string ret_date_str = "2026-11-22";
  std::string dep_date_btn_label = " 🛫 Dep: Sun, Nov 15 ";
  std::string ret_date_btn_label = " 🛬 Ret: Sun, Nov 22 ";

  void update_date_btn_labels() {
    if (selected_trip_type == 1) { // One-way
      dep_date_btn_label =
          " 📅 " +
          FlightPlanner::TUI::CalendarPicker::format_iso_to_short(dep_date_str) +
          " ";
    } else {
      dep_date_btn_label =
          " 🛫 Dep: " +
          FlightPlanner::TUI::CalendarPicker::format_iso_to_short(dep_date_str) +
          " ";
      ret_date_btn_label =
          " 🛬 Ret: " +
          FlightPlanner::TUI::CalendarPicker::format_iso_to_short(ret_date_str) +
          " ";
    }
  }

  // Dynamic Mouse Cursor Management (OSC 22: "text", "pointer", "default")
  std::string current_mouse_cursor;
  void set_mouse_cursor(const std::string &shape) {
    if (shape == current_mouse_cursor)
      return;
    current_mouse_cursor = shape;
    if (shape.empty()) {
      std::cout << "\033]22;\033\\" << std::flush;
    } else {
      std::cout << "\033]22;" << shape << "\033\\" << std::flush;
    }
  }

  // Multi-city Leg Representation (up to 4 segments)
  struct MultiCityLeg {
    std::string origin;
    std::string dest;
    std::string date_str;
    std::string date_btn_label;
    uint32_t src_id = 0;
    uint32_t dst_id = 0;
    std::vector<AirportSearch::AirportMatch> src_matches;
    std::vector<AirportSearch::AirportMatch> dst_matches;
    std::vector<RoutingResult> candidates;
    std::vector<RoutingResult> filtered;
    int selected_idx = 0;
  };
  std::vector<MultiCityLeg> multi_city_legs;
  int active_multi_leg_idx = 0;
  int num_active_legs = 2;

  // Round-trip state
  int active_roundtrip_tab = 0; // 0: Departing, 1: Returning
  std::string tab_outbound_label = " 🛫 1. Departing Flight ";
  std::string tab_return_label = " 🛬 2. Returning Flight ";
  std::vector<RoutingResult> outbound_candidates;
  std::vector<RoutingResult> outbound_filtered;
  int outbound_selected_idx = 0;

  std::vector<RoutingResult> return_candidates;
  std::vector<RoutingResult> return_filtered;
  int return_selected_idx = 0;

  uint32_t return_source_id = 0;
  uint32_t return_dest_id = 0;

  // Flight Picker Preference (0: Best Recommended, 1: Cheapest, 2: Fastest)
  int selected_preference = 0;
  int last_preference = 0;
  int selected_flight_idx = 0;

  // Tracked Interactive Bounding Boxes (Hover Cursor & Focus/Click Precision)
  std::vector<ftxui::Box> option_boxes;
  ftxui::Box cheapest_summary_box;
  ftxui::Box fastest_summary_box;
  ftxui::Box swap_box;
  ftxui::Box dep_date_box;
  ftxui::Box ret_date_box;
  ftxui::Box tab_outbound_box;
  ftxui::Box tab_return_box;
  ftxui::Box leg_tab_boxes[4];
  ftxui::Box leg_date_boxes[4];
  ftxui::Box leg_src_boxes[4];
  ftxui::Box leg_dst_boxes[4];
  ftxui::Box src_input_box;
  ftxui::Box dst_input_box;
  ftxui::Box filter_input_box;
  ftxui::Box map_toggle_box;
  ftxui::Box research_toggle_box;
  ftxui::Box exit_box;
  ftxui::Box trip_type_box;
  ftxui::Box preference_box;
  ftxui::Box search_box;
  ftxui::Box src_sugg_boxes[MAX_SUGGESTIONS];
  ftxui::Box dst_sugg_boxes[MAX_SUGGESTIONS];
  ftxui::Box add_leg_box;
  ftxui::Box remove_leg_box;
  ftxui::Box clear_filter_box;
  ftxui::Box chip_boxes[6];
  ftxui::Box footer_boxes[5];

  std::vector<std::string> preference_entries = {
      " ⭐ Best Value ", " 🏷️  Cheapest ", " ⚡ Fastest "};

  // Convert dataset flight_day (days since 2026-10-01) back to YYYY-MM-DD
  static std::string flight_day_to_iso(int flight_day) {
    int y = 2026;
    int m = 10;
    int d = 1 + std::max(0, flight_day);
    while (true) {
      int dim = FlightPlanner::TUI::CalendarPicker::days_in_month(y, m);
      if (d <= dim)
        break;
      d -= dim;
      if (++m > 12) {
        m = 1;
        y++;
      }
    }
    return FlightPlanner::TUI::CalendarPicker::format_date_iso(y, m, d);
  }

  // Returns {first_leg_departure_utc_mins, last_leg_arrival_utc_mins}
  std::pair<uint32_t, uint32_t>
  get_itinerary_utc_bounds(const RoutingResult &res, uint32_t src_id) const {
    if (!res.success || res.path.empty() || src_id >= loader.nodes.size()) {
      return {0, 0};
    }
    uint32_t curr_node = src_id;
    uint32_t first_dep_utc = 0;
    uint32_t last_arr_utc = 0;
    for (size_t i = 0; i < res.path.size(); ++i) {
      const flight &f = res.path[i];
      if (curr_node >= loader.nodes.size())
        break;
      int32_t orig_offset = loader.nodes[curr_node].utc_offset_15m * 15;
      uint32_t dep_utc = static_cast<uint32_t>(
          static_cast<int32_t>(f.flight_day * 1440 + f.departure_time) -
          orig_offset);
      uint32_t arr_utc = dep_utc + f.duration_mins;
      if (i == 0)
        first_dep_utc = dep_utc;
      last_arr_utc = arr_utc;
      curr_node = f.destination_id;
    }
    return {first_dep_utc, last_arr_utc};
  }

  // Minimum UTC departure time required for return flight after ongoing lands
  uint32_t get_outbound_min_return_utc() const {
    if (outbound_filtered.empty() || outbound_selected_idx < 0 ||
        outbound_selected_idx >= static_cast<int>(outbound_filtered.size()) ||
        return_source_id >= loader.nodes.size()) {
      return 0;
    }
    auto bounds = get_itinerary_utc_bounds(
        outbound_filtered[outbound_selected_idx], active_source_id);
    if (bounds.second == 0)
      return 0;
    return bounds.second + loader.nodes[return_source_id].min_transfer;
  }

  // Minimum allowed return ISO date based on ongoing departure & arrival
  std::string get_min_return_date_iso() const {
    std::string min_iso = dep_date_str;
    if (!outbound_filtered.empty() && outbound_selected_idx >= 0 &&
        outbound_selected_idx < static_cast<int>(outbound_filtered.size()) &&
        active_dest_id < loader.nodes.size()) {
      auto bounds = get_itinerary_utc_bounds(
          outbound_filtered[outbound_selected_idx], active_source_id);
      if (bounds.second > 0) {
        int32_t dst_offset = loader.nodes[active_dest_id].utc_offset_15m * 15;
        int32_t local_arr_mins = static_cast<int32_t>(bounds.second) + dst_offset;
        int arr_day = local_arr_mins >= 0 ? (local_arr_mins / 1440) : 0;
        std::string arr_iso = flight_day_to_iso(arr_day);
        if (arr_iso > min_iso)
          min_iso = arr_iso;
      }
    }
    return min_iso;
  }

  bool is_valid_return_after_outbound(const RoutingResult &ret_res) const {
    uint32_t min_ret_utc = get_outbound_min_return_utc();
    if (min_ret_utc == 0)
      return true;
    auto ret_bounds = get_itinerary_utc_bounds(ret_res, return_source_id);
    return ret_bounds.first >= min_ret_utc;
  }

  uint32_t get_leg_min_departure_utc(int leg_idx) const {
    if (leg_idx <= 0 || leg_idx >= num_active_legs)
      return 0;
    const auto &prev = multi_city_legs[leg_idx - 1];
    if (prev.filtered.empty() || prev.selected_idx < 0 ||
        prev.selected_idx >= static_cast<int>(prev.filtered.size()) ||
        multi_city_legs[leg_idx].src_id >= loader.nodes.size()) {
      return 0;
    }
    auto bounds = get_itinerary_utc_bounds(prev.filtered[prev.selected_idx],
                                           prev.src_id);
    if (bounds.second == 0)
      return 0;
    return bounds.second +
           loader.nodes[multi_city_legs[leg_idx].src_id].min_transfer;
  }

  // World Map Visualization State
  bool show_world_map = true;
  std::string world_map_btn_label = " 🗺️  World Map: ON ";

  // Typed Flight Needs & Filters State
  std::string typed_filter_str;
  FilterSpec active_spec;
  std::vector<std::string> airline_entries = {"All Airlines"};
  std::vector<std::string> airline_codes = {"ALL"};

  // Candidate pool & filtered itineraries for currently viewed flight leg
  std::vector<RoutingResult> all_candidates;
  std::vector<RoutingResult> filtered_itineraries;

  bool matches_filters(const RoutingResult &res) const {
    if (!res.success || res.path.empty())
      return false;

    // 1. Stops / Transits Filter
    size_t stops = res.path.size() - 1;
    if (active_spec.max_stops != -1 &&
        stops > static_cast<size_t>(active_spec.max_stops)) {
      return false;
    }

    // 2. Airline Filter
    if (!active_spec.target_airline_code.empty()) {
      bool found = false;
      for (const auto &f : res.path) {
        if (f.flight_details_id < loader.ui_flights.size()) {
          const std::string &code =
              loader.ui_flights[f.flight_details_id].airline_code;
          if (code == active_spec.target_airline_code ||
              (!active_spec.target_airline_name.empty() &&
               loader.get_airline_name(code) ==
                   active_spec.target_airline_name)) {
            found = true;
            break;
          }
        }
      }
      if (!found)
        return false;
    }

    // 3. Price Filter
    if (active_spec.max_price != -1 && res.total_price > active_spec.max_price) {
      return false;
    }

    // 4. Departure Time Filter (Local departure minute: 0..1439)
    if (active_spec.dep_time_start_min != -1 &&
        active_spec.dep_time_end_min != -1) {
      uint16_t dep_time = res.path.front().departure_time;
      if (dep_time < active_spec.dep_time_start_min ||
          dep_time >= active_spec.dep_time_end_min) {
        return false;
      }
    }

    // 5. Max Duration Filter
    if (active_spec.max_duration_mins != -1 &&
        res.total_time_mins > active_spec.max_duration_mins) {
      return false;
    }

    return true;
  }

  void update_best_cheapest_fastest(const std::vector<RoutingResult> &pool) {
    if (!pool.empty()) {
      cheapest_flight = pool[0];
      for (const auto &res : pool) {
        if (res.total_price < cheapest_flight.total_price) {
          cheapest_flight = res;
        }
      }

      fastest_flight = pool[0];
      for (const auto &res : pool) {
        if (res.total_time_mins < fastest_flight.total_time_mins) {
          fastest_flight = res;
        }
      }

      best_flight = pool[0];
      double best_score = std::numeric_limits<double>::max();
      for (const auto &res : pool) {
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

      yen_alternatives.clear();
      std::string chosen_key = serialize_path_key(
          (selected_preference == 0)
              ? best_flight.path
              : (selected_preference == 1) ? cheapest_flight.path
                                           : fastest_flight.path);

      for (const auto &res : pool) {
        if (serialize_path_key(res.path) != chosen_key) {
          yen_alternatives.push_back(res);
        }
      }

      if (selected_flight_idx < 0 ||
          selected_flight_idx >= static_cast<int>(pool.size())) {
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

  void apply_filters() {
    std::vector<std::pair<std::string, std::string>> available_airlines;
    for (const auto &code : airline_codes) {
      if (code != "ALL") {
        available_airlines.push_back({code, loader.get_airline_name(code)});
      }
    }
    for (const auto &entry : loader.airline_names) {
      available_airlines.push_back({entry.first, entry.second});
    }

    active_spec =
        FilterParser::parse_typed_filter(typed_filter_str, available_airlines);

    if (active_spec.preference_override != -1) {
      selected_preference = active_spec.preference_override;
    }

    if (selected_trip_type == 1) { // One-way
      filtered_itineraries.clear();
      for (const auto &res : all_candidates) {
        if (matches_filters(res))
          filtered_itineraries.push_back(res);
      }
      update_best_cheapest_fastest(filtered_itineraries);
    } else if (selected_trip_type == 0) { // Round-trip
      outbound_filtered.clear();
      for (const auto &res : outbound_candidates) {
        if (matches_filters(res))
          outbound_filtered.push_back(res);
      }
      if (outbound_filtered.empty()) {
        outbound_selected_idx = -1;
      } else if (outbound_selected_idx < 0 ||
                 outbound_selected_idx >=
                     static_cast<int>(outbound_filtered.size())) {
        outbound_selected_idx = 0;
      }

      return_filtered.clear();
      for (const auto &res : return_candidates) {
        if (matches_filters(res) && is_valid_return_after_outbound(res))
          return_filtered.push_back(res);
      }
      if (return_filtered.empty()) {
        return_selected_idx = -1;
      } else if (return_selected_idx < 0 ||
                 return_selected_idx >=
                     static_cast<int>(return_filtered.size())) {
        return_selected_idx = 0;
      }

      if (active_roundtrip_tab == 0) {
        all_candidates = outbound_candidates;
        filtered_itineraries = outbound_filtered;
        selected_flight_idx = outbound_selected_idx;
      } else {
        all_candidates = return_candidates;
        filtered_itineraries = return_filtered;
        selected_flight_idx = return_selected_idx;
      }
      update_best_cheapest_fastest(filtered_itineraries);
    } else { // Multi-city
      for (int i = 0; i < num_active_legs; ++i) {
        multi_city_legs[i].filtered.clear();
        uint32_t min_dep_utc = get_leg_min_departure_utc(i);
        for (const auto &res : multi_city_legs[i].candidates) {
          if (!matches_filters(res))
            continue;
          if (min_dep_utc > 0) {
            auto b = get_itinerary_utc_bounds(res, multi_city_legs[i].src_id);
            if (b.first < min_dep_utc)
              continue;
          }
          multi_city_legs[i].filtered.push_back(res);
        }
        if (multi_city_legs[i].filtered.empty()) {
          multi_city_legs[i].selected_idx = -1;
        } else if (multi_city_legs[i].selected_idx < 0 ||
                   multi_city_legs[i].selected_idx >=
                       static_cast<int>(multi_city_legs[i].filtered.size())) {
          multi_city_legs[i].selected_idx = 0;
        }
      }
      if (active_multi_leg_idx < num_active_legs) {
        all_candidates = multi_city_legs[active_multi_leg_idx].candidates;
        filtered_itineraries = multi_city_legs[active_multi_leg_idx].filtered;
        selected_flight_idx =
            multi_city_legs[active_multi_leg_idx].selected_idx;
      }
      update_best_cheapest_fastest(filtered_itineraries);
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
          on_select_flight_option(selected_flight_idx);
          return;
        }
      }
    }
    selected_flight_idx = 0;
    on_select_flight_option(selected_flight_idx);
  }

  void on_select_flight_option(int idx) {
    if (idx < 0 || idx >= (int)filtered_itineraries.size())
      return;
    selected_flight_idx = idx;

    if (selected_trip_type == 0) { // Round-trip
      if (active_roundtrip_tab == 0) {
        outbound_selected_idx = idx;
        // Ensure return flight options never precede the newly picked outbound arrival
        uint32_t min_ret_utc = get_outbound_min_return_utc();
        if (return_source_id < loader.nodes.size() &&
            return_dest_id < loader.nodes.size()) {
          std::string min_ret_iso = get_min_return_date_iso();
          if (ret_date_str < min_ret_iso) {
            ret_date_str = min_ret_iso;
            update_date_btn_labels();
          }
          return_candidates = query_corridor(return_source_id, return_dest_id,
                                             ret_date_str, min_ret_utc);
          apply_filters();
        }
      } else {
        return_selected_idx = idx;
      }
    } else if (selected_trip_type == 2) { // Multi-city
      if (active_multi_leg_idx < num_active_legs) {
        multi_city_legs[active_multi_leg_idx].selected_idx = idx;
      }
    }
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
      parts.push_back("Airline: " + active_spec.target_airline_name + " (" +
                      active_spec.target_airline_code + ")");
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

  // Mode state: starts in Customer Mode (Google Flights)
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
        target_date_str(init_date), dep_date_str(init_date),
        show_research_mode(start_research_mode) {

    // Initialize Multi-City Legs default itineraries
    multi_city_legs.resize(4);
    multi_city_legs[0] = {"DAC", "DXB", "2026-11-15", " 📅 Dep: Sun, Nov 15 ",
                          0,     0,     {},           {},
                          {},    {},    0};
    multi_city_legs[1] = {"DXB", "JFK", "2026-11-18", " 📅 Dep: Wed, Nov 18 ",
                          0,     0,     {},           {},
                          {},    {},    0};
    multi_city_legs[2] = {"JFK", "LHR", "2026-11-22", " 📅 Dep: Sun, Nov 22 ",
                          0,     0,     {},           {},
                          {},    {},    0};
    multi_city_legs[3] = {"LHR", "DAC", "2026-11-25", " 📅 Dep: Wed, Nov 25 ",
                          0,     0,     {},           {},
                          {},    {},    0};
    num_active_legs = 2;

    update_date_btn_labels();
    update_research_btn_label();
    update_suggestions();
  }

  void update_research_btn_label() {
    research_btn_label = show_research_mode ? " 🧪 Research Mode: ON  "
                                            : " 🔬 Research Mode: OFF ";
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

  void update_airline_entries_from_candidates(
      const std::vector<RoutingResult> &candidates) {
    std::set<std::string> unique_codes;
    for (const auto &res : candidates) {
      for (const auto &f : res.path) {
        if (f.flight_details_id < loader.ui_flights.size()) {
          unique_codes.insert(
              loader.ui_flights[f.flight_details_id].airline_code);
        }
      }
    }
    airline_entries = {"All Airlines"};
    airline_codes = {"ALL"};
    for (const auto &code : unique_codes) {
      airline_codes.push_back(code);
      std::string name = loader.get_airline_name(code);
      if (name.size() > 14)
        name = name.substr(0, 13) + "…";
      airline_entries.push_back(code + " - " + name);
    }
  }

  std::vector<RoutingResult> query_corridor(uint32_t src_id, uint32_t dst_id,
                                           const std::string &date_str,
                                           uint32_t min_start_utc = 0) {
    uint32_t start_day = DataLoader::parse_date_to_days(date_str);
    int32_t source_utc_offset =
        loader.nodes[src_id].utc_offset_15m * 15;
    uint32_t date_start_utc = static_cast<uint32_t>(
        static_cast<int32_t>(start_day * 1440) - source_utc_offset);
    uint32_t absolute_start_time = std::max(date_start_utc, min_start_utc);

    YenRouter yen_router;
    auto yen_start = std::chrono::high_resolution_clock::now();
    YenRouter::YenKResult yen_k = yen_router.run(
        src_id, dst_id, loader.adjacency_list, loader.nodes,
        OptimizeFor::PRICE, absolute_start_time, 15);
    yen_compute_us = std::chrono::duration_cast<std::chrono::microseconds>(
                         std::chrono::high_resolution_clock::now() - yen_start)
                         .count();

    AStar astar_router;
    RoutingResult absolute_fastest = astar_router.run(
        src_id, dst_id, loader.adjacency_list, loader.nodes,
        OptimizeFor::TIME, absolute_start_time);

    std::vector<RoutingResult> candidates;
    std::unordered_set<std::string> seen_keys;
    for (const auto &p : yen_k.k_paths) {
      if (p.success && !p.path.empty()) {
        std::string k = serialize_path_key(p.path);
        if (seen_keys.find(k) == seen_keys.end()) {
          seen_keys.insert(k);
          candidates.push_back(p);
        }
      }
    }

    if (absolute_fastest.success && !absolute_fastest.path.empty()) {
      std::string k = serialize_path_key(absolute_fastest.path);
      if (seen_keys.find(k) == seen_keys.end()) {
        seen_keys.insert(k);
        candidates.push_back(absolute_fastest);
      }
    }

    return candidates;
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

    uint32_t start_day = DataLoader::parse_date_to_days(dep_date_str);
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
    researcher_benchmarks.clear();
    update_date_btn_labels();
    target_date_str = dep_date_str;

    if (selected_trip_type == 1) { // ONE-WAY
      std::string orig_src = source_str;
      std::string orig_dst = dest_str;

      std::string res_src =
          AirportSearch::resolve_airport_code(source_str, loader);
      std::string res_dst =
          AirportSearch::resolve_airport_code(dest_str, loader);

      if (res_src.empty() ||
          loader.iata_to_id.find(res_src) == loader.iata_to_id.end()) {
        status_message = "❌ Origin airport not found for '" + source_str +
                         "'. Try city, country or IATA code.";
        has_results = false;
        return;
      }
      if (res_dst.empty() ||
          loader.iata_to_id.find(res_dst) == loader.iata_to_id.end()) {
        status_message = "❌ Destination airport not found for '" + dest_str +
                         "'. Try city, country or IATA code.";
        has_results = false;
        return;
      }

      source_str = res_src;
      dest_str = res_dst;
      update_suggestions();

      active_source_id = loader.iata_to_id[source_str];
      active_dest_id = loader.iata_to_id[dest_str];

      all_candidates =
          query_corridor(active_source_id, active_dest_id, dep_date_str);
      yen_k_paths = all_candidates;

      update_airline_entries_from_candidates(all_candidates);
      apply_filters();
      sync_selected_flight_from_preference();

      has_results = !all_candidates.empty();
      scroll_line = 0;

      if (show_research_mode || run_all_research) {
        run_research_suite();
      }

      status_message = "✅ One-way: " + source_str + " ➔ " + dest_str + " (" +
                       dep_date_str + ")  |  " +
                       std::to_string(all_candidates.size()) +
                       " itineraries found";

    } else if (selected_trip_type == 0) { // ROUND-TRIP
      std::string res_src =
          AirportSearch::resolve_airport_code(source_str, loader);
      std::string res_dst =
          AirportSearch::resolve_airport_code(dest_str, loader);

      if (res_src.empty() ||
          loader.iata_to_id.find(res_src) == loader.iata_to_id.end()) {
        status_message = "❌ Origin airport not found for '" + source_str +
                         "'. Try city, country or IATA code.";
        has_results = false;
        return;
      }
      if (res_dst.empty() ||
          loader.iata_to_id.find(res_dst) == loader.iata_to_id.end()) {
        status_message = "❌ Destination airport not found for '" + dest_str +
                         "'. Try city, country or IATA code.";
        has_results = false;
        return;
      }

      source_str = res_src;
      dest_str = res_dst;
      update_suggestions();

      active_source_id = loader.iata_to_id[source_str];
      active_dest_id = loader.iata_to_id[dest_str];
      return_source_id = active_dest_id;
      return_dest_id = active_source_id;

      if (ret_date_str < dep_date_str) {
        ret_date_str = dep_date_str;
        update_date_btn_labels();
      }

      outbound_candidates =
          query_corridor(active_source_id, active_dest_id, dep_date_str);
      outbound_selected_idx = 0;

      // Filter outbound first so we know when the selected ongoing flight arrives
      return_candidates.clear();
      return_filtered.clear();
      int saved_tab = active_roundtrip_tab;
      active_roundtrip_tab = 0;
      apply_filters();
      if (!outbound_filtered.empty()) {
        std::string target_key;
        if (selected_preference == 0 && best_flight.success)
          target_key = serialize_path_key(best_flight.path);
        else if (selected_preference == 1 && cheapest_flight.success)
          target_key = serialize_path_key(cheapest_flight.path);
        else if (selected_preference == 2 && fastest_flight.success)
          target_key = serialize_path_key(fastest_flight.path);
        for (size_t i = 0; i < outbound_filtered.size(); ++i) {
          if (serialize_path_key(outbound_filtered[i].path) == target_key) {
            outbound_selected_idx = static_cast<int>(i);
            break;
          }
        }
      }

      // Clamp return date to never be earlier than ongoing departure/arrival
      std::string min_ret_iso = get_min_return_date_iso();
      if (ret_date_str < min_ret_iso) {
        ret_date_str = min_ret_iso;
        update_date_btn_labels();
      }

      uint32_t min_ret_utc = get_outbound_min_return_utc();
      return_candidates = query_corridor(return_source_id, return_dest_id,
                                         ret_date_str, min_ret_utc);

      yen_k_paths = outbound_candidates;
      std::vector<RoutingResult> combined_all = outbound_candidates;
      combined_all.insert(combined_all.end(), return_candidates.begin(),
                          return_candidates.end());
      update_airline_entries_from_candidates(combined_all);

      return_selected_idx = 0;
      active_roundtrip_tab = 1;
      apply_filters();
      if (!return_filtered.empty()) {
        std::string target_key;
        if (selected_preference == 0 && best_flight.success)
          target_key = serialize_path_key(best_flight.path);
        else if (selected_preference == 1 && cheapest_flight.success)
          target_key = serialize_path_key(cheapest_flight.path);
        else if (selected_preference == 2 && fastest_flight.success)
          target_key = serialize_path_key(fastest_flight.path);
        for (size_t i = 0; i < return_filtered.size(); ++i) {
          if (serialize_path_key(return_filtered[i].path) == target_key) {
            return_selected_idx = static_cast<int>(i);
            break;
          }
        }
      }

      active_roundtrip_tab = saved_tab;
      apply_filters();

      has_results =
          !outbound_candidates.empty() || !return_candidates.empty();
      scroll_line = 0;

      if (show_research_mode || run_all_research) {
        run_research_suite();
      }

      status_message = "✅ Round-trip: " + source_str + " ⇄ " + dest_str +
                       "  |  " + std::to_string(outbound_filtered.size()) +
                       " outbound, " +
                       std::to_string(return_filtered.size()) +
                       " return found";

    } else { // MULTI-CITY
      std::vector<RoutingResult> combined_multi;

      for (int i = 0; i < num_active_legs; ++i) {
        if (i > 0 && multi_city_legs[i].date_str < multi_city_legs[i - 1].date_str) {
          multi_city_legs[i].date_str = multi_city_legs[i - 1].date_str;
          multi_city_legs[i].date_btn_label =
              " 📅 " +
              FlightPlanner::TUI::CalendarPicker::format_iso_to_short(
                  multi_city_legs[i].date_str) +
              " ";
        }

        std::string res_s = AirportSearch::resolve_airport_code(
            multi_city_legs[i].origin, loader);
        std::string res_d =
            AirportSearch::resolve_airport_code(multi_city_legs[i].dest, loader);

        if (res_s.empty() ||
            loader.iata_to_id.find(res_s) == loader.iata_to_id.end() ||
            res_d.empty() ||
            loader.iata_to_id.find(res_d) == loader.iata_to_id.end()) {
          status_message = "❌ Flight " + std::to_string(i + 1) +
                           " airport code invalid. Try city, country or code.";
          has_results = false;
          return;
        }

        multi_city_legs[i].origin = res_s;
        multi_city_legs[i].dest = res_d;
        multi_city_legs[i].src_id = loader.iata_to_id[res_s];
        multi_city_legs[i].dst_id = loader.iata_to_id[res_d];
        uint32_t leg_min_dep = get_leg_min_departure_utc(i);
        multi_city_legs[i].candidates =
            query_corridor(multi_city_legs[i].src_id, multi_city_legs[i].dst_id,
                           multi_city_legs[i].date_str, leg_min_dep);
        multi_city_legs[i].filtered = multi_city_legs[i].candidates;
        multi_city_legs[i].selected_idx = 0;

        combined_multi.insert(combined_multi.end(),
                              multi_city_legs[i].candidates.begin(),
                              multi_city_legs[i].candidates.end());
      }

      if (!multi_city_legs.empty()) {
        active_source_id = multi_city_legs[0].src_id;
        active_dest_id = multi_city_legs[0].dst_id;
        yen_k_paths = multi_city_legs[0].candidates;
      }

      update_airline_entries_from_candidates(combined_multi);
      apply_filters();
      sync_selected_flight_from_preference();

      has_results = true;
      for (int i = 0; i < num_active_legs; ++i) {
        if (multi_city_legs[i].candidates.empty())
          has_results = false;
      }
      scroll_line = 0;

      if (show_research_mode || run_all_research) {
        run_research_suite();
      }

      status_message = "✅ Multi-city: " + std::to_string(num_active_legs) +
                       " flights scheduled.";
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

    // 1. Google Flights Top Selector Controls
    Component trip_type_toggle =
        Toggle(&trip_type_entries, &selected_trip_type);
    Component preference_toggle =
        Toggle(&preference_entries, &selected_preference);

    // 2. Search Inputs (Origin & Dest with live autocomplete)
    InputOption src_opt;
    src_opt.multiline = false;
    src_opt.placeholder = "Where from? (City/Code)";
    src_opt.on_change = [&] { update_suggestions(); };
    src_opt.on_enter = [&] { execute_search(show_research_mode); };
    Component source_input = Input(&source_str, "Where from?", src_opt);

    InputOption dst_opt;
    dst_opt.multiline = false;
    dst_opt.placeholder = "Where to? (City/Code)";
    dst_opt.on_change = [&] { update_suggestions(); };
    dst_opt.on_enter = [&] { execute_search(show_research_mode); };
    Component dest_input = Input(&dest_str, "Where to?", dst_opt);

    // Swap button ([ ⇄ ])
    Component swap_btn = Button(
        " ⇄ ",
        [&] {
          std::swap(source_str, dest_str);
          update_suggestions();
          execute_search(show_research_mode);
        },
        ButtonOption::Border());

    // 3. Calendar Date Buttons (Opens interactive calendar popup)
    auto open_dep_calendar = [&] {
      calendar_picker.open(
          dep_date_str, "Departure Date", [&](const std::string &iso) {
            dep_date_str = iso;
            if (ret_date_str < dep_date_str) {
              ret_date_str = dep_date_str;
            }
            update_date_btn_labels();
            execute_search(show_research_mode);
          });
    };

    auto open_ret_calendar = [&] {
      std::string min_ret_iso = get_min_return_date_iso();
      calendar_picker.open(
          ret_date_str, "Return Date",
          [&](const std::string &iso) {
            std::string cur_min = get_min_return_date_iso();
            ret_date_str = (iso < cur_min) ? cur_min : iso;
            update_date_btn_labels();
            execute_search(show_research_mode);
          },
          min_ret_iso);
    };

    auto open_leg_calendar = [&](int i) {
      std::string min_iso = (i > 0) ? multi_city_legs[i - 1].date_str : "";
      calendar_picker.open(
          multi_city_legs[i].date_str,
          "Flight " + std::to_string(i + 1) + " Date",
          [&, i](const std::string &iso) {
            multi_city_legs[i].date_str = iso;
            multi_city_legs[i].date_btn_label =
                " 📅 " +
                FlightPlanner::TUI::CalendarPicker::format_iso_to_short(iso) +
                " ";
            for (int j = i + 1; j < 4; ++j) {
              if (multi_city_legs[j].date_str <
                  multi_city_legs[j - 1].date_str) {
                multi_city_legs[j].date_str = multi_city_legs[j - 1].date_str;
                multi_city_legs[j].date_btn_label =
                    " 📅 " +
                    FlightPlanner::TUI::CalendarPicker::format_iso_to_short(
                        multi_city_legs[j].date_str) +
                    " ";
              }
            }
            execute_search(show_research_mode);
          },
          min_iso);
    };

    Component dep_date_btn = Button(
        &dep_date_btn_label, [&] { open_dep_calendar(); },
        ButtonOption::Border());

    Component ret_date_btn = Button(
        &ret_date_btn_label, [&] { open_ret_calendar(); },
        ButtonOption::Border());

    // Search button
    Component search_btn = Button(
        " 🔍 Search Flights ", [&] { execute_search(show_research_mode); },
        ButtonOption::Border());

    // 4. Multi-City Components (up to 4 segments)
    Component leg_src_inputs[4];
    Component leg_dst_inputs[4];
    Component leg_date_btns[4];
    Component leg_tab_btns[4];
    std::string leg_tab_labels[4];

    for (int i = 0; i < 4; ++i) {
      InputOption s_opt;
      s_opt.multiline = false;
      s_opt.placeholder = "Origin";
      s_opt.on_enter = [&] { execute_search(show_research_mode); };
      leg_src_inputs[i] = Input(&multi_city_legs[i].origin, "Origin", s_opt);

      InputOption d_opt;
      d_opt.multiline = false;
      d_opt.placeholder = "Destination";
      d_opt.on_enter = [&] { execute_search(show_research_mode); };
      leg_dst_inputs[i] =
          Input(&multi_city_legs[i].dest, "Destination", d_opt);

      leg_date_btns[i] = Button(
          &multi_city_legs[i].date_btn_label,
          [&, i] { open_leg_calendar(i); }, ButtonOption::Border());

      leg_tab_labels[i] = " Flight " + std::to_string(i + 1) + " ";
      leg_tab_btns[i] = Button(
          &leg_tab_labels[i],
          [&, i] {
            active_multi_leg_idx = i;
            apply_filters();
          },
          ButtonOption::Ascii());
    }

    Component add_leg_btn = Button(
        " + Add flight ",
        [&] {
          if (num_active_legs < 4) {
            num_active_legs++;
            execute_search(show_research_mode);
          }
        },
        ButtonOption::Ascii());

    Component remove_leg_btn = Button(
        " ✕ Remove flight ",
        [&] {
          if (num_active_legs > 2) {
            num_active_legs--;
            if (active_multi_leg_idx >= num_active_legs)
              active_multi_leg_idx = num_active_legs - 1;
            execute_search(show_research_mode);
          }
        },
        ButtonOption::Ascii());

    // 5. Round-trip Google Flights Tab Selectors
    Component tab_outbound_btn = Button(
        &tab_outbound_label,
        [&] {
          active_roundtrip_tab = 0;
          apply_filters();
        },
        ButtonOption::Ascii());

    Component tab_return_btn = Button(
        &tab_return_label,
        [&] {
          active_roundtrip_tab = 1;
          apply_filters();
        },
        ButtonOption::Ascii());

    // 6. Interactive Suggestion Buttons (Clickable options as you type)
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

    // 7. Action / Header Buttons
    Component research_toggle_btn = Button(
        &research_btn_label,
        [&] {
          show_research_mode = !show_research_mode;
          update_research_btn_label();
          if (show_research_mode) {
            if (has_results && researcher_benchmarks.empty()) {
              run_research_suite();
            }
            scroll_line = customer_height_;
          } else {
            scroll_line = 0;
          }
        },
        ButtonOption::Border());

    Component exit_btn = Button(
        " ✕ Exit ",
        [&] {
          set_mouse_cursor("");
          screen.Exit();
        },
        ButtonOption::Border());

    Component map_toggle_btn = Button(
        &world_map_btn_label,
        [&] {
          show_world_map = !show_world_map;
          world_map_btn_label = show_world_map ? " 🗺️  World Map: ON "
                                               : " 🗺️  World Map: OFF ";
        },
        ButtonOption::Border());

    // 8. Bottom Scroll Control Buttons
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

    // 9. Typed Flight Needs Filter Component & Chips
    InputOption filter_opt;
    filter_opt.multiline = false;
    filter_opt.placeholder =
        "e.g. non-stop, under $800, Emirates, morning, < 24h";
    filter_opt.on_change = [&] { apply_filters(); };
    filter_opt.on_enter = [&] { apply_filters(); };
    Component filter_input = Input(
        &typed_filter_str, "e.g. non-stop, under $800, Emirates", filter_opt);

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
        " Non-stop ", [&] { add_need_token("non-stop"); },
        ButtonOption::Ascii());
    Component chip_1stop = Button(
        " ≤ 1 Stop ", [&] { add_need_token("1 stop"); }, ButtonOption::Ascii());
    Component chip_p800 = Button(
        " Under $800 ", [&] { add_need_token("under $800"); },
        ButtonOption::Ascii());
    Component chip_p1000 = Button(
        " Under $1,000 ", [&] { add_need_token("under $1000"); },
        ButtonOption::Ascii());
    Component chip_morning = Button(
        " Morning ", [&] { add_need_token("morning"); },
        ButtonOption::Ascii());
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

    // 10. Clickable Flight Option Buttons Pool
    constexpr static size_t MAX_OPTION_BUTTONS = 40;
    std::vector<std::string> opt_btn_labels(MAX_OPTION_BUTTONS);
    Components opt_buttons;
    for (size_t i = 0; i < MAX_OPTION_BUTTONS; ++i) {
      auto btn = Button(
          &opt_btn_labels[i],
          [&, i] {
            if (i < filtered_itineraries.size()) {
              on_select_flight_option(static_cast<int>(i));
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

    Components multi_city_components;
    for (int i = 0; i < 4; ++i) {
      multi_city_components.push_back(leg_src_inputs[i]);
      multi_city_components.push_back(leg_dst_inputs[i]);
      multi_city_components.push_back(leg_date_btns[i]);
      multi_city_components.push_back(leg_tab_btns[i]);
    }
    multi_city_components.push_back(add_leg_btn);
    multi_city_components.push_back(remove_leg_btn);

    auto controls_container = Container::Vertical({
        Container::Horizontal({
            trip_type_toggle,
            preference_toggle,
        }),
        Container::Horizontal({
            source_input,
            swap_btn,
            dest_input,
            dep_date_btn,
            Maybe(ret_date_btn, [&] { return selected_trip_type == 0; }),
            search_btn,
        }),
        src_sugg_container,
        dst_sugg_container,
        Maybe(Container::Vertical(multi_city_components),
              [&] { return selected_trip_type == 2; }),
        Maybe(Container::Horizontal({
                  tab_outbound_btn,
                  tab_return_btn,
              }),
              [&] { return selected_trip_type == 0; }),
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

    int main_focus_idx = 1;
    auto main_container = Container::Vertical(
        {
            top_header_buttons,
            controls_container,
            footer_controls,
        },
        &main_focus_idx);

    // Renderer lambda
    auto renderer = Renderer(main_container, [&] {
      // Sync trip type changes
      if (selected_trip_type != last_trip_type) {
        last_trip_type = selected_trip_type;
        update_date_btn_labels();
        execute_search(show_research_mode);
      }

      // Sync preference toggle
      if (selected_preference != last_preference) {
        last_preference = selected_preference;
        sync_selected_flight_from_preference();
      }

      // Live filter sync
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

      // Update Round-trip tab labels
      if (!outbound_filtered.empty() &&
          outbound_selected_idx < (int)outbound_filtered.size()) {
        tab_outbound_label =
            (active_roundtrip_tab == 0 ? " ▶ " : "   ") +
            std::string("🛫 1. Departing: ") + source_str + " ➔ " + dest_str +
            " ($" +
            std::to_string(outbound_filtered[outbound_selected_idx].total_price) +
            ") ";
      } else {
        tab_outbound_label =
            (active_roundtrip_tab == 0 ? " ▶ " : "   ") +
            std::string("🛫 1. Departing: ") + source_str + " ➔ " + dest_str +
            " ";
      }

      if (!return_filtered.empty() &&
          return_selected_idx < (int)return_filtered.size()) {
        tab_return_label =
            (active_roundtrip_tab == 1 ? " ▶ " : "   ") +
            std::string("🛬 2. Returning: ") + dest_str + " ➔ " + source_str +
            " ($" +
            std::to_string(return_filtered[return_selected_idx].total_price) +
            ") ";
      } else {
        tab_return_label =
            (active_roundtrip_tab == 1 ? " ▶ " : "   ") +
            std::string("🛬 2. Returning: ") + dest_str + " ➔ " + source_str +
            " ";
      }

      // Update Multi-city tab labels
      for (int i = 0; i < num_active_legs; ++i) {
        std::string price_part = "";
        if (!multi_city_legs[i].filtered.empty() &&
            multi_city_legs[i].selected_idx <
                (int)multi_city_legs[i].filtered.size()) {
          price_part =
              " ($" +
              std::to_string(
                  multi_city_legs[i]
                      .filtered[multi_city_legs[i].selected_idx]
                      .total_price) +
              ")";
        }
        leg_tab_labels[i] =
            (active_multi_leg_idx == i ? " ▶ " : "   ") +
            std::string("Flight ") + std::to_string(i + 1) + ": " +
            multi_city_legs[i].origin + " ➔ " + multi_city_legs[i].dest +
            price_part + " ";
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
      // TOP BRANDING BAR & UTILITY TOGGLES
      // ---------------------------------------------------------------------
      auto top_bar = hbox({
          vbox({
              text(" ✈️  FLIGHT ROUTE EXPLORER ") | bold |
                  color(Color::CyanLight),
              text(show_research_mode
                       ? " Customer Mode + Algorithmic Research Suite Active "
                       : " Explore flights effortlessly with live prices, calendar & map ") |
                  dim,
          }),
          filler(),
          hbox({
              map_toggle_btn->Render() |
                  (show_world_map ? color(Color::CyanLight)
                                  : color(Color::GrayLight)) |
                  reflect(map_toggle_box),
              text(" "),
              research_toggle_btn->Render() |
                  (show_research_mode ? color(Color::GreenLight)
                                      : color(Color::YellowLight)) |
                  reflect(research_toggle_box),
              text(" "),
              exit_btn->Render() | color(Color::RedLight) | reflect(exit_box),
          }),
      });

      // ---------------------------------------------------------------------
      // TRIP TYPE & ROUTE PREFERENCES BAR
      // ---------------------------------------------------------------------
      auto trip_pref_bar = hbox({
          text(" Trip: ") | bold | vcenter,
          trip_type_toggle->Render() | color(Color::CyanLight) | vcenter |
              reflect(trip_type_box),
          text("   │   View: ") | bold | color(Color::YellowLight) | vcenter,
          preference_toggle->Render() | color(Color::White) | vcenter |
              reflect(preference_box),
          filler(),
          text(status_message + " ") | vcenter |
              (has_results ? color(Color::Green) : color(Color::RedLight)),
      });

      // ---------------------------------------------------------------------
      // SEARCH CAPSULE
      // ---------------------------------------------------------------------
      std::vector<Element> capsule_elements;

      if (selected_trip_type == 0) { // Round-trip
        capsule_elements.push_back(hbox({
            text(" 🛫 Where from? ") | bold | vcenter,
            source_input->Render() | size(WIDTH, EQUAL, 18) | borderLight |
                color(source_input->Focused() ? Color::CyanLight : Color::White) |
                reflect(src_input_box),
            text(" "),
            swap_btn->Render() | color(Color::CyanLight) | vcenter |
                reflect(swap_box),
            text(" "),
            text(" 🛬 Where to? ") | bold | vcenter,
            dest_input->Render() | size(WIDTH, EQUAL, 18) | borderLight |
                color(dest_input->Focused() ? Color::CyanLight : Color::White) |
                reflect(dst_input_box),
            text("   │  ") | dim | vcenter,
            dep_date_btn->Render() | bold | color(Color::CyanLight) | vcenter |
                reflect(dep_date_box),
            text(" "),
            ret_date_btn->Render() | bold | color(Color::YellowLight) |
                vcenter | reflect(ret_date_box),
            filler(),
            search_btn->Render() | bold | color(Color::GreenLight) | vcenter |
                reflect(search_box),
        }));

        // Live city/code resolution
        capsule_elements.push_back(hbox({
            text("      " + origin_desc) |
                color(origin_valid ? Color::GreenLight : Color::YellowLight) |
                dim,
            filler(),
            text(dest_desc + "      ") |
                color(dest_valid ? Color::GreenLight : Color::YellowLight) |
                dim,
            filler(),
        }));

      } else if (selected_trip_type == 1) { // One-way
        capsule_elements.push_back(hbox({
            text(" 🛫 Where from? ") | bold | vcenter,
            source_input->Render() | size(WIDTH, EQUAL, 20) | borderLight |
                color(source_input->Focused() ? Color::CyanLight : Color::White) |
                reflect(src_input_box),
            text(" "),
            swap_btn->Render() | color(Color::CyanLight) | vcenter |
                reflect(swap_box),
            text(" "),
            text(" 🛬 Where to? ") | bold | vcenter,
            dest_input->Render() | size(WIDTH, EQUAL, 20) | borderLight |
                color(dest_input->Focused() ? Color::CyanLight : Color::White) |
                reflect(dst_input_box),
            text("   │  ") | dim | vcenter,
            dep_date_btn->Render() | bold | color(Color::CyanLight) | vcenter |
                reflect(dep_date_box),
            filler(),
            search_btn->Render() | bold | color(Color::GreenLight) | vcenter |
                reflect(search_box),
        }));

        capsule_elements.push_back(hbox({
            text("      " + origin_desc) |
                color(origin_valid ? Color::GreenLight : Color::YellowLight) |
                dim,
            filler(),
            text(dest_desc + "      ") |
                color(dest_valid ? Color::GreenLight : Color::YellowLight) |
                dim,
            filler(),
        }));

      } else { // Multi-city
        for (int i = 0; i < num_active_legs; ++i) {
          capsule_elements.push_back(hbox({
              text(" Flight " + std::to_string(i + 1) + ": ") | bold |
                  color(Color::CyanLight) | vcenter,
              text(" 🛫 ") | vcenter,
              leg_src_inputs[i]->Render() | size(WIDTH, EQUAL, 16) |
                  borderLight |
                  color(leg_src_inputs[i]->Focused() ? Color::CyanLight
                                                     : Color::White) |
                  reflect(leg_src_boxes[i]),
              text(" ➔ 🛬 ") | dim | vcenter,
              leg_dst_inputs[i]->Render() | size(WIDTH, EQUAL, 16) |
                  borderLight |
                  color(leg_dst_inputs[i]->Focused() ? Color::CyanLight
                                                     : Color::White) |
                  reflect(leg_dst_boxes[i]),
              text("   │  ") | dim | vcenter,
              leg_date_btns[i]->Render() | bold | color(Color::YellowLight) |
                  vcenter | reflect(leg_date_boxes[i]),
              filler(),
          }));
        }

        capsule_elements.push_back(hbox({
            add_leg_btn->Render() | color(Color::CyanLight) |
                reflect(add_leg_box),
            text(" "),
            remove_leg_btn->Render() | color(Color::RedLight) |
                reflect(remove_leg_box),
            filler(),
            search_btn->Render() | bold | color(Color::GreenLight) |
                reflect(search_box),
        }));
      }

      // Suggestions row (if any)
      if (selected_trip_type != 2) {
        if (!src_matches.empty() || !dst_matches.empty()) {
          std::vector<Element> sugg_items;
          if (!src_matches.empty()) {
            sugg_items.push_back(text(" Origin: ") | dim | vcenter);
            for (size_t i = 0; i < MAX_SUGGESTIONS; ++i) {
              if (i < src_matches.size()) {
                sugg_items.push_back(src_buttons[i]->Render() |
                                     reflect(src_sugg_boxes[i]));
                sugg_items.push_back(text(" "));
              }
            }
          }
          if (!dst_matches.empty()) {
            sugg_items.push_back(text("  Dest: ") | dim | vcenter);
            for (size_t i = 0; i < MAX_SUGGESTIONS; ++i) {
              if (i < dst_matches.size()) {
                sugg_items.push_back(dst_buttons[i]->Render() |
                                     reflect(dst_sugg_boxes[i]));
                sugg_items.push_back(text(" "));
              }
            }
          }
          capsule_elements.push_back(separatorLight());
          capsule_elements.push_back(hbox(std::move(sugg_items)));
        }
      }

      auto search_capsule_window = window(
          text(" 🔍 SEARCH FLIGHTS ") | bold | color(Color::CyanLight),
          vbox(std::move(capsule_elements))) |
          color(Color::Cyan);

      // ---------------------------------------------------------------------
      // TYPED FLIGHT NEEDS FILTER BAR
      // ---------------------------------------------------------------------
      bool has_active_filter =
          active_spec.has_any_filter() || !typed_filter_str.empty();

      std::string filter_badge =
          has_active_filter
              ? ("🔍 " + std::to_string(filtered_itineraries.size()) + "/" +
                 std::to_string(all_candidates.size()) + " match needs")
              : ("✈ All " + std::to_string(all_candidates.size()) +
                 " flights shown");

      std::vector<Element> parsed_badges;
      if (active_spec.max_stops == 0) {
        parsed_badges.push_back(
            text("[ 🛑 Non-stop ]") | bold | color(Color::GreenLight));
        parsed_badges.push_back(text(" "));
      } else if (active_spec.max_stops == 1) {
        parsed_badges.push_back(
            text("[ 🛑 ≤ 1 Stop ]") | bold | color(Color::YellowLight));
        parsed_badges.push_back(text(" "));
      } else if (active_spec.max_stops == 2) {
        parsed_badges.push_back(
            text("[ 🛑 ≤ 2 Stops ]") | bold | color(Color::YellowLight));
        parsed_badges.push_back(text(" "));
      }

      if (active_spec.max_price != -1) {
        parsed_badges.push_back(
            text("[ 🏷️ Max $" + std::to_string(active_spec.max_price) + " ]") |
            bold | color(Color::YellowLight));
        parsed_badges.push_back(text(" "));
      }

      if (!active_spec.target_airline_code.empty()) {
        std::string air_label = active_spec.target_airline_name.empty()
                                    ? active_spec.target_airline_code
                                    : active_spec.target_airline_name;
        if (air_label.size() > 14)
          air_label = air_label.substr(0, 13) + "…";
        parsed_badges.push_back(
            text("[ ✈ " + air_label + " ]") | bold | color(Color::CyanLight));
        parsed_badges.push_back(text(" "));
      }

      if (!active_spec.dep_time_label.empty()) {
        parsed_badges.push_back(
            text("[ 🌅 " + active_spec.dep_time_label + " ]") | bold |
            color(Color::YellowLight));
        parsed_badges.push_back(text(" "));
      }

      if (active_spec.max_duration_mins != -1) {
        int hrs = active_spec.max_duration_mins / 60;
        parsed_badges.push_back(
            text("[ ⏱ Under " + std::to_string(hrs) + "h ]") | bold |
            color(Color::MagentaLight));
        parsed_badges.push_back(text(" "));
      }

      Element badges_row;
      if (!parsed_badges.empty()) {
        badges_row = hbox({
            text("  Active Needs: ") | bold | color(Color::CyanLight) | vcenter,
            hbox(std::move(parsed_badges)) | vcenter,
        });
      } else if (!typed_filter_str.empty()) {
        badges_row = text("  ℹ️  No specific flight criteria recognized in '" +
                          typed_filter_str + "'. Showing all flights.") |
                     dim | color(Color::YellowLight);
      } else {
        badges_row = text("  💡 Tip: Type your needs (e.g. 'non-stop', 'under "
                          "$800', 'Emirates', 'morning', '< 24h') or click a chip") |
                     dim;
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
                  filter_input->Render() | size(WIDTH, EQUAL, 52) |
                      borderLight |
                      color(filter_input->Focused() ? Color::CyanLight
                                                    : Color::White) |
                      reflect(filter_input_box),
                  text(" "),
                  clear_filter_btn->Render() |
                      (has_active_filter ? color(Color::RedLight)
                                         : color(Color::GrayLight)) |
                      reflect(clear_filter_box),
                  filler(),
              }),
              badges_row,
              hbox({
                  text(" Quick Needs: ") | dim | vcenter,
                  chip_nonstop->Render() | reflect(chip_boxes[0]),
                  text(" "),
                  chip_1stop->Render() | reflect(chip_boxes[1]),
                  text(" "),
                  chip_p800->Render() | reflect(chip_boxes[2]),
                  text(" "),
                  chip_p1000->Render() | reflect(chip_boxes[3]),
                  text(" "),
                  chip_morning->Render() | reflect(chip_boxes[4]),
                  text(" "),
                  chip_dur24->Render() | reflect(chip_boxes[5]),
                  filler(),
              }),
          })) |
          color(has_active_filter ? Color::Yellow : Color::Cyan);

      // ---------------------------------------------------------------------
      // SCROLLABLE BODY: CUSTOMER RESULTS + RESEARCH SECTION
      // ---------------------------------------------------------------------
      std::vector<Element> customer_elements;
      std::vector<Element> research_elements;

      if (has_results) {
        // A. Combined Itinerary Banner & Tabs (Google Flights Experience)
        if (selected_trip_type == 0) { // Round-trip
          int total_rt_price = 0;
          int total_rt_duration = 0;
          if (!outbound_filtered.empty() &&
              outbound_selected_idx < (int)outbound_filtered.size()) {
            total_rt_price +=
                outbound_filtered[outbound_selected_idx].total_price;
            total_rt_duration +=
                outbound_filtered[outbound_selected_idx].total_time_mins;
          }
          if (!return_filtered.empty() &&
              return_selected_idx < (int)return_filtered.size()) {
            total_rt_price +=
                return_filtered[return_selected_idx].total_price;
            total_rt_duration +=
                return_filtered[return_selected_idx].total_time_mins;
          }

          customer_elements.push_back(
              window(
                  hbox({
                      text(" ✈️  ROUND-TRIP ITINERARY SUMMARY ") | bold |
                          color(Color::CyanLight),
                      filler(),
                      text(" Total Combined Fare: ") | bold,
                      text("$" + std::to_string(total_rt_price)) | bold |
                          color(Color::GreenLight),
                      text("  (" +
                           TUIViews::format_duration(total_rt_duration) + ")") |
                          dim,
                  }),
                  vbox({
                      hbox({
                          text(" 🛫 Outbound (" + dep_date_str + "): ") | bold |
                              color(Color::CyanLight),
                          text((!outbound_filtered.empty() &&
                                outbound_selected_idx <
                                    (int)outbound_filtered.size())
                                   ? TUIViews::build_route_summary(
                                         outbound_filtered
                                             [outbound_selected_idx],
                                         loader, active_source_id)
                                   : "None"),
                          filler(),
                          text((!outbound_filtered.empty() &&
                                outbound_selected_idx <
                                    (int)outbound_filtered.size())
                                   ? ("$" +
                                      std::to_string(
                                          outbound_filtered
                                              [outbound_selected_idx]
                                                  .total_price))
                                   : ""),
                      }),
                      separatorLight(),
                      hbox({
                          text(" 🛬 Return (" + ret_date_str + "): ") | bold |
                              color(Color::YellowLight),
                          text((!return_filtered.empty() &&
                                return_selected_idx <
                                    (int)return_filtered.size())
                                   ? TUIViews::build_route_summary(
                                         return_filtered[return_selected_idx],
                                         loader, return_source_id)
                                   : "None"),
                          filler(),
                          text((!return_filtered.empty() &&
                                return_selected_idx <
                                    (int)return_filtered.size())
                                   ? ("$" +
                                      std::to_string(
                                          return_filtered[return_selected_idx]
                                              .total_price))
                                   : ""),
                      }),
                  })) |
              color(Color::Cyan));

          // Leg Switcher Tabs
          customer_elements.push_back(hbox({
              tab_outbound_btn->Render() |
                  (active_roundtrip_tab == 0
                       ? (bold | color(Color::GreenLight) | inverted)
                       : color(Color::CyanLight)) |
                  reflect(tab_outbound_box),
              text("  "),
              tab_return_btn->Render() |
                  (active_roundtrip_tab == 1
                       ? (bold | color(Color::GreenLight) | inverted)
                       : color(Color::CyanLight)) |
                  reflect(tab_return_box),
              filler(),
          }));

        } else if (selected_trip_type == 2) { // Multi-city
          int total_mc_price = 0;
          for (int i = 0; i < num_active_legs; ++i) {
            if (!multi_city_legs[i].filtered.empty() &&
                multi_city_legs[i].selected_idx <
                    (int)multi_city_legs[i].filtered.size()) {
              total_mc_price += multi_city_legs[i]
                                    .filtered[multi_city_legs[i].selected_idx]
                                    .total_price;
            }
          }

          std::vector<Element> leg_rows;
          for (int i = 0; i < num_active_legs; ++i) {
            std::string summary = "None";
            std::string price = "";
            if (!multi_city_legs[i].filtered.empty() &&
                multi_city_legs[i].selected_idx <
                    (int)multi_city_legs[i].filtered.size()) {
              summary = TUIViews::build_route_summary(
                  multi_city_legs[i]
                      .filtered[multi_city_legs[i].selected_idx],
                  loader, multi_city_legs[i].src_id);
              price =
                  "$" +
                  std::to_string(
                      multi_city_legs[i]
                          .filtered[multi_city_legs[i].selected_idx]
                          .total_price);
            }
            if (i > 0)
              leg_rows.push_back(separatorLight());
            leg_rows.push_back(hbox({
                text(" Flight " + std::to_string(i + 1) + " (" +
                     multi_city_legs[i].date_str + "): ") |
                    bold | color(Color::CyanLight),
                text(summary),
                filler(),
                text(price) | bold | color(Color::YellowLight),
            }));
          }

          customer_elements.push_back(
              window(
                  hbox({
                      text(" 🔀 MULTI-CITY TRIP SUMMARY (" +
                           std::to_string(num_active_legs) + " FLIGHTS) ") |
                          bold | color(Color::CyanLight),
                      filler(),
                      text(" Combined Total Fare: ") | bold,
                      text("$" + std::to_string(total_mc_price)) | bold |
                          color(Color::GreenLight),
                  }),
                  vbox(std::move(leg_rows))) |
              color(Color::Cyan));

          // Multi-city Leg Switcher Tabs
          std::vector<Element> tabs;
          for (int i = 0; i < num_active_legs; ++i) {
            tabs.push_back(
                leg_tab_btns[i]->Render() |
                (active_multi_leg_idx == i
                     ? (bold | color(Color::GreenLight) | inverted)
                     : color(Color::CyanLight)) |
                reflect(leg_tab_boxes[i]));
            tabs.push_back(text("  "));
          }
          tabs.push_back(filler());
          customer_elements.push_back(hbox(std::move(tabs)));
        }

        // B. Active Leg Itinerary Card & World Map
        if (!filtered_itineraries.empty()) {
          std::string best_key = serialize_path_key(best_flight.path);
          std::string cheap_key = serialize_path_key(cheapest_flight.path);
          std::string fast_key = serialize_path_key(fastest_flight.path);

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
            } else {
              chosen_title = "✈ SELECTED FLIGHT ITINERARY — OPTION #" +
                             std::to_string(selected_flight_idx + 1) + " OF " +
                             std::to_string(filtered_itineraries.size());
              chosen_reason =
                  "Selected custom flight choice. Full leg-by-leg schedule "
                  "breakdown below.";
              chosen_color = Color::CyanLight;
            }

            customer_elements.push_back(TUIViews::build_itinerary_card(
                chosen_title, chosen_flight, chosen_reason, loader,
                (selected_trip_type == 0 && active_roundtrip_tab == 1)
                    ? return_source_id
                    : (selected_trip_type == 2 &&
                       active_multi_leg_idx < num_active_legs)
                          ? multi_city_legs[active_multi_leg_idx].src_id
                          : active_source_id,
                chosen_color, chosen_color, true));

            if (show_world_map) {
              customer_elements.push_back(
                  FlightPlanner::TUI::WorldMap::build_route_map(
                      chosen_flight, loader,
                      (selected_trip_type == 0 && active_roundtrip_tab == 1)
                          ? return_source_id
                          : (selected_trip_type == 2 &&
                             active_multi_leg_idx < num_active_legs)
                                ? multi_city_legs[active_multi_leg_idx].src_id
                                : active_source_id));
            }
          }

          // C. Summary Comparison Cards (Cheapest & Fastest)
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
                        text(" Route: " +
                             TUIViews::build_route_summary(
                                 cheapest_flight, loader,
                                 (selected_trip_type == 0 &&
                                  active_roundtrip_tab == 1)
                                     ? return_source_id
                                     : active_source_id)) |
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
                        text(" Route: " +
                             TUIViews::build_route_summary(
                                 fastest_flight, loader,
                                 (selected_trip_type == 0 &&
                                  active_roundtrip_tab == 1)
                                     ? return_source_id
                                     : active_source_id)) |
                            color(Color::GrayLight),
                    })) |
                color(Color::MagentaLight) | flex |
                reflect(fastest_summary_box));
          }

          if (!summary_cards.empty()) {
            customer_elements.push_back(hbox(std::move(summary_cards)));
          }

          // D. Clickable Flight Options List
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

            std::string route_str = TUIViews::build_route_summary(
                res, loader,
                (selected_trip_type == 0 && active_roundtrip_tab == 1)
                    ? return_source_id
                    : active_source_id);

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
                           " Available — Click any option to select) ") |
                          bold | color(Color::CyanLight),
                      filler(),
                      text(" Click option to inspect ") | dim,
                  }),
                  vbox(std::move(option_rows))) |
              color(Color::Cyan));
        } else if (selected_trip_type == 0 && active_roundtrip_tab == 1 &&
                   !outbound_filtered.empty()) {
          customer_elements.push_back(
              window(
                  text(" ⚠️  NO RETURN FLIGHTS AVAILABLE AFTER OUTBOUND ARRIVAL ") |
                      bold | color(Color::YellowLight),
                  vbox({
                      text(" No valid return flights depart on or after " +
                           ret_date_str +
                           " following your selected ongoing flight arrival.") |
                          color(Color::White),
                      text(" Return flights cannot depart earlier than your "
                           "ongoing flight lands (+ transfer time).") |
                          color(Color::YellowLight),
                      text(" "),
                      text(" Tip: Click the Return Date button above to choose "
                           "a later date, or pick an earlier ongoing flight.") |
                          color(Color::GrayLight),
                  })) |
              color(Color::YellowLight));
        } else {
          customer_elements.push_back(
              window(
                  text(" ⚠️  NO FLIGHTS MATCH CURRENT FILTERS ") | bold |
                      color(Color::YellowLight),
                  vbox({
                      text(" None of the " +
                           std::to_string(all_candidates.size()) +
                           " available itineraries match your active filter "
                           "criteria:") |
                          color(Color::White),
                      text("   • " + get_active_filters_description()) |
                          color(Color::YellowLight) | bold,
                      text(" "),
                      text(" Tip: Click [ ↺ Clear ] on the filter bar above or "
                           "edit your needs.") |
                          color(Color::GrayLight),
                  })) |
              color(Color::YellowLight));
        }

        // ===================================================================
        // RESEARCHER MODE SECTION
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

          // Visual Speed Profiler Gauges
          std::vector<Element> visual_gauges;
          for (const auto &bench : researcher_benchmarks) {
            float ratio = (max_time_us > 0)
                              ? float(bench.compute_time_us) / float(max_time_us)
                              : 0.0f;
            ratio = std::clamp(ratio, 0.01f, 1.0f);

            std::string label = bench.algo_name + " (" + bench.mode_label + ")";
            visual_gauges.push_back(hbox({
                text(label) | size(WIDTH, EQUAL, 24) |
                    color(bench.mode_label == "cheapest" ? Color::YellowLight
                                                         : Color::MagentaLight),
                gauge(ratio) | flex |
                    color(ratio > 0.7f   ? Color::RedLight
                          : ratio > 0.3f ? Color::YellowLight
                                         : Color::GreenLight),
                text(" " +
                     (bench.compute_time_us < 1000
                          ? (std::to_string(bench.compute_time_us) + " µs")
                          : (std::to_string(bench.compute_time_us / 1000) +
                             " ms"))) |
                    bold | size(WIDTH, EQUAL, 10),
            }));
          }

          research_elements.push_back(
              window(text(" COMPUTATION RUNTIME SPEED PROFILER ") | bold |
                         color(Color::YellowLight),
                     vbox(std::move(visual_gauges))) |
              color(Color::YellowLight));

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
                            source_str + " ➔ " + dest_str + " on " +
                            dep_date_str) |
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
          scroll_top_btn->Render() | color(Color::CyanLight) |
              reflect(footer_boxes[0]),
          text(" "),
          scroll_up_btn->Render() | color(Color::CyanLight) |
              reflect(footer_boxes[1]),
          text(" "),
          scroll_down_btn->Render() | color(Color::CyanLight) |
              reflect(footer_boxes[2]),
          text(" "),
          scroll_bottom_btn->Render() | color(Color::CyanLight) |
              reflect(footer_boxes[3]),
          text("  "),
          jump_research_btn->Render() | color(Color::YellowLight) |
              reflect(footer_boxes[4]),
          filler(),
          text(" Scroll: " + std::to_string(pct) +
               "%  │  Mouse Wheel / PgUp / PgDn  │  Esc: Exit ") |
              dim,
      });

      return vbox({
          top_bar,
          separatorLight(),
          trip_pref_bar,
          search_capsule_window,
          filters_window,
          scrollable_viewport,
          separatorLight(),
          footer_bar,
      });
    });

    // Root component with Modal overlay for Calendar Picker
    auto root_with_modal =
        Modal(renderer, calendar_picker.GetComponent(), &calendar_picker.is_open);

    auto update_mouse_cursor_at = [&](int mx, int my) {
      if (calendar_picker.is_open) {
        set_mouse_cursor(
            calendar_picker.is_over_interactive(mx, my) ? "pointer" : "default");
        return;
      }

      // Text inputs -> I-beam ("text")
      if (filter_input_box.Contain(mx, my)) {
        set_mouse_cursor("text");
        return;
      }
      if (selected_trip_type != 2) {
        if (src_input_box.Contain(mx, my) || dst_input_box.Contain(mx, my)) {
          set_mouse_cursor("text");
          return;
        }
      } else {
        for (int i = 0; i < num_active_legs; ++i) {
          if (leg_src_boxes[i].Contain(mx, my) ||
              leg_dst_boxes[i].Contain(mx, my)) {
            set_mouse_cursor("text");
            return;
          }
        }
      }

      // Clickable buttons, tabs, chips, cards -> Hand ("pointer")
      if (map_toggle_box.Contain(mx, my) ||
          research_toggle_box.Contain(mx, my) || exit_box.Contain(mx, my) ||
          trip_type_box.Contain(mx, my) || preference_box.Contain(mx, my) ||
          search_box.Contain(mx, my) || clear_filter_box.Contain(mx, my) ||
          cheapest_summary_box.Contain(mx, my) ||
          fastest_summary_box.Contain(mx, my)) {
        set_mouse_cursor("pointer");
        return;
      }
      for (int i = 0; i < 6; ++i) {
        if (chip_boxes[i].Contain(mx, my)) {
          set_mouse_cursor("pointer");
          return;
        }
      }
      for (int i = 0; i < 5; ++i) {
        if (footer_boxes[i].Contain(mx, my)) {
          set_mouse_cursor("pointer");
          return;
        }
      }
      if (selected_trip_type != 2) {
        if (swap_box.Contain(mx, my) || dep_date_box.Contain(mx, my)) {
          set_mouse_cursor("pointer");
          return;
        }
        if (selected_trip_type == 0 &&
            (ret_date_box.Contain(mx, my) || tab_outbound_box.Contain(mx, my) ||
             tab_return_box.Contain(mx, my))) {
          set_mouse_cursor("pointer");
          return;
        }
        for (size_t i = 0; i < MAX_SUGGESTIONS; ++i) {
          if ((i < src_matches.size() && src_sugg_boxes[i].Contain(mx, my)) ||
              (i < dst_matches.size() && dst_sugg_boxes[i].Contain(mx, my))) {
            set_mouse_cursor("pointer");
            return;
          }
        }
      } else {
        if (add_leg_box.Contain(mx, my) || remove_leg_box.Contain(mx, my)) {
          set_mouse_cursor("pointer");
          return;
        }
        for (int i = 0; i < num_active_legs; ++i) {
          if (leg_date_boxes[i].Contain(mx, my) ||
              leg_tab_boxes[i].Contain(mx, my)) {
            set_mouse_cursor("pointer");
            return;
          }
        }
      }
      if (viewport_box.Contain(mx, my)) {
        if (mx >= viewport_box.x_max - 1) {
          set_mouse_cursor("pointer");
          return;
        }
        for (size_t i = 0;
             i < option_boxes.size() && i < filtered_itineraries.size(); ++i) {
          if (option_boxes[i].Contain(mx, my)) {
            set_mouse_cursor("pointer");
            return;
          }
        }
      }

      set_mouse_cursor("default");
    };

    auto any_input_focused = [&]() {
      if (source_input->Focused() || dest_input->Focused() ||
          filter_input->Focused()) {
        return true;
      }
      for (int i = 0; i < num_active_legs; ++i) {
        if (leg_src_inputs[i]->Focused() || leg_dst_inputs[i]->Focused())
          return true;
      }
      return false;
    };

    // Global event handler for smooth mouse wheel, clicks, cursor, and keys
    auto event_handler = CatchEvent(root_with_modal, [&](Event event) {
      if (event.is_mouse()) {
        update_mouse_cursor_at(event.mouse().x, event.mouse().y);
      }

      if (calendar_picker.is_open) {
        if (event == Event::Escape) {
          calendar_picker.is_open = false;
          return true;
        }
        return false;
      }

      if (event == Event::Escape) {
        set_mouse_cursor("");
        screen.Exit();
        return true;
      }

      // Mouse wheel, hover cursor, scrollbar dragging & click-to-focus
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

        // Direct mouse click handlers: ensure focus & cursor update accurately!
        if (m.button == Mouse::Left && m.motion == Mouse::Pressed) {
          // 1. Input boxes (clicking border OR inner text focuses the input!)
          auto handle_input_box_click = [&](const ftxui::Box &box,
                                            Component &input_comp) -> int {
            if (!box.Contain(m.x, m.y))
              return 0; // not hit
            input_comp->TakeFocus();
            // Let inner text line event pass through for exact caret placement
            bool on_border = (m.x == box.x_min || m.x == box.x_max ||
                              m.y == box.y_min || m.y == box.y_max);
            return on_border ? 2 : 1;
          };

          if (int r = handle_input_box_click(filter_input_box, filter_input)) {
            return (r == 2);
          }
          if (selected_trip_type != 2) {
            if (int r = handle_input_box_click(src_input_box, source_input)) {
              return (r == 2);
            }
            if (int r = handle_input_box_click(dst_input_box, dest_input)) {
              return (r == 2);
            }
          } else {
            for (int i = 0; i < num_active_legs; ++i) {
              if (int r = handle_input_box_click(leg_src_boxes[i],
                                                 leg_src_inputs[i])) {
                return (r == 2);
              }
              if (int r = handle_input_box_click(leg_dst_boxes[i],
                                                 leg_dst_inputs[i])) {
                return (r == 2);
              }
            }
          }

          // 2. Controls & buttons (move focus away from inputs on click)
          if (swap_box.Contain(m.x, m.y)) {
            swap_btn->TakeFocus();
            std::swap(source_str, dest_str);
            update_suggestions();
            execute_search(show_research_mode);
            return true;
          }
          if (dep_date_box.Contain(m.x, m.y)) {
            dep_date_btn->TakeFocus();
            open_dep_calendar();
            return true;
          }
          if (selected_trip_type == 0 && ret_date_box.Contain(m.x, m.y)) {
            ret_date_btn->TakeFocus();
            open_ret_calendar();
            return true;
          }
          if (selected_trip_type == 0 && tab_outbound_box.Contain(m.x, m.y)) {
            tab_outbound_btn->TakeFocus();
            active_roundtrip_tab = 0;
            apply_filters();
            return true;
          }
          if (selected_trip_type == 0 && tab_return_box.Contain(m.x, m.y)) {
            tab_return_btn->TakeFocus();
            active_roundtrip_tab = 1;
            apply_filters();
            return true;
          }
          if (selected_trip_type == 2) {
            for (int i = 0; i < num_active_legs; ++i) {
              if (leg_tab_boxes[i].Contain(m.x, m.y)) {
                leg_tab_btns[i]->TakeFocus();
                active_multi_leg_idx = i;
                apply_filters();
                return true;
              }
              if (leg_date_boxes[i].Contain(m.x, m.y)) {
                leg_date_btns[i]->TakeFocus();
                open_leg_calendar(i);
                return true;
              }
            }
          }

          if (cheapest_summary_box.Contain(m.x, m.y)) {
            preference_toggle->TakeFocus();
            selected_preference = 1;
            last_preference = 1;
            sync_selected_flight_from_preference();
            return true;
          }
          if (fastest_summary_box.Contain(m.x, m.y)) {
            preference_toggle->TakeFocus();
            selected_preference = 2;
            last_preference = 2;
            sync_selected_flight_from_preference();
            return true;
          }
          for (size_t i = 0;
               i < option_boxes.size() && i < filtered_itineraries.size(); ++i) {
            if (option_boxes[i].Contain(m.x, m.y)) {
              if (i < MAX_OPTION_BUTTONS) {
                opt_buttons[i]->TakeFocus();
              }
              on_select_flight_option(static_cast<int>(i));
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
      if (event == Event::Home && !any_input_focused()) {
        scroll_line = 0;
        return true;
      }
      if (event == Event::End && !any_input_focused()) {
        scroll_line = get_max_scroll();
        return true;
      }

      // 'j' / 'k' vim-style scroll & 'q' exit ONLY when not typing in an input
      if (!any_input_focused()) {
        if (event == Event::Character('j')) {
          scroll_line = std::min(get_max_scroll(), scroll_line + 3);
          return true;
        }
        if (event == Event::Character('k')) {
          scroll_line = std::max(0, scroll_line - 3);
          return true;
        }
        if (event == Event::Character('q')) {
          set_mouse_cursor("");
          screen.Exit();
          return true;
        }
      }

      return false;
    });

    screen.Loop(event_handler);
    set_mouse_cursor("");
  }
};
