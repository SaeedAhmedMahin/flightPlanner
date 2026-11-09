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

# Build if binary is missing or CMakeLists.txt was updated
if [ ! -f "build/flight_router" ] || [ "CMakeLists.txt" -nt "build/flight_router" ]; then
    echo "🔨 Building flight_router..."
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
    echo "✅ Build completed."
    echo ""
fi

# Run with arguments if provided ($1: source, $2: dest, $3: date)
if [ $# -ge 3 ]; then
    echo "✈️  Running query: $1 -> $2 on $3"
    printf "%s\n%s\n%s\n" "$1" "$2" "$3" | ./build/flight_router
else
    # Interactive mode
    ./build/flight_router
fi
