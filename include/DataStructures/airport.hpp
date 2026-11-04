#pragma once
#include <cstdint>

// Total 16 bytes
struct airport {
  uint32_t id;
  float latitude;
  float longitude;
  uint8_t country_id;    // 0-234 (maps to country in country_pool)
  int8_t utc_offset_15m; // the utc offset in 15 minute intervals
  uint8_t min_transfer;  // minimum time required for transit (Large airports
                         // like JFK have higher minimums)
  uint8_t ch_rank;       // rank of the node in the CH graph
};
static_assert(sizeof(airport) == 16, "airport must be exactly 16 bytes");