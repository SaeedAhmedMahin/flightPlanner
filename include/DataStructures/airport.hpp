#pragma once
#include <cstdint>

// Total 16 bytes
struct airport {
  uint32_t id;
  float latitude;
  float longitude;
  uint16_t ch_rank;      // this is only for CH search,
                         // rank of the node in the CH graph
  int8_t utc_offset_15m; // the utc offset in 15 minute intervals
  uint8_t min_transfer;  // minimum time required for transit (Large airports
                         // like JFK have higher minimums)
};