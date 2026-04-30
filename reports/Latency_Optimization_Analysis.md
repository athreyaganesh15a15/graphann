# Latency Optimization Analysis

> [!IMPORTANT]
> The constraint is: *same recall, same avg dist comparisons* — only reduce wall-clock time per query. This means we cannot change the graph traversal order or pruning decisions. We can only make each operation faster.

## Current Bottleneck Profile

Looking at the R=32 Exact Float32 results:

| L | Recall | Avg Dist Cmps | Avg Latency (µs) | P99 Latency (µs) |
|---|--------|---------------|-------------------|-------------------|
| 50 | 0.9737 | 1517.8 | 638.2 | 2839.6 |
| 100 | 0.9922 | 2555.9 | 928.0 | 2534.2 |
| 200 | 0.9975 | 4352.1 | 1567.6 | 4024.5 |

At L=100, ~2556 distance computations at 128 dims = *~2556 × 128 = 327K float ops*. The ~928µs latency implies significant overhead beyond raw FLOPs. Sources…
[1:18 AM, 5/1/2026] Krishanthh Mohan DA 836: (gitvenv) krishanthhmohan@Krishanthhs-MacBook-Pro graphann % ./scripts/run_sift1m_full.sh --r32
=== Step 1: Building the project ===
-- Configuring done (0.3s)
-- Generating done (0.0s)
-- Build files have been written to: /Users/krishanthhmohan/Projects/graphann/build
./scripts/run_sift1m_full.sh: line 38: nproc: command not found
[  7%] Building CXX object CMakeFiles/graphann_lib.dir/src/vamana_index.cpp.o
[ 14%] Linking CXX static library libgraphann_lib.a
[ 42%] Built target graphann_lib
[ 71%] Linking CXX executable search_index
[ 71%] Linking CXX executable experiment_runner_v3
[ 71%] Linking CXX executable build_index
[ 71%] Linking CXX executable benchmark
[100%] Built target experiment_runner_v3
[100%] Built target search_index
[100%] Built target benchmark
[100%] Built target build_index

=== Step 2: Downloading SIFT1M ===
SIFT1M already downloaded, skipping.

=== Step 3: Converting to fbin/ibin ===
Binary files already exist, skipping conversion.

================================================================
  CONFIGURATION: R=32, L=75, alpha=1.2 (Baseline)
================================================================
=== Building R=32 index ===
=== Vamana Index Builder ===
Parameters:
  R     = 32
  L     = 75
  alpha = 1.1
  gamma = 1.5
Loading data from /Users/krishanthhmohan/Projects/graphann/tmp/sift_base.fbin...
  Points: 1000000, Dimensions: 128
  Medoid start node: 123742
  Random R-regular graph initialized (R=32)
Building index (R=32, L=75, alpha=1.1, gamma=1.5, gammaR=48)...
  Inserted 500000 / 1000000 points
  Build complete in 100.385 seconds.
  Average out-degree: 38.3957

Total build time: 103.216 seconds
Index saved to /Users/krishanthhmohan/Projects/graphann/tmp/sift_index_r32.bin
Done.

=== R=32 Exact Float32 Search ===
Loading index...
Index loaded: 1000000 points, 128 dims, start=123742
Loading queries from /Users/krishanthhmohan/Projects/graphann/tmp/sift_query.fbin...
  Queries: 10000 x 128
Loading ground truth from /Users/krishanthhmohan/Projects/graphann/tmp/sift_gt.ibin...
  Ground truth: 10000 x 100

=== Search Results (K=10) ===
       L     Recall@10   Avg Dist Cmps  Avg Latency (us)  P99 Latency (us)
--------------------------------------------------------------------------
      10        0.8061           560.8             217.5            1066.8
      20        0.9052           817.7             297.8            1319.9
      30        0.9435          1059.4             368.1            1167.4
      50        0.9737          1517.8             638.2            2839.6
      75        0.9869          2053.4             805.1            3417.2
     100        0.9922          2555.9             928.0            2534.2
     150        0.9961          3488.3            1291.1            3673.8
     200        0.9975          4352.1            1567.6            4024.5

Done.

=== R=32 Quantized ADC Search (Optimization A) ===
Loading index...
Index loaded: 1000000 points, 128 dims, start=123742
Building quantization tables...
  Quantization complete in 0.121397s: 122 MB (from 488 MB float32)
Loading queries from /Users/krishanthhmohan/Projects/graphann/tmp/sift_query.fbin...
  Queries: 10000 x 128
Loading ground truth from /Users/krishanthhmohan/Projects/graphann/tmp/sift_gt.ibin...
  Ground truth: 10000 x 100

=== Search Results (K=10, quantized) ===
       L     Recall@10   Avg Dist Cmps  Avg Latency (us)  P99 Latency (us)
--------------------------------------------------------------------------
      10        0.8040           560.7             160.8             355.8
      20        0.9055           817.9             232.1             465.8
      30        0.9434          1059.0             306.5             625.4
      50        0.9737          1517.6             451.7             927.3
      75        0.9868          2053.2             605.8            1211.8
     100        0.9921          2555.7             757.8            1470.8
     150        0.9960          3488.3            1111.9            3125.9
     200        0.9974          4351.9            1369.5            2922.1

Done.

=== R=32 Quantized + Dynamic Beam (Optimizations A+C) ===
Loading index...
Index loaded: 1000000 points, 128 dims, start=123742
Building quantization tables...
  Quantization complete in 0.119603s: 122 MB (from 488 MB float32)
Loading queries from /Users/krishanthhmohan/Projects/graphann/tmp/sift_query.fbin...
  Queries: 10000 x 128
Loading ground truth from /Users/krishanthhmohan/Projects/graphann/tmp/sift_gt.ibin...
  Ground truth: 10000 x 100

=== Search Results (K=10, quantized, dynamic_beam) ===
       L     Recall@10   Avg Dist Cmps  Avg Latency (us)  P99 Latency (us)
--------------------------------------------------------------------------
      10        0.4540           415.6             118.0             259.8
      20        0.8144           590.9             166.8             424.3
      30        0.9160          1001.7             311.4            1480.3
      50        0.9683          1519.1             469.7            1774.9
      75        0.9850          2064.0             614.7            1724.9
     100        0.9914          2575.6             808.6            2582.8
     150        0.9957          3507.5            1115.8            3216.2
     200        0.9974          4370.7            1408.8            3833.2

Done.

=== All benchmarks complete! ===
(gitvenv) krishanthhmohan@Krishanthhs-MacBook-Pro graphann % ./scripts/run_sift1m_full.sh --r32
=== Step 1: Building the project ===
-- Configuring done (0.1s)
-- Generating done (0.0s)
-- Build files have been written to: /Users/krishanthhmohan/Projects/graphann/build
./scripts/run_sift1m_full.sh: line 38: nproc: command not found
[ 42%] Built target graphann_lib
[100%] Built target benchmark
[100%] Built target search_index
[100%] Built target experiment_runner_v3
[100%] Built target build_index

=== Step 2: Downloading SIFT1M ===
SIFT1M already downloaded, skipping.

=== Step 3: Converting to fbin/ibin ===
Binary files already exist, skipping conversion.

================================================================
  CONFIGURATION: R=32, L=75, alpha=1.2 (Baseline)
================================================================
=== Building R=32 index ===
=== Vamana Index Builder ===
Parameters:
  R     = 32
  L     = 75
  alpha = 1.1
  gamma = 1.5
Loading data from /Users/krishanthhmohan/Projects/graphann/tmp/sift_base.fbin...
  Points: 1000000, Dimensions: 128
  Medoid start node: 123742
  Random R-regular graph initialized (R=32)
Building index (R=32, L=75, alpha=1.1, gamma=1.5, gammaR=48)...
  Inserted 400000 / 1000000 points
  Build complete in 48.6993 seconds.
  Average out-degree: 38.3852

Total build time: 51.5148 seconds
Index saved to /Users/krishanthhmohan/Projects/graphann/tmp/sift_index_r32.bin
Done.

=== R=32 Exact Float32 Search ===
Loading index...
Index loaded: 1000000 points, 128 dims, start=123742
Loading queries from /Users/krishanthhmohan/Projects/graphann/tmp/sift_query.fbin...
  Queries: 10000 x 128
Loading ground truth from /Users/krishanthhmohan/Projects/graphann/tmp/sift_gt.ibin...
  Ground truth: 10000 x 100

=== Search Results (K=10) ===
       L     Recall@10   Avg Dist Cmps  Avg Latency (us)  P99 Latency (us)
--------------------------------------------------------------------------
      10        0.8060           565.1              96.7             350.8
      20        0.9056           822.3             135.2             432.3
      30        0.9446          1064.8             176.0             517.3
      50        0.9738          1522.5             269.8             774.9
      75        0.9869          2058.0             372.4            1115.9
     100        0.9923          2560.0             461.4            1308.0
     150        0.9958          3492.2             641.5            1797.8
     200        0.9975          4355.7             820.6            2368.6

Done.

=== R=32 Quantized ADC Search (Optimization A) ===
Loading index...
Index loaded: 1000000 points, 128 dims, start=123742
Building quantization tables...
  Quantization complete in 0.126326s: 122 MB (from 488 MB float32)
Loading queries from /Users/krishanthhmohan/Projects/graphann/tmp/sift_query.fbin...
  Queries: 10000 x 128
Loading ground truth from /Users/krishanthhmohan/Projects/graphann/tmp/sift_gt.ibin...
  Ground truth: 10000 x 100

=== Search Results (K=10, quantized) ===
       L     Recall@10   Avg Dist Cmps  Avg Latency (us)  P99 Latency (us)
--------------------------------------------------------------------------
      10        0.8042           565.2              86.8             206.6
      20        0.9052           822.3             124.1             264.4
      30        0.9444          1064.7             169.6             381.0
      50        0.9738          1522.3             255.6             609.8
      75        0.9868          2057.9             333.8             698.0
     100        0.9922          2559.9             434.4            1006.2
     150        0.9958          3492.2             606.1            1292.8
     200        0.9975          4355.7             783.6            1617.3

Done.

=== R=32 Quantized + Dynamic Beam (Optimizations A+C) ===
Loading index...
Index loaded: 1000000 points, 128 dims, start=123742
Building quantization tables...
  Quantization complete in 0.128372s: 122 MB (from 488 MB float32)
Loading queries from /Users/krishanthhmohan/Projects/graphann/tmp/sift_query.fbin...
  Queries: 10000 x 128
Loading ground truth from /Users/krishanthhmohan/Projects/graphann/tmp/sift_gt.ibin...
  Ground truth: 10000 x 100

=== Search Results (K=10, quantized, dynamic_beam) ===
       L     Recall@10   Avg Dist Cmps  Avg Latency (us)  P99 Latency (us)
--------------------------------------------------------------------------
      10        0.4545           420.7              65.2             158.4
      20        0.8139           594.8              90.2             243.5
      30        0.9188          1008.0             157.5             358.4
      50        0.9684          1522.8             254.2             627.2
      75        0.9849          2072.8             331.8             687.4
     100        0.9910          2579.8             421.6             865.5
     150        0.9958          3512.2             590.3            1195.8
     200        0.9973          4374.6             759.0            1569.8

Done.

=== All benchmarks complete! ===