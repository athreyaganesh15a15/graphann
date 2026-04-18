# Vamana Index Optimizations — Implementation Plan

Implementing the 4 optimization strategies from `task.txt` to maximize recall under a fixed memory constraint (out-degree `R`), with execution time as a secondary bonus.

## Summary of Changes

| # | Optimization | Target | Files Modified |
|---|---|---|---|
| 1 | **Adaptive α-RNG Pruning** (density-aware) | Recall (smarter edges) | `vamana_index.cpp`, `vamana_index.h` |
| 2 | **Improved Greedy Search** (backtrack penalty) | Recall + Speed | `vamana_index.cpp` |
| 3 | **Medoid Start Node** | Recall | `vamana_index.cpp` |
| 4 | **SIMD-Optimized L2 Distance** | Speed | `distance.cpp`, `distance.h` |

---

## Proposed Changes

### 1. Adaptive α-RNG Pruning (Idea #2 from task.txt)

The current `robust_prune()` uses a **global, static α** for all nodes. The idea: nodes in dense clusters should use **strict pruning** (lower α → more diverse long-range edges), while outlier nodes should use **relaxed pruning** (higher α → retain nearby edges since they're few).

#### [MODIFY] [vamana_index.h](file:///c:/Athreya/Acads/Courses/DA2303/Project/graphann/include/vamana_index.h)

- Add a `std::vector<float> local_density_` member to cache per-node local density estimates.
- Add a private method `compute_local_densities(uint32_t sample_k)` that estimates local density for each point using k-NN distances (computed during the initial greedy search pass).

#### [MODIFY] [vamana_index.cpp](file:///c:/Athreya/Acads/Courses/DA2303/Project/graphann/src/vamana_index.cpp)

- **`compute_local_densities()`**: For each point, sample `k` nearest neighbors (e.g., k=10) using brute-force on a random subset, compute the average distance to them. Normalize densities across the dataset to [0, 1].
- **`robust_prune()`**: Replace the global `alpha` with an adaptive per-node alpha:
  ```
  adaptive_alpha = alpha_base - density_factor * (normalized_density - 0.5)
  ```
  Dense nodes → lower α (stricter → more diverse). Sparse nodes → higher α (relaxed → keep whatever's nearby).
- **`build()`**: Call `compute_local_densities()` after data loading, before graph construction.

---

### 2. Improved Greedy Search (Idea #3 from task.txt)

The current `greedy_search()` uses `std::set<Candidate>` and `std::set<uint32_t>` for tracking, which has high constant overhead from tree allocations. The task suggests:
- Better priority queue management
- Penalizing backtracking (nodes that take us further from the query)

#### [MODIFY] [vamana_index.cpp](file:///c:/Athreya/Acads/Courses/DA2303/Project/graphann/src/vamana_index.cpp)

- Replace `std::set<Candidate>` + `std::set<uint32_t> expanded` with a **sorted vector** approach using `std::vector<Candidate>` + a `frontier_pos` index. This avoids tree node allocations and is much more cache-friendly.
- Use `std::unordered_set<uint32_t>` for the visited set instead of `std::vector<bool>` for better scalability (avoids O(npts) allocation per query).

> [!NOTE]
> The `std::vector<bool>` approach allocates npts bits per query call. For 1M points, that's 125KB per query — which thrashes cache. An `unordered_set` only tracks actually-visited nodes (typically ~L to a few thousand) and is much more cache-friendly at query time.

---

### 3. Medoid Start Node (Idea from README, reinforced by task.txt)

The current code picks a **random** start node. The medoid (point closest to dataset centroid) is a much better starting point for greedy search because it minimizes the expected number of hops to reach any region of the space.

#### [MODIFY] [vamana_index.cpp](file:///c:/Athreya/Acads/Courses/DA2303/Project/graphann/src/vamana_index.cpp)

- **`build()`**: After loading data, compute the centroid (mean of all points), then find the point closest to the centroid. Use it as `start_node_`.
- This is a simple O(n·d) computation, done once at build time.

---

### 4. SIMD-Optimized L2 Distance (Idea #4 from task.txt — partial)

The current `compute_l2sq()` is a plain loop that relies on compiler auto-vectorization. We'll implement explicit **SSE/AVX** intrinsics for guaranteed vectorization, plus **loop unrolling** for dimensions that are multiples of 16 (SIFT has dim=128, perfect).

#### [MODIFY] [distance.h](file:///c:/Athreya/Acads/Courses/DA2303/Project/graphann/include/distance.h)

- Add `#include <immintrin.h>` for SIMD intrinsics.

#### [MODIFY] [distance.cpp](file:///c:/Athreya/Acads/Courses/DA2303/Project/graphann/src/distance.cpp)

- Implement SSE4.1 version processing 4 floats at a time with `__m128`.
- Implement AVX2 version processing 8 floats at a time with `__m256`.
- Runtime dispatch: use `#ifdef __AVX2__` / `#ifdef __SSE4_1__` to pick the best path at compile time.
- Fallback to the original scalar loop if neither is available.

---

## Open Questions

> [!IMPORTANT]
> **Density estimation cost**: Computing per-node local density is O(n·k·d) if done brute-force. For SIFT1M (n=1M, d=128), this is significant. I'll use a **random sampling** approach (sample ~1000 points, compute distances to those) to keep this fast. Is that acceptable, or do you want the exact k-NN density?

> [!IMPORTANT]
> **Adaptive alpha range**: The task suggests density-aware alpha but doesn't specify the range. I'll use `alpha ∈ [alpha_base * 0.8, alpha_base * 1.2]` (±20% of the user-provided alpha). Should this be wider?

---

## Verification Plan

### Automated Tests
- Build the project in WSL with `cmake .. -DCMAKE_BUILD_TYPE=Release && make -j`
- Run `run_sift1m.sh` and compare recall@10 at various L values against the baseline output shown in the README

### Manual Verification
- Compare before/after recall@10 numbers at L=100 (target: meaningful improvement from ~0.98 baseline)
- Check that index file size is unchanged (same R, no extra edges)
- Check that build time doesn't regress by more than ~2x (density computation + medoid are one-time costs)
