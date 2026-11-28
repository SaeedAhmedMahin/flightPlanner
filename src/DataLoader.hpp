#pragma once

// Standard libraries
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

// Your custom Data Structures
#include "DataStructures/airport.hpp"
#include "DataStructures/airportDetails.hpp"
#include "DataStructures/flight.hpp"
#include "DataStructures/flightDetails.hpp"

class DataLoader {
public:
  static uint16_t parse_date_to_days(const std::string &date_str) {
    int y, m, d;
    if (sscanf(date_str.c_str(), "%d-%d-%d", &y, &m, &d) != 3)
      return 0;
    static const int days_in_month[] = {0,  31, 28, 31, 30, 31, 30,
                                        31, 31, 30, 31, 30, 31};
    auto is_leap = [](int yr) {
      return (yr % 4 == 0 && yr % 100 != 0) || (yr % 400 == 0);
    };
    if (y < 2027 || (y == 2027 && m < 2))
      return 0;
    int days = 0;
    for (int year = 2027; year < y; ++year) {
      days += is_leap(year) ? 366 : 365;
    }
    for (int month = 1; month < m; ++month) {
      days += days_in_month[month];
      if (month == 2 && is_leap(y))
        days++;
    }
    days += d - 1;
    // Epoch is 2027-02-01 (day 31 of 2027, 0-indexed)
    return days > 31 ? static_cast<uint16_t>(days - 31) : 0;
  }

  // Core Algorithmic Graph
  std::vector<airport> nodes;
  std::vector<std::vector<flight>> adjacency_list;
  std::vector<std::vector<uint32_t>>
      static_route_graph; // Unique airport-to-airport corridors
  std::vector<std::vector<uint32_t>>
      reverse_static_graph; // Ingoing corridors to each airport
  std::vector<std::vector<uint16_t>>
      static_route_min_price; // Minimum ticket price on each corridor (aligned with static_route_graph)
  std::vector<std::vector<uint16_t>>
      static_route_min_duration; // Minimum flight duration in mins on each corridor (aligned with static_route_graph)
  std::unordered_map<std::string, uint32_t> iata_to_id;

  // UI Metadata
  std::vector<airportDetails> ui_nodes;
  std::vector<flightDetails> ui_flights;

  // String Pools
  std::vector<std::string> country_pool;
  std::vector<std::string> airline_pool;
  std::unordered_map<std::string, std::string> airline_names;

  void parse_airlines(const std::string &filepath) {
    std::string path = resolve_path(filepath);
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string line;
    while (std::getline(file, line)) {
      if (line.empty()) continue;
      auto cols = parse_csv_line(line);
      // Cols: 0: Airline ID, 1: Name, 2: Alias, 3: IATA, 4: ICAO, 5: Callsign, 6: Country, 7: Active
      if (cols.size() >= 4 && !cols[3].empty() && cols[3] != "-" && cols[3] != "\\N") {
        std::string iata = cols[3];
        std::string name = cols[1];
        if (!name.empty() && name != "\\N") {
          airline_names[iata] = name;
        }
      }
    }
  }

  std::string get_airline_name(const std::string &iata_code) const {
    auto it = airline_names.find(iata_code);
    if (it != airline_names.end()) {
      return it->second;
    }
    return iata_code;
  }

  void load_data(const std::string &airports_csv,
                 const std::string &routes_csv,
                 const std::string &airlines_csv = "data/raw/airlines.dat") {
    parse_airlines(airlines_csv);
    // Modern active carrier overrides where OpenFlights snapshot had reused codes
    airline_names["BS"] = "US-Bangla Airlines";
    parse_airports(airports_csv);

    // Resize adjacency lists to exactly match the number of airports (V)
    adjacency_list.resize(nodes.size());
    static_route_graph.resize(nodes.size());
    reverse_static_graph.resize(nodes.size());
    static_route_min_price.resize(nodes.size());
    static_route_min_duration.resize(nodes.size());

    parse_routes(routes_csv);

    // Sort each airport's outgoing flights by flight_day and departure_time so
    // routers can binary-search a narrow date window instead of scanning 334 days.
    {
      auto sort_bucket = [](std::vector<flight> &edges) {
        if (edges.size() <= 1)
          return;
        std::sort(edges.begin(), edges.end(),
                  [](const flight &a, const flight &b) {
                    if (a.flight_day != b.flight_day)
                      return a.flight_day < b.flight_day;
                    if (a.departure_time != b.departure_time)
                      return a.departure_time < b.departure_time;
                    if (a.destination_id != b.destination_id)
                      return a.destination_id < b.destination_id;
                    return a.price < b.price;
                  });
      };

      const size_t num_airports = adjacency_list.size();
      const unsigned hw = std::thread::hardware_concurrency();
      const unsigned num_threads =
          std::min<unsigned>(4, std::max<unsigned>(1, hw));
      if (num_threads > 1 && num_airports >= 64) {
        std::vector<std::thread> workers;
        workers.reserve(num_threads);
        for (unsigned t = 0; t < num_threads; ++t) {
          workers.emplace_back([&, t, num_threads]() {
            for (size_t u = t; u < num_airports; u += num_threads) {
              sort_bucket(adjacency_list[u]);
            }
          });
        }
        for (auto &w : workers)
          w.join();
      } else {
        for (auto &edges : adjacency_list) {
          sort_bucket(edges);
        }
      }
    }

    // Build unique static corridor connections with minimum route price and duration
    for (size_t u = 0; u < adjacency_list.size(); ++u) {
      struct EdgeStats {
        uint16_t min_price;
        uint16_t min_duration;
      };
      std::unordered_map<uint32_t, EdgeStats> dest_stats;

      for (const auto &f : adjacency_list[u]) {
        uint16_t dur = f.duration_mins;
        auto it = dest_stats.find(f.destination_id);
        if (it == dest_stats.end()) {
          dest_stats[f.destination_id] = {f.price, dur};
        } else {
          if (f.price < it->second.min_price) it->second.min_price = f.price;
          if (dur < it->second.min_duration) it->second.min_duration = dur;
        }
      }

      std::vector<uint32_t> neighbors;
      std::vector<uint16_t> min_prices;
      std::vector<uint16_t> min_durations;
      neighbors.reserve(dest_stats.size());
      min_prices.reserve(dest_stats.size());
      min_durations.reserve(dest_stats.size());

      for (const auto &pair : dest_stats) {
        neighbors.push_back(pair.first);
      }
      std::sort(neighbors.begin(), neighbors.end());

      for (uint32_t v : neighbors) {
        min_prices.push_back(dest_stats[v].min_price);
        min_durations.push_back(dest_stats[v].min_duration);
        reverse_static_graph[v].push_back(static_cast<uint32_t>(u));
      }

      static_route_graph[u] = std::move(neighbors);
      static_route_min_price[u] = std::move(min_prices);
      static_route_min_duration[u] = std::move(min_durations);

      // Dynamically calibrate Minimum Connecting Time (MCT):
      // - Global minimum floor: 45 minutes for standard / regional airports.
      // - Major international hubs (degree >= 40, e.g. DEL, DOH, BKK): 60 minutes.
      // - Mega-hubs (degree >= 110, e.g. DXB, LHR, JFK, CDG, FRA): 90 minutes.
      // - Extreme mega-hubs (degree >= 180, e.g. DXB, FRA, CDG): 90-120 minutes.
      size_t deg = static_route_graph[u].size();
      if (deg >= 180) {
        nodes[u].min_transfer = 105; // 105 mins for extreme mega-hubs (DXB, FRA, CDG)
      } else if (deg >= 110) {
        nodes[u].min_transfer = 90;  // 90 mins for mega-hubs (LHR, JFK, AMS, ATL, ORD)
      } else if (deg >= 40) {
        nodes[u].min_transfer = 60;  // 60 mins for major hubs (DEL, DOH, BKK, SIN)
      } else {
        nodes[u].min_transfer = 45;  // 45 mins global minimum floor
      }
    }
  }

private:
  std::unordered_map<std::string, uint16_t> country_map;
  std::unordered_map<std::string, uint8_t> airline_map;

  // A robust CSV splitter that ignores commas inside "quotes"
  std::vector<std::string> parse_csv_line(const std::string &line) {
    std::vector<std::string> result;
    std::string current;
    bool in_quotes = false;

    for (char c : line) {
      if (c == '"') {
        in_quotes = !in_quotes; // Toggle quote state
      } else if (c == ',' && !in_quotes) {
        result.push_back(current);
        current.clear();
      } else {
        current += c;
      }
    }
    result.push_back(current);
    return result;
  }

  uint16_t get_country_idx(const std::string &country) {
    auto it = country_map.find(country);
    if (it != country_map.end())
      return it->second;

    uint16_t idx = country_pool.size();
    country_pool.push_back(country);
    country_map[country] = idx;
    return idx;
  }

  uint8_t get_airline_idx(const std::string &airline) {
    auto it = airline_map.find(airline);
    if (it != airline_map.end())
      return it->second;

    uint8_t idx = airline_pool.size();
    airline_pool.push_back(airline);
    airline_map[airline] = idx;
    return idx;
  }

  static std::string resolve_path(const std::string &path) {
    if (std::filesystem::exists(path))
      return path;
    if (std::filesystem::exists("../" + path))
      return "../" + path;
    if (path.rfind("../", 0) == 0 && std::filesystem::exists(path.substr(3)))
      return path.substr(3);
    return path;
  }

  void parse_airports(const std::string &filepath) {
    std::string path = resolve_path(filepath);
    std::ifstream file(path);
    if (!file.is_open())
      throw std::runtime_error("Could not open airports CSV at: " + filepath);

    std::string line;
    std::getline(file, line); // Skip header

    while (std::getline(file, line)) {
      if (line.empty())
        continue;
      auto cols = parse_csv_line(line);

      // Expected cols: dense_id, name, city, country, iata, lat, lon,
      // utc_offset
      uint32_t id = std::stoul(cols[0]);
      float lat = std::stof(cols[5]);
      float lon = std::stof(cols[6]);
      float utc = std::stof(cols[7]);

      // Create Algorithmic Node
      airport apt;
      apt.id = id;
      apt.latitude = lat;
      apt.longitude = lon;
      apt.country_id = static_cast<uint8_t>(get_country_idx(cols[3]));
      apt.ch_rank = 0; // Set later during CH preprocessing
      apt.utc_offset_15m = static_cast<int8_t>(std::round(utc * 4.0f));
      apt.min_transfer = 45; // Default layover penalty

      // Create UI Node
      airportDetails ui_apt;
      ui_apt.id = id;
      ui_apt.name = cols[1];
      ui_apt.city = cols[2];
      ui_apt.country_id = get_country_idx(cols[3]);
      ui_apt.iata_code = cols[4];

      nodes.push_back(apt);
      ui_nodes.push_back(ui_apt);
      iata_to_id[cols[4]] = id;
    }
    std::cout << "Loaded " << nodes.size() << " airports.\n";
  }

  void parse_routes(const std::string &filepath) {
    std::string path = resolve_path(filepath);
    std::vector<char> io_buf(4 * 1024 * 1024);
    std::ifstream file;
    file.rdbuf()->pubsetbuf(io_buf.data(), static_cast<std::streamsize>(io_buf.size()));
    file.open(path);
    if (!file.is_open())
      throw std::runtime_error("Could not open routes CSV at: " + filepath);

    std::string line;
    std::getline(file, line); // Skip header

    // Reserve dynamically from file size to avoid giant reallocations at 40M+ rows
    uint64_t fsize = std::filesystem::file_size(path);
    ui_flights.reserve(static_cast<size_t>(fsize / 39 + 1024));
    uint32_t current_flight_id = 0;

    auto parse_u32 = [](std::string_view sv) -> uint32_t {
      uint32_t val = 0;
      std::from_chars(sv.data(), sv.data() + sv.size(), val);
      return val;
    };

    while (std::getline(file, line)) {
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      if (line.empty())
        continue;

      std::string_view fields[9];
      std::vector<std::string> quoted_cols;
      bool used_fast_path = (line.find('"') == std::string::npos);

      if (used_fast_path) {
        std::string_view sv(line);
        size_t start = 0;
        int col = 0;
        while (col < 8) {
          size_t comma = sv.find(',', start);
          if (comma == std::string_view::npos) {
            used_fast_path = false;
            break;
          }
          fields[col++] = sv.substr(start, comma - start);
          start = comma + 1;
        }
        if (used_fast_path) {
          fields[8] = sv.substr(start);
        }
      }

      if (!used_fast_path) {
        quoted_cols = parse_csv_line(line);
        if (quoted_cols.size() < 9)
          continue;
        for (int i = 0; i < 9; ++i)
          fields[i] = quoted_cols[i];
      }

      // Cols: 0:dense_source, 1:dense_dest, 2:airline, 3:equip, 4:price,
      //       5:date, 6:duration, 7:dep, 8:arr
      uint32_t src_id = parse_u32(fields[0]);
      if (src_id >= adjacency_list.size())
        continue;

      std::string airline_code(fields[2]);
      std::string date_str(fields[5]);

      flight f;
      f.destination_id = parse_u32(fields[1]);
      f.flight_details_id = current_flight_id++;
      f.airline_id = get_airline_idx(airline_code);
      f.price = static_cast<uint16_t>(parse_u32(fields[4]));
      f.duration_mins = static_cast<uint16_t>(parse_u32(fields[6]));
      f.departure_time = static_cast<uint16_t>(parse_u32(fields[7]));
      f.arrival_time = static_cast<uint16_t>(parse_u32(fields[8]));
      f.flight_day = parse_date_to_days(date_str);

      flightDetails ui_f;
      ui_f.airline_code = std::move(airline_code);
      ui_f.plane_model = std::string(fields[3]);
      ui_f.flight_date = std::move(date_str);
      ui_f.flight_number = static_cast<uint16_t>(
          (current_flight_id % 900) +
          100); // Generate a synthetic 3-digit flight number

      adjacency_list[src_id].push_back(f);
      ui_flights.push_back(std::move(ui_f));
    }
    std::cout << "Loaded " << ui_flights.size() << " flight edges.\n";
  }
};