#include <cstdint>
#include <string>
/*
This is for the UI, Helps the user with more info about the airport
*/

// total 96 bytes
struct airportDetails {
  uint32_t id; // airport id connecting it to the airport node
  std::string name;
  std::string city;
  uint16_t country; // country name so we have an array ["Bangladesh", "USA",
                    // "UK", "China" ...]
  std::string iata_code; // 3 letter code for the airport like "DAC", "JFK" ...
};