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
- [ ] Dijkstra with priority queue and Fenwick tree
- [x] Two-Phase Router (Topology-First Geodesic Corridors + Schedule Sweep)
- [x] Two-Phase Router with hubs
- [x] Yen's Algorithm
- [x] A* Search (Haversine heuristic)
- [x] Label-Correcting Algorithm (SPPRC Bellman-Ford variant)
- [ ] The Pulse Algorithm (DFS with bounding and rollback)
- [ ] Resource-Constrained A* (RC-A* with heuristic look-ahead)
- [ ] Lagrangian Relaxation (Dynamic penalty multipliers)
- [ ] Column Generation (Master/subproblem fleet optimization)
- [ ] Metaheuristics (Genetic Algorithms / Ant Colony Optimization)
- [ ] Contraction Hierarchies (Offline node contraction)
- [ ] Contraction Hierarchies (Live query)
- [ ] ALT Preprocessing (Landmark selection & triangle inequality)


## 4. Terminal User Interface (FTXUI)
- [ ] CMake setup linking FTXUI
- [ ] Main Menu & Input Forms (Source, Dest, Roundtrip toggle)
- [ ] Results View (Display path, total cost, and total time)
- [ ] Terminal World Map Visualization (Plot route coordinates)

## 5. Research & Benchmarking
- [ ] C++ Profiler (Track microsecond CPU time & nodes expanded)
- [ ] Headless Benchmark Mode (Run 10,000 random query pairs)
- [ ] Compare accuracy of algorithms:
    - [ ] heuristic accuracy
    - [ ] optimality gaps
    - [ ] runtime
    - [ ] if the cheapest path is actually truly the cheapest or equal to the cheapest paths among all algos and also for shortest paths do the same
- [ ] Python Analysis Script (`plot_benchmarks.py` using Seaborn/Matplotlib)
- [ ] Write 8–12 page mini-paper in LaTeX (`main.tex`)
