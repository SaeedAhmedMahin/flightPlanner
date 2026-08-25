# FlightRouter-Research: Implementation Roadmap

This document outlines the step-by-step checklist to design, implement, benchmark, and document the FlightRouter project.

---

## Phase 1: Environment Setup & Data Pipeline
- [ ] **Configure Build System**
  - Create a root `CMakeLists.txt` file configured for C++17 or C++20.
  - Structure include and source directories.
- [ ] **Create Directory Layout**
  - Verify all folders (`data/raw`, `data/processed`, `paper/figures`, `scripts`, `include/`, `src/`, `tests/`) are initialized.
- [ ] **Python Data Fetcher (`scripts/fetch_openflights.py`)**
  - Download raw datasets from OpenFlights (airports list, airlines, routes).
  - Clean missing data, filter coordinates, and write cleaned outputs to `data/raw/`.
- [ ] **C++ Data Parser**
  - Implement a parser in C++ to read the CSVs.
  - Build the flight network graph (Adjacency List with airports as vertices and routes as directed edges).

---

## Phase 2: Core C++ Foundations
- [ ] **Core Data Models (`include/Core/`)**
  - Define `Airport` struct (ID, IATA code, Name, City, Lat/Long).
  - Define `Flight` struct (Carrier, Source, Destination, Distance, Base Cost, Duration, Departure Time).
  - Define `Graph` class (Adjacency List representation optimized for cache locality and node traversal).
- [ ] **Edge Weights & Constraints**
  - Support dynamic weights: **Fastest** (flight duration + layover time), **Cheapest** (ticket price), and **Best** (composite weight).
  - Design date/time utilities to handle time-zone conversions and layover window validations.
- [ ] **Multi-Flight & Roundtrip Logic**
  - Support planning routes containing 1+ layovers.
  - Add logic to accept roundtrip criteria (returning on a future date).

---

## Phase 3: Basic & Goal-Directed Routing Algorithms
- [ ] **Baseline Dijkstra**
  - Implement standard Dijkstra using `std::priority_queue`.
  - Add path reconstruction utilities.
- [ ] **Bidirectional Dijkstra**
  - Implement forward and backward search frontiers.
  - Handle proper meeting point selection and search termination criteria.
- [ ] **A* Search**
  - Implement unidirectional A*.
  - Design the Haversine / Great-Circle distance heuristic to estimate remaining distance/time to destination.
- [ ] **Bidirectional A***
  - Implement bidirectional A* with consistent heuristics (using active potential functions to guarantee optimality).

---

## Phase 4: Preprocessing & Advanced Algorithms
- [ ] **ALT Algorithm (A*, Landmarks, Triangle Inequality)**
  - **Preprocessing:**
    - Choose landmark nodes (Random and Farthest landmark selection algorithms).
    - Precompute and serialize shortest path distances from all landmarks to all nodes.
  - **Query Stage:**
    - Implement the active landmark heuristic generator using the triangle inequality.
    - Integrate the heuristic generator into unidirectional and bidirectional A*.
- [ ] **Contraction Hierarchies (CH)**
  - **Preprocessing:**
    - Implement node ordering heuristics (edge difference, contracted neighbors, limit shortcuts).
    - Perform node contraction iteratively, generating and inserting highway shortcuts.
    - Write CH-processed graph to `data/processed/`.
  - **Query Stage:**
    - Implement a bidirectional query algorithm that only traverses edges pointing to higher-importance nodes.
    - Reconstruct final paths by unpacking shortcuts.

---

## Phase 5: Terminal User Interface (TUI) & Visualization
- [ ] **TUI Menu System**
  - Build navigation for origin/destination selection, date picking, budget, and algorithm selection.
- [ ] **Terminal Map Engine**
  - Implement a terminal-based ASCII/Unicode world map.
  - Map longitude and latitude coordinates onto the terminal grid.
- [ ] **Path Visualizer**
  - Display step-by-step progress of the search frontier (forward/backward scans) directly on the world map.
  - Highlight the finalized flight path route upon completion.

---

## Phase 6: Benchmarking & Statistical Evaluation
- [ ] **C++ Benchmarking Suite**
  - Record query times (nanosecond resolution) and number of settled/scanned nodes.
  - Collect memory consumption statistics for graphs, landmark tables, and CH shortcut lists.
  - Export benchmarks to `data/processed/benchmarks.csv`.
- [ ] **Python Analytics Script (`scripts/plot_benchmarks.py`)**
  - Generate runtime comparison graphs (y-axis: time/nodes, x-axis: path hop-count / geographic distance).
  - Perform ANOVA and t-tests on query speeds to determine statistical significance of performance gaps.

---

## Phase 7: Research Paper Compilation
- [ ] **LaTeX Structure (`paper/main.tex`)**
  - Set up a standard double-column academic format (ACM/IEEE style).
- [ ] **Draft Sections**
  - **Abstract:** Summary of routing challenge and results.
  - **Introduction:** Motivation behind high-performance air routing.
  - **Problem Formulation:** Formalize shortest path under duration, cost, and scheduling constraints.
  - **Methodology:** Details on Dijkstra, A*, ALT, and CH.
  - **Experimental Results:** Insert benchmarking plots and statistical significance tables.
  - **Discussion & Future Work:** Scaling behavior, constraints, and algorithmic trade-offs.
