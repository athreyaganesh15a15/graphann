# Quick Start Guide: Running Vamana Experiments

## One-Command Setup

```bash
# Download data (requires curl and python3)
./scripts/run_sift1m.sh

# Build all (from graphann root)
cd build && cmake .. -DCMAKE_BUILD_TYPE=Release && make -j4 && cd ..
```

## Run Experiments

### Individual Experiments (fastest to understand)

```bash
# Test different beam widths (L values)
./build/experiment_runner beam_width \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --output results.csv

# Compare start node strategies
./build/experiment_runner medoid_start \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --output results.csv

# Test graph refinement (two-pass build)
./build/experiment_runner multipass_build \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --output results.csv

# Analyze degree distribution (different alpha values)
./build/experiment_runner degree_analysis \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --output results.csv

# Search optimization benchmark
./build/experiment_runner search_optimization \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --output results.csv
```

### Run All at Once

```bash
./build/experiment_runner all \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --output results.csv

# Takes ~5-10 minutes depending on machine
```

## Visualize Results

```bash
python3 scripts/visualize_experiments.py results.csv plots/

# View generated plots
open plots/*.png  # macOS
xdg-open plots/   # Linux
```

## Understanding Results

### CSV Columns Explained

```
experiment_name   - Which experiment (beam_width, medoid_start, etc)
variant           - Variant tested (e.g., L=75, alpha=1.2)
build_time_sec    - Index construction time in seconds
avg_recall        - Average recall@10 (higher = better)
p50_recall        - Median recall
p95_recall        - 95th percentile recall
avg_dist_cmps     - Average distance computations per query (lower = faster)
avg_latency_us    - Average query time in microseconds (lower = faster)
avg_degree        - Average node out-degree (connectivity)
min_degree        - Minimum out-degree observed
max_degree        - Maximum out-degree observed
```

### What Each Experiment Shows

| Experiment | Key Insight |
|------------|-------------|
| **beam_width** | Find optimal L: where recall plateaus but build is still fast |
| **medoid_start** | Does central node help? Usually no, random is fine |
| **multipass_build** | Can we improve recall by refining edges? (2-5% typical) |
| **degree_analysis** | α=1.2 usually optimal; 1.0 sparse, 1.5 denser |
| **search_optimization** | Pre-allocated buffer saves ~10-20% latency |

### Expected Values (SIFT1M)

```
Metric              Min      Typical   Max
Recall@10          75%       95%       99%
Build time         20s       45s       120s
Avg latency        200μs     500μs     2ms
Avg degree         15        30        50
Distance cmps      500       1000      3000
```

## Custom Parameters

Default: R=32, L=75, α=1.2, γ=1.5, K=10

```bash
# Custom parameters
./build/experiment_runner beam_width \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --R 64 --alpha 1.3 --gamma 1.4 --K 20 \
  --output results_custom.csv
```

## Tips

1. **Start with single experiment** to understand parameter effects
2. **Compare results** side-by-side (e.g., alpha values)
3. **Look at plots first**, then dive into CSV numbers
4. **Check recall** before optimizing for speed
5. **Beam width** usually most impactful for build quality

## Troubleshooting

| Issue | Solution |
|-------|----------|
| Slow build | Reduce L (e.g., 50 instead of 200) |
| Low recall | Increase L or alpha |
| High latency | Increase L at search time (different param) |
| Out of memory | Reduce R or dataset size |
| Build fails | Check data paths, ensure tmp/*.fbin exist |

## One-Line Summary

Experiment different parameter combinations to find your recall/speed/memory sweet spot.

