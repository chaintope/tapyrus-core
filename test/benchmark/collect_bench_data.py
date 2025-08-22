#!/usr/bin/env python3
# Copyright (c) 2025 Chaintope Inc.
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.

"""
Benchmark data collection and analysis script for Tapyrus Core CI
"""

import json
import os
import sys
import re
import subprocess
import datetime
from pathlib import Path
from typing import Dict, List, Optional, Tuple
import argparse

class BenchmarkCollector:
    def __init__(self, build_dir: str, output_dir: str):
        self.build_dir = Path(build_dir)
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)
        
    def run_benchmark(self, min_time: int = 5000) -> Optional[str]:
        """Run benchmark and return output file path"""
        bench_exe = self.build_dir / "bin" / "bench_tapyrus"
        if not bench_exe.exists():
            print(f"Error: Benchmark executable not found at {bench_exe}")
            return None
            
        timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        output_file = self.output_dir / f"bench_raw_{timestamp}.txt"
        
        try:
            print(f"Running benchmark with min-time={min_time}ms...")
            with open(output_file, 'w', encoding='utf8') as f:
                result = subprocess.run([
                    str(bench_exe),
                    f"-min-time={min_time}",
                    "-output-csv"
                ], stdout=f, stderr=subprocess.PIPE, text=True, timeout=1800)  # 30 min timeout
                
            if result.returncode != 0:
                print(f"Benchmark failed with return code {result.returncode}")
                print(f"stderr: {result.stderr}")
                return None
                
            print(f"Benchmark completed successfully, output saved to {output_file}")
            return str(output_file)
            
        except subprocess.TimeoutExpired:
            print("Benchmark timed out after 30 minutes")
            return None
        except Exception as e:
            print(f"Error running benchmark: {e}")
            return None
    
    def parse_benchmark_output(self, output_file: str) -> Dict:
        """Parse benchmark output and extract metrics"""
        results = {
            "timestamp": datetime.datetime.now().isoformat(),
            "platform": self._get_platform_info(),
            "git_info": self._get_git_info(),
            "benchmarks": {}
        }
        
        try:
            with open(output_file, 'r', encoding='utf8') as f:
                content = f.read()
                
            # Parse CSV-style output from bench_tapyrus
            lines = content.strip().split('\n')
            for line in lines:
                if ',' in line and not line.startswith('#'):
                    parts = line.split(',')
                    if len(parts) >= 3:
                        benchmark_name = parts[0].strip().strip('"')
                        try:
                            # Parse timing information (assuming ns/op format)
                            time_str = parts[1].strip().strip('"')
                            if 'ns/op' in time_str:
                                time_value = float(re.findall(r'[\d.]+', time_str)[0])
                                results["benchmarks"][benchmark_name] = {
                                    "time_ns": time_value,
                                    "raw_value": time_str
                                }
                            elif any(unit in time_str for unit in ['us/op', 'ms/op', 's/op']):
                                # Convert to nanoseconds for consistency
                                time_value = float(re.findall(r'[\d.]+', time_str)[0])
                                if 'us/op' in time_str:
                                    time_value *= 1000  # us to ns
                                elif 'ms/op' in time_str:
                                    time_value *= 1000000  # ms to ns
                                elif 's/op' in time_str:
                                    time_value *= 1000000000  # s to ns
                                    
                                results["benchmarks"][benchmark_name] = {
                                    "time_ns": time_value,
                                    "raw_value": time_str
                                }
                        except (ValueError, IndexError):
                            # Skip lines we can't parse
                            continue
                            
        except Exception as e:
            print(f"Error parsing benchmark output: {e}")
            
        return results
    
    def save_structured_data(self, data: Dict) -> str:
        """Save benchmark data in structured JSON format"""
        timestamp = data["timestamp"].replace(":", "-").replace(".", "-")
        platform = data["platform"]["os"]
        arch = data["platform"]["arch"]
        
        filename = f"bench_data_{platform}_{arch}_{timestamp}.json"
        output_file = self.output_dir / filename
        
        with open(output_file, 'w', encoding='utf8') as f:
            json.dump(data, f, indent=2)
            
        print(f"Structured benchmark data saved to {output_file}")
        return str(output_file)
    
    def _get_platform_info(self) -> Dict:
        """Get platform information"""
        import platform
        return {
            "os": platform.system(),
            "arch": platform.machine(),
            "python_version": platform.python_version(),
            "node": platform.node()
        }
    
    def _get_git_info(self) -> Dict:
        """Get git repository information"""
        try:
            commit_hash = subprocess.check_output(
                ["git", "rev-parse", "HEAD"], 
                text=True,
                cwd=self.build_dir.parent
            ).strip()
            
            commit_message = subprocess.check_output(
                ["git", "log", "-1", "--pretty=%s"],
                text=True,
                cwd=self.build_dir.parent
            ).strip()
            
            branch = subprocess.check_output(
                ["git", "rev-parse", "--abbrev-ref", "HEAD"],
                text=True,
                cwd=self.build_dir.parent
            ).strip()
            
            return {
                "commit_hash": commit_hash,
                "commit_message": commit_message,
                "branch": branch
            }
        except Exception as e:
            print(f"Warning: Could not get git info: {e}")
            return {
                "commit_hash": "unknown",
                "commit_message": "unknown",
                "branch": "unknown"
            }

def main():
    parser = argparse.ArgumentParser(description='Collect Tapyrus Core benchmark data')
    parser.add_argument('--build-dir', required=True, help='Build directory containing bench_tapyrus')
    parser.add_argument('--output-dir', required=True, help='Output directory for benchmark data')
    parser.add_argument('--min-time', type=int, default=5000, help='Minimum time per benchmark in ms')
    parser.add_argument('--skip-run', action='store_true', help='Skip running benchmark, only parse existing output')
    parser.add_argument('--input-file', help='Input file to parse (when using --skip-run)')
    
    args = parser.parse_args()
    
    collector = BenchmarkCollector(args.build_dir, args.output_dir)
    
    if args.skip_run:
        if not args.input_file:
            print("Error: --input-file required when using --skip-run")
            sys.exit(1)
        output_file = args.input_file
    else:
        output_file = collector.run_benchmark(args.min_time)
        if not output_file:
            print("Failed to run benchmark")
            sys.exit(1)
    
    # Parse and save structured data
    data = collector.parse_benchmark_output(output_file)
    
    if not data["benchmarks"]:
        print("Warning: No benchmark data found in output")
        sys.exit(1)
    
    structured_file = collector.save_structured_data(data)
    
    print(f"\nBenchmark summary:")
    print(f"Platform: {data['platform']['os']} {data['platform']['arch']}")
    print(f"Git commit: {data['git_info']['commit_hash'][:8]}")
    print(f"Benchmarks collected: {len(data['benchmarks'])}")
    
    # Print top 5 slowest benchmarks
    if data["benchmarks"]:
        sorted_benchmarks = sorted(
            data["benchmarks"].items(),
            key=lambda x: x[1]["time_ns"],
            reverse=True
        )
        print("\nTop 5 slowest benchmarks:")
        for name, results in sorted_benchmarks[:5]:
            print(f"  {name}: {results['raw_value']}")
    
    print(f"\nStructured data saved to: {structured_file}")

if __name__ == "__main__":
    main()