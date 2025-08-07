# Tapyrus Core Benchmark Analysis

This directory contains tools for collecting and analyzing Tapyrus Core benchmark performance data.

## Overview

The benchmark analysis system consists of three main components:

1. **Data Collection** (`collect_bench_data.py`) - Runs benchmarks and saves structured data
2. **Trend Analysis** (`plot_bench_trends.py`) - Generates trend plots from historical data  
3. **Automation Script** (`run_benchmark_analysis.sh`) - Orchestrates the full process

## Automated CI Integration

The daily CI test automatically collects benchmark data and generates trend plots for:
- **Linux x86_64** - All daily test configurations
- **macOS ARM64** - All daily test configurations

Results are uploaded as artifacts with 90-day retention and available in the GitHub Actions interface.

## Manual Usage

### Quick Start

To run a complete benchmark analysis:

```bash
# Run benchmarks and generate trends
./test/benchmark/run_benchmark_analysis.sh --build-dir build --platform Linux --arch x86_64

# Or just generate trends from existing data
./test/benchmark/run_benchmark_analysis.sh --skip-collection --platform Darwin --arch arm64
```

### Individual Components

#### 1. Collect Benchmark Data

```bash
python3 test/benchmark/collect_bench_data.py \
  --build-dir build \
  --output-dir benchmark_results/data \
  --min-time 10000
```

#### 2. Generate Trend Plots

```bash
python3 test/benchmark/plot_bench_trends.py \
  --data-dir benchmark_results/data \
  --output-dir benchmark_results/plots \
  --platform Linux \
  --arch x86_64 \
  --days-back 30
```

## Output Files

### Data Files
- `benchmark_results/data/bench_data_*.json` - Structured benchmark data
- Each file contains platform info, git commit details, and benchmark timing data

### Analysis Files
- `benchmark_results/plots/benchmark_trends_*.png` - Trend plots showing performance over time
- `benchmark_results/plots/benchmark_summary_*.txt` - Text summary with key statistics

## Understanding the Results

### Trend Analysis
- **Green**: Performance improving (faster execution)
- **Red**: Performance regressing (slower execution)
- **Blue**: Performance stable (< 5% change)
- **Gray**: Insufficient data for trend analysis

### Key Metrics
- **Time per operation**: Lower is better
- **Trend percentage**: Positive = slower, Negative = faster
- **Standard deviation**: Lower indicates more consistent performance

## Requirements

### Python Dependencies
```bash
pip3 install matplotlib  # For plotting (automatically installed in CI)
```

### System Requirements
- Built Tapyrus Core with bench_tapyrus executable
- Python 3.6+ 
- Git repository (for commit tracking)

## Configuration

### Benchmark Duration
- **Quick**: `--min-time 1000` (1 second per benchmark)
- **Standard**: `--min-time 5000` (5 seconds per benchmark) 
- **Comprehensive**: `--min-time 10000` (10 seconds per benchmark) - Used in CI

### Historical Data
- **Default**: 30 days of historical data
- **Extended**: Use `--days-back 90` for longer trends
- Data is preserved via GitHub Actions cache

## Troubleshooting

### Common Issues

1. **No benchmark executable found**
   ```bash
   # Ensure you've built the project
   cmake --build build --target all
   ls build/bin/bench_tapyrus  # Should exist
   ```

2. **Matplotlib import error**
   ```bash
   pip3 install matplotlib --user
   ```

3. **No historical data for trends**
   - Trends require at least 3 data points
   - Run collection multiple times or check cache

4. **Permission errors**
   ```bash
   chmod +x test/benchmark/run_benchmark_analysis.sh
   ```

### CI Integration Issues

If benchmark collection fails in CI:
- Check the "Upload benchmark analysis results" artifacts
- Review the workflow logs for specific error messages
- Ensure the target platforms (Linux x86_64, macOS ARM64) are running

## Contributing

When adding new benchmarks:
1. Ensure they output in CSV format compatible with the collector
2. Test collection manually before CI integration
3. Consider performance impact (longer benchmarks = longer CI times)

For questions or issues with the benchmark analysis system, please check the workflow logs or create an issue.