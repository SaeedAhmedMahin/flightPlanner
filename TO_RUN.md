# How to Run FlightPlanner

> 💡 **Detailed guide available:** See [HOW_TO_RUN.md](file:///home/saeed/code/flightPlanner/HOW_TO_RUN.md) for full documentation, troubleshooting, and airport code references.

---

## ⚡ Quick Start

### Option 1: Using the Runner Script (Recommended)
```bash
# Interactive mode
./run.sh

# Or pass arguments directly: <origin> <dest> <date>
./run.sh DAC JFK 2026-11-04
```

### Option 2: Standard CMake Build & Run
```bash
# 1. Build
cmake -B build
cmake --build build

# 2. Run (Must run from repository root)
./build/flight_router
```

### Option 3: Automated One-Liner
```bash
printf "DAC\nJFK\n2026-11-04\n" | ./build/flight_router
```

---

## ⚠️ Important Notes
- **Valid Date Range:** The preprocessed flight database covers **`2026-11-01` to `2027-01-29`** (e.g., `2026-11-04`). Dates outside this window will yield `[No Route Found]`.
- **Working Directory:** Always execute from the repository root folder (`/home/saeed/code/flightPlanner`) so the application can load `data/processed/` and `data/raw/` files.
- **Airport Codes:** Use standard 3-letter uppercase IATA codes (e.g., `DAC`, `JFK`, `LHR`, `DXB`, `SIN`).
