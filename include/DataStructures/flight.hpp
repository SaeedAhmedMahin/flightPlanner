#pragma once
#include <cstdint>

// 16 bytes
struct flight {
  uint32_t destination_id;    // Target airport ID
  uint32_t flight_details_id; // Foreign key to lookup flightDetails

  uint16_t price;
  uint16_t departure_time; // Mins from midnight local time (0 - 1439)
  uint16_t arrival_time;   // Mins from midnight local time (0 - 1439)
  uint8_t flight_day;      // Days since 2020-01-01

  uint8_t airline_id; // Index for airline penalty heuristics
};