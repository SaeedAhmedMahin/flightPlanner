#!/usr/bin/env bash
set -e

# Resolve repository root directory
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Clean build option
if [ "$1" = "--clean" ] || [ "$1" = "-c" ]; then
    echo "🧹 Performing clean build..."
    rm -rf build
    shift
fi

# Determine number of CPU cores on macOS
CORES=$(sysctl -n hw.ncpu 2>/dev/null || echo 4)

# Check if rebuild is needed:
# - binary missing
# - CMakeLists.txt modified
# - any C++ source or header modified since binary was built
REBUILD_NEEDED=0
if [ ! -f "build/flight_router" ] || [ "CMakeLists.txt" -nt "build/flight_router" ]; then
    REBUILD_NEEDED=1
elif [ -n "$(find src include -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \) -newer "build/flight_router" 2>/dev/null | head -n 1)" ]; then
    REBUILD_NEEDED=1
fi

if [ "$REBUILD_NEEDED" -eq 1 ]; then
    echo "🔨 Building flight_router on macOS (${CORES} cores)..."
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j"$CORES"
    echo "✅ Build completed."
    echo ""
fi

# Run flight_router with arguments or interactively
./build/flight_router "$@"

