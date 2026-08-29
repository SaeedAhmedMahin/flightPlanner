#include "DataLoader.hpp"
#include <exception>
#include <iostream>

int main() {
  DataLoader loader;

  std::cout << "Initializing AeroGraph Engine...\n";
  std::cout << "Reading synthetic flight data...\n";

  try {
    // Adjust the paths if your compiled executable runs from a different
    // working directory
    loader.load_data("../data/processed/airports_clean.csv",
                     "../data/processed/routes_clean.csv");
  } catch (const std::exception &e) {
    std::cerr << "Fatal Data Error: " << e.what() << "\n";
    return 1;
  }

  std::cout << "\n--- Data Structure Verification ---\n";

  // Inspect the first airport in the dataset (Dense ID 0)
  if (!loader.nodes.empty()) {
    const airport &test_node = loader.nodes[0];
    const airportDetails &test_ui = loader.ui_nodes[0];

    std::cout << "Airport Node 0:\n";
    std::cout << "  Name:     " << test_ui.name << " (" << test_ui.iata_code
              << ")\n";
    std::cout << "  Location: " << test_ui.city << ", "
              << loader.country_pool[test_ui.country_id] << "\n";
    std::cout << "  Coords:   " << test_node.latitude << ", "
              << test_node.longitude << "\n";
    std::cout << "  UTC Base: " << (int)test_node.utc_offset_15m * 0.25
              << " hours\n";
    std::cout << "  Outbound: " << loader.adjacency_list[0].size()
              << " flights\n\n";

    // Inspect the first outbound flight from this airport
    if (!loader.adjacency_list[0].empty()) {
      const flight &test_flight = loader.adjacency_list[0][0];
      const flightDetails &flight_ui =
          loader.ui_flights[test_flight.flight_details_id];

      std::cout << "First Outbound Flight from Node 0:\n";
      std::cout << "  Carrier:  " << flight_ui.airline_code << " "
                << flight_ui.flight_number << " (" << flight_ui.plane_model
                << ")\n";
      std::cout << "  Dest ID:  " << test_flight.destination_id << "\n";
      std::cout << "  Price:    $" << test_flight.price << "\n";
      std::cout << "  Departs:  " << test_flight.departure_time / 60 << ":"
                << (test_flight.departure_time % 60 == 0
                        ? "00"
                        : std::to_string(test_flight.departure_time % 60))
                << " Local\n";
      std::cout << "  Arrives:  " << test_flight.arrival_time / 60 << ":"
                << (test_flight.arrival_time % 60 == 0
                        ? "00"
                        : std::to_string(test_flight.arrival_time % 60))
                << " Local\n";
      std::cout << "  Day Shift:" << (int)test_flight.day_change << "\n";
    }
  }

  std::cout << "\nGraph built successfully. Ready for routing algorithms.\n";

  return 0;
}