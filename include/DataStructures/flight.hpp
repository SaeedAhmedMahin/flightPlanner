#pragma once
#include <cstdint>

// 20 bytes (exact alignment, no added padding)
struct flight {
  uint32_t destination_id;    // Target airport ID
  uint32_t flight_details_id; // Foreign key to lookup flightDetails

  uint16_t price;
  uint16_t departure_time; // Mins from midnight local time (0 - 1439)
  uint16_t arrival_time;   // Mins from midnight local time (0 - 1439)
  uint16_t duration_mins;  // True wall-clock flight duration in minutes
  uint16_t flight_day;     // Days since 2027-02-01 (supports > 255 days cleanly)

  uint8_t airline_id; // Index for airline penalty heuristics
};