#pragma once

#include "Algorithms/Dijkstra.hpp"
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <sys/resource.h>
#include <vector>

struct AlgorithmBenchmark {
  std::string algo_name;
  std::string mode_label;
  long compute_time_us = 0; // Mirrors cpu_time_us for backwards compatibility
  long cpu_time_us = 0;     // Thread CPU time in microseconds
  long wall_time_us = 0;    // Monotonic wall-clock time in microseconds
  uint64_t nodes_expanded = 0;
  uint64_t edges_relaxed = 0;
  RoutingResult result{};
};

struct ProfilerSummary {
  long total_cpu_us = 0;
  long total_wall_us = 0;
  uint64_t total_nodes_expanded = 0;
  uint64_t total_edges_relaxed = 0;
  uint64_t peak_rss_bytes = 0;
  std::string fastest_label = "N/A";
  long fastest_cpu_us = 0;
  std::string fewest_nodes_label = "N/A";
  uint64_t fewest_nodes = 0;
};

// High-resolution C++ Profiler tracking microsecond CPU time, monotonic wall
// time, nodes expanded, edges relaxed, and peak process memory footprint.
class Profiler {
public:
  static inline int64_t cpu_now_ns() noexcept {
    struct timespec ts {};
#if defined(CLOCK_THREAD_CPUTIME_ID)
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0) {
      return static_cast<int64_t>(ts.tv_sec) * 1000000000LL +
             static_cast<int64_t>(ts.tv_nsec);
    }
#endif
#if defined(CLOCK_PROCESS_CPUTIME_ID)
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts) == 0) {
      return static_cast<int64_t>(ts.tv_sec) * 1000000000LL +
             static_cast<int64_t>(ts.tv_nsec);
    }
#endif
    return static_cast<int64_t>(std::clock()) *
           (1000000000LL / static_cast<int64_t>(CLOCKS_PER_SEC));
  }

  static inline int64_t wall_now_ns() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
  }

  static inline uint64_t peak_rss_bytes() noexcept {
    struct rusage ru {};
    if (getrusage(RUSAGE_SELF, &ru) != 0) {
      return 0;
    }
#if defined(__APPLE__)
    // macOS reports ru_maxrss directly in bytes
    return static_cast<uint64_t>(ru.ru_maxrss);
#else
    // Linux/POSIX reports ru_maxrss in kilobytes
    return static_cast<uint64_t>(ru.ru_maxrss) * 1024ULL;
#endif
  }

  class ScopedTimer {
  public:
    ScopedTimer(long &out_cpu_us, long &out_wall_us) noexcept
        : out_cpu_us_(out_cpu_us),
          out_wall_us_(out_wall_us),
          start_cpu_ns_(cpu_now_ns()),
          start_wall_ns_(wall_now_ns()) {}

    void stop() noexcept {
      if (stopped_) return;
      stopped_ = true;
      int64_t end_cpu_ns = cpu_now_ns();
      int64_t end_wall_ns = wall_now_ns();
      out_cpu_us_ = static_cast<long>(std::max<int64_t>(
          0, (end_cpu_ns - start_cpu_ns_) / 1000LL));
      out_wall_us_ = static_cast<long>(std::max<int64_t>(
          0, (end_wall_ns - start_wall_ns_) / 1000LL));
    }

    ~ScopedTimer() { stop(); }

    ScopedTimer(const ScopedTimer &) = delete;
    ScopedTimer &operator=(const ScopedTimer &) = delete;

  private:
    long &out_cpu_us_;
    long &out_wall_us_;
    int64_t start_cpu_ns_;
    int64_t start_wall_ns_;
    bool stopped_ = false;
  };

  template <typename Fn>
  static AlgorithmBenchmark measure(const std::string &algo_name,
                                    const std::string &mode_label, Fn &&fn) {
    long cpu_us = 0;
    long wall_us = 0;
    RoutingResult res{};
    {
      ScopedTimer timer(cpu_us, wall_us);
      res = fn();
    }
    AlgorithmBenchmark bench;
    bench.algo_name = algo_name;
    bench.mode_label = mode_label;
    bench.cpu_time_us = cpu_us;
    bench.wall_time_us = wall_us;
    bench.compute_time_us = cpu_us;
    bench.nodes_expanded = res.metrics.nodes_explored;
    bench.edges_relaxed = res.metrics.edges_relaxed;
    bench.result = std::move(res);
    return bench;
  }

  template <typename Fn>
  static void record(std::vector<AlgorithmBenchmark> &benchmarks,
                     const std::string &algo_name,
                     const std::string &mode_label, Fn &&fn) {
    benchmarks.push_back(
        measure(algo_name, mode_label, std::forward<Fn>(fn)));
  }

  static ProfilerSummary
  summarize(const std::vector<AlgorithmBenchmark> &benchmarks) {
    ProfilerSummary s;
    s.peak_rss_bytes = peak_rss_bytes();
    long best_cpu = std::numeric_limits<long>::max();
    uint64_t min_nodes = std::numeric_limits<uint64_t>::max();

    for (const auto &b : benchmarks) {
      s.total_cpu_us += b.cpu_time_us;
      s.total_wall_us += b.wall_time_us;
      s.total_nodes_expanded += b.nodes_expanded;
      s.total_edges_relaxed += b.edges_relaxed;

      if (b.result.success) {
        std::string label = b.algo_name + " [" + b.mode_label + "]";
        if (b.cpu_time_us < best_cpu) {
          best_cpu = b.cpu_time_us;
          s.fastest_cpu_us = b.cpu_time_us;
          s.fastest_label = label;
        }
        if (b.nodes_expanded < min_nodes) {
          min_nodes = b.nodes_expanded;
          s.fewest_nodes = b.nodes_expanded;
          s.fewest_nodes_label = label;
        }
      }
    }
    return s;
  }

  static std::string format_ms(long us, int precision = 3) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(precision) << (us / 1000.0) << " ms";
    return oss.str();
  }

  static std::string format_us_compact(long us) {
    return format_ms(us, 3);
  }

  static std::string format_cpu_us(long us) {
    return format_ms(us, 3);
  }
};
