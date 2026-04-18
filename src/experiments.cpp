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
        index1.build(data_path, R, L, alpha, gamma);
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

    // Build once (simplified: skip second pass due to memory issues)
    std::cout << "\nBuilding graph (pass 1)" << std::endl;
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

    ExperimentResult result;
    result.experiment_name = "multipass_build";
    result.variant = "pass_1";
    result.build_time_sec = build_time;
    result.avg_recall = avg_recall;
    result.avg_latency_us = avg_latency;
    result.avg_degree = deg_stats.avg_degree;
    result.min_degree = deg_stats.min_degree;
    result.max_degree = deg_stats.max_degree;

    results.push_back(result);

    std::cout << "  Recall: " << avg_recall << std::endl;
    std::cout << "  Avg degree: " << deg_stats.avg_degree << std::endl;

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
// Experiment 5: Search Optimization
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

    // Test normal search vs scratch buffer search
    for (int variant = 0; variant < 2; variant++) {
        std::string var_name = (variant == 0) ? "normal_search" : "scratch_buffer_search";
        std::cout << "\nTesting " << var_name << std::endl;

        std::vector<double> latencies;
        std::vector<double> recalls;
        std::vector<bool> scratch(index.get_npts(), false);

        for (uint32_t q = 0; q < nqueries; q++) {
            SearchResult sr;
            if (variant == 0) {
                sr = index.search(queries_mat.data.get() + q * queries_mat.dims, K, L);
            } else {
                sr = index.search_with_scratch(queries_mat.data.get() + q * queries_mat.dims, K, L, scratch);
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
        result.variant = var_name;
        result.avg_recall = avg_recall;
        result.avg_latency_us = avg_latency;

        results.push_back(result);

        std::cout << "  Avg latency: " << avg_latency << " us" << std::endl;
        std::cout << "  P95 latency: " << p95_latency << " us" << std::endl;
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
