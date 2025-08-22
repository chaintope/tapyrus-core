#!/bin/bash
# Copyright (c) 2025 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

# Benchmark analysis runner script for Tapyrus Core CI

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

# Default values
BUILD_DIR=""
OUTPUT_DIR="$PROJECT_ROOT/benchmark_results"
MIN_TIME=5000
PLATFORM=""
ARCH=""
SKIP_COLLECTION=false
SKIP_PLOTTING=false
DAYS_BACK=30

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --build-dir DIR      Build directory containing bench_tapyrus (required unless --skip-collection)"
    echo "  --output-dir DIR     Output directory for results (default: benchmark_results/)"
    echo "  --min-time MS        Minimum time per benchmark in ms (default: 5000)"
    echo "  --platform NAME      Platform name for filtering/labeling"
    echo "  --arch NAME          Architecture name for filtering/labeling"
    echo "  --skip-collection    Skip benchmark collection, only do plotting"
    echo "  --skip-plotting      Skip plotting, only do collection"
    echo "  --days-back N        Number of days of historical data to include (default: 30)"
    echo "  --help               Show this help message"
    echo ""
    echo "Examples:"
    echo "  $0 --build-dir build --platform Linux --arch x86_64"
    echo "  $0 --skip-collection --platform Darwin --arch arm64"
}

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --output-dir)
            OUTPUT_DIR="$2"
            shift 2
            ;;
        --min-time)
            MIN_TIME="$2"
            shift 2
            ;;
        --platform)
            PLATFORM="$2"
            shift 2
            ;;
        --arch)
            ARCH="$2"
            shift 2
            ;;
        --skip-collection)
            SKIP_COLLECTION=true
            shift
            ;;
        --skip-plotting)
            SKIP_PLOTTING=true
            shift
            ;;
        --days-back)
            DAYS_BACK="$2"
            shift 2
            ;;
        --help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            usage
            exit 1
            ;;
    esac
done

# Validate inputs
if [[ "$SKIP_COLLECTION" == "false" && -z "$BUILD_DIR" ]]; then
    echo "Error: --build-dir is required unless --skip-collection is used"
    usage
    exit 1
fi

if [[ "$SKIP_COLLECTION" == "true" && "$SKIP_PLOTTING" == "true" ]]; then
    echo "Error: Cannot skip both collection and plotting"
    exit 1
fi

# Create output directories
DATA_DIR="$OUTPUT_DIR/data"
PLOTS_DIR="$OUTPUT_DIR/plots"
mkdir -p "$DATA_DIR" "$PLOTS_DIR"

# Auto-detect platform and arch if not provided
if [[ -z "$PLATFORM" ]]; then
    case "$(uname -s)" in
        Linux*)     PLATFORM="Linux";;
        Darwin*)    PLATFORM="Darwin";;
        CYGWIN*)    PLATFORM="Windows";;
        MINGW*)     PLATFORM="Windows";;
        *)          PLATFORM="Unknown";;
    esac
fi

if [[ -z "$ARCH" ]]; then
    ARCH="$(uname -m)"
fi

echo "=== Tapyrus Core Benchmark Analysis ==="
echo "Platform: $PLATFORM"
echo "Architecture: $ARCH"
echo "Output directory: $OUTPUT_DIR"
echo ""

# Step 1: Collect benchmark data
if [[ "$SKIP_COLLECTION" == "false" ]]; then
    echo "Step 1: Collecting benchmark data..."
    echo "Build directory: $BUILD_DIR"
    echo "Minimum time per benchmark: ${MIN_TIME}ms"
    
    if ! python3 "$SCRIPT_DIR/collect_bench_data.py" \
        --build-dir "$BUILD_DIR" \
        --output-dir "$DATA_DIR" \
        --min-time "$MIN_TIME"; then
        echo "Error: Benchmark collection failed"
        exit 1
    fi
    echo "✅ Benchmark data collection completed"
    echo ""
else
    echo "Step 1: Skipping benchmark collection (--skip-collection)"
    echo ""
fi

# Step 2: Generate trend plots and analysis
if [[ "$SKIP_PLOTTING" == "false" ]]; then
    echo "Step 2: Generating trend analysis..."
    echo "Platform filter: $PLATFORM"
    echo "Architecture filter: $ARCH"
    echo "Historical data: $DAYS_BACK days"
    
    # Check if we have data files
    DATA_COUNT=$(find "$DATA_DIR" -name "bench_data_*.json" | wc -l)
    if [[ "$DATA_COUNT" -eq 0 ]]; then
        echo "Warning: No benchmark data files found in $DATA_DIR"
        echo "Run without --skip-collection first to collect data"
        exit 1
    fi
    
    echo "Found $DATA_COUNT benchmark data files"
    
    # Install matplotlib if not available (for CI environments)
    if ! python3 -c "import matplotlib" 2>/dev/null; then
        echo "Installing matplotlib for plotting..."
        python3 -m pip install matplotlib --user --quiet || {
            echo "Warning: Could not install matplotlib"
            echo "Plots will be skipped, but text reports will be generated"
        }
    fi
    
    if ! python3 "$SCRIPT_DIR/plot_bench_trends.py" \
        --data-dir "$DATA_DIR" \
        --output-dir "$PLOTS_DIR" \
        --platform "$PLATFORM" \
        --arch "$ARCH" \
        --days-back "$DAYS_BACK"; then
        echo "Error: Trend analysis failed"
        exit 1
    fi
    echo "✅ Trend analysis completed"
    echo ""
else
    echo "Step 2: Skipping trend plotting (--skip-plotting)"
    echo ""
fi

# Summary
echo "=== Summary ==="
echo "Data directory: $DATA_DIR"
echo "Plots directory: $PLOTS_DIR"

if [[ -d "$PLOTS_DIR" ]]; then
    PLOT_COUNT=$(find "$PLOTS_DIR" -name "*.png" | wc -l)
    REPORT_COUNT=$(find "$PLOTS_DIR" -name "*.txt" | wc -l)
    echo "Generated plots: $PLOT_COUNT"
    echo "Generated reports: $REPORT_COUNT"
    
    # Show latest files
    echo ""
    echo "Latest files:"
    find "$PLOTS_DIR" -type f \( -name "*.png" -o -name "*.txt" \) -exec ls -la {} \; | tail -5
fi

echo ""
echo "✅ Benchmark analysis completed successfully!"