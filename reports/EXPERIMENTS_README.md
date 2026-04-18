# Vamana Algorithm Experiments

This document explains how to run and interpret the experiments implemented for understanding and optimizing the Vamana graph-based ANN index.

## Prerequisites

- C++17 compiler with OpenMP support
- Python 3 with matplotlib and numpy (for visualization)
- SIFT1M or similar ANN benchmark dataset

## Building

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
```

This produces an `experiment_runner` executable.

## Dataset Preparation

Using the provided script:

```bash
./scripts/run_sift1m.sh
```

This downloads SIFT1M and converts it to the required binary format.

Or manually:

```bash
# Download data (already converted to fbin/ibin format)
# Place in tmp/ directory:
# - tmp/sift_base.fbin (1M base vectors)
# - tmp/sift_query.fbin (10K queries)
# - tmp/sift_gt.ibin (ground truth)
```

## Experiments

### 1. Beam Width Experiments

**What it does**: Tests different `L` values (search list size during build) and measures:
- Build time
- Recall@10
- Average graph degree
- Distance computations per query

**Why it matters**: L is a key parameter controlling build speed vs graph quality.

```bash
./build/experiment_runner beam_width \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --R 32 --alpha 1.2 --gamma 1.5 --K 10 \
  --output results_beam_width.csv
```

**Interpretation**:
- **Sweet spot**: Find where recall plateaus but build time is still acceptable
- **Typical finding**: L=100-150 often balances quality and build time

### 2. Medoid Start Node

**What it does**: Compares two strategies for choosing the start node:
- Random (standard Vamana)
- Medoid (closest point to centroid of dataset)

**Why it matters**: Start node affects search convergence and overall search quality.

```bash
./build/experiment_runner medoid_start \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --R 32 --L 75 --alpha 1.2 --gamma 1.5 --K 10 \
  --output results_medoid.csv
```

**Interpretation**:
- If medoid has better recall: The center of the dataset is a better pivot
- If similar: Random start is already near-optimal due to RNG properties
- Check latency: Medoid might have different search complexity

### 3. Multi-Pass Build

**What it does**: Runs the build algorithm twice:
- Pass 1: Normal build from scratch
- Pass 2: Starts with Pass 1 graph, refines edges

**Why it matters**: Graph refinement can improve edge quality without rebuilding from scratch.

```bash
./build/experiment_runner multipass_build \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --R 32 --L 75 --alpha 1.2 --gamma 1.5 --K 10 \
  --output results_multipass.csv
```

**Interpretation**:
- **Recall improvement**: Second pass re-evaluates edges with better candidates
- **Build time**: Second pass is typically faster (existing graph structure helps)
- **Diminishing returns**: Check if improvement justifies 2x build time

### 4. Degree Analysis

**What it does**: Analyzes how alpha (α) parameter affects:
- Node degree distribution (uniformity)
- Graph navigability
- Build time
- Search recall

**Why it matters**: α controls the RNG pruning rule, affecting edge diversity.

```bash
./build/experiment_runner degree_analysis \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --R 32 --L 75 --gamma 1.5 --K 10 \
  --output results_alpha.csv
```

**Interpretation**:
- **α = 1.0** (strict RNG): Sparser graph, more diverse edges, may struggle with connectivity
- **α = 1.2** (default): Good balance between diversity and connectivity
- **α = 1.5** (relaxed RNG): Denser graph with shortcuts, faster search but less diverse

### 5. Search Optimization

**What it does**: Compares two search implementations:
- Normal: Allocates `std::vector<bool>` per query
- Scratch: Uses pre-allocated scratch buffer (thread-local)

**Why it matters**: Memory allocation overhead can impact latency on high-QPS systems.

```bash
./build/experiment_runner search_optimization \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --R 32 --L 75 --alpha 1.2 --gamma 1.5 --K 10 \
  --output results_search_opt.csv
```

**Interpretation**:
- **Latency reduction**: Scratch buffer eliminates per-query allocation
- **P95 latency**: More important than average — check tail latencies
- **Trade-off**: Scratch buffer uses more memory but is faster

### Run All Experiments

```bash
./build/experiment_runner all \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --output results_all.csv
```

## Visualization

Generate plots from results:

```bash
python3 scripts/visualize_experiments.py results_all.csv plots
```

This generates PNG plots in the `plots/` directory:
- `plots_beam_width.png` — Recall, build time, degree vs L
- `plots_medoid_start.png` — Comparison of start node strategies
- `plots_multipass.png` — Single vs multi-pass recall and degree
- `plots_degree_analysis.png` — Effect of α on recall, degree, build time
- `plots_search_optimization.png` — Latency comparison

## Understanding the Results

### Key Metrics

| Metric | Meaning | Target |
|--------|---------|--------|
| **Recall@10** | % of queries where true NN is in top-10 results | >95% |
| **Avg Latency** | Mean query time | <1ms typical |
| **P95 Latency** | 95th percentile query time | <5ms typical |
| **Dist Cmps** | Distance function calls per query | Lower = faster |
| **Build Time** | Index construction time | Depends on dataset |
| **Avg Degree** | Average out-degree of nodes | Should be ~R or <2R |

### Trade-offs

1. **Build Time vs Recall**
   - Larger L → slower build, better recall
   - Sweet spot: typically L = 100-150 for SIFT1M

2. **Search Latency vs Recall**
   - Larger L (search) → more accurate, slower
   - Typical range: search L ∈ [50, 200]

3. **Memory vs Search Speed**
   - Larger R → more memory, faster search (fewer hops)
   - Typical range: R ∈ [32, 64]

4. **Alpha (α) Effect**
   - Larger α → denser graph, easier to navigate, more memory
   - Range α ∈ [1.0, 1.5], default 1.2

## Example Workflow

```bash
# 1. Download and prepare data
./scripts/run_sift1m.sh

# 2. Run all experiments
./build/experiment_runner all \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --output results.csv

# 3. Visualize results
python3 scripts/visualize_experiments.py results.csv plots

# 4. Analyze results
cat results.csv | head -20
```

## Advanced: Custom Parameters

You can combine experiments with custom parameters:

```bash
# Test specific beam widths
for L in 50 75 100 125 150; do
  ./build/experiment_runner beam_width \
    --data tmp/sift_base.fbin \
    --queries tmp/sift_query.fbin \
    --gt tmp/sift_gt.ibin \
    --L $L --R 32 --alpha 1.2 --gamma 1.5 \
    --output results_L${L}.csv
done
```

## Expected Results (SIFT1M)

Baseline with R=32, L=75, α=1.2, γ=1.5:

| Metric | Value |
|--------|-------|
| Build Time | ~30-60 seconds |
| Recall@10 | ~95-98% |
| Avg Latency | ~200-500 μs |
| Avg Degree | ~28-32 |
| Distance Computations | ~800-1200 |

These vary based on system specs and exact dataset.

## Troubleshooting

**High recall but slow build time**: Reduce L during build
**Low recall**: Increase L or alpha
**High search latency**: Check distance computation count; may need larger L at search time
**Memory issues**: Reduce R (but recall may suffer)

