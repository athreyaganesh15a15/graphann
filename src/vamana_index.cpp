#include "vamana_index.h"
#include "distance.h"
#include "io_utils.h"
#include "timer.h"
#include "quantizer.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <set>
#include <stdexcept>
#include <cstdlib>

// ============================================================================
// Destructor
// ============================================================================

VamanaIndex::~VamanaIndex() {
    if (owns_data_ && data_) {
        std::free(data_);
        data_ = nullptr;
    }
}

// ============================================================================
// Medoid Computation
// ============================================================================
// Finds the dataset point closest to the centroid (approximate medoid).
// This gives a geometrically central start node for search, reducing
// average traversal depth and P99 latency.

uint32_t VamanaIndex::compute_medoid(const float* data, uint32_t npts, uint32_t dim) {
    // Compute centroid
    std::vector<float> centroid(dim, 0.0f);
    for (uint32_t i = 0; i < npts; i++) {
        const float* row = data + (size_t)i * dim;
        for (uint32_t d = 0; d < dim; d++) {
            centroid[d] += row[d];
        }
    }
    for (uint32_t d = 0; d < dim; d++) {
        centroid[d] /= npts;
    }

    // Find point closest to centroid
    uint32_t medoid = 0;
    float min_dist = std::numeric_limits<float>::max();
    for (uint32_t i = 0; i < npts; i++) {
        float d = compute_l2sq(data + (size_t)i * dim, centroid.data(), dim);
        if (d < min_dist) {
            min_dist = d;
            medoid = i;
        }
    }
    return medoid;
}

// ============================================================================
// Greedy Search (Optimizations D, B, C + Full V + Early Termination)
// ============================================================================
// Beam search starting from start_node_. Maintains a sorted vector of at most
// L candidates (Optimization D: flat vector replacing std::set). Supports:
//   - Asymmetric quantized distance (Optimization A, via use_quantized)
//   - Early-abandoning distance checks (Optimization B)
//   - Dynamic beam width adaptation (Optimization C, via dynamic_beam)
//   - Full visited set collection (for build-time RobustPrune)
//   - Neighbor copy elimination (read-only during search)
//   - Early termination (3x frontier distance heuristic)

VamanaIndex::GreedySearchResult
VamanaIndex::greedy_search(const float* query, uint32_t L,
                           bool use_quantized, bool dynamic_beam) const {
    GreedySearchResult result;
    result.dist_cmps = 0;

    // --- Optimization D: Flat vectors ---
    std::vector<Candidate> candidates;
    candidates.reserve(L + 1);

    std::vector<bool> visited(npts_, false);
    std::vector<bool> expanded_flags(npts_, false);

    // Collect ALL visited nodes with distances (for build-time full-V to prune)
    std::vector<Candidate> all_visited;
    all_visited.reserve(L * 4);  // heuristic initial capacity

    // --- Seed with start node ---
    float start_dist = compute_l2sq(query, get_vector(start_node_), dim_);
    result.dist_cmps++;
    candidates.push_back({start_dist, start_node_});
    visited[start_node_] = true;
    all_visited.push_back({start_dist, start_node_});

    // --- Dynamic beam state (Optimization C) ---
    uint32_t active_L = L;
    int stall_count = 0;
    float prev_best = FLT_MAX;
    const float SHRINK_FLOOR = 0.5f;
    const float EXPAND_MULT  = 2.0f;
    const int   MAX_STALL_HOPS = 10;
    // Minimum beam size: must hold at least 1 candidate
    const uint32_t MIN_BEAM = 1;

    // --- Main loop ---
    while (true) {
        // Find closest unexpanded candidate.
        // MUST scan from position 0 every time: new candidates inserted at
        // the front (closer to query) need to be expanded even if we've
        // already scanned past that position in a previous iteration.
        uint32_t best_node = UINT32_MAX;
        float best_node_dist = FLT_MAX;

        for (uint32_t i = 0; i < candidates.size(); i++) {
            if (!expanded_flags[candidates[i].second]) {
                best_node = candidates[i].second;
                best_node_dist = candidates[i].first;
                break;
            }
        }
        if (best_node == UINT32_MAX)
            break;  // all candidates expanded

        // NOTE: No early termination heuristic. Standard Vamana convergence
        // is guaranteed by exhausting all unexpanded candidates within the
        // beam of size L. The 3x-distance heuristic from best.md was tuned
        // for R=64 graphs and hurts recall on sparser graphs (R=32).

        expanded_flags[best_node] = true;

        // --- Expand: evaluate all neighbors of best_node ---
        // During build: lock and copy for thread safety.
        // During search: direct reference (no concurrent writes).
        const std::vector<uint32_t>* neighbors_ptr;
        std::vector<uint32_t> neighbors_copy;

        if (build_mode_) {
            std::lock_guard<std::mutex> lock(locks_[best_node]);
            neighbors_copy = graph_[best_node];
            neighbors_ptr = &neighbors_copy;
        } else {
            neighbors_ptr = &graph_[best_node];
        }
        const auto& neighbors = *neighbors_ptr;

        // Current threshold for early-abandoning (Optimization B)
        // During build: no EA — we need exact distances for all_visited / RobustPrune
        float threshold = FLT_MAX;
        if (!build_mode_ && candidates.size() >= active_L) {
            threshold = candidates.back().first;
        }

        for (uint32_t nbr : neighbors) {
            if (visited[nbr])
                continue;
            visited[nbr] = true;

            // --- Compute distance (Optimization A + B) ---
            // During build: always use exact distance (no EA) to ensure
            // complete visited set for RobustPrune.
            // During search: use EA for speed (Optimization B).
            float d;
            if (build_mode_) {
                // Build mode: exact distance, no early abandoning
                d = compute_l2sq(query, get_vector(nbr), dim_);
            } else if (use_quantized && quantized_) {
                d = compute_l2sq_asymmetric_ea(
                    query,
                    quantizer_.get_row(quant_data_.data(), nbr),
                    quantizer_.min_.data(),
                    quantizer_.scale_.data(),
                    dim_, threshold);
            } else {
                d = compute_l2sq_ea(query, get_vector(nbr), dim_, threshold);
            }
            result.dist_cmps++;

            // Record in full visited set (for build-time prune)
            all_visited.push_back({d, nbr});

            // --- Insert into sorted candidate vector (Optimization D) ---
            if (d < FLT_MAX) {
                if (candidates.size() < active_L) {
                    // Binary search for insert position
                    auto pos = std::lower_bound(candidates.begin(), candidates.end(),
                                                Candidate{d, nbr});
                    candidates.insert(pos, {d, nbr});
                } else if (d < candidates.back().first) {
                    // Replace worst candidate
                    candidates.pop_back();
                    auto pos = std::lower_bound(candidates.begin(), candidates.end(),
                                                Candidate{d, nbr});
                    candidates.insert(pos, {d, nbr});
                }
                // Update threshold for next distance computation
                if (!build_mode_ && candidates.size() >= active_L) {
                    threshold = candidates.back().first;
                }
            }
        }

        // --- Dynamic beam width adaptation (Optimization C) ---
        if (dynamic_beam && !candidates.empty()) {
            float cur_best = candidates.front().first;
            if (cur_best < prev_best) {
                // Making progress — shrink beam toward floor
                active_L = std::max(static_cast<uint32_t>(L * SHRINK_FLOOR), MIN_BEAM);
                stall_count = 0;
            } else {
                stall_count++;
                if (stall_count >= MAX_STALL_HOPS) {
                    // Stalled — expand beam back
                    active_L = std::min(static_cast<uint32_t>(active_L * EXPAND_MULT), L);
                    stall_count = 0;
                }
            }
            prev_best = cur_best;

            // Trim candidates if active_L shrank (keep top active_L)
            if (candidates.size() > active_L) {
                candidates.resize(active_L);
            }
        }
    }

    result.candidates = std::move(candidates);
    result.visited = std::move(all_visited);
    return result;
}

// ============================================================================
// Robust Prune (Alpha-RNG Rule)
// ============================================================================
// Given a node and a set of candidates, greedily select neighbors that are
// "diverse" — a candidate c is added only if it's not too close to any
// already-selected neighbor (within a factor of alpha).
//
// Formally: add c if for ALL already-chosen neighbors n:
//     dist(node, c) <= alpha * dist(c, n)
//
// This ensures good graph navigability by keeping some long-range edges
// (alpha > 1 makes it easier for a candidate to survive pruning).

void VamanaIndex::robust_prune(uint32_t node, std::vector<Candidate>& candidates,
                               float alpha, uint32_t R) {
    // Remove self from candidates if present
    candidates.erase(
        std::remove_if(candidates.begin(), candidates.end(),
                       [node](const Candidate& c) { return c.second == node; }),
        candidates.end());

    // Sort by distance to node (ascending)
    std::sort(candidates.begin(), candidates.end());

    std::vector<uint32_t> new_neighbors;
    new_neighbors.reserve(R);

    for (const auto& [dist_to_node, cand_id] : candidates) {
        if (new_neighbors.size() >= R)
            break;

        // Check alpha-RNG condition against all already-selected neighbors
        bool keep = true;
        for (uint32_t selected : new_neighbors) {
            float dist_cand_to_selected =
                compute_l2sq(get_vector(cand_id), get_vector(selected), dim_);
            if (dist_to_node > alpha * dist_cand_to_selected) {
                keep = false;
                break;
            }
        }

        if (keep)
            new_neighbors.push_back(cand_id);
    }

    graph_[node] = std::move(new_neighbors);
}

// ============================================================================
// Build
// ============================================================================
// Implements all structural optimizations from best.md:
//   - Medoid initialization (replaces random start node)
//   - Random R-regular graph pre-initialization (bootstraps connectivity)
//   - Full visited set V passed to RobustPrune (richer candidate pool)

void VamanaIndex::build(const std::string& data_path, uint32_t R, uint32_t L,
                        float alpha, float gamma) {
    // --- Load data ---
    std::cout << "Loading data from " << data_path << "..." << std::endl;
    FloatMatrix mat = load_fbin(data_path);
    npts_ = mat.npts;
    dim_  = mat.dims;
    data_ = mat.data.release();
    owns_data_ = true;

    std::cout << "  Points: " << npts_ << ", Dimensions: " << dim_ << std::endl;

    if (L < R) {
        std::cerr << "Warning: L (" << L << ") < R (" << R
                  << "). Setting L = R." << std::endl;
        L = R;
    }

    // --- Initialize graph and per-node locks ---
    graph_.resize(npts_);
    locks_ = std::vector<std::mutex>(npts_);

    // --- Compute medoid start node (best.md) ---
    start_node_ = compute_medoid(data_, npts_, dim_);
    std::cout << "  Medoid start node: " << start_node_ << std::endl;

    // --- Random R-regular graph pre-initialization (best.md) ---
    // Seed each node with R random neighbors to bootstrap connectivity.
    std::mt19937 rng(42);  // fixed seed for reproducibility
    {
        std::uniform_int_distribution<uint32_t> dist(0, npts_ - 1);
        for (uint32_t i = 0; i < npts_; i++) {
            std::set<uint32_t> random_nbrs;
            while (random_nbrs.size() < R) {
                uint32_t r = dist(rng);
                if (r != i) random_nbrs.insert(r);
            }
            graph_[i].assign(random_nbrs.begin(), random_nbrs.end());
        }
        std::cout << "  Random R-regular graph initialized (R=" << R << ")" << std::endl;
    }

    // --- Create random insertion order ---
    std::vector<uint32_t> perm(npts_);
    std::iota(perm.begin(), perm.end(), 0);
    std::shuffle(perm.begin(), perm.end(), rng);

    // --- Build graph: parallel insertion with per-node locking ---
    uint32_t gamma_R = static_cast<uint32_t>(gamma * R);
    std::cout << "Building index (R=" << R << ", L=" << L
              << ", alpha=" << alpha << ", gamma=" << gamma
              << ", gammaR=" << gamma_R << ")..." << std::endl;

    build_mode_ = true;
    Timer build_timer;

    #pragma omp parallel for schedule(dynamic, 64)
    for (size_t idx = 0; idx < npts_; idx++) {
        uint32_t point = perm[idx];

        // Step 1: Search for this point in the current graph to find candidates
        auto gs_result = greedy_search(get_vector(point), L);

        // Step 2: Prune using FULL visited set (best.md: full V to RobustPrune)
        // Lock required: another thread may be adding backward edges to graph_[point]
        // concurrently, and robust_prune writes graph_[point] via assignment.
        {
            std::lock_guard<std::mutex> lock(locks_[point]);
            robust_prune(point, gs_result.visited, alpha, R);
        }

        // Step 3: Copy neighbor list under lock — another thread may push_back
        // to graph_[point] at any time (backward edge from its own insertion).
        std::vector<uint32_t> my_neighbors;
        {
            std::lock_guard<std::mutex> lock(locks_[point]);
            my_neighbors = graph_[point];
        }

        // Step 4: Add backward edges from each new neighbor back to this point
        for (uint32_t nbr : my_neighbors) {
            std::lock_guard<std::mutex> lock(locks_[nbr]);

            // Add backward edge
            graph_[nbr].push_back(point);

            // Step 5: If neighbor's degree exceeds gamma*R, prune its neighborhood
            if (graph_[nbr].size() > gamma_R) {
                std::vector<Candidate> nbr_candidates;
                nbr_candidates.reserve(graph_[nbr].size());
                for (uint32_t nn : graph_[nbr]) {
                    float d = compute_l2sq(get_vector(nbr), get_vector(nn), dim_);
                    nbr_candidates.push_back({d, nn});
                }
                robust_prune(nbr, nbr_candidates, alpha, R);
            }
        }

        // Progress reporting (from one thread only)
        if (idx % 10000 == 0) {
            #pragma omp critical
            {
                std::cout << "\r  Inserted " << idx << " / " << npts_
                          << " points" << std::flush;
            }
        }
    }

    build_mode_ = false;
    double build_time = build_timer.elapsed_seconds();

    // Compute average degree
    size_t total_edges = 0;
    for (uint32_t i = 0; i < npts_; i++)
        total_edges += graph_[i].size();
    double avg_degree = (double)total_edges / npts_;

    std::cout << "\n  Build complete in " << build_time << " seconds."
              << std::endl;
    std::cout << "  Average out-degree: " << avg_degree << std::endl;
}

// ============================================================================
// Search
// ============================================================================

SearchResult VamanaIndex::search(const float* query, uint32_t K, uint32_t L) const {
    if (L < K) L = K;

    Timer t;
    auto gs_result = greedy_search(query, L);
    double latency = t.elapsed_us();

    // Return top-K results
    SearchResult result;
    result.dist_cmps = gs_result.dist_cmps;
    result.latency_us = latency;
    result.ids.reserve(K);
    for (uint32_t i = 0; i < K && i < gs_result.candidates.size(); i++) {
        result.ids.push_back(gs_result.candidates[i].second);
    }
    return result;
}

// ============================================================================
// Quantized Search with Re-Ranking (Optimization A)
// ============================================================================
// Phase 1: Graph traversal using asymmetric quantized distances.
// Phase 2: Re-rank all candidates with exact float32 distances.
// This preserves recall while cutting traversal memory bandwidth ~4x.

SearchResult VamanaIndex::search_quantized(const float* query, uint32_t K,
                                           uint32_t L, bool dynamic_beam) const {
    if (L < K) L = K;
    if (!quantized_) {
        throw std::runtime_error("Quantization not built. Call build_quantization() first.");
    }

    Timer t;
    auto gs_result = greedy_search(query, L, /*use_quantized=*/true, dynamic_beam);

    // Phase 2: Float32 re-ranking of ALL candidates
    for (auto& [dist, id] : gs_result.candidates) {
        dist = compute_l2sq(query, get_vector(id), dim_);
    }
    std::sort(gs_result.candidates.begin(), gs_result.candidates.end());

    double latency = t.elapsed_us();

    SearchResult result;
    result.dist_cmps = gs_result.dist_cmps;
    result.latency_us = latency;
    result.ids.reserve(K);
    for (uint32_t i = 0; i < K && i < gs_result.candidates.size(); i++) {
        result.ids.push_back(gs_result.candidates[i].second);
    }
    return result;
}

// ============================================================================
// Build Quantization
// ============================================================================

void VamanaIndex::build_quantization() {
    if (!data_) {
        throw std::runtime_error("No data loaded for quantization");
    }

    std::cout << "Building quantization tables..." << std::endl;
    Timer qt;
    quantizer_.train(data_, npts_, dim_);
    quant_data_ = quantizer_.quantize(data_, npts_);
    quantized_ = true;

    size_t quant_mb = quant_data_.size() / (1024 * 1024);
    size_t float_mb = (size_t)npts_ * dim_ * sizeof(float) / (1024 * 1024);
    std::cout << "  Quantization complete in " << qt.elapsed_seconds() << "s: "
              << quant_mb << " MB (from " << float_mb << " MB float32)"
              << std::endl;
}

// ============================================================================
// Save / Load
// ============================================================================
// Binary format:
//   [uint32] npts
//   [uint32] dim
//   [uint32] start_node
//   For each node i in [0, npts):
//     [uint32] degree
//     [uint32 * degree] neighbor IDs

void VamanaIndex::save(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open())
        throw std::runtime_error("Cannot open file for writing: " + path);

    out.write(reinterpret_cast<const char*>(&npts_), 4);
    out.write(reinterpret_cast<const char*>(&dim_), 4);
    out.write(reinterpret_cast<const char*>(&start_node_), 4);

    for (uint32_t i = 0; i < npts_; i++) {
        uint32_t deg = graph_[i].size();
        out.write(reinterpret_cast<const char*>(&deg), 4);
        if (deg > 0) {
            out.write(reinterpret_cast<const char*>(graph_[i].data()),
                      deg * sizeof(uint32_t));
        }
    }

    std::cout << "Index saved to " << path << std::endl;
}

void VamanaIndex::load(const std::string& index_path,
                       const std::string& data_path) {
    // Load data vectors
    FloatMatrix mat = load_fbin(data_path);
    npts_ = mat.npts;
    dim_  = mat.dims;
    data_ = mat.data.release();
    owns_data_ = true;

    // Load graph
    std::ifstream in(index_path, std::ios::binary);
    if (!in.is_open())
        throw std::runtime_error("Cannot open index file: " + index_path);

    uint32_t file_npts, file_dim;
    in.read(reinterpret_cast<char*>(&file_npts), 4);
    in.read(reinterpret_cast<char*>(&file_dim), 4);
    in.read(reinterpret_cast<char*>(&start_node_), 4);

    if (file_npts != npts_ || file_dim != dim_)
        throw std::runtime_error(
            "Index/data mismatch: index has " + std::to_string(file_npts) +
            "x" + std::to_string(file_dim) + ", data has " +
            std::to_string(npts_) + "x" + std::to_string(dim_));

    graph_.resize(npts_);
    locks_ = std::vector<std::mutex>(npts_);

    for (uint32_t i = 0; i < npts_; i++) {
        uint32_t deg;
        in.read(reinterpret_cast<char*>(&deg), 4);
        graph_[i].resize(deg);
        if (deg > 0) {
            in.read(reinterpret_cast<char*>(graph_[i].data()),
                    deg * sizeof(uint32_t));
        }
    }

    std::cout << "Index loaded: " << npts_ << " points, " << dim_
              << " dims, start=" << start_node_ << std::endl;
}

// ============================================================================
// Build with Custom Start Node (for experiments)
// ============================================================================

void VamanaIndex::build_with_start_node(const std::string& data_path, uint32_t R, uint32_t L,
                                        float alpha, float gamma, uint32_t start_node) {
    // Load data
    std::cout << "Loading data from " << data_path << "..." << std::endl;
    FloatMatrix mat = load_fbin(data_path);
    npts_ = mat.npts;
    dim_  = mat.dims;
    data_ = mat.data.release();
    owns_data_ = true;

    std::cout << "  Points: " << npts_ << ", Dimensions: " << dim_ << std::endl;

    if (L < R) {
        std::cerr << "Warning: L (" << L << ") < R (" << R << "). Setting L = R." << std::endl;
        L = R;
    }

    // Initialize graph and locks
    graph_.resize(npts_);
    locks_ = std::vector<std::mutex>(npts_);

    // Use provided start node
    start_node_ = start_node;
    std::cout << "  Start node: " << start_node_ << std::endl;

    // Random R-regular graph pre-initialization
    std::mt19937 rng(42);
    {
        std::uniform_int_distribution<uint32_t> dist(0, npts_ - 1);
        for (uint32_t i = 0; i < npts_; i++) {
            std::set<uint32_t> random_nbrs;
            while (random_nbrs.size() < R) {
                uint32_t r = dist(rng);
                if (r != i) random_nbrs.insert(r);
            }
            graph_[i].assign(random_nbrs.begin(), random_nbrs.end());
        }
    }

    // Create random insertion order
    std::vector<uint32_t> perm(npts_);
    std::iota(perm.begin(), perm.end(), 0);
    std::shuffle(perm.begin(), perm.end(), rng);

    // Build graph
    uint32_t gamma_R = static_cast<uint32_t>(gamma * R);
    std::cout << "Building index (R=" << R << ", L=" << L
              << ", alpha=" << alpha << ", gamma=" << gamma
              << ", gammaR=" << gamma_R << ")..." << std::endl;

    build_mode_ = true;
    Timer build_timer;

    #pragma omp parallel for schedule(dynamic, 64)
    for (size_t idx = 0; idx < npts_; idx++) {
        uint32_t point = perm[idx];

        auto gs_result = greedy_search(get_vector(point), L);
        // Full visited set to RobustPrune (locked for thread safety)
        {
            std::lock_guard<std::mutex> lock(locks_[point]);
            robust_prune(point, gs_result.visited, alpha, R);
        }

        std::vector<uint32_t> my_neighbors;
        {
            std::lock_guard<std::mutex> lock(locks_[point]);
            my_neighbors = graph_[point];
        }

        for (uint32_t nbr : my_neighbors) {
            std::lock_guard<std::mutex> lock(locks_[nbr]);
            graph_[nbr].push_back(point);

            if (graph_[nbr].size() > gamma_R) {
                std::vector<Candidate> nbr_candidates;
                nbr_candidates.reserve(graph_[nbr].size());
                for (uint32_t nn : graph_[nbr]) {
                    float d = compute_l2sq(get_vector(nbr), get_vector(nn), dim_);
                    nbr_candidates.push_back({d, nn});
                }
                robust_prune(nbr, nbr_candidates, alpha, R);
            }
        }

        if (idx % 10000 == 0) {
            #pragma omp critical
            {
                std::cout << "\r  Inserted " << idx << " / " << npts_
                          << " points" << std::flush;
            }
        }
    }

    build_mode_ = false;
    double build_time = build_timer.elapsed_seconds();

    size_t total_edges = 0;
    for (uint32_t i = 0; i < npts_; i++)
        total_edges += graph_[i].size();
    double avg_degree = (double)total_edges / npts_;

    std::cout << "\n  Build complete in " << build_time << " seconds." << std::endl;
    std::cout << "  Average out-degree: " << avg_degree << std::endl;
}

// ============================================================================
// Second Pass Build (graph refinement)
// ============================================================================

void VamanaIndex::build_second_pass(uint32_t R, uint32_t L, float alpha, float gamma) {
    if (!data_) {
        throw std::runtime_error("No data loaded for second pass");
    }

    std::cout << "Running second pass build..." << std::endl;

    uint32_t gamma_R = static_cast<uint32_t>(gamma * R);

    build_mode_ = true;
    Timer build_timer;

    #pragma omp parallel for schedule(dynamic, 64)
    for (size_t idx = 0; idx < npts_; idx++) {
        uint32_t point = idx;

        auto gs_result = greedy_search(get_vector(point), L);
        // Full visited set to RobustPrune (locked for thread safety)
        {
            std::lock_guard<std::mutex> lock(locks_[point]);
            robust_prune(point, gs_result.visited, alpha, R);
        }

        std::vector<uint32_t> my_neighbors;
        {
            std::lock_guard<std::mutex> lock(locks_[point]);
            my_neighbors = graph_[point];
        }

        for (uint32_t nbr : my_neighbors) {
            std::lock_guard<std::mutex> lock(locks_[nbr]);
            
            // Check if edge already exists
            bool exists = false;
            for (uint32_t existing : graph_[nbr]) {
                if (existing == point) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                graph_[nbr].push_back(point);
            }

            if (graph_[nbr].size() > gamma_R) {
                std::vector<Candidate> nbr_candidates;
                nbr_candidates.reserve(graph_[nbr].size());
                for (uint32_t nn : graph_[nbr]) {
                    float d = compute_l2sq(get_vector(nbr), get_vector(nn), dim_);
                    nbr_candidates.push_back({d, nn});
                }
                robust_prune(nbr, nbr_candidates, alpha, R);
            }
        }

        if (idx % 10000 == 0) {
            #pragma omp critical
            {
                std::cout << "\r  Processed " << idx << " / " << npts_
                          << " points" << std::flush;
            }
        }
    }

    build_mode_ = false;
    double build_time = build_timer.elapsed_seconds();
    std::cout << "\n  Second pass complete in " << build_time << " seconds." << std::endl;
}
