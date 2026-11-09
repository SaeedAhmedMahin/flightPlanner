# How to Run FlightPlanner

This guide walks you through building, configuring, and running the **FlightPlanner** C++ routing engine and algorithm benchmark suite.

---

## ⚡ Quick Start (TL;DR)

If you have CMake and a C++20 compiler installed:

```bash
# 1. Build the project
cmake -B build
cmake --build build

# 2. Run the router (Must be run from the repository root!)
./build/flight_router
```

Or simply use the included runner script:
```bash
chmod +x run.sh
./run.sh
```

You can also pass arguments directly:
```bash
./run.sh DAC JFK 2026-11-04
```

---

## 📋 Prerequisites

Ensure your system has the following tools installed:

| Tool | Minimum Version | Linux (Debian/Ubuntu) | macOS |
| :--- | :--- | :--- | :--- |
| **C++ Compiler** | C++20 standard (`g++` 11+ or `clang++` 13+) | `sudo apt install g++` | `xcode-select --install` |
| **CMake** | `>= 3.20` | `sudo apt install cmake` | `brew install cmake` |
| **Make / Ninja** | Standard build tool | `sudo apt install make` | Built-in / `brew install ninja` |
| **Python 3** *(optional)* | 3.8+ (for data scripts) | `sudo apt install python3 python3-pandas python3-numpy` | `brew install python3` |

---

## 🔨 Step-by-Step Build Guide

### 1. Configure the Build
From the repository root directory:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

> [!NOTE]
> If you pull new code or switch environments (e.g. from macOS to Linux) and CMake complains about `CMakeCache.txt`, clean the build folder first:
> ```bash
> rm -rf build
> cmake -B build -DCMAKE_BUILD_TYPE=Release
> ```

### 2. Compile
Compile using all available CPU cores:
```bash
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
```

This generates the binary at `./build/flight_router`.

---

## 🚀 Running the Application

> [!IMPORTANT]
> **Always run the executable from the project root directory** (`/home/saeed/code/flightPlanner`), because the application accesses dataset files via relative paths (`data/processed/...` and `data/raw/...`).

### Interactive Mode
Run the executable directly:
```bash
./build/flight_router
```

The program will prompt you for:
1. **Source Airport (IATA)**: 3-letter airport code (e.g. `DAC`)
2. **Destination Airport (IATA)**: 3-letter airport code (e.g. `JFK`)
3. **Target Date (YYYY-MM-DD)**: Date in format `YYYY-MM-DD` (e.g. `2026-11-04`)

### Automated / Piped Mode (One-Liner)
You can pipe inputs directly for automated testing or scripting:
```bash
printf "DAC\nJFK\n2026-11-04\n" | ./build/flight_router
```

---

## 📅 Valid Date Range & Airport Codes

### Flight Schedule Date Range
The preprocessed flight schedule dataset (`data/processed/routes_clean.csv`) contains over **1.1 million flights** simulated across a 90-day window:
- **Start Date:** `2026-11-01`
- **End Date:** `2027-01-29`

*(Selecting a date outside this window will return `[No Route Found]` because no flights are scheduled outside this time horizon.)*

### Popular Airport Codes (IATA)
| Region | Code | City / Airport |
| :--- | :--- | :--- |
| **South Asia** | `DAC` | Dhaka (Hazrat Shahjalal) |
| | `DEL` | New Delhi (Indira Gandhi) |
| | `BOM` | Mumbai (Chhatrapati Shivaji) |
| **North America** | `JFK` | New York (John F. Kennedy) |
| | `LAX` | Los Angeles International |
| | `ORD` | Chicago (O'Hare) |
| | `YYZ` | Toronto Pearson |
| **Europe** | `LHR` | London Heathrow |
| | `CDG` | Paris Charles de Gaulle |
| | `AMS` | Amsterdam Schiphol |
| | `FRA` | Frankfurt |
| **Middle East** | `DXB` | Dubai International |
| | `DOH` | Doha (Hamad) |
| | `JED` | Jeddah (King Abdulaziz) |
| **East / SE Asia** | `SIN` | Singapore Changi |
| | `HKG` | Hong Kong International |
| | `HND` | Tokyo Haneda |
| | `BKK` | Bangkok Suvarnabhumi |

---

## 📊 Sample Output Walkthrough

When you run a route query, the engine performs the search across 6 different algorithms and displays:

1. **Algorithm Performance Benchmarks:**
   - Query time in milliseconds (`ms`)
   - Number of nodes explored (`nodes`)
   - Number of edges relaxed (`relaxed`)
   - Objective metric achieved (minimum cost `$` or total duration `hours/mins`)
2. **Side-by-Side Algorithm Routes:**
   - **Dijkstra** (Cheapest & Fastest)
   - **A\*** (Geodesic / Haversine-guided Pareto search)
   - **Label-Correcting** (SPPRC Bellman-Ford variant)
   - **BFS** (Fewest Hops baseline)
   - **Two-Phase Router** (Topology corridor discovery + forward timetable sweep)
   - **Yen's K-Shortest Paths**
3. **Yen's Top-5 Alternative Itineraries:**
   - Displays multiple ranked itinerary options with price, total duration, and transit count.
4. **Best Flights Recommendation:**
   - Automatically computes the optimal trade-off between ticket price, travel duration, and layover inconvenience using a **Generalized Economic Cost** model ($25/hr value of time + $15/layover penalty).

---

## 🔄 Optional: Regenerating Flight Datasets

If you wish to download fresh OpenFlights raw data or re-generate randomized flight schedules with updated dynamic pricing:

```bash
# Install Python dependencies if needed
pip install pandas numpy

# Fetch raw data and generate data/processed/airports_clean.csv & routes_clean.csv
python3 scripts/fetch_openflights.py
```

---

## 🛠 Troubleshooting

| Problem | Cause | Solution |
| :--- | :--- | :--- |
| `CMake Error: The current CMakeCache.txt directory ... is different` | Build directory was copied or CMake was run in another path | Run `rm -rf build` and then `cmake -B build` |
| `Cannot open data/processed/...` | Executable was run from `build/` directory instead of root | Run from project root: `./build/flight_router` |
| `Status: No valid route found.` | Date is outside the scheduled range (`2026-11-01` to `2027-01-29`) or airport has no connection | Use a date between `2026-11-01` and `2027-01-29`, e.g., `2026-11-04` |
| `Error: Unknown airport code(s)` | IATA code is invalid or in lowercase | Use uppercase 3-letter IATA codes (e.g. `DAC`, `JFK`) |
