cmake_minimum_required(VERSION 3.20)
project(FlightRouterResearch VERSION 1.0 LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include directories
include_directories(include)

# Placeholder target - update as source files are added
add_executable(flight_router src/main.cpp)

#  To run:

# cmake -B build     
# cmake --build build
# ./build/flight_router
