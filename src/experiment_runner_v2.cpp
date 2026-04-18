#include "experiments.h"
#include "vamana_index.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>

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
    std::cout << "\n✓ Results saved to " << filename << "\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: experiment_runner_v2 <experiment> [options]\n\n"
                  << "Experiments:\n"
                  << "  beam_width      - Test different L values\n"
                  << "  medoid_start    - Random vs medoid start\n"
                  << "  multipass_build - Single vs two-pass\n"
                  << "  degree_analysis - Test different alpha values\n"
                  << "  search_opt      - Normal vs scratch buffer search\n\n"
                  << "Options:\n"
                  << "  --data <path>   Data file (fbin)\n"
                  << "  --queries <path> Query file (fbin)\n"
                  << "  --gt <path>     Ground truth (ibin)\n"
                  << "  --output <path> Output CSV file\n"
                  << "  --R <int>       Max degree (default: 32)\n"
                  << "  --L <int>       Search width (default: 75)\n"
                  << "  --alpha <float> Alpha parameter (default: 1.2)\n"
                  << "  --gamma <float> Gamma parameter (default: 1.5)\n"
                  << "  --K <int>       Num neighbors (default: 10)\n";
        return 1;
    }

    std::string experiment = argv[1];
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

    try {
        if (experiment == "beam_width") {
            std::cout << "Running beam_width experiment...\n";
            std::vector<uint32_t> L_values = {50, 75, 100};
            auto results = ExperimentRunner::experiment_beam_width(
                data_path, query_path, gt_path, R, alpha, gamma, L_values, K);
            all_results.insert(all_results.end(), results.begin(), results.end());

        } else if (experiment == "medoid_start") {
            std::cout << "Running medoid_start experiment...\n";
            auto results = ExperimentRunner::experiment_medoid_start(
                data_path, query_path, gt_path, R, L, alpha, gamma, K);
            all_results.insert(all_results.end(), results.begin(), results.end());

        } else if (experiment == "multipass_build") {
            std::cout << "Running multipass_build experiment...\n";
            auto results = ExperimentRunner::experiment_multipass_build(
                data_path, query_path, gt_path, R, L, alpha, gamma, K);
            all_results.insert(all_results.end(), results.begin(), results.end());

        } else if (experiment == "degree_analysis") {
            std::cout << "Running degree_analysis experiment...\n";
            std::vector<float> alpha_values = {1.0f, 1.2f};
            auto results = ExperimentRunner::experiment_degree_analysis(
                data_path, query_path, gt_path, R, L, gamma, alpha_values, K);
            all_results.insert(all_results.end(), results.begin(), results.end());

        } else if (experiment == "search_opt") {
            std::cout << "Running search_opt experiment...\n";
            auto results = ExperimentRunner::experiment_search_optimization(
                data_path, query_path, gt_path, R, L, alpha, gamma, K);
            all_results.insert(all_results.end(), results.begin(), results.end());

        } else {
            std::cerr << "Unknown experiment: " << experiment << "\n";
            return 1;
        }

        save_results(all_results, output_path);
        std::cout << "✓ Experiment completed successfully!\n";

    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
