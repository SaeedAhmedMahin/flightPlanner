#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "Algorithms/Dijkstra.hpp"
#include "DataLoader.hpp"
#include <ftxui/dom/canvas.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>

namespace FlightPlanner::TUI {

struct GeoCoord {
  float lat;
  float lon;
};

class WorldMap {
public:
  // Haversine distance in kilometers between two geo coordinates
  static double haversine_km(float lat1, float lon1, float lat2, float lon2) {
    constexpr double R = 6371.0;
    constexpr double DEG_TO_RAD = 3.14159265358979323846 / 180.0;
    double dlat = (lat2 - lat1) * DEG_TO_RAD;
    double dlon = (lon2 - lon1) * DEG_TO_RAD;
    double rlat1 = lat1 * DEG_TO_RAD;
    double rlat2 = lat2 * DEG_TO_RAD;
    double a = std::sin(dlat / 2.0) * std::sin(dlat / 2.0) +
               std::cos(rlat1) * std::cos(rlat2) * std::sin(dlon / 2.0) *
                   std::sin(dlon / 2.0);
    double c = 2.0 * std::atan2(std::sqrt(a), std::sqrt(std::max(0.0, 1.0 - a)));
    return R * c;
  }

  // Haversine distance in miles
  static double haversine_miles(float lat1, float lon1, float lat2, float lon2) {
    return haversine_km(lat1, lon1, lat2, lon2) * 0.621371;
  }

  // Interpolates N points along the Great Circle (orthodromic geodesic) on the sphere
  static std::vector<GeoCoord>
  compute_great_circle_points(GeoCoord p1, GeoCoord p2, int num_samples = 36) {
    std::vector<GeoCoord> pts;
    double rlat1 = p1.lat * M_PI / 180.0;
    double rlon1 = p1.lon * M_PI / 180.0;
    double rlat2 = p2.lat * M_PI / 180.0;
    double rlon2 = p2.lon * M_PI / 180.0;

    double sin_dlat = std::sin((rlat2 - rlat1) / 2.0);
    double sin_dlon = std::sin((rlon2 - rlon1) / 2.0);
    double a = sin_dlat * sin_dlat +
               std::cos(rlat1) * std::cos(rlat2) * sin_dlon * sin_dlon;
    double d = 2.0 * std::atan2(std::sqrt(a), std::sqrt(std::max(0.0, 1.0 - a)));

    if (d < 1e-6) {
      pts.push_back(p1);
      pts.push_back(p2);
      return pts;
    }

    pts.reserve(num_samples + 1);
    for (int i = 0; i <= num_samples; ++i) {
      double f = static_cast<double>(i) / num_samples;
      double A = std::sin((1.0 - f) * d) / std::sin(d);
      double B = std::sin(f * d) / std::sin(d);
      double x = A * std::cos(rlat1) * std::cos(rlon1) +
                 B * std::cos(rlat2) * std::cos(rlon2);
      double y = A * std::cos(rlat1) * std::sin(rlon1) +
                 B * std::cos(rlat2) * std::sin(rlon2);
      double z = A * std::sin(rlat1) + B * std::sin(rlat2);
      double plat = std::atan2(z, std::sqrt(x * x + y * y)) * 180.0 / M_PI;
      double plon = std::atan2(y, x) * 180.0 / M_PI;
      pts.push_back({static_cast<float>(plat), static_cast<float>(plon)});
    }
    return pts;
  }

  // Equirectangular projection mapping (lat, lon) -> (canvas_x, canvas_y)
  // Lat: +80 (top) to -60 (bottom)
  // Lon: -180 (left) to +180 (right)
  static std::pair<int, int> project(float lat, float lon, int W, int H) {
    float norm_x = (lon + 180.0f) / 360.0f;
    float norm_y = (80.0f - lat) / 140.0f; // 80°N to 60°S (140° span)
    int px = std::clamp(static_cast<int>(norm_x * (W - 1)), 0, W - 1);
    int py = std::clamp(static_cast<int>(norm_y * (H - 1)), 0, H - 1);
    return {px, py};
  }

  // Pre-compiled continent & major island coastlines
  static const std::vector<std::vector<GeoCoord>> &get_coastlines() {
    static const std::vector<std::vector<GeoCoord>> coastlines = {
        // 1. North America (Alaska, Canada, Greenland, US, Mexico, Central America)
        {{65, -168}, {71, -165}, {71, -156}, {70, -140}, {69, -130}, {69, -120},
         {69, -95},  {70, -85},  {64, -95},  {63, -82},  {55, -78},  {51, -82},
         {58, -80},  {60, -65},  {55, -60},  {52, -55},  {45, -65},  {43, -70},
         {40, -74},  {37, -75},  {32, -80},  {25, -81},  {28, -82},  {30, -88},
         {29, -95},  {26, -97},  {20, -97},  {21, -90},  {21, -87},  {16, -90},
         {14, -88},  {9, -83},   {8, -77},   {8, -78},   {10, -85},  {16, -95},
         {21, -105}, {24, -110}, {30, -115}, {33, -117}, {37, -122}, {42, -124},
         {49, -125}, {54, -130}, {59, -140}, {60, -150}, {56, -160}, {65, -168}},

        // 2. Greenland
        {{60, -50}, {60, -43}, {65, -35}, {70, -20}, {77, -18}, {82, -20},
         {83, -35}, {82, -55}, {76, -70}, {70, -55}, {60, -50}},

        // 3. South America
        {{8, -77},   {12, -72},  {10, -65},  {9, -60},   {0, -50},   {-2, -45},
         {-5, -35},  {-9, -35},  {-13, -37}, {-18, -39}, {-23, -43}, {-28, -48},
         {-33, -53}, {-38, -58}, {-43, -65}, {-50, -68}, {-55, -66}, {-53, -75},
         {-45, -74}, {-37, -73}, {-30, -71}, {-20, -70}, {-14, -76}, {-5, -81},
         {0, -80},   {4, -77},   {8, -77}},

        // 4. Scandinavia
        {{58, 5}, {58, 10}, {60, 18}, {65, 24}, {70, 28},
         {70, 20}, {65, 14}, {63, 10}, {62, 5}, {58, 5}},

        // 5. Western & Southern Europe (Iberia, France, Germany, Italy, Balkans)
        {{54, 9},  {52, 4},  {49, 0},   {48, -4},  {46, -1},  {43, -2},
         {43, -9}, {37, -9}, {36, -5},  {39, 0},   {43, 3},   {44, 9},
         {44, 12}, {41, 15}, {40, 18},  {38, 16},  {40, 15},  {42, 12},
         {44, 9},  {42, 19}, {38, 22},  {35, 24},  {40, 26},  {41, 28}},

        // 6. British Isles
        {{50, -5}, {51, 1}, {53, 0}, {58, -1}, {59, -5}, {55, -6}, {50, -5}},
        {{52, -10}, {52, -6}, {55, -6}, {54, -10}, {52, -10}},

        // 7. Africa
        {{35, -6},  {36, 3},   {37, 10},  {33, 11},  {32, 15},  {32, 25},
         {31, 32},  {27, 34},  {27, 35},  {22, 38},  {13, 43},  {10, 51},
         {2, 48},   {-4, 41},  {-10, 40}, {-20, 36}, {-27, 33}, {-34, 28},
         {-34, 18}, {-23, 15}, {-15, 12}, {-6, 12},  {4, 9},    {6, 3},
         {5, -5},   {7, -12},  {15, -17}, {21, -17}, {28, -12}, {35, -6}},

        // 8. Madagascar
        {{-12, 44}, {-13, 50}, {-25, 50}, {-25, 44}, {-12, 44}},

        // 9. Arabian Peninsula & Middle East
        {{30, 35}, {13, 44}, {15, 50}, {22, 60}, {26, 56}, {27, 50}, {30, 48}},

        // 10. South Asia (India & Sri Lanka)
        {{24, 68}, {20, 70}, {15, 73}, {8, 77}, {13, 80}, {18, 83}, {22, 88}, {21, 92}},
        {{9, 80}, {6, 81}, {8, 82}, {9, 80}},

        // 11. Southeast Asia (Indochina, Malay Peninsula)
        {{10, 98}, {5, 100}, {1, 104}, {10, 104}, {12, 108}, {20, 108}},

        // 12. East Asia & Arctic Russia
        {{21, 108}, {22, 115}, {27, 120}, {32, 122}, {38, 120}, {40, 124},
         {43, 130}, {47, 135}, {53, 142}, {59, 150}, {55, 160}, {60, 162},
         {65, 170}, {66, 180}, {70, 160}, {73, 140}, {75, 115}, {73, 80},
         {69, 60},  {67, 40},  {70, 30}},

        // 13. Japan
        {{31, 130}, {34, 135}, {36, 140}, {43, 142}, {44, 145}, {39, 140}, {33, 133}, {31, 130}},

        // 14. Indonesia, Philippines, PNG
        {{-6, 106}, {-7, 114}, {-8, 115}, {-6, 106}}, // Java
        {{0, 109}, {4, 117}, {-4, 118}, {0, 109}},     // Borneo
        {{15, 120}, {10, 125}, {6, 121}, {15, 120}},   // Philippines
        {{-3, 141}, {-10, 150}, {-9, 141}, {-3, 141}}, // Papua New Guinea

        // 15. Australia & Tasmania
        {{-22, 114}, {-26, 113}, {-34, 115}, {-34, 122}, {-32, 130},
         {-35, 138}, {-38, 141}, {-38, 147}, {-28, 153}, {-22, 150},
         {-11, 143}, {-12, 136}, {-13, 131}, {-16, 124}, {-22, 114}},
        {{-41, 145}, {-43, 147}, {-41, 148}, {-41, 145}},

        // 16. New Zealand
        {{-46, 168}, {-41, 174}, {-38, 178}, {-35, 173}},
    };
    return coastlines;
  }

  // Builds the complete FTXUI World Map Element for an active flight itinerary
  static ftxui::Element
  build_route_map(const RoutingResult &res, const DataLoader &loader,
                  uint32_t source_id, int canvas_width = -1,
                  int canvas_height = 46) {
    using namespace ftxui;

    // Responsive dimensions
    int term_cols = Terminal::Size().dimx;
    int W = (canvas_width > 0) ? canvas_width
                               : std::clamp((term_cols - 8) * 2, 160, 220);
    int H = canvas_height;

    Canvas c(W, H);

    // 1. Reference Grid Lines
    // Equator (lat = 0)
    auto eq_pt = project(0.0f, 0.0f, W, H);
    int eq_y = eq_pt.second;
    for (int x = 0; x < W; x += 3) {
      c.DrawPoint(x, eq_y, true, Color::RGB(35, 45, 60));
    }
    // Prime Meridian (lon = 0)
    auto pm_pt = project(0.0f, 0.0f, W, H);
    int pm_x = pm_pt.first;
    for (int y = 0; y < H; y += 3) {
      c.DrawPoint(pm_x, y, true, Color::RGB(35, 45, 60));
    }
    // Tropics (lat = +23.5 and -23.5)
    auto tr_n = project(23.5f, 0.0f, W, H);
    auto tr_s = project(-23.5f, 0.0f, W, H);
    int tr_n_y = tr_n.second;
    int tr_s_y = tr_s.second;
    for (int x = 0; x < W; x += 6) {
      c.DrawPoint(x, tr_n_y, true, Color::RGB(28, 36, 48));
      c.DrawPoint(x, tr_s_y, true, Color::RGB(28, 36, 48));
    }

    // 2. Global Airport Hubs Network Background (Subtle starry illumination)
    size_t node_step = std::max<size_t>(1, loader.nodes.size() / 350);
    for (size_t i = 0; i < loader.nodes.size(); i += node_step) {
      float alat = loader.nodes[i].latitude;
      float alon = loader.nodes[i].longitude;
      if (alat >= -60.0f && alat <= 80.0f) {
        auto [ax, ay] = project(alat, alon, W, H);
        c.DrawPoint(ax, ay, true, Color::RGB(32, 42, 54));
      }
    }

    // 3. Draw Continents & Coastlines
    const auto &coastlines = get_coastlines();
    for (const auto &poly : coastlines) {
      for (size_t i = 0; i + 1 < poly.size(); ++i) {
        auto [x1, y1] = project(poly[i].lat, poly[i].lon, W, H);
        auto [x2, y2] = project(poly[i + 1].lat, poly[i + 1].lon, W, H);
        c.DrawPointLine(x1, y1, x2, y2, Color::RGB(65, 85, 115));
      }
    }

    // 4. Extract Route Airport Sequence
    struct RouteNode {
      uint32_t id;
      std::string iata;
      std::string city;
      std::string country;
      GeoCoord coord;
    };

    auto get_country = [&](uint32_t id) -> std::string {
      if (id < loader.ui_nodes.size()) {
        uint16_t cid = loader.ui_nodes[id].country_id;
        if (cid < loader.country_pool.size()) {
          return loader.country_pool[cid];
        }
      }
      return "";
    };

    std::vector<RouteNode> route_nodes;
    if (source_id < loader.nodes.size() && source_id < loader.ui_nodes.size()) {
      route_nodes.push_back({
          source_id,
          loader.ui_nodes[source_id].iata_code,
          loader.ui_nodes[source_id].city,
          get_country(source_id),
          {loader.nodes[source_id].latitude, loader.nodes[source_id].longitude},
      });
    }

    for (const auto &leg : res.path) {
      uint32_t dest_id = leg.destination_id;
      if (dest_id < loader.nodes.size() && dest_id < loader.ui_nodes.size()) {
        route_nodes.push_back({
            dest_id,
            loader.ui_nodes[dest_id].iata_code,
            loader.ui_nodes[dest_id].city,
            get_country(dest_id),
            {loader.nodes[dest_id].latitude, loader.nodes[dest_id].longitude},
        });
      }
    }

    // 5. Plot Route Legs (Great Circle Arcs)
    std::vector<Color> leg_colors = {
        Color::CyanLight,
        Color::YellowLight,
        Color::MagentaLight,
        Color::GreenLight,
        Color::RedLight,
    };

    double total_distance_km = 0.0;

    for (size_t i = 0; i + 1 < route_nodes.size(); ++i) {
      const auto &p1 = route_nodes[i].coord;
      const auto &p2 = route_nodes[i + 1].coord;

      total_distance_km += haversine_km(p1.lat, p1.lon, p2.lat, p2.lon);
      Color leg_col = leg_colors[i % leg_colors.size()];

      auto arc_points = compute_great_circle_points(p1, p2, 40);
      int prev_x = -1, prev_y = -1;

      for (size_t k = 0; k < arc_points.size(); ++k) {
        auto [px, py] = project(arc_points[k].lat, arc_points[k].lon, W, H);
        if (prev_x != -1) {
          // Wrap prevention over international date line
          if (std::abs(px - prev_x) < W / 2) {
            c.DrawPointLine(prev_x, prev_y, px, py, leg_col);
          }
        }
        prev_x = px;
        prev_y = py;
      }

      // Draw mid-flight airplane glyph at leg midpoint
      if (arc_points.size() > 20) {
        size_t mid_idx = arc_points.size() / 2;
        auto [mx, my] =
            project(arc_points[mid_idx].lat, arc_points[mid_idx].lon, W, H);
        int tx = std::clamp(mx - 2, 0, W - 6);
        int ty = std::clamp(my - 2, 0, H - 4);
        c.DrawText(tx, ty, "✈", leg_col);
      }
    }

    // 6. Draw Airport Markers and IATA Labels
    for (size_t i = 0; i < route_nodes.size(); ++i) {
      auto [px, py] =
          project(route_nodes[i].coord.lat, route_nodes[i].coord.lon, W, H);

      bool is_origin = (i == 0);
      bool is_dest = (i + 1 == route_nodes.size());

      Color marker_col;
      if (is_origin) {
        marker_col = Color::GreenLight;
      } else if (is_dest) {
        marker_col = Color::RedLight;
      } else {
        marker_col = Color::YellowLight;
      }

      // Airport node marker circle
      c.DrawPointCircleFilled(px, py, 2, marker_col);

      // Label offset positioning to avoid canvas edges
      int label_x = (px + 12 < W) ? (px + 3) : (px - 10);
      int label_y = (py + 4 < H) ? py : (py - 4);
      label_x = std::clamp(label_x, 0, W - 8);
      label_y = std::clamp(label_y, 0, H - 4);

      c.DrawText(label_x, label_y, route_nodes[i].iata, marker_col);
    }

    // 7. Format Information Banner & Metrics
    double total_distance_mi = total_distance_km * 0.621371;

    std::ostringstream dist_oss;
    dist_oss << std::fixed << std::setprecision(0) << total_distance_mi
             << " mi (" << total_distance_km << " km)";

    // Route sequence breadcrumb
    std::vector<Element> breadcrumbs;
    for (size_t i = 0; i < route_nodes.size(); ++i) {
      if (i > 0) {
        breadcrumbs.push_back(text(" ──▶ ") | dim | color(Color::CyanLight));
      }
      if (i == 0) {
        breadcrumbs.push_back(
            text("🟢 " + route_nodes[i].iata + " (" + route_nodes[i].city + ")") |
            bold | color(Color::GreenLight));
      } else if (i + 1 == route_nodes.size()) {
        breadcrumbs.push_back(
            text("🔴 " + route_nodes[i].iata + " (" + route_nodes[i].city + ")") |
            bold | color(Color::RedLight));
      } else {
        breadcrumbs.push_back(
            text("🟡 " + route_nodes[i].iata + " (" + route_nodes[i].city + ")") |
            bold | color(Color::YellowLight));
      }
    }

    int transits_count = (route_nodes.size() > 2)
                             ? static_cast<int>(route_nodes.size() - 2)
                             : 0;

    auto metrics_row = hbox({
        text("  📏 Great Circle Distance: ") | bold,
        text(dist_oss.str()) | bold | color(Color::CyanLight),
        text("   │   Transits: ") | bold,
        text(transits_count == 0 ? "Non-Stop Direct"
                                 : (std::to_string(transits_count) + " Layover" +
                                    (transits_count > 1 ? "s" : ""))) |
            bold |
            (transits_count == 0 ? color(Color::GreenLight)
                                 : color(Color::YellowLight)),
        text("   │   Total Airfare: ") | bold,
        text("$" + std::to_string(res.total_price)) | bold |
            color(Color::YellowLight),
        filler(),
        text("Equirectangular Geodesic  ") | dim,
    });

    return window(
        hbox({
            text(" 🗺️  FLIGHT TRAJECTORY WORLD MAP ") | bold |
                color(Color::CyanLight),
            filler(),
            text(" Route Visualization ") | dim,
        }),
        vbox({
            canvas(std::move(c)) | hcenter,
            separatorLight(),
            hbox(std::move(breadcrumbs)) | hcenter,
            separatorLight(),
            metrics_row,
        })) |
        color(Color::Cyan);
  }

  // Pre-search or reference world map showing direct geodesic connection between source and destination
  static ftxui::Element
  build_preview_map(uint32_t source_id, uint32_t dest_id,
                    const DataLoader &loader, int canvas_width = -1,
                    int canvas_height = 46) {
    using namespace ftxui;

    RoutingResult preview_res;
    preview_res.success = true;
    preview_res.total_price = 0;
    preview_res.total_time_mins = 0;

    flight f;
    f.destination_id = dest_id;
    f.flight_details_id = 0;
    preview_res.path.push_back(f);

    return build_route_map(preview_res, loader, source_id, canvas_width,
                           canvas_height);
  }
};

} // namespace FlightPlanner::TUI
