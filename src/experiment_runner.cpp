#include "experiments.h"
#include <iostream>
#include <vector>
#include <string>

void print_usage() {
    std::cout << "Usage: experiment_runner <experiment> [options]\n\n"
              << "Experiments:\n"
              << "  beam_width          - Test different L values during build\n"
              << "  medoid_start        - Compare random vs medoid start node\n"
              << "  multipass_build     - Test two-pass building\n"
              << "  degree_analysis     - Analyze degree distribution for different alpha values\n"
              << "  search_optimization - Compare search methods\n"
              << "  all                 - Run all experiments\n\n"
              << "Options (required for most experiments):\n"
              << "  --data <path>       Data file (fbin format)\n"
              << "  --queries <path>    Query file (fbin format)\n"
              << "  --gt <path>         Ground truth file (ibin format)\n"
              << "  --R <int>           Max out-degree (default: 32)\n"
              << "  --L <int>           Search list size (default: 75)\n"
              << "  --alpha <float>     Alpha parameter (default: 1.2)\n"
              << "  --gamma <float>     Gamma parameter (default: 1.5)\n"
              << "  --K <int>           Num neighbors (default: 10)\n"
              << "  --output <path>     Output CSV file (default: results.csv)\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    std::string experiment = argv[1];

    // Parse arguments
    std::string data_path, query_path, gt_path, output_path = "results.csv";
    uint32_t R = 32, L = 75, K = 10;
    float alpha = 1.2, gamma = 1.5;

    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--data" && i + 1 < argc) {
            data_path = argv[++i];
        } else if (arg == "--queries" && i + 1 < argc) {
            query_path = argv[++i];
        } else if (arg == "--gt" && i + 1 < argc) {
            gt_path = argv[++i];
        } else if (arg == "--R" && i + 1 < argc) {
            R = std::stoul(argv[++i]);
        } else if (arg == "--L" && i + 1 < argc) {
            L = std::stoul(argv[++i]);
        } else if (arg == "--alpha" && i + 1 < argc) {
            alpha = std::stof(argv[++i]);
        } else if (arg == "--gamma" && i + 1 < argc) {
            gamma = std::stof(argv[++i]);
        } else if (arg == "--K" && i + 1 < argc) {
            K = std::stoul(argv[++i]);
        } else if (arg == "--output" && i + 1 < argc) {
            output_path = argv[++i];
        }
    }

    std::vector<ExperimentResult> all_results;

    if (experiment == "beam_width") {
        std::vector<uint32_t> L_values = {50, 75, 100, 125, 150, 200};
        auto results = ExperimentRunner::experiment_beam_width(
            data_path, query_path, gt_path, R, alpha, gamma, L_values, K);
        all_results.insert(all_results.end(), results.begin(), results.end());

    } else if (experiment == "medoid_start") {
        auto results = ExperimentRunner::experiment_medoid_start(
            data_path, query_path, gt_path, R, L, alpha, gamma, K);
        all_results.insert(all_results.end(), results.begin(), results.end());

    } else if (experiment == "multipass_build") {
        auto results = ExperimentRunner::experiment_multipass_build(
            data_path, query_path, gt_path, R, L, alpha, gamma, K);
        all_results.insert(all_results.end(), results.begin(), results.end());

    } else if (experiment == "degree_analysis") {
        std::vector<float> alpha_values = {1.0f, 1.2f, 1.5f};
        auto results = ExperimentRunner::experiment_degree_analysis(
            data_path, query_path, gt_path, R, L, gamma, alpha_values, K);
        all_results.insert(all_results.end(), results.begin(), results.end());

    } else if (experiment == "search_optimization") {
        auto results = ExperimentRunner::experiment_search_optimization(
            data_path, query_path, gt_path, R, L, alpha, gamma, K);
        all_results.insert(all_results.end(), results.begin(), results.end());

    } else if (experiment == "all") {
        std::cout << "Running all experiments...\n" << std::endl;

        std::vector<uint32_t> L_values = {50, 75, 100, 125, 150, 200};
        auto r1 = ExperimentRunner::experiment_beam_width(
            data_path, query_path, gt_path, R, alpha, gamma, L_values, K);
        all_results.insert(all_results.end(), r1.begin(), r1.end());

        auto r2 = ExperimentRunner::experiment_medoid_start(
            data_path, query_path, gt_path, R, L, alpha, gamma, K);
        all_results.insert(all_results.end(), r2.begin(), r2.end());

        auto r3 = ExperimentRunner::experiment_multipass_build(
            data_path, query_path, gt_path, R, L, alpha, gamma, K);
        all_results.insert(all_results.end(), r3.begin(), r3.end());

        std::vector<float> alpha_values = {1.0f, 1.2f, 1.5f};
        auto r4 = ExperimentRunner::experiment_degree_analysis(
            data_path, query_path, gt_path, R, L, gamma, alpha_values, K);
        all_results.insert(all_results.end(), r4.begin(), r4.end());

        auto r5 = ExperimentRunner::experiment_search_optimization(
            data_path, query_path, gt_path, R, L, alpha, gamma, K);
        all_results.insert(all_results.end(), r5.begin(), r5.end());

    } else {
        std::cerr << "Unknown experiment: " << experiment << std::endl;
        print_usage();
        return 1;
    }

    ExperimentRunner::print_results_csv(all_results, output_path);
    return 0;
}
