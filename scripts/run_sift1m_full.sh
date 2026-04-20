#!/usr/bin/env bash
#
# Runs SIFT1M benchmark with BOTH R=32 (baseline) and R=64 (best) configurations.
# Optionally tests quantized + dynamic beam search on the R=64 index.
#
# Usage:
#   ./scripts/run_sift1m_full.sh           # Run both R=32 and R=64
#   ./scripts/run_sift1m_full.sh --r64     # Run only R=64
#   ./scripts/run_sift1m_full.sh --r32     # Run only R=32
#
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DATA_DIR="$ROOT/tmp"
BUILD_DIR="$ROOT/build"
SIFT_DIR="$DATA_DIR/sift"

SIFT_URL="ftp://ftp.irisa.fr/local/texmex/corpus/sift.tar.gz"
SIFT_TAR="$DATA_DIR/sift.tar.gz"

BASE_FBIN="$DATA_DIR/sift_base.fbin"
QUERY_FBIN="$DATA_DIR/sift_query.fbin"
GT_IBIN="$DATA_DIR/sift_gt.ibin"

# Parse flags
RUN_R32=true
RUN_R64=true
if [[ "${1:-}" == "--r64" ]]; then RUN_R32=false; fi
if [[ "${1:-}" == "--r32" ]]; then RUN_R64=false; fi

L_VALUES="10,20,30,50,75,100,150,200"

# ─── 1. Build ────────────────────────────────────────────────────────────────
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

# ─── 3. Convert ──────────────────────────────────────────────────────────────
echo "=== Step 3: Converting to fbin/ibin ==="
if [ -f "$BASE_FBIN" ] && [ -f "$QUERY_FBIN" ] && [ -f "$GT_IBIN" ]; then
    echo "Binary files already exist, skipping conversion."
else
    python3 "$ROOT/scripts/convert_vecs.py" "$SIFT_DIR/sift_base.fvecs"        "$BASE_FBIN"
    python3 "$ROOT/scripts/convert_vecs.py" "$SIFT_DIR/sift_query.fvecs"       "$QUERY_FBIN"
    python3 "$ROOT/scripts/convert_vecs.py" "$SIFT_DIR/sift_groundtruth.ivecs" "$GT_IBIN"
fi
echo ""

# ─── 4. R=32 Baseline ────────────────────────────────────────────────────────
if $RUN_R32; then
    INDEX_R32="$DATA_DIR/sift_index_r32.bin"

    echo "================================================================"
    echo "  CONFIGURATION: R=32, L=75, alpha=1.2 (Baseline)"
    echo "================================================================"

    echo "=== Building R=32 index ==="
    "$BUILD_DIR/build_index" \
        --data "$BASE_FBIN" \
        --output "$INDEX_R32" \
        --R 32 --L 75 --alpha 1.2 --gamma 1.5
    echo ""

    echo "=== R=32 Exact Float32 Search ==="
    "$BUILD_DIR/search_index" \
        --index "$INDEX_R32" \
        --data "$BASE_FBIN" \
        --queries "$QUERY_FBIN" \
        --gt "$GT_IBIN" \
        --K 10 \
        --L "$L_VALUES"
    echo ""
fi

# ─── 5. R=64 Best Configuration ──────────────────────────────────────────────
if $RUN_R64; then
    INDEX_R64="$DATA_DIR/sift_index_r64.bin"

    echo "================================================================"
    echo "  CONFIGURATION: R=64, L=100, alpha=1.2 (Best)"
    echo "================================================================"

    echo "=== Building R=64 index ==="
    "$BUILD_DIR/build_index" \
        --data "$BASE_FBIN" \
        --output "$INDEX_R64" \
        --R 64 --L 100 --alpha 1.2 --gamma 1.5
    echo ""

    echo "=== R=64 Exact Float32 Search ==="
    "$BUILD_DIR/search_index" \
        --index "$INDEX_R64" \
        --data "$BASE_FBIN" \
        --queries "$QUERY_FBIN" \
        --gt "$GT_IBIN" \
        --K 10 \
        --L "$L_VALUES"
    echo ""

    echo "=== R=64 Quantized ADC Search (Optimization A) ==="
    "$BUILD_DIR/search_index" \
        --index "$INDEX_R64" \
        --data "$BASE_FBIN" \
        --queries "$QUERY_FBIN" \
        --gt "$GT_IBIN" \
        --K 10 \
        --L "$L_VALUES" \
        --quantized
    echo ""

    echo "=== R=64 Quantized + Dynamic Beam (Optimizations A+C) ==="
    "$BUILD_DIR/search_index" \
        --index "$INDEX_R64" \
        --data "$BASE_FBIN" \
        --queries "$QUERY_FBIN" \
        --gt "$GT_IBIN" \
        --K 10 \
        --L "$L_VALUES" \
        --quantized --dynamic
    echo ""
fi

echo "=== All benchmarks complete! ==="
