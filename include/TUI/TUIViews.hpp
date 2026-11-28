#pragma once

#include "Core/Profiler.hpp"
#include "DataLoader.hpp"
#include "TUI/WorldMap.hpp"
#include <ftxui/dom/elements.hpp>
#include <ftxui/dom/table.hpp>
#include <ftxui/screen/screen.hpp>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

class TUIViews {
public:
  enum class UIMode { CUSTOMER, RESEARCHER };

  // =========================================================================
  // CUSTOMER MODE RENDERING
  // =========================================================================
  static void render_customer_mode(
      const std::string &source_iata, const std::string &dest_iata,
      const std::string &target_date, const DataLoader &loader,
      uint32_t source_id, uint32_t dest_id,
      const RoutingResult &best_recommendation,
      const std::string &recommendation_reason,
      const RoutingResult &cheapest_result, const RoutingResult &fastest_result,
      const std::vector<RoutingResult> &alternatives) {

    using namespace ftxui;

    std::string origin_name = loader.ui_nodes[source_id].name;
    std::string origin_city = loader.ui_nodes[source_id].city;
    std::string dest_name = loader.ui_nodes[dest_id].name;
    std::string dest_city = loader.ui_nodes[dest_id].city;

    // Header Element
    auto header_box = window(
        text(" FLIGHT PLANNER  |  CUSTOMER MODE ") | bold | color(Color::CyanLight),
        vbox({
            hbox({
                text(" Trip: ") | bold | color(Color::White),
                text(origin_city + " (" + source_iata + ")") | bold |
                    color(Color::GreenLight),
                text(" ────────▶ ") | color(Color::Yellow),
                text(dest_city + " (" + dest_iata + ")") | bold |
                    color(Color::GreenLight),
                filler(),
                text(" Departure Date: ") | bold | color(Color::White),
                text(target_date) | bold | color(Color::CyanLight),
                text(" "),
            }),
            separatorLight(),
            hbox({
                text(" Route: ") | dim,
                text(origin_name + " to " + dest_name) | color(Color::GrayLight),
                filler(),
                text("Verified Schedules Available") | color(Color::Green),
                text(" "),
            }),
        })) |
        color(Color::Cyan);

    // Recommended Best Flight Card
    Element best_card;
    if (best_recommendation.success) {
      best_card = build_itinerary_card(
          "⭐ RECOMMENDED BEST FLIGHT (BEST BALANCE)", best_recommendation,
          recommendation_reason, loader, source_id, Color::GreenLight,
          Color::Green, true);
    } else {
      best_card = window(text(" Best Flight ") | bold,
                         text("No flight route available for this date.") |
                             color(Color::Red)) |
                  color(Color::Red);
    }

    // Secondary options (Cheapest & Fastest)
    std::vector<Element> summary_cards;
    if (cheapest_result.success) {
      std::string route_str = build_route_summary(cheapest_result, loader, source_id);
      summary_cards.push_back(
          window(
              text(" 🏷️  CHEAPEST ITINERARY ") | bold | color(Color::Yellow),
              vbox({
                  hbox({
                      text(" Price: ") | bold,
                      text("$" + std::to_string(cheapest_result.total_price)) |
                          bold | color(Color::Yellow),
                      text("  │  Duration: ") | bold,
                      text(format_duration(cheapest_result.total_time_mins)) |
                          color(Color::CyanLight),
                      text("  │  Stops: ") | bold,
                      text(std::to_string(cheapest_result.path.size() - 1)) |
                          color(Color::White),
                  }),
                  separatorLight(),
                  text(" Route: " + route_str) | color(Color::GrayLight),
              })) |
          color(Color::Yellow) | flex);
    }

    if (fastest_result.success) {
      std::string route_str = build_route_summary(fastest_result, loader, source_id);
      summary_cards.push_back(
          window(
              text(" ⚡ FASTEST ITINERARY ") | bold | color(Color::MagentaLight),
              vbox({
                  hbox({
                      text(" Price: ") | bold,
                      text("$" + std::to_string(fastest_result.total_price)) |
                          bold | color(Color::Yellow),
                      text("  │  Duration: ") | bold,
                      text(format_duration(fastest_result.total_time_mins)) |
                          bold | color(Color::MagentaLight),
                      text("  │  Stops: ") | bold,
                      text(std::to_string(fastest_result.path.size() - 1)) |
                          color(Color::White),
                  }),
                  separatorLight(),
                  text(" Route: " + route_str) | color(Color::GrayLight),
              })) |
          color(Color::MagentaLight) | flex);
    }

    // Top alternative options
    std::vector<Element> alt_rows;
    if (!alternatives.empty()) {
      alt_rows.push_back(
          text(" ALTERNATIVE FLIGHT ITINERARIES") | bold | color(Color::CyanLight));
      alt_rows.push_back(separatorLight());

      for (size_t i = 0; i < alternatives.size(); ++i) {
        const auto &alt = alternatives[i];
        if (!alt.success)
          continue;

        std::string stops_str = (alt.path.size() <= 1)
                                    ? "Non-stop"
                                    : (std::to_string(alt.path.size() - 1) +
                                       (alt.path.size() == 2 ? " Stop" : " Stops"));
        std::string route_str = build_route_summary(alt, loader, source_id);

        alt_rows.push_back(hbox({
            text(" Option #" + std::to_string(i + 1) + ": ") | bold |
                color(Color::White),
            text("$" + std::to_string(alt.total_price)) | bold |
                color(Color::Yellow),
            text("  │  ") | dim,
            text(format_duration(alt.total_time_mins)) | color(Color::CyanLight),
            text("  │  ") | dim,
            text(stops_str) | color(Color::White),
            text("  │  ") | dim,
            text(route_str) | color(Color::GrayLight),
        }));
      }
    }

    auto alt_box = window(text(" Additional Flight Choices ") | bold,
                          vbox(std::move(alt_rows))) |
                   color(Color::GrayLight);

    // Assembly
    std::vector<Element> main_layout;
    main_layout.push_back(header_box);
    main_layout.push_back(best_card);
    if (best_recommendation.success) {
      main_layout.push_back(FlightPlanner::TUI::WorldMap::build_route_map(
          best_recommendation, loader, source_id));
    }
    if (!summary_cards.empty()) {
      main_layout.push_back(hbox(std::move(summary_cards)));
    }
    if (!alternatives.empty()) {
      main_layout.push_back(alt_box);
    }

    auto document = vbox(std::move(main_layout));

    document->ComputeRequirement();
    int req_width = std::max(110, document->requirement().min_x);
    int req_height = std::max(10, document->requirement().min_y);
    auto screen = Screen::Create(Dimension::Fixed(req_width), Dimension::Fixed(req_height));
    Render(screen, document);
    screen.Print();
    std::cout << "\n";
  }

  // =========================================================================
  // RESEARCHER MODE RENDERING
  // =========================================================================
  static void render_researcher_mode(
      const std::string &source_iata, const std::string &dest_iata,
      const std::string &target_date, const DataLoader &loader,
      uint32_t source_id, uint32_t dest_id,
      const std::vector<AlgorithmBenchmark> &benchmarks,
      const RoutingResult &alt_best, const std::string &alt_reason,
      double alt_gen_cost, const std::vector<RoutingResult> &alt_k_paths) {

    using namespace ftxui;

    std::string origin_name = loader.ui_nodes[source_id].name;
    std::string dest_name = loader.ui_nodes[dest_id].name;

    // 1. Header Banner
    auto header_box = window(
        text(" FLIGHT ROUTE BENCHMARK LAB  |  RESEARCHER MODE ") | bold |
            color(Color::CyanLight),
        vbox({
            hbox({
                text(" Research Query: ") | bold | color(Color::White),
                text(source_iata + " (" + origin_name + ")") | bold |
                    color(Color::Green),
                text(" ──▶ ") | color(Color::Yellow),
                text(dest_iata + " (" + dest_name + ")") | bold |
                    color(Color::Green),
                filler(),
                text(" Flight Date: ") | bold | color(Color::White),
                text(target_date) | bold | color(Color::Cyan),
                text(" "),
            }),
            separatorLight(),
            text(" Profiling Fast Routing Algorithms: Dynamic Programming, "
                 "Heuristics, Multiple ALT, & Hierarchies.") |
                dim,
        })) |
        color(Color::Cyan);

    // 2. Algorithm Performance Table (C++ Microsecond CPU & Node Expansion Profiler)
    std::vector<std::vector<std::string>> table_data;
    table_data.push_back({"Algorithm", "Mode", "CPU Time", "Wall Time",
                          "Nodes Expanded", "Edges Relaxed", "Price",
                          "Journey Duration"});

    std::string last_algo = "";
    long max_cpu_us = 1;

    for (const auto &bench : benchmarks) {
      long cpu_us = (bench.cpu_time_us > 0 || bench.wall_time_us > 0)
                        ? bench.cpu_time_us
                        : bench.compute_time_us;
      if (cpu_us > max_cpu_us)
        max_cpu_us = cpu_us;

      std::string display_algo = "";
      if (bench.algo_name != last_algo) {
        display_algo = bench.algo_name;
        last_algo = bench.algo_name;
      }

      std::string cpu_str = Profiler::format_cpu_us(cpu_us);
      std::string wall_str = Profiler::format_us_compact(
          bench.wall_time_us > 0 ? bench.wall_time_us : cpu_us);

      std::string cost_str =
          bench.result.success ? ("$" + std::to_string(bench.result.total_price))
                               : "N/A";
      std::string journey_str =
          bench.result.success ? format_duration(bench.result.total_time_mins)
                               : "N/A";
      uint64_t nodes_val = (bench.nodes_expanded > 0)
                               ? bench.nodes_expanded
                               : bench.result.metrics.nodes_explored;
      uint64_t edges_val = (bench.edges_relaxed > 0)
                               ? bench.edges_relaxed
                               : bench.result.metrics.edges_relaxed;
      std::string nodes_str =
          bench.result.success ? std::to_string(nodes_val) : "N/A";
      std::string edges_str =
          bench.result.success ? std::to_string(edges_val) : "N/A";

      table_data.push_back({display_algo, bench.mode_label, cpu_str, wall_str,
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
      ftx_table.SelectCell(3, r).Decorate(color(Color::CyanLight));
      ftx_table.SelectCell(6, r).Decorate(color(Color::YellowLight));
      ftx_table.SelectCell(7, r).Decorate(color(Color::MagentaLight));
    }

    auto table_box = window(
        text(" ALGORITHM BENCHMARK EXECUTION MATRIX (C++ CPU PROFILER) ") | bold |
            color(Color::GreenLight),
        ftx_table.Render()) |
        color(Color::GreenLight);

    // 3. Performance Visual Gauges (Microsecond CPU Time & Expansion Summary)
    ProfilerSummary prof_sum = Profiler::summarize(benchmarks);
    std::ostringstream sum_oss;
    sum_oss << " Total CPU: " << Profiler::format_cpu_us(prof_sum.total_cpu_us)
            << " │ Wall: " << Profiler::format_us_compact(prof_sum.total_wall_us)
            << " │ Nodes Expanded: " << prof_sum.total_nodes_expanded
            << " │ Peak RSS: " << std::fixed << std::setprecision(2)
            << (static_cast<double>(prof_sum.peak_rss_bytes) /
                (1024.0 * 1024.0 * 1024.0))
            << " GB";

    std::vector<Element> visual_gauges;
    visual_gauges.push_back(
        text(" C++ PROFILER SUMMARY (MICROSECOND CPU TIME & NODES EXPANDED) ") |
        bold | color(Color::YellowLight));
    visual_gauges.push_back(text(sum_oss.str()) | color(Color::CyanLight));
    visual_gauges.push_back(
        text(" Fastest CPU: " + prof_sum.fastest_label + " (" +
             Profiler::format_cpu_us(prof_sum.fastest_cpu_us) +
             ") │ Fewest Nodes: " + prof_sum.fewest_nodes_label + " (" +
             std::to_string(prof_sum.fewest_nodes) + " nodes)") |
        color(Color::GreenLight));
    visual_gauges.push_back(separatorLight());

    for (const auto &bench : benchmarks) {
      long cpu_us = (bench.cpu_time_us > 0 || bench.wall_time_us > 0)
                        ? bench.cpu_time_us
                        : bench.compute_time_us;
      float ratio = (max_cpu_us > 0) ? (float)cpu_us / (float)max_cpu_us : 0.0f;
      if (ratio < 0.02f)
        ratio = 0.02f;

      std::string label = bench.algo_name + " [" + bench.mode_label + "]";
      if (label.size() < 24)
        label += std::string(24 - label.size(), ' ');
      else
        label = label.substr(0, 24);

      visual_gauges.push_back(hbox({
          text(label) | color(Color::GrayLight),
          gauge(ratio) | color(Color::Cyan) | flex,
          text(" " + Profiler::format_cpu_us(cpu_us)) | bold |
              color(Color::GreenLight) | size(WIDTH, EQUAL, 12),
      }));
    }

    auto gauge_box = window(text(" VISUAL PROFILER ") | bold | color(Color::YellowLight),
                            vbox(std::move(visual_gauges))) |
                     color(Color::YellowLight);

    // 4. Multiple ALT Recommendation & Trade-off Analysis
    Element multi_alt_box;
    if (alt_best.success) {
      std::ostringstream oss;
      oss << "Recommended: $" << alt_best.total_price << " ("
          << format_duration(alt_best.total_time_mins) << ", "
          << (alt_best.path.empty() ? 0 : alt_best.path.size() - 1)
          << " transits) | Reason: " << alt_reason;

      std::ostringstream oss_gen;
      oss_gen << "Generalized Economic Cost: $" << std::fixed
              << std::setprecision(2) << alt_gen_cost
              << " (Value of Time: $25.00/hr, Layover Penalty: $15/stop)";

      std::vector<Element> alt_rows;
      alt_rows.push_back(text(oss.str()) | bold | color(Color::GreenLight));
      alt_rows.push_back(text(oss_gen.str()) | color(Color::YellowLight));
      alt_rows.push_back(separatorLight());

      for (size_t i = 0; i < alt_k_paths.size(); ++i) {
        const auto &res = alt_k_paths[i];
        std::string r_str = build_route_summary(res, loader, source_id);
        alt_rows.push_back(hbox({
            text("Alt #" + std::to_string(i + 1) + ": ") | bold |
                color(Color::White),
            text("$" + std::to_string(res.total_price)) | bold |
                color(Color::Yellow),
            text(" │ " + format_duration(res.total_time_mins)) |
                color(Color::CyanLight),
            text(" │ " + std::to_string(res.path.size() - 1) + " stops") |
                color(Color::GrayLight),
            text(" │ " + r_str) | color(Color::White),
        }));
      }

      multi_alt_box =
          window(text(" MULTIPLE ALT RECOMMENDATIONS & ECONOMIC ANALYSIS ") |
                     bold | color(Color::MagentaLight),
                 vbox(std::move(alt_rows))) |
          color(Color::MagentaLight);
    }

    // Assembly
    std::vector<Element> main_layout;
    main_layout.push_back(header_box);
    main_layout.push_back(table_box);
    main_layout.push_back(gauge_box);
    if (alt_best.success) {
      main_layout.push_back(multi_alt_box);
      main_layout.push_back(FlightPlanner::TUI::WorldMap::build_route_map(
          alt_best, loader, source_id));
    }

    auto document = vbox(std::move(main_layout));

    document->ComputeRequirement();
    int req_width = std::max(115, document->requirement().min_x);
    int req_height = std::max(10, document->requirement().min_y);
    auto screen = Screen::Create(Dimension::Fixed(req_width), Dimension::Fixed(req_height));
    Render(screen, document);
    screen.Print();
    std::cout << "\n";
  }

public:
  static std::string format_duration(uint32_t total_mins) {
    uint32_t h = total_mins / 60;
    uint32_t m = total_mins % 60;
    std::string s = std::to_string(h) + "h";
    if (m > 0 || h == 0) {
      s += " " + std::to_string(m) + "m";
    }
    return s;
  }

  static std::string build_route_summary(const RoutingResult &res,
                                         const DataLoader &loader,
                                         uint32_t source_id) {
    if (res.path.empty())
      return loader.ui_nodes[source_id].iata_code;
    std::string s = loader.ui_nodes[source_id].iata_code;
    for (const auto &f : res.path) {
      s += " ──▶ " + loader.ui_nodes[f.destination_id].iata_code;
    }
    return s;
  }

  static ftxui::Element build_itinerary_card(
      const std::string &card_title, const RoutingResult &res,
      const std::string &reason, const DataLoader &loader, uint32_t source_id,
      ftxui::Color title_color, ftxui::Color border_color, bool detailed) {

    using namespace ftxui;

    std::vector<Element> elements;

    // Metric Summary Badges
    std::string stops_str =
        (res.path.size() <= 1)
            ? "Direct (Non-Stop)"
            : (std::to_string(res.path.size() - 1) +
               (res.path.size() == 2 ? " Transit Layover" : " Transit Layovers"));

    elements.push_back(hbox({
        text(" Total Ticket Price: ") | bold,
        text("$" + std::to_string(res.total_price)) | bold |
            color(Color::YellowLight),
        text("   │   Total Journey Time: ") | bold,
        text(format_duration(res.total_time_mins)) | bold |
            color(Color::CyanLight),
        text("   │   Stops: ") | bold,
        text(stops_str) | bold | color(Color::GreenLight),
    }));

    if (!reason.empty()) {
      elements.push_back(
          text(" 💡 " + reason) | color(Color::GrayLight));
    }

    elements.push_back(separatorLight());

    // Leg-by-leg breakdown
    uint32_t curr_airport = source_id;
    for (size_t i = 0; i < res.path.size(); ++i) {
      const flight &f = res.path[i];
      const flightDetails &f_ui = loader.ui_flights[f.flight_details_id];
      std::string orig_iata = loader.ui_nodes[curr_airport].iata_code;
      std::string dest_iata = loader.ui_nodes[f.destination_id].iata_code;
      std::string orig_city = loader.ui_nodes[curr_airport].city;
      std::string dest_city = loader.ui_nodes[f.destination_id].city;
      std::string airline_name = loader.get_airline_name(f_ui.airline_code);

      uint32_t dep_min = f.departure_time;
      uint32_t arr_min = f.arrival_time;

      std::string dep_str =
          (dep_min / 60 < 10 ? "0" : "") + std::to_string(dep_min / 60) + ":" +
          (dep_min % 60 < 10 ? "0" : "") + std::to_string(dep_min % 60);
      std::string arr_str =
          (arr_min / 60 < 10 ? "0" : "") + std::to_string(arr_min / 60) + ":" +
          (arr_min % 60 < 10 ? "0" : "") + std::to_string(arr_min % 60);
      std::string next_day = (arr_min < dep_min) ? " (+1 day)" : "";

      std::string flight_num_str = f_ui.airline_code + " " + std::to_string(f_ui.flight_number);

      elements.push_back(hbox({
          text("  Flight " + std::to_string(i + 1) + ": ") | bold |
              color(Color::White),
          text(airline_name + " (" + flight_num_str + ")") | bold |
              color(Color::CyanLight),
          text("  |  Aircraft: " + f_ui.plane_model) | dim,
      }));

      elements.push_back(hbox({
          text("    " + orig_city + " (" + orig_iata + ") ") |
              color(Color::White),
          text(dep_str) | bold | color(Color::GreenLight),
          text(" ──▶ ") | color(Color::Yellow),
          text(dest_city + " (" + dest_iata + ") ") | color(Color::White),
          text(arr_str + next_day) | bold | color(Color::GreenLight),
          text("  (" + format_duration(f.duration_mins) + ")") |
              color(Color::GrayLight),
      }));

      // Layover information between legs
      if (i + 1 < res.path.size()) {
        const flight &next_f = res.path[i + 1];
        int32_t orig_offset = loader.nodes[curr_airport].utc_offset_15m * 15;
        uint32_t dep_utc = static_cast<uint32_t>(
            static_cast<int32_t>(f.flight_day * 1440 + f.departure_time) -
            orig_offset);
        uint32_t arr_utc = dep_utc + f.duration_mins;

        int32_t dest_offset = loader.nodes[f.destination_id].utc_offset_15m * 15;
        uint32_t next_dep_utc = static_cast<uint32_t>(
            static_cast<int32_t>(next_f.flight_day * 1440 + next_f.departure_time) -
            dest_offset);

        uint32_t transit_min =
            (next_dep_utc >= arr_utc) ? (next_dep_utc - arr_utc) : 0;

        elements.push_back(hbox({
            text("    ⏳ Layover: ") | bold | color(Color::Yellow),
            text(format_duration(transit_min)) | bold | color(Color::YellowLight),
            text(" transit at " + dest_city + " (" + dest_iata + ")") |
                color(Color::GrayLight),
        }));
      }

      curr_airport = f.destination_id;
    }

    return window(text(" " + card_title + " ") | bold | color(title_color),
                  vbox(std::move(elements))) |
           color(border_color);
  }
};
