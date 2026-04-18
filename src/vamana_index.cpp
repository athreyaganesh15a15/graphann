#include "vamana_index.h"
#include "distance.h"
#include "io_utils.h"
#include "timer.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <stdexcept>
#include <unordered_set>
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
// Compute Medoid
// ============================================================================
// Returns the point closest to the dataset centroid (mean vector).
// This is a much better start node than a random pick — it minimizes
// the expected number of hops to reach any region of the space.

uint32_t VamanaIndex::compute_medoid() const {
    // Step 1: compute centroid (mean of all vectors)
    std::vector<double> centroid(dim_, 0.0);
    for (uint32_t i = 0; i < npts_; i++) {
        const float* vec = get_vector(i);
        for (uint32_t d = 0; d < dim_; d++) {
            centroid[d] += vec[d];
        }
    }
    for (uint32_t d = 0; d < dim_; d++) {
        centroid[d] /= npts_;
    }

    // Convert centroid to float for distance computation
    std::vector<float> centroid_f(dim_);
    for (uint32_t d = 0; d < dim_; d++) {
        centroid_f[d] = static_cast<float>(centroid[d]);
    }

    // Step 2: find point closest to centroid
    uint32_t best_id = 0;
    float best_dist = compute_l2sq(centroid_f.data(), get_vector(0), dim_);

    #pragma omp parallel
    {
        uint32_t local_best_id = 0;
        float local_best_dist = best_dist;

        #pragma omp for nowait
        for (uint32_t i = 1; i < npts_; i++) {
            float d = compute_l2sq(centroid_f.data(), get_vector(i), dim_);
            if (d < local_best_dist) {
                local_best_dist = d;
                local_best_id = i;
            }
        }

        #pragma omp critical
        {
            if (local_best_dist < best_dist) {
                best_dist = local_best_dist;
                best_id = local_best_id;
            }
        }
    }

    return best_id;
}

// ============================================================================
// Greedy Search (Optimized)
// ============================================================================
// Beam search starting from start_node_. Maintains a sorted candidate
// vector of at most L nodes, always expanding the closest unvisited node.
// Returns when no unvisited candidates remain.
//
// Optimizations over the original std::set-based version:
//   - Uses a sorted std::vector<Candidate> instead of std::set for cache
//     locality and fewer allocations
//   - Tracks frontier position with a simple index instead of a separate
//     std::set<uint32_t> for expanded nodes
//   - Uses std::unordered_set for the visited set, which only tracks
//     actually-visited nodes (~L to a few thousand) rather than allocating
//     a full npts_-sized vector<bool> per query

std::pair<std::vector<VamanaIndex::Candidate>, uint32_t>
VamanaIndex::greedy_search(const float* query, uint32_t L) const {
    // Sorted candidate list: always kept in ascending distance order.
    std::vector<Candidate> candidates;
    candidates.reserve(L + 1);

    // Track which nodes we've already computed distance for.
    std::unordered_set<uint32_t> visited;
    visited.reserve(L * 4);  // pre-allocate to reduce rehashing

    uint32_t dist_cmps = 0;

    // Seed with start node
    float start_dist = compute_l2sq(query, get_vector(start_node_), dim_);
    dist_cmps++;
    candidates.push_back({start_dist, start_node_});
    visited.insert(start_node_);

    // frontier_pos: index into candidates[]. Everything before this index
    // has been expanded (neighbors explored). We always expand the closest
    // unexpanded candidate.
    uint32_t frontier_pos = 0;

    while (frontier_pos < candidates.size()) {
        uint32_t best_node = candidates[frontier_pos].second;
        frontier_pos++;

        // Expand: evaluate all neighbors of best_node
        // Copy neighbor list under lock to avoid data race with parallel build
        std::vector<uint32_t> neighbors;
        {
            std::lock_guard<std::mutex> lock(locks_[best_node]);
            neighbors = graph_[best_node];
        }

        for (uint32_t nbr : neighbors) {
            if (visited.count(nbr))
                continue;
            visited.insert(nbr);

            float d = compute_l2sq(query, get_vector(nbr), dim_);
            dist_cmps++;

            // Skip if candidate set is full and this is worse than the worst
            if (candidates.size() >= L && d >= candidates.back().first)
                continue;

            // Insert in sorted position (binary search)
            Candidate new_cand = {d, nbr};
            auto insert_pos = std::lower_bound(candidates.begin(),
                                                candidates.end(),
                                                new_cand);
            candidates.insert(insert_pos, new_cand);

            // Trim to L if needed
            if (candidates.size() > L) {
                candidates.pop_back();
            }

            // Adjust frontier_pos if insertion was before it
            // (shouldn't re-expand already expanded nodes, but the sorted
            // insert might shift indices)
            // Note: insert_pos is at or after frontier_pos in practice for
            // good candidates, but we need to handle the edge case.
            uint32_t ins_idx = static_cast<uint32_t>(
                insert_pos - candidates.begin());
            if (ins_idx < frontier_pos) {
                // This new candidate is closer than some already-expanded ones.
                // We should expand it too — set frontier back.
                frontier_pos = ins_idx;
            }
        }
    }

    return {candidates, dist_cmps};
}

// ============================================================================
// Robust Prune (Adaptive Alpha-RNG Rule)
// ============================================================================
// Given a node and a set of candidates, greedily select neighbors that are
// "diverse" — a candidate c is added only if it's not too close to any
// already-selected neighbor (within a factor of alpha).
//
// With density-adaptive pruning enabled (density_spread_ > 0), alpha is
// modulated per-node:
//   adaptive_alpha = alpha * (1.0 - density_spread * (density[node] - 0.5))
//
// Dense nodes get a lower alpha (stricter pruning → more diverse long-range
// edges). Sparse/outlier nodes get a higher alpha (relaxed → keep nearby
// edges since they're scarce).

void VamanaIndex::robust_prune(uint32_t node, std::vector<Candidate>& candidates,
                               float alpha, uint32_t R) {
    // Remove self from candidates if present
    candidates.erase(
        std::remove_if(candidates.begin(), candidates.end(),
                       [node](const Candidate& c) { return c.second == node; }),
        candidates.end());

    // Sort by distance to node (ascending)
    std::sort(candidates.begin(), candidates.end());

    // Compute adaptive alpha for this node
    float effective_alpha = alpha;
    if (density_spread_ > 0.0f && !local_density_.empty()) {
        // density is in [0, 1], centered at 0.5
        // dense (density=1) → alpha decreases by density_spread/2
        // sparse (density=0) → alpha increases by density_spread/2
        float density = local_density_[node];
        effective_alpha = alpha * (1.0f - density_spread_ * (density - 0.5f));
        // Clamp to reasonable range [0.5, 3.0]
        effective_alpha = std::max(0.5f, std::min(3.0f, effective_alpha));
    }

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
            if (dist_to_node > effective_alpha * dist_cand_to_selected) {
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

void VamanaIndex::build(const std::string& data_path, uint32_t R, uint32_t L,
                        float alpha, float gamma, float density_spread) {
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

    // Store density spread parameter
    density_spread_ = density_spread;

    // --- Initialize empty graph and per-node locks ---
    graph_.resize(npts_);
    locks_ = std::vector<std::mutex>(npts_);

    // --- Compute medoid as start node ---
    std::cout << "  Computing medoid start node..." << std::endl;
    Timer medoid_timer;
    start_node_ = compute_medoid();
    double medoid_time = medoid_timer.elapsed_seconds();
    std::cout << "  Medoid start node: " << start_node_
              << " (computed in " << medoid_time << "s)" << std::endl;

    // --- Create random insertion order ---
    std::mt19937 rng(42);  // fixed seed for reproducibility
    std::vector<uint32_t> perm(npts_);
    std::iota(perm.begin(), perm.end(), 0);
    std::shuffle(perm.begin(), perm.end(), rng);

    // --- Build graph ---
    uint32_t gamma_R = static_cast<uint32_t>(gamma * R);

    if (density_spread > 0.0f) {
        // ================================================================
        // TWO-PASS BUILD: density-adaptive pruning
        // ================================================================
        // Pass 1: build with global alpha, collect density estimates
        // Pass 2: rebuild with adaptive per-node alpha
        // ================================================================

        std::cout << "Building index PASS 1 (R=" << R << ", L=" << L
                  << ", alpha=" << alpha << ", gamma=" << gamma
                  << ") — collecting density estimates..." << std::endl;

        // Per-node raw density: average distance to top-k candidates
        std::vector<float> raw_density(npts_, 0.0f);
        const uint32_t density_k = std::min(L, (uint32_t)10);  // use top-10 candidates

        Timer pass1_timer;

        #pragma omp parallel for schedule(dynamic, 64)
        for (size_t idx = 0; idx < npts_; idx++) {
            uint32_t point = perm[idx];

            // Search for this point in the current graph
            auto [candidates, _dc] = greedy_search(get_vector(point), L);

            // Collect density estimate: average distance to top-k candidates
            float avg_dist = 0.0f;
            uint32_t count = 0;
            for (uint32_t c = 0; c < density_k && c < candidates.size(); c++) {
                if (candidates[c].second != point) {
                    avg_dist += candidates[c].first;
                    count++;
                }
            }
            if (count > 0) avg_dist /= count;
            raw_density[point] = avg_dist;

            // Prune with global alpha (pass 1)
            robust_prune(point, candidates, alpha, R);

            // Add backward edges
            for (uint32_t nbr : graph_[point]) {
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
                    std::cout << "\r  [Pass 1] Inserted " << idx << " / " << npts_
                              << " points" << std::flush;
                }
            }
        }

        double pass1_time = pass1_timer.elapsed_seconds();
        std::cout << "\n  Pass 1 complete in " << pass1_time << " seconds." << std::endl;

        // --- Normalize densities to [0, 1] ---
        // Note: raw_density is average distance — *lower* distance = *denser* region.
        // So we invert: density[i] = 1 - (raw[i] - min) / (max - min)
        float min_d = *std::min_element(raw_density.begin(), raw_density.end());
        float max_d = *std::max_element(raw_density.begin(), raw_density.end());
        float range = max_d - min_d;

        local_density_.resize(npts_);
        if (range > 1e-8f) {
            for (uint32_t i = 0; i < npts_; i++) {
                local_density_[i] = 1.0f - (raw_density[i] - min_d) / range;
            }
        } else {
            // All densities are the same — set to 0.5 (neutral)
            std::fill(local_density_.begin(), local_density_.end(), 0.5f);
        }

        std::cout << "  Density stats: min_raw=" << min_d << ", max_raw=" << max_d
                  << ", spread=" << density_spread << std::endl;

        // --- Pass 2: rebuild with adaptive alpha ---
        std::cout << "Building index PASS 2 (adaptive alpha, density_spread="
                  << density_spread << ")..." << std::endl;

        // Reset graph
        for (uint32_t i = 0; i < npts_; i++)
            graph_[i].clear();

        // Reshuffle for pass 2
        std::shuffle(perm.begin(), perm.end(), rng);

        Timer pass2_timer;

        #pragma omp parallel for schedule(dynamic, 64)
        for (size_t idx = 0; idx < npts_; idx++) {
            uint32_t point = perm[idx];

            auto [candidates, _dc] = greedy_search(get_vector(point), L);

            // robust_prune now uses adaptive alpha internally
            robust_prune(point, candidates, alpha, R);

            for (uint32_t nbr : graph_[point]) {
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
                    std::cout << "\r  [Pass 2] Inserted " << idx << " / " << npts_
                              << " points" << std::flush;
                }
            }
        }

        double pass2_time = pass2_timer.elapsed_seconds();
        std::cout << "\n  Pass 2 complete in " << pass2_time << " seconds." << std::endl;

    } else {
        // ================================================================
        // SINGLE-PASS BUILD: global alpha (original behavior)
        // ================================================================
        std::cout << "Building index (R=" << R << ", L=" << L
                  << ", alpha=" << alpha << ", gamma=" << gamma
                  << ", gammaR=" << gamma_R << ")..." << std::endl;

        Timer build_timer;

        #pragma omp parallel for schedule(dynamic, 64)
        for (size_t idx = 0; idx < npts_; idx++) {
            uint32_t point = perm[idx];

            // Step 1: Search
            auto [candidates, _dist_cmps] = greedy_search(get_vector(point), L);

            // Step 2: Prune
            robust_prune(point, candidates, alpha, R);

            // Step 3: Backward edges
            for (uint32_t nbr : graph_[point]) {
                std::lock_guard<std::mutex> lock(locks_[nbr]);
                graph_[nbr].push_back(point);

                // Step 4: Degree-triggered pruning
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

        double build_time = build_timer.elapsed_seconds();
        std::cout << "\n  Build complete in " << build_time << " seconds." << std::endl;
    }

    // Compute average degree
    size_t total_edges = 0;
    for (uint32_t i = 0; i < npts_; i++)
        total_edges += graph_[i].size();
    double avg_degree = (double)total_edges / npts_;

    std::cout << "  Average out-degree: " << avg_degree << std::endl;
}

// ============================================================================
// Search
// ============================================================================

SearchResult VamanaIndex::search(const float* query, uint32_t K, uint32_t L) const {
    if (L < K) L = K;

    Timer t;
    auto [candidates, dist_cmps] = greedy_search(query, L);
    double latency = t.elapsed_us();

    // Return top-K results
    SearchResult result;
    result.dist_cmps = dist_cmps;
    result.latency_us = latency;
    result.ids.reserve(K);
    for (uint32_t i = 0; i < K && i < candidates.size(); i++) {
        result.ids.push_back(candidates[i].second);
    }
    return result;
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
