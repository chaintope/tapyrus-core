#!/usr/bin/env python3
# Copyright (c) 2025 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Benchmark trend plotting script for Tapyrus Core CI
"""

import json
import os
import sys
import re
import glob
from pathlib import Path
from typing import Dict, List, Optional, Tuple
import argparse
from datetime import datetime, timedelta
import statistics

try:
    import matplotlib.pyplot as plt
    import matplotlib.dates as mdates
    from matplotlib.ticker import FuncFormatter
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False
    print("Warning: matplotlib not available, will generate data only")

class BenchmarkTrendAnalyzer:
    def __init__(self, data_dir: str, output_dir: str):
        self.data_dir = Path(data_dir)
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)
        
    def load_benchmark_data(self, platform_filter: Optional[str] = None, 
                           arch_filter: Optional[str] = None,
                           days_back: int = 30) -> List[Dict]:
        """Load benchmark data files"""
        pattern = "bench_data_*.json"
        if platform_filter and arch_filter:
            pattern = f"bench_data_{platform_filter}_{arch_filter}_*.json"
        elif platform_filter:
            pattern = f"bench_data_{platform_filter}_*_*.json"
            
        data_files = list(self.data_dir.glob(pattern))
        data_files.sort()
        
        cutoff_date = datetime.now() - timedelta(days=days_back)
        
        loaded_data = []
        for file_path in data_files:
            try:
                with open(file_path, 'r', encoding='utf8') as f:
                    data = json.load(f)
                    
                # Filter by date if needed
                try:
                    data_date = datetime.fromisoformat(data["timestamp"].replace('Z', '+00:00'))
                    if data_date < cutoff_date:
                        continue
                except:
                    pass  # Include data with unparseable dates
                    
                loaded_data.append(data)
                
            except Exception as e:
                print(f"Warning: Could not load {file_path}: {e}")
                
        print(f"Loaded {len(loaded_data)} benchmark data files")
        return loaded_data
    
    def extract_time_series(self, data_list: List[Dict], benchmark_name: str) -> Tuple[List[datetime], List[float]]:
        """Extract time series data for a specific benchmark"""
        timestamps = []
        values = []
        
        for data in data_list:
            if benchmark_name in data.get("benchmarks", {}):
                try:
                    timestamp = datetime.fromisoformat(data["timestamp"].replace('Z', '+00:00'))
                    value = data["benchmarks"][benchmark_name]["time_ns"]
                    timestamps.append(timestamp)
                    values.append(value)
                except Exception as e:
                    print(f"Warning: Could not parse data point: {e}")
                    continue
                    
        # Sort by timestamp
        paired_data = list(zip(timestamps, values))
        paired_data.sort(key=lambda x: x[0])
        
        if paired_data:
            timestamps, values = zip(*paired_data)
            return list(timestamps), list(values)
        else:
            return [], []
    
    def get_common_benchmarks(self, data_list: List[Dict], min_occurrences: int = 3) -> List[str]:
        """Get benchmarks that appear in multiple data points"""
        benchmark_counts = {}
        
        for data in data_list:
            for benchmark_name in data.get("benchmarks", {}):
                benchmark_counts[benchmark_name] = benchmark_counts.get(benchmark_name, 0) + 1
                
        common_benchmarks = [
            name for name, count in benchmark_counts.items() 
            if count >= min_occurrences
        ]
        
        # Sort by name for consistency
        common_benchmarks.sort()
        return common_benchmarks
    
    def calculate_trend_stats(self, values: List[float]) -> Dict:
        """Calculate trend statistics"""
        if len(values) < 2:
            return {"error": "Not enough data points"}
            
        recent_values = values[-5:] if len(values) >= 5 else values
        older_values = values[:-5] if len(values) >= 10 else values[:-len(recent_values)//2] if len(values) >= 4 else []
        
        stats = {
            "count": len(values),
            "latest": values[-1],
            "min": min(values),
            "max": max(values),
            "mean": statistics.mean(values),
            "median": statistics.median(values),
        }
        
        if len(values) > 1:
            stats["std_dev"] = statistics.stdev(values)
            
        # Trend analysis
        if len(recent_values) >= 2 and len(older_values) >= 2:
            recent_mean = statistics.mean(recent_values)
            older_mean = statistics.mean(older_values)
            change_pct = ((recent_mean - older_mean) / older_mean) * 100
            stats["trend_change_pct"] = change_pct
            
            if abs(change_pct) > 5:
                stats["trend"] = "improving" if change_pct < 0 else "regressing"
            else:
                stats["trend"] = "stable"
        else:
            stats["trend"] = "unknown"
            
        return stats
    
    def plot_benchmark_trends(self, data_list: List[Dict], platform: str, arch: str, 
                             benchmark_names: Optional[List[str]] = None,
                             max_plots: int = 12) -> List[str]:
        """Generate trend plots for benchmarks"""
        if not HAS_MATPLOTLIB:
            print("matplotlib not available, skipping plot generation")
            return []
            
        if not benchmark_names:
            benchmark_names = self.get_common_benchmarks(data_list)
            
        # Limit number of plots
        benchmark_names = benchmark_names[:max_plots]
        
        if not benchmark_names:
            print("No common benchmarks found for plotting")
            return []
            
        # Create subplots
        n_plots = len(benchmark_names)
        n_cols = min(3, n_plots)
        n_rows = (n_plots + n_cols - 1) // n_cols
        
        fig, axes = plt.subplots(n_rows, n_cols, figsize=(5*n_cols, 4*n_rows))
        if n_plots == 1:
            axes = [axes]
        elif n_rows == 1:
            axes = axes.reshape(1, -1)
        
        fig.suptitle(f'Benchmark Trends - {platform} {arch}', fontsize=16, fontweight='bold')
        
        plot_files = []
        
        for i, benchmark_name in enumerate(benchmark_names):
            row = i // n_cols
            col = i % n_cols
            ax = axes[row, col] if n_rows > 1 else axes[col]
            
            timestamps, values = self.extract_time_series(data_list, benchmark_name)
            
            if not timestamps:
                ax.text(0.5, 0.5, 'No data', ha='center', va='center', transform=ax.transAxes)
                ax.set_title(benchmark_name, fontsize=10)
                continue
                
            # Convert nanoseconds to appropriate unit
            if max(values) > 1e9:  # > 1 second
                values = [v / 1e9 for v in values]
                unit = 's'
            elif max(values) > 1e6:  # > 1 millisecond
                values = [v / 1e6 for v in values]
                unit = 'ms'
            elif max(values) > 1e3:  # > 1 microsecond
                values = [v / 1e3 for v in values]
                unit = 'μs'
            else:
                unit = 'ns'
                
            # Plot data
            ax.plot(timestamps, values, 'o-', linewidth=2, markersize=4)
            
            # Calculate and show trend
            stats = self.calculate_trend_stats([v * (1e9 if unit == 's' else 1e6 if unit == 'ms' else 1e3 if unit == 'μs' else 1) for v in values])
            
            trend_color = {'improving': 'green', 'regressing': 'red', 'stable': 'blue', 'unknown': 'gray'}
            trend_text = stats.get('trend', 'unknown')
            if 'trend_change_pct' in stats:
                trend_text += f" ({stats['trend_change_pct']:+.1f}%)"
                
            ax.set_title(f"{benchmark_name}\n{trend_text}", 
                        fontsize=9, color=trend_color.get(stats.get('trend', 'unknown'), 'black'))
            ax.set_ylabel(f'Time ({unit})')
            
            # Format x-axis
            if len(timestamps) > 1:
                ax.xaxis.set_major_formatter(mdates.DateFormatter('%m/%d'))
                ax.xaxis.set_major_locator(mdates.DayLocator(interval=max(1, len(timestamps)//5)))
                plt.setp(ax.xaxis.get_majorticklabels(), rotation=45)
            
            ax.grid(True, alpha=0.3)
            
        # Hide empty subplots
        for i in range(n_plots, n_rows * n_cols):
            row = i // n_cols
            col = i % n_cols
            axes[row, col].set_visible(False)
            
        plt.tight_layout()
        
        # Save plot
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        plot_file = self.output_dir / f"benchmark_trends_{platform}_{arch}_{timestamp}.png"
        plt.savefig(plot_file, dpi=150, bbox_inches='tight')
        plt.close()
        
        plot_files.append(str(plot_file))
        print(f"Trend plot saved to {plot_file}")
        
        return plot_files
    
    def generate_summary_report(self, data_list: List[Dict], platform: str, arch: str) -> str:
        """Generate a text summary report"""
        if not data_list:
            return "No benchmark data available"
            
        benchmark_names = self.get_common_benchmarks(data_list)
        
        report_lines = [
            f"Benchmark Trend Summary - {platform} {arch}",
            "=" * 50,
            f"Data points: {len(data_list)}",
            f"Date range: {data_list[0]['timestamp'][:10]} to {data_list[-1]['timestamp'][:10]}",
            f"Common benchmarks: {len(benchmark_names)}",
            "",
            "Benchmark Analysis:",
            "-" * 20
        ]
        
        for benchmark_name in benchmark_names[:20]:  # Limit to top 20
            timestamps, values = self.extract_time_series(data_list, benchmark_name)
            if not values:
                continue
                
            stats = self.calculate_trend_stats(values)
            
            # Format time value
            latest_val = stats['latest']
            if latest_val > 1e9:
                latest_str = f"{latest_val/1e9:.3f}s"
            elif latest_val > 1e6:
                latest_str = f"{latest_val/1e6:.3f}ms"
            elif latest_val > 1e3:
                latest_str = f"{latest_val/1e3:.3f}μs"
            else:
                latest_str = f"{latest_val:.0f}ns"
                
            trend_str = stats.get('trend', 'unknown')
            if 'trend_change_pct' in stats:
                trend_str += f" ({stats['trend_change_pct']:+.1f}%)"
                
            report_lines.append(f"{benchmark_name:40} {latest_str:>10} {trend_str}")
            
        # Save report
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        report_file = self.output_dir / f"benchmark_summary_{platform}_{arch}_{timestamp}.txt"
        
        with open(report_file, 'w', encoding='utf8') as f:
            f.write('\n'.join(report_lines))
            
        print(f"Summary report saved to {report_file}")
        return str(report_file)

def main():
    parser = argparse.ArgumentParser(description='Generate Tapyrus Core benchmark trend plots')
    parser.add_argument('--data-dir', required=True, help='Directory containing benchmark JSON files')
    parser.add_argument('--output-dir', required=True, help='Output directory for plots and reports')
    parser.add_argument('--platform', help='Platform filter (e.g., Linux, Darwin)')
    parser.add_argument('--arch', help='Architecture filter (e.g., x86_64, arm64)')
    parser.add_argument('--days-back', type=int, default=30, help='Number of days of data to include')
    parser.add_argument('--max-plots', type=int, default=12, help='Maximum number of benchmark plots')
    
    args = parser.parse_args()
    
    analyzer = BenchmarkTrendAnalyzer(args.data_dir, args.output_dir)
    
    # Load data
    data_list = analyzer.load_benchmark_data(
        platform_filter=args.platform,
        arch_filter=args.arch,
        days_back=args.days_back
    )
    
    if not data_list:
        print("No benchmark data found")
        sys.exit(1)
        
    platform = args.platform or data_list[0]["platform"]["os"]
    arch = args.arch or data_list[0]["platform"]["arch"]
    
    # Generate plots
    plot_files = analyzer.plot_benchmark_trends(
        data_list, platform, arch, max_plots=args.max_plots
    )
    
    # Generate summary report
    report_file = analyzer.generate_summary_report(data_list, platform, arch)
    
    print(f"\nGenerated files:")
    for plot_file in plot_files:
        print(f"  Plot: {plot_file}")
    print(f"  Report: {report_file}")

if __name__ == "__main__":
    main()