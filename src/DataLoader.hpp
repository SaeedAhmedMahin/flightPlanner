#pragma once

// Standard libraries
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// Your custom Data Structures
#include "DataStructures/airport.hpp"
#include "DataStructures/airportDetails.hpp"
#include "DataStructures/flight.hpp"
#include "DataStructures/flightDetails.hpp"

class DataLoader {
public:
  // Core Algorithmic Graph
  std::vector<airport> nodes;
  std::vector<std::vector<flight>> adjacency_list;

  // UI Metadata
  std::vector<airportDetails> ui_nodes;
  std::vector<flightDetails> ui_flights;

  // String Pools
  std::vector<std::string> country_pool;
  std::vector<std::string> airline_pool;

  void load_data(const std::string &airports_csv,
                 const std::string &routes_csv) {
    parse_airports(airports_csv);

    // Resize the adjacency list to exactly match the number of airports (V)
    adjacency_list.resize(nodes.size());

    parse_routes(routes_csv);
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
    }
    std::cout << "Loaded " << nodes.size() << " airports.\n";
  }

  void parse_routes(const std::string &filepath) {
    std::string path = resolve_path(filepath);
    std::ifstream file(path);
    if (!file.is_open())
      throw std::runtime_error("Could not open routes CSV at: " + filepath);

    std::string line;
    std::getline(file, line); // Skip header

    uint32_t current_flight_id = 0;

    while (std::getline(file, line)) {
      if (line.empty())
        continue;
      auto cols = parse_csv_line(line);

      // Cols: dense_source, dense_dest, airline, equip, price, duration, dep,
      // arr, day_change
      uint32_t src_id = std::stoul(cols[0]);

      flight f;
      f.destination_id = std::stoul(cols[1]);
      f.flight_details_id = current_flight_id++;
      f.airline_id = get_airline_idx(cols[2]);
      f.price = static_cast<uint16_t>(std::stoul(cols[4]));
      f.departure_time = static_cast<uint16_t>(std::stoul(cols[6]));
      f.arrival_time = static_cast<uint16_t>(std::stoul(cols[7]));
      f.day_change = static_cast<int8_t>(std::stoi(cols[8]));

      flightDetails ui_f;
      ui_f.airline_code = cols[2];
      ui_f.plane_model = cols[3];
      ui_f.flight_number = static_cast<uint16_t>(
          (current_flight_id % 900) +
          100); // Generate a synthetic 3-digit flight number

      adjacency_list[src_id].push_back(f);
      ui_flights.push_back(ui_f);
    }
    std::cout << "Loaded " << ui_flights.size() << " flight edges.\n";
  }
};