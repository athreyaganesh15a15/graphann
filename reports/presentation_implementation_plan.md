# Simulation & Plot Plan for Presentation

## What You Already Have

From reviewing your project, here's your current data inventory:

### R=32 Data (just ran — fresh results in terminal output)
| Mode | Status |
|---|---|
| Exact Float32 (Baseline) | ✅ Complete |
| Quantized ADC (Opt A) | ✅ Complete |
| Quantized + Dynamic Beam (Opt A+C) | ✅ Complete |

### R=64 Data (from `best.md` — Mac results only)
| Mode | Status |
|---|---|
| Exact Float32 | ✅ Have Mac data |
| Quantized ADC | ✅ Have Mac data |
| Quantized + Dynamic Beam | ❌ **Not run** |

### Existing Plots (in `plots/`)
These are from the `experiment_runner_v3` experiments (beam width, medoid, degree, etc.) — **not** the SIFT1M optimization comparisons.

---

## Simulations Still Needed

### 1. ✅ R=64 Full Benchmark (same machine — **REQUIRED**)

You have R=32 data from your Linux machine, but the R=64 data in `best.md` is from a Mac. **You need R=64 results from the same machine for a fair comparison.**

```bash
./scripts/run_sift1m_full.sh --r64
```

**Time estimate:** ~5-6 minutes (build ~3min, 3 search runs ~1min each)  
**Produces:** Exact, Quantized, Quantized+Dynamic results for R=64

---

### 2. 🆕 Exact-only search on R=64 with `--dynamic` flag (optional)

The script doesn't test `Exact + Dynamic Beam` (only `Quantized + Dynamic`). If you want to isolate the dynamic beam improvement independently from quantization, you'd run:

```bash
# After R=64 index is built:
./build/search_index \
  --index tmp/sift_index_r64.bin \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --K 10 --L 10,20,30,50,75,100,150,200 \
  --dynamic
```

Same for R=32:
```bash
./build/search_index \
  --index tmp/sift_index_r32.bin \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --K 10 --L 10,20,30,50,75,100,150,200 \
  --dynamic
```

> [!TIP]
> This isolates **Optimization C alone** from **Optimization A**, which makes your ablation study cleaner for the presentation.

---

### 3. 🆕 Different K values — Recall@1 and Recall@100 (optional, high-impact)

Your current benchmarks only measure **Recall@10**. Testing K=1 and K=100 would show how your optimizations scale:

```bash
# Recall@1 (hardest — must find THE nearest neighbor)
./build/search_index \
  --index tmp/sift_index_r32.bin \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --K 1 --L 10,20,30,50,75,100,150,200

./build/search_index \
  --index tmp/sift_index_r32.bin \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --K 1 --L 10,20,30,50,75,100,150,200 \
  --quantized

# Recall@100 (easiest — find any 100 close neighbors)
./build/search_index \
  --index tmp/sift_index_r32.bin \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --K 100 --L 100,150,200,300

./build/search_index \
  --index tmp/sift_index_r32.bin \
  --data tmp/sift_base.fbin \
  --queries tmp/sift_query.fbin \
  --gt tmp/sift_gt.ibin \
  --K 100 --L 100,150,200,300 \
  --quantized
```

> [!NOTE]
> For K=100, L must be ≥ K, so start from L=100.

---

## Summary Table: What to Run

| # | Simulation | Command | Time Est. | Priority |
|---|---|---|---|---|
| 1 | **R=64 full benchmark** | `./scripts/run_sift1m_full.sh --r64` | ~6 min | 🔴 Required |
| 2 | R=32 Exact + Dynamic | `search_index --dynamic` (no `--quantized`) | ~1 min | 🟡 Nice for ablation |
| 3 | R=64 Exact + Dynamic | `search_index --dynamic` (no `--quantized`) | ~1 min | 🟡 Nice for ablation |
| 4 | R=32 Recall@1 (Exact vs Quant) | Two `search_index --K 1` runs | ~2 min | 🟢 Optional |
| 5 | R=32 Recall@100 | Two `search_index --K 100` runs | ~2 min | 🟢 Optional |

**Total time for all simulations: ~12 minutes**  
**No codebase changes needed** — all use existing binaries and flags.

---

## Plots to Generate (Python Script)

Once you have the data, I'll create a standalone Python plotting script (no codebase changes) that produces these **8 presentation-quality plots**:

### Core Comparison Plots
1. **Recall vs Latency (Pareto curve)** — The most important plot. Shows all 3 search modes (Exact, Quantized, Quantized+Dynamic) as separate curves. One plot for R=32, one for R=64.
2. **Recall vs Distance Computations** — Shows that recall is preserved while reducing latency.
3. **Latency Speedup Bar Chart** — At fixed recall targets (0.95, 0.99), show the latency of each method as grouped bars.
4. **P99 Tail Latency Comparison** — Highlights worst-case improvements.

### R=32 vs R=64 Plots
5. **R=32 vs R=64 Recall-Latency Pareto** — Overlay both R values to show the tradeoff.
6. **Equivalent Recall Comparison Table/Bar** — At the same recall, show how much faster R=64 is (the "L=20 matches L=75" argument from `best.md`).

### Memory & Build
7. **Memory Footprint Bar Chart** — Float32 (488 MB) vs Quantized (122 MB). Simple and impactful.
8. **Build Time Comparison** — R=32 vs R=64.

---

## Open Questions

> [!IMPORTANT]
> 1. **Should I run `./scripts/run_sift1m_full.sh --r64` now?** This takes ~6 minutes and builds a new index. Your R=32 index already exists in `tmp/`.
> 2. **Do you want the ablation runs** (Exact + Dynamic, different K values), or should I focus on just the core R=32 vs R=64 comparison plots?
> 3. **Plot format preference:** PNG (for slides) or PDF (for LaTeX report), or both?
