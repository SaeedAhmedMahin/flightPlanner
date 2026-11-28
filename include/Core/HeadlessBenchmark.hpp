#pragma once

#include "Algorithms/ALTRouter.hpp"
#include "Algorithms/AStar.hpp"
#include "Algorithms/BFS.hpp"
#include "Algorithms/ColumnGeneration.hpp"
#include "Algorithms/ContractionHierarchies.hpp"
#include "Algorithms/Dijkstra.hpp"
#include "Algorithms/LagrangianRelaxation.hpp"
#include "Algorithms/RCAStar.hpp"
#include "Algorithms/TwoPhaseHubRouter.hpp"
#include "Algorithms/TwoPhaseRouter.hpp"
#include "Core/Profiler.hpp"
#include "DataLoader.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

class HeadlessBenchmark {
public:
  struct Config {
    uint32_t num_queries = 10000;
    uint32_t seed = 42;
    uint32_t num_threads = 0; // 0 = auto hardware concurrency
    std::string output_csv = "benchmarks/headless_benchmarks.csv";
    std::string summary_csv = "benchmarks/headless_summary.csv";
    bool quiet = false;
  };

  struct SavedSummaryRow {
    std::string algo;
    std::string mode;
    uint64_t total_runs = 0;
    uint64_t successes = 0;
    double succ_pct = 0.0;
    double mean_cpu_ms = 0.0;
    double p95_cpu_ms = 0.0;
    double max_cpu_ms = 0.0;
    double mean_nodes = 0.0;
    double mean_edges = 0.0;
    double avg_price = 0.0;
    double avg_dur_mins = 0.0;
  };

  struct SavedBenchmarkSummary {
    bool loaded = false;
    uint32_t num_queries = 0;
    double total_wall_s = 0.0;
    double peak_rss_gb = 0.0;
    double ch_price_ms = 0.0;
    double ch_time_ms = 0.0;
    double alt_lm_ms = 0.0;
    std::string source_file;
    std::vector<SavedSummaryRow> rows;
  };

  struct QuerySpec {
    uint32_t query_id;
    uint32_t source_id;
    uint32_t dest_id;
    uint16_t start_day;
    uint32_t start_time_utc;
    std::string date_iso;
  };

  static std::string days_to_iso(uint16_t flight_day) {
    static const int dim[] = {0,  31, 28, 31, 30, 31, 30,
                              31, 31, 30, 31, 30, 31};
    int y = 2027;
    int m = 2;
    int d = 1 + static_cast<int>(flight_day);
    while (true) {
      int m_days = dim[m];
      if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || (y % 400 == 0)))
        m_days = 29;
      if (d <= m_days) break;
      d -= m_days;
      if (++m > 12) {
        m = 1;
        y++;
      }
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
    return std::string(buf);
  }

  static void run_suite_for_query(
      const QuerySpec &q, const DataLoader &loader,
      ContractionHierarchiesRouter::CHGraph &ch_price,
      ContractionHierarchiesRouter::CHGraph &ch_time,
      ALTRouter::LandmarkData &alt_landmarks,
      std::vector<AlgorithmBenchmark> &out) {

    out.clear();
    out.reserve(21);

    const uint32_t src = q.source_id;
    const uint32_t dst = q.dest_id;
    const uint32_t t0 = q.start_time_utc;

    Dijkstra dijkstra_router;
    Profiler::record(out, "Dijkstra", "cheapest", [&] {
      return dijkstra_router.run(src, dst, loader.adjacency_list, loader.nodes,
                                 OptimizeFor::PRICE, t0);
    });
    Profiler::record(out, "Dijkstra", "fastest", [&] {
      return dijkstra_router.run(src, dst, loader.adjacency_list, loader.nodes,
                                 OptimizeFor::TIME, t0);
    });

    AStar astar_router;
    Profiler::record(out, "A*", "cheapest", [&] {
      return astar_router.run(src, dst, loader.adjacency_list, loader.nodes,
                              OptimizeFor::PRICE, t0);
    });
    Profiler::record(out, "A*", "fastest", [&] {
      return astar_router.run(src, dst, loader.adjacency_list, loader.nodes,
                              OptimizeFor::TIME, t0);
    });

    ALTRouter alt_router;
    Profiler::record(out, "ALT", "cheapest", [&] {
      return alt_router.run(
          src, dst, loader.adjacency_list, loader.nodes,
          loader.static_route_graph, loader.reverse_static_graph,
          loader.static_route_min_price, loader.static_route_min_duration,
          alt_landmarks, OptimizeFor::PRICE, t0);
    });
    Profiler::record(out, "ALT", "fastest", [&] {
      return alt_router.run(
          src, dst, loader.adjacency_list, loader.nodes,
          loader.static_route_graph, loader.reverse_static_graph,
          loader.static_route_min_price, loader.static_route_min_duration,
          alt_landmarks, OptimizeFor::TIME, t0);
    });

    RCAStarRouter rcastar_router;
    Profiler::record(out, "RC-A*", "cheapest", [&] {
      return rcastar_router.run(
          src, dst, loader.adjacency_list, loader.nodes, OptimizeFor::PRICE, t0,
          RCAStarRouter::ResourceConstraints{2880, 50000, 5});
    });
    Profiler::record(out, "RC-A*", "fastest", [&] {
      return rcastar_router.run(
          src, dst, loader.adjacency_list, loader.nodes, OptimizeFor::TIME, t0,
          RCAStarRouter::ResourceConstraints{2880, 50000, 5});
    });

    ContractionHierarchiesRouter ch_router;
    Profiler::record(out, "Contraction H.", "cheapest", [&] {
      return ch_router.run(
          src, dst, loader.adjacency_list, loader.static_route_graph,
          loader.static_route_min_price, loader.static_route_min_duration,
          loader.nodes, OptimizeFor::PRICE, t0, ch_price);
    });
    Profiler::record(out, "Contraction H.", "fastest", [&] {
      return ch_router.run(
          src, dst, loader.adjacency_list, loader.static_route_graph,
          loader.static_route_min_price, loader.static_route_min_duration,
          loader.nodes, OptimizeFor::TIME, t0, ch_time);
    });

    LagrangianRelaxationRouter lr_router;
    Profiler::record(out, "Lagrangian", "cheapest", [&] {
      return lr_router.run(
          src, dst, loader.adjacency_list, loader.nodes, OptimizeFor::PRICE, t0,
          LagrangianRelaxationRouter::ResourceConstraints{2880, 50000, 5});
    });
    Profiler::record(out, "Lagrangian", "fastest", [&] {
      return lr_router.run(
          src, dst, loader.adjacency_list, loader.nodes, OptimizeFor::TIME, t0,
          LagrangianRelaxationRouter::ResourceConstraints{2880, 50000, 5});
    });

    ColumnGenerationRouter cg_router;
    Profiler::record(out, "Column-Gen", "cheapest", [&] {
      return cg_router.run(src, dst, loader.adjacency_list, loader.nodes,
                           OptimizeFor::PRICE, t0);
    });
    Profiler::record(out, "Column-Gen", "fastest", [&] {
      return cg_router.run(src, dst, loader.adjacency_list, loader.nodes,
                           OptimizeFor::TIME, t0);
    });

    BFSRouter bfs_router;
    Profiler::record(out, "BFS (Hops)", "cheapest", [&] {
      return bfs_router.run(src, dst, loader.adjacency_list, loader.nodes,
                            OptimizeFor::PRICE, t0);
    });
    Profiler::record(out, "BFS (Hops)", "fastest", [&] {
      return bfs_router.run(src, dst, loader.adjacency_list, loader.nodes,
                            OptimizeFor::TIME, t0);
    });

    TwoPhaseRouter two_phase_router;
    Profiler::record(out, "Two-Phase", "cheapest", [&] {
      return two_phase_router.run(
          src, dst, loader.adjacency_list, loader.static_route_graph,
          loader.static_route_min_price, loader.static_route_min_duration,
          loader.nodes, OptimizeFor::PRICE, t0);
    });
    Profiler::record(out, "Two-Phase", "fastest", [&] {
      return two_phase_router.run(
          src, dst, loader.adjacency_list, loader.static_route_graph,
          loader.static_route_min_price, loader.static_route_min_duration,
          loader.nodes, OptimizeFor::TIME, t0);
    });

    TwoPhaseHubRouter two_phase_hub_router;
    Profiler::record(out, "Two-Phase Hub", "cheapest", [&] {
      return two_phase_hub_router.run(
          src, dst, loader.adjacency_list, loader.static_route_graph,
          loader.static_route_min_price, loader.static_route_min_duration,
          loader.nodes, OptimizeFor::PRICE, t0);
    });
    Profiler::record(out, "Two-Phase Hub", "fastest", [&] {
      return two_phase_hub_router.run(
          src, dst, loader.adjacency_list, loader.static_route_graph,
          loader.static_route_min_price, loader.static_route_min_duration,
          loader.nodes, OptimizeFor::TIME, t0);
    });

    Profiler::record(out, "Multiple ALT", "diverse", [&] {
      ALTRouter::DiverseKResult multi_alt_res = alt_router.run_k_diverse(
          src, dst, loader.adjacency_list, loader.nodes,
          loader.static_route_graph, loader.reverse_static_graph,
          loader.static_route_min_price, loader.static_route_min_duration,
          alt_landmarks, t0, 10, 5, t0);
      RoutingResult primary =
          multi_alt_res.best_recommendation.success
              ? multi_alt_res.best_recommendation
              : (multi_alt_res.paths.empty()
                     ? RoutingResult{false, 0, 0, {}, {}}
                     : multi_alt_res.paths[0]);
      primary.metrics = multi_alt_res.total_metrics;
      return primary;
    });
  }

  static bool save_summary(const std::string &path,
                           const SavedBenchmarkSummary &summary) {
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
      std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << std::fixed << std::setprecision(3);
    out << "#meta," << summary.num_queries << "," << summary.total_wall_s << ","
        << summary.peak_rss_gb << "," << summary.ch_price_ms << ","
        << summary.ch_time_ms << "," << summary.alt_lm_ms << "\n";
    out << "algo,mode,total_runs,successes,succ_pct,mean_cpu_ms,p95_cpu_ms,"
           "max_cpu_ms,mean_nodes,mean_edges,avg_price,avg_dur_mins\n";
    for (const auto &r : summary.rows) {
      out << r.algo << "," << r.mode << "," << r.total_runs << ","
          << r.successes << "," << std::setprecision(2) << r.succ_pct << ","
          << std::setprecision(3) << r.mean_cpu_ms << "," << r.p95_cpu_ms << ","
          << r.max_cpu_ms << "," << std::setprecision(2) << r.mean_nodes << ","
          << r.mean_edges << "," << r.avg_price << "," << r.avg_dur_mins
          << "\n";
    }
    return true;
  }

  static SavedBenchmarkSummary load_or_build_summary(
      const std::string &summary_path = "benchmarks/headless_summary.csv",
      const std::string &raw_csv_path = "benchmarks/headless_benchmarks.csv") {
    SavedBenchmarkSummary summary;

    // 1. Fast path: load pre-aggregated summary CSV (< 0.5 ms)
    std::ifstream sf(summary_path);
    if (sf.is_open()) {
      std::string line;
      while (std::getline(sf, line)) {
        if (line.empty()) continue;
        if (line.back() == '\r') line.pop_back();
        if (line.rfind("#meta,", 0) == 0) {
          std::stringstream ss(line.substr(6));
          std::string tok;
          std::vector<std::string> toks;
          while (std::getline(ss, tok, ',')) toks.push_back(tok);
          if (toks.size() >= 6) {
            summary.num_queries = static_cast<uint32_t>(std::stoul(toks[0]));
            summary.total_wall_s = std::stod(toks[1]);
            summary.peak_rss_gb = std::stod(toks[2]);
            summary.ch_price_ms = std::stod(toks[3]);
            summary.ch_time_ms = std::stod(toks[4]);
            summary.alt_lm_ms = std::stod(toks[5]);
          }
          continue;
        }
        if (line.rfind("algo,", 0) == 0) continue;

        std::stringstream ss(line);
        std::string tok;
        std::vector<std::string> c;
        while (std::getline(ss, tok, ',')) c.push_back(tok);
        if (c.size() < 12) continue;

        SavedSummaryRow r;
        r.algo = c[0];
        r.mode = c[1];
        r.total_runs = std::stoull(c[2]);
        r.successes = std::stoull(c[3]);
        r.succ_pct = std::stod(c[4]);
        r.mean_cpu_ms = std::stod(c[5]);
        r.p95_cpu_ms = std::stod(c[6]);
        r.max_cpu_ms = std::stod(c[7]);
        r.mean_nodes = std::stod(c[8]);
        r.mean_edges = std::stod(c[9]);
        r.avg_price = std::stod(c[10]);
        r.avg_dur_mins = std::stod(c[11]);
        summary.rows.push_back(std::move(r));
      }
      if (!summary.rows.empty()) {
        summary.loaded = true;
        summary.source_file = summary_path;
        return summary;
      }
    }

    // 2. Fallback: aggregate directly from saved raw benchmark CSV and cache summary
    std::ifstream rf(raw_csv_path);
    if (!rf.is_open()) {
      return summary;
    }

    struct RawAgg {
      std::string algo;
      std::string mode;
      uint64_t total_runs = 0;
      uint64_t successes = 0;
      double sum_cpu_us = 0.0;
      double sum_wall_us = 0.0;
      double sum_nodes = 0.0;
      double sum_edges = 0.0;
      double sum_price = 0.0;
      double sum_dur = 0.0;
      long max_cpu_us = 0;
      std::vector<long> cpu_samples;
    };

    std::vector<RawAgg> aggs;
    std::map<std::pair<std::string, std::string>, size_t> slot_map;
    uint32_t max_qid = 0;
    uint64_t line_count = 0;
    std::string line;
    std::getline(rf, line); // Skip CSV header

    while (std::getline(rf, line)) {
      if (line.empty()) continue;
      if (line.back() == '\r') line.pop_back();
      std::string_view sv(line);
      std::string_view cols[14];
      size_t start = 0;
      int ci = 0;
      bool ok = true;
      while (ci < 13) {
        size_t comma = sv.find(',', start);
        if (comma == std::string_view::npos) {
          ok = false;
          break;
        }
        cols[ci++] = sv.substr(start, comma - start);
        start = comma + 1;
      }
      if (!ok) continue;
      cols[13] = sv.substr(start);

      auto parse_i64 = [](std::string_view s) {
        long long v = 0;
        std::from_chars(s.data(), s.data() + s.size(), v);
        return v;
      };

      uint32_t qid = static_cast<uint32_t>(parse_i64(cols[0]));
      if (qid + 1 > max_qid) max_qid = qid + 1;
      std::string algo(cols[4]);
      std::string mode(cols[5]);
      bool succ = (parse_i64(cols[6]) != 0);
      long cpu_us = static_cast<long>(parse_i64(cols[7]));
      long wall_us = static_cast<long>(parse_i64(cols[8]));
      double nodes_v = static_cast<double>(parse_i64(cols[9]));
      double edges_v = static_cast<double>(parse_i64(cols[10]));
      double price_v = static_cast<double>(parse_i64(cols[11]));
      double dur_v = static_cast<double>(parse_i64(cols[12]));

      auto key = std::make_pair(algo, mode);
      auto it = slot_map.find(key);
      size_t idx = 0;
      if (it == slot_map.end()) {
        idx = aggs.size();
        slot_map[key] = idx;
        aggs.push_back(RawAgg{algo, mode});
      } else {
        idx = it->second;
      }
      auto &a = aggs[idx];
      a.total_runs++;
      a.sum_cpu_us += static_cast<double>(cpu_us);
      a.sum_wall_us += static_cast<double>(wall_us);
      a.sum_nodes += nodes_v;
      a.sum_edges += edges_v;
      if (cpu_us > a.max_cpu_us) a.max_cpu_us = cpu_us;
      a.cpu_samples.push_back(cpu_us);
      if (succ) {
        a.successes++;
        a.sum_price += price_v;
        a.sum_dur += dur_v;
      }
      line_count++;
    }

    if (aggs.empty() || line_count == 0) {
      return summary;
    }

    summary.loaded = true;
    summary.num_queries = max_qid;
    summary.peak_rss_gb = static_cast<double>(Profiler::peak_rss_bytes()) /
                          (1024.0 * 1024.0 * 1024.0);
    summary.source_file = raw_csv_path;

    for (auto &a : aggs) {
      SavedSummaryRow r;
      r.algo = a.algo;
      r.mode = a.mode;
      r.total_runs = a.total_runs;
      r.successes = a.successes;
      r.succ_pct = 100.0 * static_cast<double>(a.successes) /
                   static_cast<double>(a.total_runs);
      r.mean_cpu_ms = (a.sum_cpu_us / a.total_runs) / 1000.0;
      r.max_cpu_ms = static_cast<double>(a.max_cpu_us) / 1000.0;
      r.mean_nodes = a.sum_nodes / a.total_runs;
      r.mean_edges = a.sum_edges / a.total_runs;
      r.avg_price = (a.successes > 0) ? (a.sum_price / a.successes) : 0.0;
      r.avg_dur_mins = (a.successes > 0) ? (a.sum_dur / a.successes) : 0.0;
      long p95_us = 0;
      if (!a.cpu_samples.empty()) {
        size_t p95_idx = static_cast<size_t>(
            0.95 * static_cast<double>(a.cpu_samples.size() - 1));
        std::nth_element(a.cpu_samples.begin(),
                         a.cpu_samples.begin() + p95_idx,
                         a.cpu_samples.end());
        p95_us = a.cpu_samples[p95_idx];
      }
      r.p95_cpu_ms = static_cast<double>(p95_us) / 1000.0;
      summary.rows.push_back(std::move(r));
    }

    save_summary(summary_path, summary);
    return summary;
  }

  static int execute(
      const DataLoader &loader, const Config &cfg,
      const std::atomic<bool> *cancel_flag = nullptr,
      std::function<void(uint32_t, uint32_t, double, double, double)>
          progress_cb = nullptr) {
    // 1. Collect active airports with outgoing flights
    std::vector<uint32_t> active_airports;
    active_airports.reserve(loader.nodes.size());
    for (uint32_t u = 0; u < loader.nodes.size(); ++u) {
      if (!loader.static_route_graph[u].empty() &&
          !loader.adjacency_list[u].empty()) {
        active_airports.push_back(u);
      }
    }

    if (active_airports.size() < 2) {
      if (!cfg.quiet) {
        std::cerr << "Error: Not enough active airports for benchmark.\n";
      }
      return 1;
    }

    if (!cfg.quiet) {
      std::cout << "\n============================================================"
                   "========\n";
      std::cout << " HEADLESS BENCHMARK MODE  |  " << cfg.num_queries
                << " RANDOM QUERY PAIRS\n";
      std::cout << "============================================================"
                   "========\n";
      std::cout << "Active Airports Pool : " << active_airports.size() << "\n";
      std::cout << "Seed                 : " << cfg.seed << "\n";
      std::cout << "Output CSV           : " << cfg.output_csv << "\n";
    }

    // 2. Offline Preprocessing (once before queries)
    long ch_price_cpu = 0, ch_price_wall = 0;
    long ch_time_cpu = 0, ch_time_wall = 0;
    long alt_cpu = 0, alt_wall = 0;

    ContractionHierarchiesRouter::CHGraph ch_graph_price;
    ContractionHierarchiesRouter::CHGraph ch_graph_time;
    ALTRouter::LandmarkData alt_landmarks;

    {
      Profiler::ScopedTimer t(ch_price_cpu, ch_price_wall);
      ContractionHierarchiesRouter::preprocess(
          loader.static_route_graph, loader.static_route_min_price,
          loader.static_route_min_duration, ch_graph_price, OptimizeFor::PRICE);
    }
    {
      Profiler::ScopedTimer t(ch_time_cpu, ch_time_wall);
      ContractionHierarchiesRouter::preprocess(
          loader.static_route_graph, loader.static_route_min_price,
          loader.static_route_min_duration, ch_graph_time, OptimizeFor::TIME);
    }
    {
      Profiler::ScopedTimer t(alt_cpu, alt_wall);
      ALTRouter::preprocess(
          loader.nodes, loader.static_route_graph, loader.reverse_static_graph,
          loader.static_route_min_price, loader.static_route_min_duration,
          alt_landmarks, 16);
    }

    if (!cfg.quiet) {
      std::cout << "Preprocess CH (Price): " << Profiler::format_ms(ch_price_wall)
                << " | CH (Time): " << Profiler::format_ms(ch_time_wall)
                << " | ALT (16 LM): " << Profiler::format_ms(alt_wall) << "\n";
    }

    // 3. Generate deterministic random query pairs across [2027-02-02 .. 2027-12-28]
    std::vector<QuerySpec> queries;
    queries.reserve(cfg.num_queries);
    std::mt19937 rng(cfg.seed);
    std::uniform_int_distribution<size_t> apt_dist(0,
                                                   active_airports.size() - 1);
    std::uniform_int_distribution<uint16_t> day_dist(1, 330);

    for (uint32_t qid = 0; qid < cfg.num_queries; ++qid) {
      uint32_t src = active_airports[apt_dist(rng)];
      uint32_t dst = active_airports[apt_dist(rng)];
      while (dst == src) {
        dst = active_airports[apt_dist(rng)];
      }
      uint16_t day = day_dist(rng);
      int32_t utc_off = loader.nodes[src].utc_offset_15m * 15;
      uint32_t start_utc = static_cast<uint32_t>(
          static_cast<int32_t>(day * 1440) - utc_off);
      queries.push_back(
          {qid, src, dst, day, start_utc, days_to_iso(day)});
    }

    // 4. Open CSV output file with large stream buffer
    std::filesystem::path out_path(cfg.output_csv);
    if (out_path.has_parent_path()) {
      std::filesystem::create_directories(out_path.parent_path());
    }
    std::vector<char> csv_buf(1024 * 1024);
    std::ofstream csv;
    csv.rdbuf()->pubsetbuf(csv_buf.data(),
                           static_cast<std::streamsize>(csv_buf.size()));
    csv.open(cfg.output_csv);
    if (!csv.is_open()) {
      if (!cfg.quiet) {
        std::cerr << "Error: Could not open benchmark CSV output: "
                  << cfg.output_csv << "\n";
      }
      return 1;
    }
    csv << "query_id,src_iata,dst_iata,date_iso,algo,mode,success,cpu_us,"
           "wall_us,nodes_expanded,edges_relaxed,price,duration_mins,hops\n";

    // 5. Execute across worker threads (CLOCK_THREAD_CPUTIME_ID stays thread-local!)
    unsigned hw = std::thread::hardware_concurrency();
    uint32_t num_threads =
        (cfg.num_threads > 0)
            ? cfg.num_threads
            : std::min<uint32_t>(8u, std::max<uint32_t>(1u, hw));
    num_threads = std::min<uint32_t>(num_threads, std::max<uint32_t>(1u, cfg.num_queries));
    if (!cfg.quiet) {
      std::cout << "Worker Threads       : " << num_threads << "\n\n";
    }

    constexpr size_t NUM_SLOTS = 21;
    struct AlgoAgg {
      std::string algo;
      std::string mode;
      uint64_t total_runs = 0;
      uint64_t successes = 0;
      double sum_cpu_us = 0.0;
      double sum_wall_us = 0.0;
      double sum_nodes = 0.0;
      double sum_edges = 0.0;
      double sum_price = 0.0;
      double sum_dur = 0.0;
      long max_cpu_us = 0;
      std::vector<long> cpu_samples;
    };

    std::vector<std::vector<AlgoAgg>> thread_aggs(num_threads,
                                                  std::vector<AlgoAgg>(NUM_SLOTS));
    std::atomic<uint32_t> next_idx{0};
    std::atomic<uint32_t> completed{0};
    std::mutex io_mutex;

    auto bench_start = std::chrono::steady_clock::now();

    auto worker_fn = [&](uint32_t tid) {
      // Each worker passes its own shallow handle referencing read-only landmark vectors
      ALTRouter::LandmarkData local_landmarks = alt_landmarks;
      std::vector<AlgorithmBenchmark> res_vec;
      std::string local_csv;
      local_csv.reserve(65536);

      auto &aggs = thread_aggs[tid];
      for (size_t s = 0; s < NUM_SLOTS; ++s) {
        aggs[s].cpu_samples.reserve(cfg.num_queries / num_threads + 16);
      }

      while (true) {
        if (cancel_flag && cancel_flag->load()) break;
        uint32_t start_q = next_idx.fetch_add(16);
        if (start_q >= cfg.num_queries) break;
        uint32_t end_q = std::min<uint32_t>(cfg.num_queries, start_q + 16);

        local_csv.clear();
        for (uint32_t qi = start_q; qi < end_q; ++qi) {
          if (cancel_flag && cancel_flag->load()) break;
          const QuerySpec &q = queries[qi];
          run_suite_for_query(q, loader, ch_graph_price, ch_graph_time,
                              local_landmarks, res_vec);

          const std::string &src_iata = loader.ui_nodes[q.source_id].iata_code;
          const std::string &dst_iata = loader.ui_nodes[q.dest_id].iata_code;

          for (size_t s = 0; s < res_vec.size() && s < NUM_SLOTS; ++s) {
            const auto &b = res_vec[s];
            auto &a = aggs[s];
            if (a.algo.empty()) {
              a.algo = b.algo_name;
              a.mode = b.mode_label;
            }
            a.total_runs++;
            a.sum_cpu_us += static_cast<double>(b.cpu_time_us);
            a.sum_wall_us += static_cast<double>(b.wall_time_us);
            a.sum_nodes += static_cast<double>(b.nodes_expanded);
            a.sum_edges += static_cast<double>(b.edges_relaxed);
            if (b.cpu_time_us > a.max_cpu_us) a.max_cpu_us = b.cpu_time_us;
            a.cpu_samples.push_back(b.cpu_time_us);
            if (b.result.success) {
              a.successes++;
              a.sum_price += static_cast<double>(b.result.total_price);
              a.sum_dur += static_cast<double>(b.result.total_time_mins);
            }

            local_csv += std::to_string(q.query_id);
            local_csv.push_back(',');
            local_csv += src_iata;
            local_csv.push_back(',');
            local_csv += dst_iata;
            local_csv.push_back(',');
            local_csv += q.date_iso;
            local_csv.push_back(',');
            local_csv += b.algo_name;
            local_csv.push_back(',');
            local_csv += b.mode_label;
            local_csv.push_back(',');
            local_csv += (b.result.success ? "1," : "0,");
            local_csv += std::to_string(b.cpu_time_us);
            local_csv.push_back(',');
            local_csv += std::to_string(b.wall_time_us);
            local_csv.push_back(',');
            local_csv += std::to_string(b.nodes_expanded);
            local_csv.push_back(',');
            local_csv += std::to_string(b.edges_relaxed);
            local_csv.push_back(',');
            local_csv += std::to_string(b.result.success ? b.result.total_price : 0);
            local_csv.push_back(',');
            local_csv += std::to_string(b.result.success ? b.result.total_time_mins : 0);
            local_csv.push_back(',');
            local_csv += std::to_string(b.result.success ? b.result.path.size() : 0);
            local_csv.push_back('\n');
          }
        }

        uint32_t done_now = completed.fetch_add(end_q - start_q) + (end_q - start_q);
        {
          std::lock_guard<std::mutex> lock(io_mutex);
          csv.write(local_csv.data(),
                    static_cast<std::streamsize>(local_csv.size()));
          uint32_t step_size = std::max<uint32_t>(100, cfg.num_queries / 20);
          uint32_t prev_step = (done_now - (end_q - start_q)) / step_size;
          uint32_t curr_step = done_now / step_size;
          if (curr_step > prev_step || done_now == cfg.num_queries) {
            double elapsed_s =
                std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - bench_start)
                    .count();
            double qps = (elapsed_s > 1e-6) ? (done_now / elapsed_s) : 0.0;
            double eta_s = (qps > 1e-6) ? ((cfg.num_queries - done_now) / qps) : 0.0;
            if (progress_cb) {
              progress_cb(done_now, cfg.num_queries, elapsed_s, qps, eta_s);
            }
            if (!cfg.quiet) {
              std::cout << "  [" << done_now << "/" << cfg.num_queries << "] "
                        << std::fixed << std::setprecision(1)
                        << (100.0 * done_now / cfg.num_queries) << "%  |  "
                        << elapsed_s << "s elapsed  |  " << qps << " q/s  |  ETA "
                        << eta_s << "s\n"
                        << std::flush;
            }
          }
        }
      }
    };

    std::vector<std::thread> workers;
    workers.reserve(num_threads);
    for (uint32_t t = 0; t < num_threads; ++t) {
      workers.emplace_back(worker_fn, t);
    }
    for (auto &w : workers) {
      w.join();
    }
    csv.close();

    if (cancel_flag && cancel_flag->load()) {
      return 1;
    }

    double total_wall_s =
        std::chrono::duration<double>(
            std::chrono::steady_clock::now() - bench_start)
            .count();

    // 6. Merge per-thread aggregates and compute percentiles
    std::vector<AlgoAgg> merged(NUM_SLOTS);
    for (uint32_t t = 0; t < num_threads; ++t) {
      for (size_t s = 0; s < NUM_SLOTS; ++s) {
        const auto &src_a = thread_aggs[t][s];
        if (src_a.total_runs == 0) continue;
        auto &dst_a = merged[s];
        if (dst_a.algo.empty()) {
          dst_a.algo = src_a.algo;
          dst_a.mode = src_a.mode;
        }
        dst_a.total_runs += src_a.total_runs;
        dst_a.successes += src_a.successes;
        dst_a.sum_cpu_us += src_a.sum_cpu_us;
        dst_a.sum_wall_us += src_a.sum_wall_us;
        dst_a.sum_nodes += src_a.sum_nodes;
        dst_a.sum_edges += src_a.sum_edges;
        dst_a.sum_price += src_a.sum_price;
        dst_a.sum_dur += src_a.sum_dur;
        if (src_a.max_cpu_us > dst_a.max_cpu_us) {
          dst_a.max_cpu_us = src_a.max_cpu_us;
        }
        dst_a.cpu_samples.insert(dst_a.cpu_samples.end(),
                                 src_a.cpu_samples.begin(),
                                 src_a.cpu_samples.end());
      }
    }

    SavedBenchmarkSummary summary;
    summary.loaded = true;
    summary.num_queries = cfg.num_queries;
    summary.total_wall_s = total_wall_s;
    summary.peak_rss_gb = static_cast<double>(Profiler::peak_rss_bytes()) /
                          (1024.0 * 1024.0 * 1024.0);
    summary.ch_price_ms = static_cast<double>(ch_price_wall) / 1000.0;
    summary.ch_time_ms = static_cast<double>(ch_time_wall) / 1000.0;
    summary.alt_lm_ms = static_cast<double>(alt_wall) / 1000.0;
    summary.source_file = cfg.summary_csv;

    for (auto &m : merged) {
      if (m.total_runs == 0) continue;
      SavedSummaryRow r;
      r.algo = m.algo;
      r.mode = m.mode;
      r.total_runs = m.total_runs;
      r.successes = m.successes;
      r.succ_pct = 100.0 * static_cast<double>(m.successes) /
                   static_cast<double>(m.total_runs);
      r.mean_cpu_ms = (m.sum_cpu_us / m.total_runs) / 1000.0;
      r.max_cpu_ms = static_cast<double>(m.max_cpu_us) / 1000.0;
      r.mean_nodes = m.sum_nodes / m.total_runs;
      r.mean_edges = m.sum_edges / m.total_runs;
      r.avg_price = (m.successes > 0) ? (m.sum_price / m.successes) : 0.0;
      r.avg_dur_mins = (m.successes > 0) ? (m.sum_dur / m.successes) : 0.0;

      long p95_us = 0;
      if (!m.cpu_samples.empty()) {
        size_t p95_idx = static_cast<size_t>(
            0.95 * static_cast<double>(m.cpu_samples.size() - 1));
        std::nth_element(m.cpu_samples.begin(),
                         m.cpu_samples.begin() + p95_idx,
                         m.cpu_samples.end());
        p95_us = m.cpu_samples[p95_idx];
      }
      r.p95_cpu_ms = static_cast<double>(p95_us) / 1000.0;
      summary.rows.push_back(std::move(r));
    }

    save_summary(cfg.summary_csv, summary);

    if (!cfg.quiet) {
      std::cout << "\n============================================================"
                   "====================================================\n";
      std::cout << " HEADLESS BENCHMARK SUMMARY (" << summary.num_queries
                << " Queries | " << std::fixed << std::setprecision(2)
                << summary.total_wall_s << "s Total Wall | Peak RSS: "
                << summary.peak_rss_gb << " GB)\n";
      std::cout << "============================================================"
                   "====================================================\n";
      std::cout << std::left << std::setw(16) << "Algorithm" << std::setw(11)
                << "Mode" << std::right << std::setw(9) << "Succ%"
                << std::setw(12) << "Mean CPU" << std::setw(12) << "P95 CPU"
                << std::setw(12) << "Max CPU" << std::setw(13) << "Mean Nodes"
                << std::setw(13) << "Mean Edges" << std::setw(10) << "Avg$"
                << std::setw(10) << "AvgDur" << "\n";
      std::cout << std::string(118, '-') << "\n";

      for (const auto &r : summary.rows) {
        std::ostringstream cpu_s, p95_s, max_s;
        cpu_s << std::fixed << std::setprecision(3) << r.mean_cpu_ms << " ms";
        p95_s << std::fixed << std::setprecision(3) << r.p95_cpu_ms << " ms";
        max_s << std::fixed << std::setprecision(3) << r.max_cpu_ms << " ms";

        std::cout << std::left << std::setw(16) << r.algo << std::setw(11)
                  << r.mode << std::right << std::fixed << std::setprecision(1)
                  << std::setw(8) << r.succ_pct << "%" << std::setw(12)
                  << cpu_s.str() << std::setw(12) << p95_s.str() << std::setw(12)
                  << max_s.str() << std::setw(13) << std::setprecision(1)
                  << r.mean_nodes << std::setw(13) << std::setprecision(0)
                  << r.mean_edges << std::setw(10) << std::setprecision(0)
                  << r.avg_price << std::setw(9) << r.avg_dur_mins << "m\n";
      }
      std::cout << "============================================================"
                   "====================================================\n";
      std::cout << "Saved " << (cfg.num_queries * summary.rows.size())
                << " records to: " << cfg.output_csv << " & " << cfg.summary_csv
                << "\n";
    }
    return 0;
  }
};
