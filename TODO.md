# FlightPlanner Algorithmic Research Project - TODO Roadmap

## 1. Basics & Data Structures
- [x] Cache-optimized core structs (`airport` and `flight`)
- [x] UI Metadata structs (`airportDetails` and `flightDetails`)
- [x] Custom Priority Queue (Min-Heap supporting `SearchState`)
- [x] store non-dominated Pareto tuples (cost, arrival_time) per airport instead of a single scalar min_cost[node]

## 2. Data Handling
- [x] Python Data Fetcher & Cleaner (`fetch_openflights.py`)
- [x] Synthetic multi-variable weights (Cost, Duration, Time Zones)
- [x] C++ CSV Parser (Read `airports_clean.csv` & `routes_clean.csv`)
- [x] Generate 1 Million+ flights
- [x] Metadata Registry (Hash maps for IATA -> ID, String pooling)
- [x] Graph Builder (Construct the contiguous Adjacency List)
- [x] A best path which could be a shorter journey than the cheapest one but not the shortest and be around 10-15% more expensive than the cheapest journey.

## 3. Algorithms & Routing
- [x] Multi-criteria edge weighting (Scalarization: cost vs. time vs. layovers)
- [x] Dijkstra's Algorithm (Baseline control group)
- [x] Two-Phase Router (Topology-First Geodesic Corridors + Schedule Sweep)
- [x] Two-Phase Router with hubs
- [x] Yen's Algorithm
- [x] A* Search (Haversine heuristic)
- [x] Resource-Constrained A* (RC-A* with heuristic look-ahead)
- [x] Label-Correcting Algorithm (SPPRC Bellman-Ford variant)
- [x] The Pulse Algorithm (DFS with bounding and rollback)
- [x] Lagrangian Relaxation (Dynamic penalty multipliers)
- [x] Column Generation (Master/subproblem fleet optimization)
- [x] Metaheuristics (Genetic Algorithms / Ant Colony Optimization)
- [x] Contraction Hierarchies (Offline node contraction)
- [x] Contraction Hierarchies (Live query)
- [x] ALT Preprocessing (Landmark selection & triangle inequality)


## 4. Terminal User Interface (FTXUI)
- [x] CMake setup linking FTXUI
- [x] 2 Modes Default:Customer Mode, and another is Researcher Mode 
    - [x] Customer Mode: Simple Doesnt show the algorithm things just shows flight info =
    - [x] Researcher Mode: Shows algo table graphs and all that information 
- [x] Main Menu & Input Forms (Source, Dest, Roundtrip toggle)
- [x] Results View (Display path, total cost, and total time)
- [x] Terminal World Map Visualization (Plot route coordinates)
- [x] Separate User Version and Researcher Version
     - [x] Add Filtering using airlines, transits, time range, price range etc (natural typed text input e.g. "non-stop", "under $800", "Emirates", "morning", "< 24h" + clickable quick chips)
     - [x] Also make sure We can search by just named Bangkok or even bang and we get the options automatically including DMK and BKK
     - [x] Clickable Flight Options & Dedicated Detailed Flight View Space (mouse & button selection for all options, live card updates with leg breakdowns, times, transits, and fares)
     - [x] Google Flights UI Overhaul:
         - [x] Modern Google Flights search capsule layout with clean horizontal inputs
         - [x] Support 3 Trip Types: Round-trip (`⇄ Round trip`), One-way (`➔ One-way`), and Multi-city (` Multi-city`)
         - [x] Swap button (`[ ⇄ ]`) to instantly reverse origin and destination
         - [x] Single Date Button(s) replacing separate year/month/day dropdowns
         - [x] Interactive Calendar Picker Modal (`CalendarPicker.hpp`) with 7-column day grid, month/year navigation, and date confirmation
         - [x] Round-trip Outbound + Return leg selection tabs with combined total fare computation
         - [x] Multi-city dynamic flight segments with leg selection tabs and combined total fare computation
         - [x] Great Circle World Map trajectory integration across all itineraries

## 5. Research & Benchmarking
- [x] C++ Profiler (Track microsecond CPU time & nodes expanded)
- [ ] Headless Benchmark Mode (Run 10,000 random query pairs)
- [ ] Compare accuracy of algorithms:
    - [ ] heuristic accuracy
    - [ ] optimality gaps
    - [ ] runtime
    - [ ] if the cheapest path is actually truly the cheapest or equal to the cheapest paths among all algos and also for shortest paths do the same
- [ ] Python Analysis Script (`plot_benchmarks.py` using Seaborn/Matplotlib)
- [ ] Write 8–12 page mini-paper in LaTeX (`main.tex`)
