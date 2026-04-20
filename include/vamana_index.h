#pragma once

#include <cstdint>
#include <vector>
#include <mutex>
#include <string>

#include "quantizer.h"

// Result of a single query search.
struct SearchResult {
    std::vector<uint32_t> ids;  // nearest neighbor IDs (sorted by distance)
    uint32_t dist_cmps;         // number of distance computations
    double latency_us;          // search latency in microseconds
};

// Vamana graph-based approximate nearest neighbor index.
//
// Key concepts:
//   - The graph is built incrementally: each point is inserted by searching
//     the current graph, pruning candidates with the alpha-RNG rule, and
//     adding forward + backward edges.
//   - Greedy search starts from a fixed start node and follows edges to
//     find nearest neighbors, maintaining a candidate list of size L.
//   - The alpha parameter controls edge diversity (alpha > 1 favors long-range
//     edges for better navigability).
//   - R is the max out-degree; gamma*R is the threshold that triggers pruning
//     on neighbor nodes when backward edges are added.
class VamanaIndex {
  public:
    VamanaIndex() = default;
    ~VamanaIndex();

    // ---- Build ----
    // Loads data from an fbin file and builds the Vamana graph.
    //   R:     max out-degree per node
    //   L:     search list size during construction (L >= R)
    //   alpha: RNG pruning parameter (typically 1.0 - 1.5)
    //   gamma: max-degree multiplier for triggering neighbor pruning (e.g. 1.5)
    void build(const std::string& data_path, uint32_t R, uint32_t L,
               float alpha, float gamma);

    // ---- Search ----
    // Search for K nearest neighbors of a query vector (exact float32).
    //   query: pointer to query vector (must have dim_ floats)
    //   K:     number of nearest neighbors to return
    //   L:     search list size (L >= K)
    SearchResult search(const float* query, uint32_t K, uint32_t L) const;

    // Quantized search with float32 re-ranking (Optimization A).
    // Uses asymmetric distance (float32 query vs uint8 data) for graph
    // traversal, then re-ranks all candidates with exact float32 distance.
    SearchResult search_quantized(const float* query, uint32_t K, uint32_t L,
                                  bool dynamic_beam = false) const;

    // ---- Quantization ----
    // Build quantization tables from the loaded dataset.
    // Call after build() or load(). Required before search_quantized().
    void build_quantization();

    // ---- Persistence ----
    // Save index (graph + metadata) to a binary file.
    void save(const std::string& path) const;

    // Load index from a binary file. Data file must also be loaded separately.
    void load(const std::string& index_path, const std::string& data_path);

    // ---- Experiment Support ----
    // Build with custom start node
    void build_with_start_node(const std::string& data_path, uint32_t R, uint32_t L,
                               float alpha, float gamma, uint32_t start_node);

    // Build a second pass (graph refinement)
    void build_second_pass(uint32_t R, uint32_t L, float alpha, float gamma);

    // Get graph for direct access (for experiments)
    const std::vector<std::vector<uint32_t>>& get_graph() const { return graph_; }

    uint32_t get_npts() const { return npts_; }
    uint32_t get_dim()  const { return dim_; }

  private:
    // A candidate = (distance, node_id). Ordered by distance.
    using Candidate = std::pair<float, uint32_t>;

    // Result from greedy_search including full visited set for build.
    struct GreedySearchResult {
        std::vector<Candidate> candidates;   // top-L sorted by distance
        std::vector<Candidate> visited;      // ALL visited nodes with distances
        uint32_t dist_cmps;
    };

    // ---- Core algorithms ----

    // Greedy search starting from start_node_.
    // Returns top-L candidates, full visited set, and distance computation count.
    //   use_quantized: use asymmetric quantized distance for traversal
    //   dynamic_beam:  enable dynamic beam width adaptation (Optimization C)
    GreedySearchResult greedy_search(const float* query, uint32_t L,
                                     bool use_quantized = false,
                                     bool dynamic_beam = false) const;

    // Alpha-RNG pruning: selects a diverse subset of candidates as neighbors.
    // Modifies graph_[node] in place. Candidates should NOT include node itself.
    void robust_prune(uint32_t node, std::vector<Candidate>& candidates,
                      float alpha, uint32_t R);

    // Compute medoid (closest point to centroid).
    static uint32_t compute_medoid(const float* data, uint32_t npts, uint32_t dim);

    // ---- Data ----
    float*   data_    = nullptr;  // contiguous row-major [npts x dim], aligned
    uint32_t npts_    = 0;
    uint32_t dim_     = 0;
    bool     owns_data_ = false;  // whether we allocated data_

    // ---- Graph ----
    std::vector<std::vector<uint32_t>> graph_;  // adjacency lists
    uint32_t start_node_ = 0;

    // ---- Quantization (Optimization A) ----
    ScalarQuantizer quantizer_;
    std::vector<uint8_t> quant_data_;  // quantized dataset [npts × dim]
    bool quantized_ = false;           // whether quantization is available

    // ---- Build mode flag ----
    // When true, greedy_search locks and copies neighbor lists for thread safety.
    // When false (during search), neighbor lists are read directly (no copies).
    bool build_mode_ = false;

    // ---- Concurrency ----
    // Per-node locks for parallel build (mutable so search can be const).
    mutable std::vector<std::mutex> locks_;

    // ---- Helpers ----
    const float* get_vector(uint32_t id) const {
        return data_ + (size_t)id * dim_;
    }
};
