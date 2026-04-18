#include "experiments.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>
#include <omp.h>

void save_results(const std::vector<ExperimentResult>& results, const std::string& filename) {
    std::ofstream out(filename);
    if (!out.is_open()) {
        std::cerr << "ERROR: Cannot open " << filename << " for writing\n";
        return;
    }

    out << "experiment_name,variant,build_time_sec,avg_recall,p50_recall,p95_recall,"
        << "avg_dist_cmps,avg_latency_us,avg_degree,min_degree,max_degree\n";

    for (const auto& r : results) {
        out << r.experiment_name << ","
            << r.variant << ","
            << std::fixed << std::setprecision(4)
            << r.build_time_sec << ","
            << r.avg_recall << ","
            << r.p50_recall << ","
            << r.p95_recall << ","
            << r.avg_dist_cmps << ","
            << r.avg_latency_us << ","
            << r.avg_degree << ","
            << r.min_degree << ","
            << r.max_degree << "\n";
    }

    out.close();
    std::cout << "\n✓ Results saved to " << filename << " (" << results.size() << " results)\n";
}

int main(int argc, char** argv) {
    // Limit OMP threads to avoid crashes with high thread count
    omp_set_num_threads(2);
    
    if (argc < 2) {
        std::cout << "Usage: experiment_runner_v3 <experiment> [options]\n\n"
                  << "Experiments:\n"
                  << "  1 - beam_width (quick)\n"
                  << "  2 - medoid_start\n"
                  << "  3 - multipass_build\n"
                  << "  4 - degree_analysis\n"
                  << "  5 - search_opt\n\n"
                  << "Options: --data, --queries, --gt, --output, --R, --L, --alpha, --gamma, --K\n";
        return 1;
    }

    int exp_num = std::stoi(argv[1]);
    std::string data_path, query_path, gt_path, output_path = "results.csv";
    uint32_t R = 32, L = 75, K = 10;
    float alpha = 1.2, gamma = 1.5;

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--data" && i + 1 < argc) data_path = argv[++i];
        else if (arg == "--queries" && i + 1 < argc) query_path = argv[++i];
        else if (arg == "--gt" && i + 1 < argc) gt_path = argv[++i];
        else if (arg == "--output" && i + 1 < argc) output_path = argv[++i];
        else if (arg == "--R" && i + 1 < argc) R = std::stoul(argv[++i]);
        else if (arg == "--L" && i + 1 < argc) L = std::stoul(argv[++i]);
        else if (arg == "--alpha" && i + 1 < argc) alpha = std::stof(argv[++i]);
        else if (arg == "--gamma" && i + 1 < argc) gamma = std::stof(argv[++i]);
        else if (arg == "--K" && i + 1 < argc) K = std::stoul(argv[++i]);
    }

    std::vector<ExperimentResult> all_results;

    try {
        if (exp_num == 1) {
            std::cout << "=== EXPERIMENT 1: Beam Width (L=50,75,100,125,150) ===\n";
            std::vector<uint32_t> L_values = {50, 75, 100, 125, 150};
            auto results = ExperimentRunner::experiment_beam_width(
                data_path, query_path, gt_path, R, alpha, gamma, L_values, K);
            all_results.insert(all_results.end(), results.begin(), results.end());

        } else if (exp_num == 2) {
            std::cout << "=== EXPERIMENT 2: Medoid Start ===\n";
            auto results = ExperimentRunner::experiment_medoid_start(
                data_path, query_path, gt_path, R, L, alpha, gamma, K);
            all_results.insert(all_results.end(), results.begin(), results.end());

        } else if (exp_num == 3) {
            std::cout << "=== EXPERIMENT 3: Multi-Pass Build ===\n";
            auto results = ExperimentRunner::experiment_multipass_build(
                data_path, query_path, gt_path, R, L, alpha, gamma, K);
            all_results.insert(all_results.end(), results.begin(), results.end());

        } else if (exp_num == 4) {
            std::cout << "=== EXPERIMENT 4: Degree Analysis (alpha=1.0,1.2,1.5) ===\n";
            std::vector<float> alpha_values = {1.0f, 1.2f, 1.5f};
            auto results = ExperimentRunner::experiment_degree_analysis(
                data_path, query_path, gt_path, R, L, gamma, alpha_values, K);
            all_results.insert(all_results.end(), results.begin(), results.end());

        } else if (exp_num == 5) {
            std::cout << "=== EXPERIMENT 5: Search Optimization ===\n";
            auto results = ExperimentRunner::experiment_search_optimization(
                data_path, query_path, gt_path, R, L, alpha, gamma, K);
            all_results.insert(all_results.end(), results.begin(), results.end());

        } else {
            std::cerr << "Unknown experiment: " << exp_num << "\n";
            return 1;
        }

        if (!all_results.empty()) {
            save_results(all_results, output_path);
            std::cout << "✓ Done!\n";
        } else {
            std::cerr << "ERROR: No results collected\n";
            return 1;
        }

    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
