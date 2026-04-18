# Vamana Algorithm Experimental Framework

This directory contains a comprehensive experimental framework for understanding and optimizing the **Vamana graph-based approximate nearest neighbor (ANN) algorithm**.

## 📖 Start Here

**New to this project?** Start with [QUICK_START.md](QUICK_START.md) (5 minutes)

**Want to understand the algorithm?** Read [CODE_UNDERSTANDING.md](CODE_UNDERSTANDING.md) (10 minutes)

**Need a complete overview?** See [INDEX.md](INDEX.md) (navigation guide)

## 🔬 What's Inside

### Documentation
| File | Purpose | Read Time |
|------|---------|-----------|
| [INDEX.md](INDEX.md) | Navigation and overview | 10 min |
| [QUICK_START.md](QUICK_START.md) | Setup and basic experiments | 5 min |
| [CODE_UNDERSTANDING.md](CODE_UNDERSTANDING.md) | Algorithm explanation | 10 min |
| [EXPERIMENTS_README.md](EXPERIMENTS_README.md) | Detailed experiment guide | 20 min |
| [IMPLEMENTATION_SUMMARY.md](IMPLEMENTATION_SUMMARY.md) | What was built | 15 min |

### Executable
```bash
./build/experiment_runner [experiment] [options]
```

Supports 5 experiment types + run all:
- `beam_width` - Test different L values
- `medoid_start` - Compare start node strategies
- `multipass_build` - Test graph refinement
- `degree_analysis` - Analyze alpha parameter effects
- `search_optimization` - Benchmark search methods
- `all` - Run everything

### Visualization
```bash
python3 scripts/visualize_experiments.py results.csv output_prefix
```

Generates 5 PNG plots from CSV results.

## 🚀 Quick Start (3 commands)

```bash
# 1. Prepare data
./scripts/run_sift1m.sh

# 2. Run experiments
./build/experiment_runner all \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --output results.csv

# 3. Visualize
python3 scripts/visualize_experiments.py results.csv plots/
```

Done! Check `plots/` and `results.csv` for results.

## 💡 The Experiments

### 1️⃣ Beam Width (L values)
**Question**: What's the optimal search list size during build?

**Tests**: L ∈ {50, 75, 100, 125, 150, 200}

**Measures**: Build time, recall, degree, distance computations

**Output**: 3-panel plot showing recall vs L, build time vs L, degree vs L

### 2️⃣ Medoid Start Node
**Question**: Does choosing the center of the dataset help?

**Tests**: Random start vs medoid (closest to centroid)

**Measures**: Recall, latency, degree

**Output**: 2-panel comparison (recall + latency bars)

### 3️⃣ Multi-Pass Build
**Question**: Can we improve recall by refining edges?

**Tests**: Single pass vs two-pass construction

**Measures**: Recall before/after, degree, build time

**Output**: 2-panel comparison (recall + degree before/after)

### 4️⃣ Degree Analysis (Alpha values)
**Question**: How does the RNG parameter (alpha) affect graph structure?

**Tests**: α ∈ {1.0, 1.2, 1.5}

**Measures**: Recall, degree distribution, build time

**Output**: 3-panel plot showing recall vs α, degree vs α, build time vs α

### 5️⃣ Search Optimization
**Question**: Can pre-allocated buffers reduce latency?

**Tests**: Normal search (vector<bool>) vs scratch buffer (pre-allocated)

**Measures**: Query latency, recall

**Output**: Latency bar chart comparing methods

## 📊 Expected Results

On SIFT1M dataset (1M vectors, 10K queries):

```
Metric                Min      Typical   Max
Recall@10            75%       95%       99%
Build time          20s        45s       120s
Search latency      200μs      500μs     2ms
Avg degree          15         30        50
Distance cmps       500        1000      3000
```

## 🎓 Learning Outcomes

After running these experiments, you'll understand:

✅ How L affects build speed vs graph quality  
✅ Whether start node choice matters  
✅ How to refine graph structure  
✅ The role of alpha in edge diversity  
✅ Memory allocation overhead in search  
✅ Parameter trade-offs for your use case  

## 🔨 How to Use

### Run Single Experiment
```bash
./build/experiment_runner beam_width \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --output results.csv
```

### Run with Custom Parameters
```bash
./build/experiment_runner degree_analysis \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --R 64 --L 100 --gamma 1.4 \
  --output custom_results.csv
```

### Analyze Results
```bash
# View CSV
cat results.csv

# Generate plots
python3 scripts/visualize_experiments.py results.csv output_prefix

# View plots
ls output_prefix*.png
```

## 📁 Output Format

### CSV Columns
```
experiment_name     Which experiment
variant             Which variant (e.g., "L=75")
build_time_sec      Index construction time
avg_recall          Average recall@10
p50_recall          Median recall
p95_recall          95th percentile recall
avg_dist_cmps       Average distance computations
avg_latency_us      Average query time (microseconds)
avg_degree          Average node out-degree
min_degree          Minimum degree observed
max_degree          Maximum degree observed
```

### PNG Plots
- `output_prefix_beam_width.png` - 3 subplots
- `output_prefix_medoid_start.png` - 2 subplots
- `output_prefix_multipass.png` - 2 subplots
- `output_prefix_degree_analysis.png` - 3 subplots
- `output_prefix_search_optimization.png` - 1 subplot

## 🛠️ Building from Source

```bash
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
cd ..

# Verify
./build/experiment_runner --help
```

## 📚 Deep Dives

**Want to modify experiments?**
- Edit `src/experiments.cpp`
- Add new experiment method to `ExperimentRunner` class
- Update `experiment_runner.cpp` CLI

**Want to extend Vamana?**
- Add methods to `VamanaIndex` class
- Implement in `src/vamana_index.cpp`
- Update `include/vamana_index.h`

**Want custom analysis?**
- Read CSV with your tool (pandas, R, Excel, etc.)
- Modify `scripts/visualize_experiments.py` for new plots

## ❓ FAQ

**Q: How long do experiments take?**
A: ~10-15 minutes for all experiments on SIFT1M

**Q: Can I use my own dataset?**
A: Yes! Provide fbin/ibin files (see scripts/convert_vecs.py)

**Q: What parameters should I use?**
A: Defaults (R=32, L=75, α=1.2, γ=1.5) are good for SIFT1M

**Q: How do I interpret the plots?**
A: See EXPERIMENTS_README.md "Understanding Results" section

**Q: Can I parallelize experiments?**
A: Yes, modify experiment_runner.cpp to submit jobs in parallel

## 📖 References

- **DiskANN Paper**: [Fast Accurate Billion-point Nearest Neighbor Search on a Single Node](https://proceedings.neurips.cc/paper/2019/hash/09853c7fb1d3f8ee67a61b6bf4a7f8e6-Abstract.html) (NeurIPS 2019)
- **RNG**: [Relative Neighborhood Graphs](https://en.wikipedia.org/wiki/Relative_neighborhood_graph)
- **Small-World Graphs**: [Navigable Small-World Networks](https://en.wikipedia.org/wiki/Small-world_network)

## 📝 Citation

If you use this framework, cite the original DiskANN/Vamana paper:

```bibtex
@inproceedings{subramanya2019diskann,
  title={DiskANN: Fast Accurate Billion-point Nearest Neighbor Search on a Single Node},
  author={Subramanya, Suhas J and others},
  booktitle={NeurIPS},
  year={2019}
}
```

## 🎯 Next Steps

1. **Read** [QUICK_START.md](QUICK_START.md)
2. **Read** [CODE_UNDERSTANDING.md](CODE_UNDERSTANDING.md)
3. **Run** beam_width experiment
4. **Generate** plots
5. **Explore** other experiments

Good luck! Questions? See [INDEX.md](INDEX.md).

