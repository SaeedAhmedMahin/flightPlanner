# FlightPlanner Algorithmic Research Project - TODO Roadmap

## 1. Basics & Data Structures
- [x] Cache-optimized core structs (`airport` and `flight`)
- [x] UI Metadata structs (`airportDetails` and `flightDetails`)
- [ ] Custom Priority Queue (Min-Heap supporting `SearchState`)

## 2. Data Handling
- [x] Python Data Fetcher & Cleaner (`fetch_openflights.py`)
- [x] Synthetic multi-variable weights (Cost, Duration, Time Zones)
- [ ] C++ CSV Parser (Read `airports_clean.csv` & `routes_clean.csv`)
- [ ] Metadata Registry (Hash maps for IATA -> ID, String pooling)
- [ ] Graph Builder (Construct the contiguous Adjacency List)

## 3. Algorithms & Routing
- [ ] Multi-criteria edge weighting (Scalarization: cost vs. time vs. layovers)
- [ ] Dijkstra's Algorithm (Baseline control group)
- [ ] A* Search (Haversine heuristic)
- [ ] Bi-directional Dijkstra / A*
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
- [ ] Python Analysis Script (`plot_benchmarks.py` using Seaborn/Matplotlib)
- [ ] Write 8–12 page mini-paper in LaTeX (`main.tex`)