#!/usr/bin/env bash
#
# Downloads SIFT1M, converts to fbin/ibin, builds Vamana indices, and runs search.
# Builds both a baseline (global alpha) and an optimized (adaptive alpha) index
# for comparison.
#
# Usage: ./scripts/run_sift1m.sh
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DATA_DIR="$ROOT/tmp"
BUILD_DIR="$ROOT/build"
SIFT_DIR="$DATA_DIR/sift"

SIFT_URL="ftp://ftp.irisa.fr/local/texmex/corpus/sift.tar.gz"
SIFT_TAR="$DATA_DIR/sift.tar.gz"

# Output binary files
BASE_FBIN="$DATA_DIR/sift_base.fbin"
QUERY_FBIN="$DATA_DIR/sift_query.fbin"
GT_IBIN="$DATA_DIR/sift_gt.ibin"
INDEX_BIN="$DATA_DIR/sift_index.bin"
INDEX_ADAPTIVE_BIN="$DATA_DIR/sift_index_adaptive.bin"

# ─── 1. Build the project ────────────────────────────────────────────────────
echo "=== Step 1: Building the project ==="
mkdir -p "$BUILD_DIR"
pushd "$BUILD_DIR" > /dev/null
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j"$(nproc)"
popd > /dev/null
echo ""

# ─── 2. Download SIFT1M ──────────────────────────────────────────────────────
echo "=== Step 2: Downloading SIFT1M ==="
mkdir -p "$DATA_DIR"
if [ -d "$SIFT_DIR" ] && \
   [ -f "$SIFT_DIR/sift_base.fvecs" ] && \
   [ -f "$SIFT_DIR/sift_query.fvecs" ] && \
   [ -f "$SIFT_DIR/sift_groundtruth.ivecs" ]; then
    echo "SIFT1M already downloaded, skipping."
else
    echo "Downloading from $SIFT_URL ..."
    curl -o "$SIFT_TAR" "$SIFT_URL"
    echo "Extracting..."
    tar -xzf "$SIFT_TAR" -C "$DATA_DIR"
    rm -f "$SIFT_TAR"
fi
echo ""

# ─── 3. Convert to fbin / ibin ───────────────────────────────────────────────
echo "=== Step 3: Converting to fbin/ibin ==="
if [ -f "$BASE_FBIN" ] && [ -f "$QUERY_FBIN" ] && [ -f "$GT_IBIN" ]; then
    echo "Binary files already exist, skipping conversion."
else
    python3 "$ROOT/scripts/convert_vecs.py" "$SIFT_DIR/sift_base.fvecs"        "$BASE_FBIN"
    python3 "$ROOT/scripts/convert_vecs.py" "$SIFT_DIR/sift_query.fvecs"       "$QUERY_FBIN"
    python3 "$ROOT/scripts/convert_vecs.py" "$SIFT_DIR/sift_groundtruth.ivecs" "$GT_IBIN"
fi
echo ""

# ─── 4a. Build BASELINE index (global alpha, medoid start node) ──────────────
echo "=== Step 4a: Building BASELINE Vamana index ==="
"$BUILD_DIR/build_index" \
    --data "$BASE_FBIN" \
    --output "$INDEX_BIN" \
    --R 32 --L 75 --alpha 1.2 --gamma 1.5 \
    --density_spread 0.0
echo ""

# ─── 4b. Build ADAPTIVE index (density-aware alpha, two-pass) ────────────────
echo "=== Step 4b: Building ADAPTIVE Vamana index ==="
"$BUILD_DIR/build_index" \
    --data "$BASE_FBIN" \
    --output "$INDEX_ADAPTIVE_BIN" \
    --R 32 --L 75 --alpha 1.2 --gamma 1.5 \
    --density_spread 0.4
echo ""

# ─── 5a. Search BASELINE ─────────────────────────────────────────────────────
echo "=== Step 5a: Searching BASELINE index ==="
"$BUILD_DIR/search_index" \
    --index "$INDEX_BIN" \
    --data "$BASE_FBIN" \
    --queries "$QUERY_FBIN" \
    --gt "$GT_IBIN" \
    --K 10 \
    --L 10,20,30,50,75,100,150,200
echo ""

# ─── 5b. Search ADAPTIVE ─────────────────────────────────────────────────────
echo "=== Step 5b: Searching ADAPTIVE index ==="
"$BUILD_DIR/search_index" \
    --index "$INDEX_ADAPTIVE_BIN" \
    --data "$BASE_FBIN" \
    --queries "$QUERY_FBIN" \
    --gt "$GT_IBIN" \
    --K 10 \
    --L 10,20,30,50,75,100,150,200
echo ""

echo "=== Done! ==="
