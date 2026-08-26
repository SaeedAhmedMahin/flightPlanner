# FlightRouter-Research

A high-performance C++ research project designed to benchmark, analyze, and visualize various graph search algorithms on global airline route networks (using the OpenFlights dataset). The goal is to determine the most space- and time-efficient algorithms for finding optimal multi-constraint routes (cheapest, fastest, or best overall) while providing an interactive Terminal User Interface (TUI) with a terminal-based world map view.

---

##  Research Objectives & Questions

The primary objective of this project is to evaluate the performance of hierarchical, goal-directed, and bidirectional graph traversal techniques under different constraints (time, cost, layovers). 

### Key Research Questions:
1. **Algorithmic Scaling:** Which algorithms scale best as the graph size and search radius increase?
2. **Graph Density Impacts:** How does the density of airport connections affect query times and search space sizes for preprocessing-based algorithms (like CH and ALT) compared to live search algorithms?
3. **Heuristics Evaluation:** What landmark selection techniques (e.g., Random, Farthest, Avoid) provide the most effective heuristic bounds for ALT?
4. **Bidirectional Search Efficiency:** Under what network topologies does Bidirectional A* outperform unidirectional variants, and how do their search spaces compare visually?

---

##  Algorithms to Implement & Compare

We implement and benchmark several routing algorithms:

1. **Dijkstra's Algorithm:** The baseline single-source shortest path algorithm.
2. **Bidirectional Dijkstra:** Searches outward from both source and destination to reduce the search space.
3. **A* Search:** Goal-directed search utilizing geographic distance heuristics (Great-Circle / Haversine distance).
4. **Bidirectional A*:** Combines bidirectional search with consistent heuristics.
5. **ALT (A*, Landmarks, Triangle Inequality):** Uses precomputed distances to a set of landmark nodes to establish tight lower bounds via the triangle inequality.
6. **Contraction Hierarchies (CH):** Preprocesses the graph by contracting nodes based on importance and inserting shortcuts to accelerate query times to microsecond scales.

---

##  Directory Structure

The project follows a modular C++ structure designed to separate core routing logic, algorithms, preprocessing steps, tests, benchmarking scripts, and the TUI:

```text
FlightRouter-Research/
├── CMakeLists.txt           # Build configuration
├── README.md                # Project overview (this file)
├── TODO.md                  # Detailed task checklist and roadmap
├── data/
│   ├── raw/                 # Raw OpenFlights CSVs (airports.dat, routes.dat)
│   └── processed/           # Serialized graph data, CH shortcut graphs, ALT landmarks
├── paper/
│   ├── main.tex             # LaTeX source for the 8-12 page mini-paper
│   └── figures/             # Auto-generated benchmark plots (PDF/PNG)
├── scripts/
│   ├── fetch_openflights.py # Script to download and clean OpenFlights data
│   └── plot_benchmarks.py   # Python/Seaborn scripts for ANOVA, t-tests, and plotting
├── include/
│   ├── Core/                # Graph representation, Date/Time, Flight/Route structures
│   ├── Algorithms/
│   │   ├── Queries/         # Live search engines (Dijkstra, A*, Bi-A*, ALT, CH)
│   │   └── Preprocessing/   # Preprocessing (Landmark selection, CH node contraction)
│   └── TUI/                 # Terminal UI modules, rendering, map visualization
├── src/                     # C++ Implementation source files
└── tests/                   # Unit and integration tests (Catch2 or GTest)
```

---

##  Benchmarking & Analysis

To support a rigorous academic-style mini-paper (8–12 pages), the project features automated benchmarking scripts to capture and analyze:
- **Query Runtime:** Wall-clock query execution times (microseconds).
- **Search Space Size:** The number of nodes settled/scanned during pathfinding.
- **Memory Consumption:** Memory footprint of standard graphs vs. preprocessed structures (ALT tables, CH shortcut graphs).
- **Statistical Significance:** Scripts to run ANOVA and t-tests to validate whether observed runtime differences are statistically significant.

---

##  Terminal User Interface (TUI)

The frontend operates completely within the terminal, featuring:
- **Interactive Queries:** Choose origin, destination, date, flight type (one-way vs. multi-flight roundtrip), and optimization metrics (cheapest, fastest, best).
- **Interactive Map View:** An ASCII/Unicode-based world map that plots coordinates of airports and draws paths showing search frontiers and final paths in real-time.
- **Comparison Engine:** Side-by-side execution display showcasing runtime, visited nodes count, and memory overhead of selected algorithms.
