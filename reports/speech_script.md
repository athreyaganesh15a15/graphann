# GraphANN Presentation Speech Script

**Target Duration:** ~8 minutes (approx. 1100-1200 words)
**Presenters:** Person A (Introduction & Problem), Person B (Core Algorithm & Search Optimizations), Person C (Build Optimizations, Results & Conclusion)

---

## Part 1: Introduction & Problem Statement (Person A)
**Time allocation: ~2 minutes (Slides 1-3)**

**(Slide 1: Title)**
**Person A:** Today we will show our work on the DiskANN algorithm and how we optimized it further.

**(Slide 2: The Problem)**
**Person A:** Let's start w ith the core problem. We are given one million vectors, each with 128 dimensions—this is the SIFT1M dataset. Given a new query vector, we need to find its 10 closest neighbors. 
A naive brute-force approach requires computing the distance between the query and every single point in the dataset. That's 128 million floating-point operations per query, which is far too slow for real-world applications.
Graph-based Approximate Nearest Neighbor, or ANN, solves this by building a navigable graph where points are nodes and edges connect close neighbors. Instead of an exhaustive search, we greedily "walk" the graph. 
Our objective for this project was strict: How can we maximize our recall—specifically getting a Recall@10 of at least 99%—while keeping the average search latency under one millisecond and minimizing the memory footprint?

**(Slide 3: Vamana Algorithm)**
**Person A:** The foundation of our system is the Vamana algorithm. 
The algorithm operates in two main phases. First is the *Build* phase, where points are inserted sequentially, and a greedy search is used to find candidate neighbors. The edges are then pruned using an alpha-RNG rule. It ensures the graph retains a mix of very close neighbors and longer-range "highway" edges, preventing the search from getting stuck in local minima.
The second phase is *Search*. We start from a designated entry node and greedily expand the closest unvisited nodes using a candidate list, or "beam," of size *L*. 
The fundamental trade-off here revolves around this beam width *L*, the graph degree *R*, and the pruning parameter alpha. A larger *L* explores more nodes, giving higher recall, but at the cost of higher latency. I will now hand it over to [Person B] to explain how we broke through this trade-off using our core optimizations.

---

## Part 2: Search Optimizations (Person B)
**Time allocation: ~3 minutes (Slides 4-6)**

**(Slide 4: Our Optimizations)**
**Person B:** I will explain the search optimizations.

**(Slide 5: Opt A: Quantized ADC)**
**Person B:** Our most impactful optimization was Asymmetric Distance Computation using Quantization.
In high-dimensional space, memory bandwidth is often the primary bottleneck, not just CPU cycles. Every distance computation required reading 512 bytes of float32 data.
We implemented per-dimension scalar quantization, compressing the entire dataset from 32-bit floats down to 8-bit integers. This immediately gave us a 4x memory reduction. 
During search, we compute distances asymmetrically—we compare the exact float32 query against the quantized uint8 dataset. To ensure we don't lose accuracy, we maintain a small float32 cache and perform an exact re-ranking step on the final top-L candidates. As you'll see in our results, this gave us massive speedups with virtually zero recall degradation.

**(Slide 6: Opts B, C, D)**
**Person B:** Beyond quantization, we layered three more search-time optimizations.
First, Early Abandoning. When computing the distance between two 128-dimensional vectors, we check the partial sum every 16 dimensions. Since squared distances only grow, if the partial sum exceeds our worst candidate in the beam, we immediately abort the computation. This gave us a 3 to 6% free speedup and aligns perfectly with AVX register boundaries.
Second, Dynamic Beam Width. A fixed beam width *L* wastes time on easy queries and struggles on hard ones. We made our beam adaptive. If the search is rapidly finding better candidates, we shrink the active beam to save time. If it stalls, we expand it.
Finally, Data Structure Flattening. We completely ripped out the standard library `std::set` which uses red-black trees and causes heap allocations. We replaced it with a contiguous sorted `std::vector` and O(1) bitsets for visited tracking. This eliminated pointer chasing and vastly improved CPU cache locality.
Now, [Person C] will discuss how we improved the graph construction itself and present our final results.

---

## Part 3: Build Improvements & Results (Person C)
**Time allocation: ~3 minutes (Slides 7-15)**

**(Slide 7: Build Improvements)**
**Person C:** Thanks, [Person B]. A fast search algorithm is useless on a poorly constructed graph. We implemented four key build improvements to ensure our graph topology was highly navigable.
First, instead of starting searches from a random node, we compute the dataset's centroid and start from the closest real point—the medoid. This simple geometric fix slashed our worst-case P99 latency by 60%.
Second, we doubled the graph's maximum degree from R=32 to R=64. While this doubled build time, it dramatically increased edge diversity, allowing the search to converge in far fewer hops. 
We also fed the *entire* visited set into the pruning algorithm rather than just the top candidates, giving it better choices for long-range edges, and bootstrapped the graph with random connectivity to help early insertions.

**(Slide 8: Hero Plot - Recall vs Latency)**
**Person C:** Let's look at the results. This Pareto frontier plot is the culmination of our work. The X-axis is recall, and the Y-axis is latency. Lower and further right is better.
You can clearly see that our Quantized ADC implementation (the green line) strictly dominates the exact float32 baseline (the blue line) across all target recalls.

**(Slide 9: Latency Speedup Bars)**
**Person C:** Looking closer at specific beam widths, quantization saves us between 15% and 25% in average latency while hitting the exact same recall targets. 

**(Slide 10: R=32 vs R=64)**
**Person C:** The real power comes when we combine quantization with the denser R=64 graph. This is the key insight of our project: To hit a recall of 0.98, the baseline R=32 graph needed a beam width of 75. Our R=64 graph hits that same recall with a beam width of just 20, resulting in significantly lower latency. Furthermore, R=64 pushes our peak recall ceiling to an incredible 0.9992.

**(Slide 11: P99 Tail Latency)**
**Person C:** And because of our medoid initialization and denser graph, the worst-case queries—the P99 tail latency—saw massive reductions, making the system much more stable and predictable.

**(Slide 12: Key Numbers)**
**Person C:** To summarize our metrics:
*   We reduced the memory footprint by 75%, from 488 Megabytes down to 122 Megabytes.
*   We achieved a peak Recall@10 of 0.9992.
*   We saved roughly 17% latency using quantization.
*   The trade-off was a 2x increase in index build time, but this is a one-time offline cost.

**(Slide 13: Negative Results)**
**Person C:** We also want to briefly mention what *didn't* work. We tried using k-means to create multiple entry points and attempted PCA traversal. Both failed to yield benefits, largely because the SIFT1M dataset has very high intrinsic dimensionality and lacks clear, isolated clusters.

**(Slide 14 & 15: Conclusion & Q&A)**
**Person C:** In conclusion, by combining algorithmic insights—like medoid initialization and dynamic beams—with systems engineering—like memory flattening and SIMD-friendly quantization—we successfully transformed a baseline graph algorithm into a highly efficient, production-grade ANN system. All seven of our optimizations compose cleanly together to achieve our goals.

Thank you for your time and guidance this semester. We would now be happy to answer any questions.
