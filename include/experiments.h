#pragma once

#include <string>
#include <vector>
#include <cstdint>

// Result of a single experiment run
struct ExperimentResult {
    std::string experiment_name;
    std::string variant;
    double build_time_sec;
    double avg_recall;
    double p50_recall;
    double p95_recall;
    double avg_dist_cmps;
    double avg_latency_us;
    double avg_degree;
    uint32_t min_degree;
    uint32_t max_degree;
};

// Experiment runner
class ExperimentRunner {
  public:
    // Run beam width experiments (different L values during build)
    static std::vector<ExperimentResult>
    experiment_beam_width(const std::string& data_path,
                         const std::string& query_path,
                         const std::string& gt_path,
                         uint32_t R, float alpha, float gamma,
                         const std::vector<uint32_t>& L_values,
                         uint32_t K);

    // Compare random start node vs medoid start node
    static std::vector<ExperimentResult>
    experiment_medoid_start(const std::string& data_path,
                           const std::string& query_path,
                           const std::string& gt_path,
                           uint32_t R, uint32_t L, float alpha, float gamma,
                           uint32_t K);

    // Run build twice and compare results
    static std::vector<ExperimentResult>
    experiment_multipass_build(const std::string& data_path,
                              const std::string& query_path,
                              const std::string& gt_path,
                              uint32_t R, uint32_t L, float alpha, float gamma,
                              uint32_t K);

    // Analyze degree distribution for different alpha values
    static std::vector<ExperimentResult>
    experiment_degree_analysis(const std::string& data_path,
                              const std::string& query_path,
                              const std::string& gt_path,
                              uint32_t R, uint32_t L, float gamma,
                              const std::vector<float>& alpha_values,
                              uint32_t K);

    // Test search optimization (exact vs quantized vs dynamic)
    static std::vector<ExperimentResult>
    experiment_search_optimization(const std::string& data_path,
                                  const std::string& query_path,
                                  const std::string& gt_path,
                                  uint32_t R, uint32_t L, float alpha, float gamma,
                                  uint32_t K);

    // Compute medoid (closest point to centroid)
    static uint32_t compute_medoid(const float* data, uint32_t npts, uint32_t dim);

    // Print results to CSV
    static void print_results_csv(const std::vector<ExperimentResult>& results,
                                  const std::string& filename);

    // Degree statistics
    struct DegreeStats {
        uint32_t min_degree;
        uint32_t max_degree;
        double avg_degree;
        double stddev_degree;
        std::vector<uint32_t> histogram;
    };

    static DegreeStats compute_degree_stats(const std::vector<std::vector<uint32_t>>& graph);
};
