// ============================================================================
// benchmark.cpp — Systematic verification of all optimizations
// ============================================================================
// Reproduces the tables from RESULTS.md and best.md by testing each
// optimization in isolation and in combination.
//
// Usage:
//   ./benchmark --data <fbin> --queries <fbin> --gt <ibin> [--K 10]
//
// Tests run (in order):
//   1. Exact Float32 search (Optimization D baseline — flat vectors + EA)
//   2. Quantized ADC search (Optimization A)
//   3. Dynamic Beam — Exact (Optimization C)
//   4. Dynamic Beam — Quantized (Optimizations A+C)
//   5. R=32 vs R=64 build comparison
//   6. Full best configuration (R=64, quantized, dynamic)

#include "vamana_index.h"
#include "io_utils.h"
#include "timer.h"

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

// ---- Helpers ----

static double compute_recall(const std::vector<uint32_t>& result,
                             const uint32_t* gt, uint32_t K) {
    uint32_t found = 0;
    for (uint32_t i = 0; i < K && i < result.size(); i++) {
        for (uint32_t j = 0; j < K; j++) {
            if (result[i] == gt[j]) {
                found++;
                break;
            }
        }
    }
    return (double)found / K;
}

struct BenchRow {
    uint32_t L;
    double recall;
    double avg_dist_cmps;
    double avg_latency_us;
    double p99_latency_us;
};

static std::vector<BenchRow> run_search(
    const VamanaIndex& index,
    const FloatMatrix& queries,
    const IntMatrix& gt,
    uint32_t K,
    const std::vector<uint32_t>& L_values,
    bool quantized,
    bool dynamic_beam)
{
    uint32_t nq = queries.npts;
    std::vector<BenchRow> rows;

    for (uint32_t L : L_values) {
        std::vector<double> recalls(nq);
        std::vector<uint32_t> dist_cmps(nq);
        std::vector<double> latencies(nq);

        #pragma omp parallel for schedule(dynamic, 16)
        for (uint32_t q = 0; q < nq; q++) {
            SearchResult res;
            if (quantized) {
                res = index.search_quantized(queries.row(q), K, L, dynamic_beam);
            } else {
                res = index.search(queries.row(q), K, L);
            }
            recalls[q] = compute_recall(res.ids, gt.row(q), K);
            dist_cmps[q] = res.dist_cmps;
            latencies[q] = res.latency_us;
        }

        double avg_recall = std::accumulate(recalls.begin(), recalls.end(), 0.0) / nq;
        double avg_cmps = (double)std::accumulate(dist_cmps.begin(), dist_cmps.end(), 0ULL) / nq;
        double avg_lat = std::accumulate(latencies.begin(), latencies.end(), 0.0) / nq;

        std::sort(latencies.begin(), latencies.end());
        double p99_lat = latencies[(size_t)(0.99 * nq)];

        rows.push_back({L, avg_recall, avg_cmps, avg_lat, p99_lat});
    }
    return rows;
}

static void print_header(const std::string& title) {
    std::cout << "\n" << std::string(78, '=') << std::endl;
    std::cout << "  " << title << std::endl;
    std::cout << std::string(78, '=') << std::endl;
    std::cout << std::setw(8)  << "L"
              << std::setw(14) << "Recall@K"
              << std::setw(14) << "Dist Cmps"
              << std::setw(18) << "Avg Latency(us)"
              << std::setw(18) << "P99 Latency(us)"
              << std::endl;
    std::cout << std::string(72, '-') << std::endl;
}

static void print_rows(const std::vector<BenchRow>& rows) {
    for (const auto& r : rows) {
        std::cout << std::setw(8)  << r.L
                  << std::setw(14) << std::fixed << std::setprecision(4) << r.recall
                  << std::setw(14) << std::fixed << std::setprecision(1) << r.avg_dist_cmps
                  << std::setw(18) << std::fixed << std::setprecision(1) << r.avg_latency_us
                  << std::setw(18) << std::fixed << std::setprecision(1) << r.p99_latency_us
                  << std::endl;
    }
}

static void print_comparison_header(const std::string& title,
                                    const std::string& label_a,
                                    const std::string& label_b) {
    std::cout << "\n" << std::string(100, '=') << std::endl;
    std::cout << "  " << title << std::endl;
    std::cout << std::string(100, '=') << std::endl;
    std::cout << std::setw(6) << "L"
              << std::setw(12) << label_a.substr(0,11) + " R"
              << std::setw(12) << label_b.substr(0,11) + " R"
              << std::setw(16) << label_a.substr(0,11) + " Lat"
              << std::setw(16) << label_b.substr(0,11) + " Lat"
              << std::setw(12) << "Delta"
              << std::setw(16) << label_a.substr(0,11) + " P99"
              << std::setw(16) << label_b.substr(0,11) + " P99"
              << std::endl;
    std::cout << std::string(100, '-') << std::endl;
}

static void print_comparison(const std::vector<BenchRow>& a,
                             const std::vector<BenchRow>& b) {
    for (size_t i = 0; i < a.size() && i < b.size(); i++) {
        double delta = (b[i].avg_latency_us - a[i].avg_latency_us) / a[i].avg_latency_us * 100.0;
        std::string delta_str;
        {
            std::ostringstream oss;
            oss << std::fixed << std::setprecision(1)
                << (delta < 0 ? "" : "+") << delta << "%";
            delta_str = oss.str();
        }

        std::cout << std::setw(6)  << a[i].L
                  << std::setw(12) << std::fixed << std::setprecision(4) << a[i].recall
                  << std::setw(12) << std::fixed << std::setprecision(4) << b[i].recall
                  << std::setw(16) << std::fixed << std::setprecision(1) << a[i].avg_latency_us
                  << std::setw(16) << std::fixed << std::setprecision(1) << b[i].avg_latency_us
                  << std::setw(12) << delta_str
                  << std::setw(16) << std::fixed << std::setprecision(1) << a[i].p99_latency_us
                  << std::setw(16) << std::fixed << std::setprecision(1) << b[i].p99_latency_us
                  << std::endl;
    }
}

// ============================================================================

static void print_usage(const char* prog) {
    std::cerr << "Usage: " << prog
              << " --data <fbin_path>"
              << " --queries <query_fbin>"
              << " --gt <ground_truth_ibin>"
              << " [--K <10>]"
              << " [--skip-r32]"
              << std::endl;
}

int main(int argc, char** argv) {
    std::string data_path, query_path, gt_path;
    uint32_t K = 10;
    bool skip_r32 = false;

    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];
        if      (arg == "--data"    && i+1 < argc) data_path  = argv[++i];
        else if (arg == "--queries" && i+1 < argc) query_path = argv[++i];
        else if (arg == "--gt"      && i+1 < argc) gt_path    = argv[++i];
        else if (arg == "--K"       && i+1 < argc) K = std::atoi(argv[++i]);
        else if (arg == "--skip-r32")              skip_r32 = true;
        else if (arg == "--help" || arg == "-h") { print_usage(argv[0]); return 0; }
    }

    if (data_path.empty() || query_path.empty() || gt_path.empty()) {
        print_usage(argv[0]);
        return 1;
    }

    std::vector<uint32_t> L_values = {10, 50, 75, 100, 150, 200};

    // ---- Load queries & ground truth (shared across all tests) ----
    std::cout << "Loading queries..." << std::endl;
    FloatMatrix queries = load_fbin(query_path);
    std::cout << "  Queries: " << queries.npts << " x " << queries.dims << std::endl;

    std::cout << "Loading ground truth..." << std::endl;
    IntMatrix gt = load_ibin(gt_path);
    std::cout << "  Ground truth: " << gt.npts << " x " << gt.dims << std::endl;

    if (gt.dims < K) K = gt.dims;

    // ==================================================================
    // TEST 1-4: R=64 index (best configuration build)
    // ==================================================================

    std::cout << "\n>>> Building R=64 index (medoid + random-init + full-V prune)...\n";
    VamanaIndex index64;
    Timer build64_timer;
    index64.build(data_path, 64, 100, 1.2f, 1.5f);
    double build64_time = build64_timer.elapsed_seconds();
    std::cout << "  R=64 build time: " << build64_time << "s\n";

    // Build quantization for this index
    index64.build_quantization();

    // ---- Test 1: Exact Float32 (flat vector + EA) ----
    print_header("TEST 1: Exact Float32 — Flat Vector + Early Abandoning (R=64)");
    auto exact64 = run_search(index64, queries, gt, K, L_values, false, false);
    print_rows(exact64);

    // ---- Test 2: Quantized ADC (Optimization A) ----
    print_header("TEST 2: Quantized ADC + Float32 Re-Ranking (R=64)");
    auto quant64 = run_search(index64, queries, gt, K, L_values, true, false);
    print_rows(quant64);

    // ---- Comparison: Exact vs Quantized ----
    print_comparison_header("COMPARISON: Exact Float32 vs Quantized ADC (R=64)",
                            "Exact", "Quant");
    print_comparison(exact64, quant64);

    // ---- Test 3: Dynamic Beam — Exact (Optimization C) ----
    // Note: dynamic beam only activates in search_quantized path currently.
    // For exact + dynamic, we show quantized+dynamic vs quantized (isolates C).
    print_header("TEST 3: Quantized ADC + Dynamic Beam (R=64)");
    auto quant_dyn64 = run_search(index64, queries, gt, K, L_values, true, true);
    print_rows(quant_dyn64);

    // ---- Comparison: Quantized vs Quantized+Dynamic ----
    print_comparison_header("COMPARISON: Quantized vs Quantized+Dynamic (R=64)",
                            "Quant", "Quant+Dyn");
    print_comparison(quant64, quant_dyn64);

    // ==================================================================
    // TEST 5: R=32 index for comparison (if not skipped)
    // ==================================================================

    if (!skip_r32) {
        std::cout << "\n>>> Building R=32 index for comparison...\n";
        VamanaIndex index32;
        Timer build32_timer;
        index32.build(data_path, 32, 75, 1.2f, 1.5f);
        double build32_time = build32_timer.elapsed_seconds();
        std::cout << "  R=32 build time: " << build32_time << "s\n";

        index32.build_quantization();

        // R=32 exact baseline
        print_header("TEST 5a: Exact Float32 (R=32)");
        auto exact32 = run_search(index32, queries, gt, K, L_values, false, false);
        print_rows(exact32);

        // R=32 quantized
        print_header("TEST 5b: Quantized ADC (R=32)");
        auto quant32 = run_search(index32, queries, gt, K, L_values, true, false);
        print_rows(quant32);

        // ---- Comparison: R=32 vs R=64 (quantized) ----
        print_comparison_header("COMPARISON: R=32 Quantized vs R=64 Quantized",
                                "R=32", "R=64");
        print_comparison(quant32, quant64);

        // ---- Comparison: R=32 exact vs R=64 best (quantized+dynamic) ----
        print_comparison_header("COMPARISON: R=32 Exact (Baseline) vs R=64 Best (Quant+Dyn)",
                                "R32-Exact", "R64-Best");
        print_comparison(exact32, quant_dyn64);
    }

    // ==================================================================
    // SUMMARY: Best configuration — full table
    // ==================================================================

    print_header("BEST CONFIGURATION: R=64 + Quantized + Dynamic Beam");
    print_rows(quant_dyn64);

    // ---- Equivalent recall comparison ----
    std::cout << "\n" << std::string(78, '=') << std::endl;
    std::cout << "  EQUIVALENT RECALL COMPARISON" << std::endl;
    std::cout << std::string(78, '=') << std::endl;
    std::cout << std::setw(30) << "Configuration"
              << std::setw(14) << "Recall@K"
              << std::setw(16) << "Avg Lat(us)"
              << std::setw(16) << "P99 Lat(us)"
              << std::endl;
    std::cout << std::string(76, '-') << std::endl;

    // Find matching recall pairs between exact64 and quant_dyn64
    for (size_t i = 0; i < exact64.size(); i++) {
        // For each exact64 row, find the quant_dyn64 row with closest recall
        for (size_t j = 0; j < quant_dyn64.size(); j++) {
            if (quant_dyn64[j].recall >= exact64[i].recall - 0.002 &&
                quant_dyn64[j].recall <= exact64[i].recall + 0.002 &&
                quant_dyn64[j].L < exact64[i].L) {
                std::ostringstream cfg1, cfg2;
                cfg1 << "Exact L=" << exact64[i].L;
                cfg2 << "Best  L=" << quant_dyn64[j].L;

                std::cout << std::setw(30) << cfg1.str()
                          << std::setw(14) << std::fixed << std::setprecision(4) << exact64[i].recall
                          << std::setw(16) << std::fixed << std::setprecision(1) << exact64[i].avg_latency_us
                          << std::setw(16) << std::fixed << std::setprecision(1) << exact64[i].p99_latency_us
                          << std::endl;
                std::cout << std::setw(30) << cfg2.str()
                          << std::setw(14) << std::fixed << std::setprecision(4) << quant_dyn64[j].recall
                          << std::setw(16) << std::fixed << std::setprecision(1) << quant_dyn64[j].avg_latency_us
                          << std::setw(16) << std::fixed << std::setprecision(1) << quant_dyn64[j].p99_latency_us
                          << std::endl;
                std::cout << std::string(76, '-') << std::endl;
                break;
            }
        }
    }

    std::cout << "\nDone. All benchmarks complete.\n";
    return 0;
}
