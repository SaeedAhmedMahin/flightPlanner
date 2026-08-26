#include <string>

struct flightDetails {
  std::string airline_code; // "EK" (Emirates)
  uint16_t flight_number;   //  202
  std::string
      plane_model; //  "Boeing 777-300ER" (OpenFlights API "Equipment" column)
};