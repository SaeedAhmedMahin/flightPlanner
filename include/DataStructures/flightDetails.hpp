#pragma once
#include <string>

struct flightDetails {
  std::string airline_code; // "EK" (Emirates)
  uint16_t flight_number;   //  202
  std::string plane_model;  //  "Boeing 777-300ER" (Add OpenFlights API
                            //  "Equipment" column)
  std::string flight_date;  //  "2026-11-01"
};