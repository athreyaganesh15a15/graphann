# Vamana Algorithm: Code Understanding

## Algorithm Overview

The Vamana algorithm is a **navigable small-world graph** construction and search method for approximate nearest neighbor (ANN) search. It's a heuristic approximation of a **Relative Neighborhood Graph (RNG)** with pruning and navigability constraints.

### Core Concept: RNG (Relative Neighborhood Graph)

An RNG is a graph where two points p and q are connected if and only if there is no point r that is closer to both p and q than they are to each other. Formally:

```
Connect(p, q) ⟺ ¬∃r: dist(p, r) < dist(p, q) AND dist(q, r) < dist(p, q)
```

The RNG has excellent **navigability** — you can always reach the nearest neighbor through greedy hops. However, computing exact RNGs is expensive.

## Vamana: Approximating RNG with Efficiency

### 1. **Greedy Search (Part 1: Finding Candidates)**

For each query, start from a fixed node and greedily follow edges to closer neighbors:

```
candidates ← {start_node}
visited ← {start_node}
while candidates with unexpanded nodes remain:
    best ← closest unexpanded candidate
    mark best as expanded
    for each neighbor of best:
        if neighbor not visited:
            add to candidates (keep top L by distance)
```

**Why it works**: The graph structure ensures that local greedy traversal can find good neighbors without exploring the entire dataset.

### 2. **Robust Prune / Alpha-RNG Rule (Part 2: Enforcing Diversity)**

When inserting a point, we don't add all nearby points as edges. Instead, we prune candidates to maintain **diversity** using the alpha-RNG rule:

A candidate `c` is kept as a neighbor of point `p` if:
```
∀ already_selected neighbors n:
    dist(p, c) ≤ α · dist(c, n)
```

**Why this works**:
- **α = 1.0** (strict RNG): Only add edges that satisfy the RNG property — maximizes diversity but may disconnect the graph
- **α > 1.0** (relaxed RNG): Allow some "shortcuts" — trade diversity for graph connectivity
- Typical **α = 1.2** balances both

**Intuition**: If candidate `c` is much closer to an already-selected neighbor `n` than to the current point `p`, then `c` is redundant — adding the edge to `p` doesn't help navigability.

### 3. **Parallel Insertion with Degree Constraints**

Points are inserted in random order. For each point:
1. Search the current graph to find candidates (greedy_search)
2. Prune candidates using alpha-RNG to get final neighbors (robust_prune)
3. Add forward edges: `p → neighbors`
4. Add backward edges: `neighbors → p`
5. If any neighbor's degree exceeds `γ·R`, prune its neighborhood

**Why random order + degree constraints**:
- Random order prevents bias from insertion order
- Degree constraints (max out-degree = R) keep memory bounded and search efficient
- γ > 1 allows temporary degree growth, then prunes to remove poor edges

## How It's a Heuristic Approximation

| Aspect | Exact RNG | Vamana |
|--------|-----------|--------|
| **Definition** | Connect if no closer point exists | Connect if alpha-RNG rule satisfied |
| **Guarantees** | Guaranteed connectivity + optimality | Navigable in practice (empirical) |
| **Cost** | O(n²) or O(n log n) approximate | O(n·R·L) with parallel insertion |
| **Edges** | No degree constraint | Max out-degree R |
| **Flexibility** | None | α, R, L, γ parameters |

### Key Trade-offs

1. **Alpha parameter (α)**
   - **Small α** → Strict RNG, more diverse edges, sparser graph
   - **Large α** → Relaxed RNG, more shortcuts, denser graph

2. **Search width (L) during build**
   - **Small L** → Faster build, limited candidate pool, weaker graph
   - **Large L** → Slower build, better candidates, stronger graph

3. **Out-degree (R)**
   - **Small R** → Faster search, lower memory, may need more hops
   - **Large R** → Slower search, higher memory, fewer hops

## Why Greedy Search Works on Vamana

The alpha-RNG property ensures **local optimality**:
- If you're at node `p` and want to reach query `q`, the neighbors of `p` form a diverse set
- At least one neighbor should be closer to `q` than `p` is (unless `p` is already a local minimum)
- Greedy hops eventually reach a local minimum that's often close to global nearest neighbor

## Summary: The Heuristic Nature

Vamana is a **heuristic** because:
1. It doesn't guarantee finding the exact nearest neighbor (it finds approximate)
2. It uses parameter tuning (α, R, L, γ) rather than theoretical guarantees
3. It trades completeness for speed — achieves practical ~99% recall with 10-100x speedup

Yet it's **effective** because:
1. RNG structure with greedy search provides navigability
2. Alpha-RNG pruning maintains good connectivity while keeping graphs sparse
3. Empirical studies show excellent recall-vs-latency trade-offs on real datasets
