cmake_minimum_required(VERSION 3.20)
project(FlightRouterResearch VERSION 1.0 LANGUAGES CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# FTXUI Integration (supports local checkout or FetchContent)
include(FetchContent)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/ftxui/CMakeLists.txt")
  add_subdirectory(external/ftxui)
else()
  FetchContent_Declare(ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
    GIT_TAG v5.0.0
    GIT_SHALLOW TRUE
  )
  FetchContent_MakeAvailable(ftxui)
endif()

# Include directories
include_directories(include src)

# Target
add_executable(flight_router src/main.cpp)

target_link_libraries(flight_router PRIVATE
  ftxui::screen
  ftxui::dom
  ftxui::component
)
