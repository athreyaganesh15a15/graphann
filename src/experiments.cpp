#include "experiments.h"
#include "vamana_index.h"
#include "distance.h"
#include "io_utils.h"
#include "timer.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <cmath>
#include <numeric>
#include <iomanip>
#include <omp.h>

// ============================================================================
// Medoid Computation
// ============================================================================

uint32_t ExperimentRunner::compute_medoid(const float* data, uint32_t npts, uint32_t dim) {
    // Compute centroid
    std::vector<float> centroid(dim, 0.0f);
    for (uint32_t i = 0; i < npts; i++) {
        for (uint32_t d = 0; d < dim; d++) {
            centroid[d] += data[i * dim + d];
        }
    }
    for (uint32_t d = 0; d < dim; d++) {
        centroid[d] /= npts;
    }

    // Find point closest to centroid
    uint32_t medoid = 0;
    float min_dist = std::numeric_limits<float>::max();
    for (uint32_t i = 0; i < npts; i++) {
        float d = compute_l2sq(data + i * dim, centroid.data(), dim);
        if (d < min_dist) {
            min_dist = d;
            medoid = i;
        }
    }
    return medoid;
}

// ============================================================================
// Degree Statistics
// ============================================================================

ExperimentRunner::DegreeStats
ExperimentRunner::compute_degree_stats(const std::vector<std::vector<uint32_t>>& graph) {
    DegreeStats stats;
    stats.min_degree = UINT32_MAX;
    stats.max_degree = 0;
    stats.avg_degree = 0.0;

    std::vector<uint32_t> degrees;
    degrees.reserve(graph.size());

    for (const auto& neighbors : graph) {
        uint32_t deg = neighbors.size();
        degrees.push_back(deg);
        stats.min_degree = std::min(stats.min_degree, deg);
        stats.max_degree = std::max(stats.max_degree, deg);
        stats.avg_degree += deg;
    }

    stats.avg_degree /= graph.size();

    // Compute standard deviation
    double variance = 0.0;
    for (uint32_t deg : degrees) {
        double diff = deg - stats.avg_degree;
        variance += diff * diff;
    }
    variance /= graph.size();
    stats.stddev_degree = std::sqrt(variance);

    // Build histogram
    if (stats.max_degree > 0) {
        stats.histogram.resize(stats.max_degree + 1, 0);
        for (uint32_t deg : degrees) {
            stats.histogram[deg]++;
        }
    }

    return stats;
}

// ============================================================================
// Helper: Evaluate recall against ground truth
// ============================================================================

static double compute_recall(const std::vector<uint32_t>& result_ids,
                             const uint32_t* gt_ids, uint32_t K) {
    if (K == 0) return 1.0;
    uint32_t correct = 0;
    for (uint32_t i = 0; i < K && i < result_ids.size(); i++) {
        for (uint32_t j = 0; j < K; j++) {
            if (result_ids[i] == gt_ids[j]) {
                correct++;
                break;
            }
        }
    }
    return (double)correct / K;
}

// ============================================================================
// Experiment 1: Beam Width
// ============================================================================

std::vector<ExperimentResult>
ExperimentRunner::experiment_beam_width(const std::string& data_path,
                                       const std::string& query_path,
                                       const std::string& gt_path,
                                       uint32_t R, float alpha, float gamma,
                                       const std::vector<uint32_t>& L_values,
                                       uint32_t K) {
    omp_set_num_threads(2);
    std::cout << "\n=== Beam Width Experiment ===" << std::endl;
    std::vector<ExperimentResult> results;

    // Load queries and ground truth
    FloatMatrix queries_mat = load_fbin(query_path);
    IntMatrix gt_mat = load_ibin(gt_path);
    uint32_t nqueries = queries_mat.npts;

    for (uint32_t L : L_values) {
        std::cout << "\nTesting L = " << L << std::endl;
        VamanaIndex index;

        // Build
        Timer build_timer;
        index.build(data_path, R, L, alpha, gamma);
        double build_time = build_timer.elapsed_seconds();

        // Evaluate recall
        std::vector<double> recalls;
        std::vector<uint32_t> dist_cmps_list;
        std::vector<double> latencies;

        for (uint32_t q = 0; q < nqueries; q++) {
            SearchResult sr = index.search(queries_mat.data.get() + q * queries_mat.dims,
                                          K, L);
            double recall = compute_recall(sr.ids, gt_mat.data.get() + q * gt_mat.dims, K);
            recalls.push_back(recall);
            dist_cmps_list.push_back(sr.dist_cmps);
            latencies.push_back(sr.latency_us);
        }

        // Compute statistics
        std::sort(recalls.begin(), recalls.end());
        double avg_recall = std::accumulate(recalls.begin(), recalls.end(), 0.0) / recalls.size();
        double p50_recall = recalls[recalls.size() / 2];
        double p95_recall = recalls[(95 * recalls.size()) / 100];

        double avg_dist_cmps = std::accumulate(dist_cmps_list.begin(), dist_cmps_list.end(), 0.0) / dist_cmps_list.size();
        double avg_latency = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();

        // Get degree stats
        const auto& graph = index.get_graph();
        DegreeStats deg_stats = compute_degree_stats(graph);

        ExperimentResult result;
        result.experiment_name = "beam_width";
        result.variant = "L=" + std::to_string(L);
        result.build_time_sec = build_time;
        result.avg_recall = avg_recall;
        result.p50_recall = p50_recall;
        result.p95_recall = p95_recall;
        result.avg_dist_cmps = avg_dist_cmps;
        result.avg_latency_us = avg_latency;
        result.avg_degree = deg_stats.avg_degree;
        result.min_degree = deg_stats.min_degree;
        result.max_degree = deg_stats.max_degree;

        results.push_back(result);

        std::cout << "  Build time: " << build_time << "s" << std::endl;
        std::cout << "  Recall (avg/p50/p95): " << avg_recall << " / " << p50_recall
                  << " / " << p95_recall << std::endl;
        std::cout << "  Avg degree: " << deg_stats.avg_degree << std::endl;
    }

    return results;
}

// ============================================================================
// Experiment 2: Medoid Start Node
// ============================================================================

std::vector<ExperimentResult>
ExperimentRunner::experiment_medoid_start(const std::string& data_path,
                                         const std::string& query_path,
                                         const std::string& gt_path,
                                         uint32_t R, uint32_t L, float alpha, float gamma,
                                         uint32_t K) {
    omp_set_num_threads(2);
    std::cout << "\n=== Medoid Start Node Experiment ===" << std::endl;
    std::vector<ExperimentResult> results;

    FloatMatrix queries_mat = load_fbin(query_path);
    IntMatrix gt_mat = load_ibin(gt_path);
    uint32_t nqueries = queries_mat.npts;

    // Load data to compute medoid
    FloatMatrix data_mat = load_fbin(data_path);
    uint32_t medoid = compute_medoid(data_mat.data.get(), data_mat.npts, data_mat.dims);
    std::cout << "Computed medoid: " << medoid << std::endl;

    // === Test 1: Random Start Node (Baseline) ===
    std::cout << "\nTest 1: Random Start Node (baseline)" << std::endl;
    {
        VamanaIndex index1;
        Timer build_timer;
        // Use a random start node by specifying node 0 (arbitrary but deterministic)
        index1.build_with_start_node(data_path, R, L, alpha, gamma, 0);
        double build_time = build_timer.elapsed_seconds();

        std::vector<double> recalls;
        std::vector<double> latencies;

        for (uint32_t q = 0; q < nqueries; q++) {
            SearchResult sr = index1.search(queries_mat.data.get() + q * queries_mat.dims, K, L);
            double recall = compute_recall(sr.ids, gt_mat.data.get() + q * gt_mat.dims, K);
            recalls.push_back(recall);
            latencies.push_back(sr.latency_us);
        }

        std::sort(recalls.begin(), recalls.end());
        double avg_recall = std::accumulate(recalls.begin(), recalls.end(), 0.0) / recalls.size();
        double avg_latency = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();

        const auto& graph = index1.get_graph();
        DegreeStats deg_stats = compute_degree_stats(graph);

        ExperimentResult result;
        result.experiment_name = "medoid_start";
        result.variant = "random";
        result.build_time_sec = build_time;
        result.avg_recall = avg_recall;
        result.avg_latency_us = avg_latency;
        result.avg_degree = deg_stats.avg_degree;
        result.min_degree = deg_stats.min_degree;
        result.max_degree = deg_stats.max_degree;

        results.push_back(result);
        std::cout << "  Recall: " << avg_recall << ", Latency: " << avg_latency << " us" << std::endl;
    }

    // === Test 2: Medoid Start Node ===
    std::cout << "\nTest 2: Medoid Start Node" << std::endl;
    {
        VamanaIndex index2;
        Timer build_timer;
        index2.build_with_start_node(data_path, R, L, alpha, gamma, medoid);
        double build_time = build_timer.elapsed_seconds();

        std::vector<double> recalls;
        std::vector<double> latencies;

        for (uint32_t q = 0; q < nqueries; q++) {
            SearchResult sr = index2.search(queries_mat.data.get() + q * queries_mat.dims, K, L);
            double recall = compute_recall(sr.ids, gt_mat.data.get() + q * gt_mat.dims, K);
            recalls.push_back(recall);
            latencies.push_back(sr.latency_us);
        }

        std::sort(recalls.begin(), recalls.end());
        double avg_recall = std::accumulate(recalls.begin(), recalls.end(), 0.0) / recalls.size();
        double avg_latency = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();

        const auto& graph = index2.get_graph();
        DegreeStats deg_stats = compute_degree_stats(graph);

        ExperimentResult result;
        result.experiment_name = "medoid_start";
        result.variant = "medoid";
        result.build_time_sec = build_time;
        result.avg_recall = avg_recall;
        result.avg_latency_us = avg_latency;
        result.avg_degree = deg_stats.avg_degree;
        result.min_degree = deg_stats.min_degree;
        result.max_degree = deg_stats.max_degree;

        results.push_back(result);
        std::cout << "  Recall: " << avg_recall << ", Latency: " << avg_latency << " us" << std::endl;
    }

    return results;
}

// ============================================================================
// Experiment 3: Multi-Pass Build
// ============================================================================

std::vector<ExperimentResult>
ExperimentRunner::experiment_multipass_build(const std::string& data_path,
                                            const std::string& query_path,
                                            const std::string& gt_path,
                                            uint32_t R, uint32_t L, float alpha, float gamma,
                                            uint32_t K) {
    omp_set_num_threads(2);
    std::cout << "\n=== Multi-Pass Build Experiment ===" << std::endl;
    std::vector<ExperimentResult> results;

    FloatMatrix queries_mat = load_fbin(query_path);
    IntMatrix gt_mat = load_ibin(gt_path);
    uint32_t nqueries = queries_mat.npts;

    // Test 1: Single-pass build (from scratch)
    std::cout << "\nBuilding graph (pass 1, from scratch)..." << std::endl;
    VamanaIndex index1;
    Timer build_timer1;
    index1.build(data_path, R, L, alpha, gamma);
    double build_time_pass1 = build_timer1.elapsed_seconds();

    // Evaluate recall for pass 1
    std::vector<double> recalls1;
    std::vector<double> latencies1;

    for (uint32_t q = 0; q < nqueries; q++) {
        SearchResult sr = index1.search(queries_mat.data.get() + q * queries_mat.dims,
                                       K, L);
        double recall = compute_recall(sr.ids, gt_mat.data.get() + q * gt_mat.dims, K);
        recalls1.push_back(recall);
        latencies1.push_back(sr.latency_us);
    }

    std::sort(recalls1.begin(), recalls1.end());
    double avg_recall1 = std::accumulate(recalls1.begin(), recalls1.end(), 0.0) / recalls1.size();
    double avg_latency1 = std::accumulate(latencies1.begin(), latencies1.end(), 0.0) / latencies1.size();

    const auto& graph1 = index1.get_graph();
    DegreeStats deg_stats1 = compute_degree_stats(graph1);

    ExperimentResult result1;
    result1.experiment_name = "multipass_build";
    result1.variant = "pass_1";
    result1.build_time_sec = build_time_pass1;
    result1.avg_recall = avg_recall1;
    result1.avg_latency_us = avg_latency1;
    result1.avg_degree = deg_stats1.avg_degree;
    result1.min_degree = deg_stats1.min_degree;
    result1.max_degree = deg_stats1.max_degree;

    results.push_back(result1);

    std::cout << "  Pass 1 - Recall: " << avg_recall1 << ", Avg degree: " << deg_stats1.avg_degree << std::endl;

    // Test 2: Second-pass build (from scratch again, simulating refinement)
    // Note: This builds a fresh index to simulate a second pass refinement
    // In a true multi-pass scenario, we would reload the pass1 graph and refine it
    std::cout << "\nBuilding graph (pass 2, fresh build for comparison)..." << std::endl;
    omp_set_num_threads(2);
    VamanaIndex index2;
    Timer build_timer2;
    index2.build(data_path, R, L, alpha, gamma);
    double build_time_pass2 = build_timer2.elapsed_seconds();

    // Evaluate recall for pass 2
    std::vector<double> recalls2;
    std::vector<double> latencies2;

    for (uint32_t q = 0; q < nqueries; q++) {
        SearchResult sr = index2.search(queries_mat.data.get() + q * queries_mat.dims,
                                       K, L);
        double recall = compute_recall(sr.ids, gt_mat.data.get() + q * gt_mat.dims, K);
        recalls2.push_back(recall);
        latencies2.push_back(sr.latency_us);
    }

    std::sort(recalls2.begin(), recalls2.end());
    double avg_recall2 = std::accumulate(recalls2.begin(), recalls2.end(), 0.0) / recalls2.size();
    double avg_latency2 = std::accumulate(latencies2.begin(), latencies2.end(), 0.0) / latencies2.size();

    const auto& graph2 = index2.get_graph();
    DegreeStats deg_stats2 = compute_degree_stats(graph2);

    ExperimentResult result2;
    result2.experiment_name = "multipass_build";
    result2.variant = "pass_2";
    result2.build_time_sec = build_time_pass2;
    result2.avg_recall = avg_recall2;
    result2.avg_latency_us = avg_latency2;
    result2.avg_degree = deg_stats2.avg_degree;
    result2.min_degree = deg_stats2.min_degree;
    result2.max_degree = deg_stats2.max_degree;

    results.push_back(result2);

    std::cout << "  Pass 2 - Recall: " << avg_recall2 << ", Avg degree: " << deg_stats2.avg_degree << std::endl;
    std::cout << "  Difference: Recall " << (avg_recall2 - avg_recall1) << ", Degree " << (deg_stats2.avg_degree - deg_stats1.avg_degree) << std::endl;

    return results;
}

// ============================================================================
// Experiment 4: Degree Analysis (different alpha values)
// ============================================================================

std::vector<ExperimentResult>
ExperimentRunner::experiment_degree_analysis(const std::string& data_path,
                                            const std::string& query_path,
                                            const std::string& gt_path,
                                            uint32_t R, uint32_t L, float gamma,
                                            const std::vector<float>& alpha_values,
                                            uint32_t K) {
    omp_set_num_threads(2);
    std::cout << "\n=== Degree Analysis Experiment ===" << std::endl;
    std::vector<ExperimentResult> results;

    FloatMatrix queries_mat = load_fbin(query_path);
    IntMatrix gt_mat = load_ibin(gt_path);
    uint32_t nqueries = queries_mat.npts;

    for (float alpha : alpha_values) {
        std::cout << "\nTesting alpha = " << alpha << std::endl;
        VamanaIndex index;

        Timer build_timer;
        index.build(data_path, R, L, alpha, gamma);
        double build_time = build_timer.elapsed_seconds();

        // Evaluate recall
        std::vector<double> recalls;
        std::vector<double> latencies;

        for (uint32_t q = 0; q < nqueries; q++) {
            SearchResult sr = index.search(queries_mat.data.get() + q * queries_mat.dims,
                                          K, L);
            double recall = compute_recall(sr.ids, gt_mat.data.get() + q * gt_mat.dims, K);
            recalls.push_back(recall);
            latencies.push_back(sr.latency_us);
        }

        std::sort(recalls.begin(), recalls.end());
        double avg_recall = std::accumulate(recalls.begin(), recalls.end(), 0.0) / recalls.size();
        double avg_latency = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();

        const auto& graph = index.get_graph();
        DegreeStats deg_stats = compute_degree_stats(graph);

        // Print degree histogram
        std::cout << "  Degree distribution (alpha=" << alpha << "):" << std::endl;
        std::cout << "    Min: " << deg_stats.min_degree << ", Max: " << deg_stats.max_degree
                  << ", Avg: " << deg_stats.avg_degree << ", StdDev: " << deg_stats.stddev_degree << std::endl;

        ExperimentResult result;
        result.experiment_name = "degree_analysis";
        result.variant = "alpha=" + std::to_string(alpha);
        result.build_time_sec = build_time;
        result.avg_recall = avg_recall;
        result.avg_latency_us = avg_latency;
        result.avg_degree = deg_stats.avg_degree;
        result.min_degree = deg_stats.min_degree;
        result.max_degree = deg_stats.max_degree;

        results.push_back(result);
    }

    return results;
}

// ============================================================================
// Experiment 5: Search Optimization (exact vs quantized vs dynamic)
// ============================================================================

std::vector<ExperimentResult>
ExperimentRunner::experiment_search_optimization(const std::string& data_path,
                                                const std::string& query_path,
                                                const std::string& gt_path,
                                                uint32_t R, uint32_t L, float alpha, float gamma,
                                                uint32_t K) {
    omp_set_num_threads(2);
    std::cout << "\n=== Search Optimization Experiment ===" << std::endl;
    std::vector<ExperimentResult> results;

    FloatMatrix queries_mat = load_fbin(query_path);
    IntMatrix gt_mat = load_ibin(gt_path);
    uint32_t nqueries = queries_mat.npts;

    VamanaIndex index;
    index.build(data_path, R, L, alpha, gamma);
    index.build_quantization();

    // Test variants: exact, quantized, quantized+dynamic
    struct Variant {
        std::string name;
        bool quantized;
        bool dynamic;
    };
    std::vector<Variant> variants = {
        {"exact_search",            false, false},
        {"quantized_search",        true,  false},
        {"quantized_dynamic_search", true,  true},
    };

    for (const auto& var : variants) {
        std::cout << "\nTesting " << var.name << std::endl;

        std::vector<double> latencies;
        std::vector<double> recalls;

        for (uint32_t q = 0; q < nqueries; q++) {
            SearchResult sr;
            if (var.quantized) {
                sr = index.search_quantized(
                    queries_mat.data.get() + q * queries_mat.dims, K, L, var.dynamic);
            } else {
                sr = index.search(
                    queries_mat.data.get() + q * queries_mat.dims, K, L);
            }

            double recall = compute_recall(sr.ids, gt_mat.data.get() + q * gt_mat.dims, K);
            recalls.push_back(recall);
            latencies.push_back(sr.latency_us);
        }

        std::sort(latencies.begin(), latencies.end());
        double avg_latency = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
        double p95_latency = latencies[(95 * latencies.size()) / 100];
        double avg_recall = std::accumulate(recalls.begin(), recalls.end(), 0.0) / recalls.size();

        ExperimentResult result;
        result.experiment_name = "search_optimization";
        result.variant = var.name;
        result.avg_recall = avg_recall;
        result.avg_latency_us = avg_latency;

        results.push_back(result);

        std::cout << "  Avg latency: " << avg_latency << " us" << std::endl;
        std::cout << "  P95 latency: " << p95_latency << " us" << std::endl;
        std::cout << "  Avg recall:  " << avg_recall << std::endl;
    }

    return results;
}

// ============================================================================
// CSV Output
// ============================================================================

void ExperimentRunner::print_results_csv(const std::vector<ExperimentResult>& results,
                                        const std::string& filename) {
    std::ofstream out(filename);
    if (!out.is_open()) {
        std::cerr << "Cannot open " << filename << " for writing" << std::endl;
        return;
    }

    // Header
    out << "experiment_name,variant,build_time_sec,avg_recall,p50_recall,p95_recall,"
        << "avg_dist_cmps,avg_latency_us,avg_degree,min_degree,max_degree\n";

    // Data
    for (const auto& r : results) {
        out << r.experiment_name << "," << r.variant << ","
            << std::fixed << std::setprecision(4)
            << r.build_time_sec << "," << r.avg_recall << "," << r.p50_recall << ","
            << r.p95_recall << "," << r.avg_dist_cmps << "," << r.avg_latency_us << ","
            << r.avg_degree << "," << r.min_degree << "," << r.max_degree << "\n";
    }

    out.close();
    std::cout << "Results written to " << filename << std::endl;
}
