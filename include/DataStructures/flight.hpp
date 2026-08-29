#include <cstdint>

// 16 bytes
struct flight {
  uint32_t destination_id;    // Target airport ID
  uint32_t flight_details_id; // Foreign key to lookup flightDetails

  uint16_t price;
  uint16_t departure_time; // Mins from midnight local time (0 - 1439)
  uint16_t arrival_time;   // Mins from midnight local time (0 - 1439)

  int8_t day_change;  // e.g., +1 for overnight flights, 0 for same day
  uint8_t airline_id; // Index for airline penalty heuristics
};