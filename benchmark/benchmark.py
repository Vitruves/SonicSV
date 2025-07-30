#!/usr/bin/env python3
"""
SonicSV vs libcsv Benchmark Suite

Generates CSV test files of varying sizes and complexity, then benchmarks
both SonicSV and libcsv parsers to compare performance.

Usage:
    python benchmark.py --generate-bench-data --jobs 8
    python benchmark.py --benchmark --jobs 8 --runs 3
    python benchmark.py --generate-bench-data --benchmark --jobs 8 --runs 3
"""

import argparse
import concurrent.futures
import csv
import json
import os
import random
import statistics
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Tuple
import threading


@dataclass
class BenchmarkConfig:
    """Configuration for benchmark test cases"""
    name: str
    size_mb: float
    rows: int
    cols: int
    complexity: str
    quoted_ratio: float
    special_chars: bool
    long_fields: bool


@dataclass
class BenchmarkResult:
    """Result from a single benchmark run"""
    parser: str
    config: BenchmarkConfig
    throughput_mbps: float
    run_time: float
    success: bool
    error: str = ""


class CSVGenerator:
    """Generates CSV test data with various complexity patterns"""
    
    def __init__(self, jobs: int = 4):
        self.jobs = jobs
        self.lock = threading.Lock()
        
    def generate_field(self, complexity: str, quoted: bool = False, long_field: bool = False) -> str:
        """Generate a single CSV field based on complexity"""
        if long_field:
            # Generate long fields (1KB - 10KB)
            length = random.randint(1024, 10240)
            data = ''.join(random.choices('abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ', k=length))
        elif complexity == "simple":
            # Simple numeric or short text
            if random.random() < 0.5:
                data = str(random.randint(1, 1000000))
            else:
                data = ''.join(random.choices('abcdefghijklmnopqrstuvwxyz', k=random.randint(3, 15)))
        elif complexity == "medium":
            # Mixed data with some special characters
            if random.random() < 0.3:
                data = str(random.randint(1, 1000000))
            elif random.random() < 0.6:
                # Text with spaces and punctuation
                words = [''.join(random.choices('abcdefghijklmnopqrstuvwxyz', k=random.randint(3, 10))) 
                        for _ in range(random.randint(1, 4))]
                data = ' '.join(words)
                if random.random() < 0.3:
                    data += random.choice(['.', '!', '?', ',', ';'])
            else:
                # Email-like or URL-like
                data = f"{''.join(random.choices('abcdefghijklmnopqrstuvwxyz', k=random.randint(5, 12)))}@example.com"
        else:  # complex
            # Complex data with quotes, commas, newlines
            base_data = []
            for _ in range(random.randint(1, 5)):
                word = ''.join(random.choices('abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789', k=random.randint(3, 15)))
                base_data.append(word)
            
            data = ' '.join(base_data)
            
            # Add special characters that require quoting
            if random.random() < 0.4:
                data += random.choice([',', '"', '\n', '\r\n'])
            if random.random() < 0.3:
                data = f"Line 1\nLine 2: {data}"
            if random.random() < 0.2:
                data += f' "quoted section" with more text'
                
        # Force quoting for some fields
        if quoted or (complexity == "complex" and random.random() < 0.5):
            # Escape any existing quotes
            data = data.replace('"', '""')
            return f'"{data}"'
        
        return data
    
    def generate_row(self, config: BenchmarkConfig) -> List[str]:
        """Generate a single row based on configuration"""
        row = []
        for col in range(config.cols):
            quoted = random.random() < config.quoted_ratio
            long_field = config.long_fields and random.random() < 0.1  # 10% chance of long fields
            field = self.generate_field(config.complexity, quoted, long_field)
            row.append(field)
        return row
    
    def generate_chunk(self, config: BenchmarkConfig, start_row: int, num_rows: int, chunk_id: int) -> List[List[str]]:
        """Generate a chunk of rows for multithreading"""
        print(f"  Thread {chunk_id}: generating rows {start_row} to {start_row + num_rows - 1}")
        
        # Set different random seed for each thread
        random.seed(42 + chunk_id)
        
        chunk_data = []
        for i in range(num_rows):
            if i % 10000 == 0 and i > 0:
                with self.lock:
                    print(f"  Thread {chunk_id}: {i}/{num_rows} rows generated")
            row = self.generate_row(config)
            chunk_data.append(row)
        
        return chunk_data
    
    def generate_csv_file(self, config: BenchmarkConfig, filepath: Path) -> bool:
        """Generate a CSV file with specified configuration using multithreading"""
        print(f"Generating {config.name} ({config.size_mb:.1f}MB, {config.rows:,} rows, {config.cols} cols)...")
        
        start_time = time.time()
        
        # Calculate rows per thread
        rows_per_thread = max(1, config.rows // self.jobs)
        threads_data = []
        
        # Generate data in parallel
        with concurrent.futures.ThreadPoolExecutor(max_workers=self.jobs) as executor:
            futures = []
            
            for i in range(self.jobs):
                start_row = i * rows_per_thread
                if i == self.jobs - 1:
                    # Last thread handles remaining rows
                    num_rows = config.rows - start_row
                else:
                    num_rows = rows_per_thread
                
                if num_rows > 0:
                    future = executor.submit(self.generate_chunk, config, start_row, num_rows, i)
                    futures.append(future)
            
            # Collect results
            for future in concurrent.futures.as_completed(futures):
                chunk_data = future.result()
                threads_data.extend(chunk_data)
        
        # Write to file
        print(f"  Writing {len(threads_data):,} rows to {filepath}")
        with open(filepath, 'w', newline='', encoding='utf-8') as f:
            writer = csv.writer(f, quoting=csv.QUOTE_MINIMAL)
            
            # Write header
            header = [f"col_{i}" for i in range(config.cols)]
            writer.writerow(header)
            
            # Write data in chunks to avoid memory issues
            chunk_size = 10000
            for i in range(0, len(threads_data), chunk_size):
                chunk = threads_data[i:i + chunk_size]
                writer.writerows(chunk)
                
                if i % 50000 == 0:
                    print(f"  Written {i + len(chunk):,}/{len(threads_data):,} rows")
        
        # Verify file size
        actual_size = filepath.stat().st_size / (1024 * 1024)
        elapsed = time.time() - start_time
        
        print(f"  ✓ Generated {filepath.name} in {elapsed:.1f}s (actual: {actual_size:.1f}MB)")
        return True


class BenchmarkRunner:
    """Runs benchmarks and collects performance data"""
    
    def __init__(self, jobs: int = 4):
        self.jobs = jobs
    
    def detect_cpu_features(self) -> List[str]:
        """Detect available CPU features and return appropriate GCC flags"""
        import platform
        
        # Base optimization flags
        flags = ["-O3", "-march=native", "-mtune=native", "-DNDEBUG"]
        
        try:
            # Get CPU info
            machine = platform.machine()
            system = platform.system()
            
            print(f"Detected: {system} on {machine}")
            
            # For x86_64 (Intel/AMD), add common SIMD flags that are likely supported
            if machine in ["x86_64", "AMD64"]:
                # These are safe bets for modern x86_64 CPUs (2010+)
                flags.extend(["-msse4.2", "-mavx"])
                
                # Try to detect AVX2 support by testing compilation
                test_result = subprocess.run(
                    ["gcc", "-mavx2", "-c", "-x", "c", "/dev/null", "-o", "/dev/null"],
                    capture_output=True, stderr=subprocess.DEVNULL
                )
                if test_result.returncode == 0:
                    flags.append("-mavx2")
                    print("  AVX2 support detected")
                
            elif machine in ["arm64", "aarch64"]:
                # ARM64 - add explicit arch and Apple Silicon optimizations
                if system == "Darwin":
                    flags.extend(["-arch", "arm64", "-mcpu=apple-m1"])
                    print("  ARM64 with Apple Silicon optimizations")
                else:
                    # Generic ARM64
                    flags.append("-mcpu=native")
                    print("  ARM64 NEON support (built-in)")
            
            # Add safe general optimization flags
            flags.extend(["-ffast-math", "-funroll-loops", "-fomit-frame-pointer"])
            
        except Exception as e:
            print(f"Warning: Could not fully detect CPU features ({e}), using basic optimization")
        
        return flags
        
    def compile_benchmarks(self) -> bool:
        """Compile benchmark executables"""
        print("Compiling benchmark executables...")
        
        # Detect optimal compilation flags
        opt_flags = self.detect_cpu_features()
        print(f"Using optimization flags: {' '.join(opt_flags)}")
        
        # Compile SonicSV benchmark
        sonic_cmd = ["gcc"] + opt_flags + ["benchmark_sonicsv.c", "-o", "benchmark_sonicsv"]
        try:
            result = subprocess.run(sonic_cmd, capture_output=True, text=True)
            if result.returncode != 0:
                print(f"Failed to compile SonicSV benchmark: {result.stderr}")
                return False
        except Exception as e:
            print(f"Error compiling SonicSV benchmark: {e}")
            return False
            
        # Compile libcsv benchmark with same flags
        libcsv_cmd = ["gcc"] + opt_flags + ["benchmark_libcsv.c", "-lcsv", "-o", "benchmark_libcsv"]
        try:
            result = subprocess.run(libcsv_cmd, capture_output=True, text=True)
            if result.returncode != 0:
                print(f"Failed to compile libcsv benchmark: {result.stderr}")
                return False
        except Exception as e:
            print(f"Error compiling libcsv benchmark: {e}")
            return False
            
        print("✓ Benchmarks compiled successfully")
        return True
    
    def run_single_benchmark(self, parser: str, filepath: Path, timeout: int = 300) -> Tuple[float, float, bool, str]:
        """Run a single benchmark and return (throughput_mbps, runtime, success, error)"""
        executable = f"./benchmark_{parser}"
        
        if not Path(executable[2:]).exists():  # Remove ./ for existence check
            return 0.0, 0.0, False, f"Executable {executable} not found"
            
        try:
            start_time = time.time()
            result = subprocess.run([executable, str(filepath)], 
                                  capture_output=True, text=True, timeout=timeout)
            runtime = time.time() - start_time
            
            if result.returncode != 0:
                return 0.0, runtime, False, f"Exit code {result.returncode}: {result.stderr.strip()}"
                
            try:
                throughput = float(result.stdout.strip())
                return throughput, runtime, True, ""
            except ValueError:
                return 0.0, runtime, False, f"Invalid output: {result.stdout.strip()}"
                
        except subprocess.TimeoutExpired:
            return 0.0, timeout, False, f"Timeout after {timeout}s"
        except Exception as e:
            return 0.0, 0.0, False, f"Exception: {str(e)}"
    
    def run_benchmarks(self, configs: List[BenchmarkConfig], runs: int = 3) -> List[BenchmarkResult]:
        """Run benchmarks on all configurations"""
        if not self.compile_benchmarks():
            return []
            
        results = []
        total_tests = len(configs) * 2 * runs  # 2 parsers, multiple runs
        test_count = 0
        
        for config in configs:
            filepath = Path(f"../benchmark_data/{config.name}.csv")
            if not filepath.exists():
                print(f"⚠️  Skipping {config.name} - file not found at {filepath}")
                continue
                
            file_size_mb = filepath.stat().st_size / (1024 * 1024)
            print(f"\nBenchmarking {config.name} ({file_size_mb:.1f}MB)...")
            
            for parser in ["sonicsv", "libcsv"]:
                parser_results = []
                
                print(f"  {parser}:", end=" ", flush=True)
                for run in range(runs):
                    test_count += 1
                    throughput, runtime, success, error = self.run_single_benchmark(parser, filepath)
                    
                    result = BenchmarkResult(
                        parser=parser,
                        config=config,
                        throughput_mbps=throughput,
                        run_time=runtime,
                        success=success,
                        error=error
                    )
                    results.append(result)
                    parser_results.append(result)
                    
                    if success:
                        print(f"{throughput:.1f}", end=" ", flush=True)
                    else:
                        print(f"FAIL({error[:30]})", end=" ", flush=True)
                        
                    progress = (test_count / total_tests) * 100
                    if test_count % 5 == 0:
                        print(f"[{progress:.1f}%]", end=" ", flush=True)
                
                # Calculate statistics for this parser
                successful_runs = [r for r in parser_results if r.success]
                if successful_runs:
                    throughputs = [r.throughput_mbps for r in successful_runs]
                    avg_throughput = statistics.mean(throughputs)
                    print(f"(avg: {avg_throughput:.1f} MB/s)")
                else:
                    print("(all runs failed)")
        
        return results


class BenchmarkAnalyzer:
    """Analyzes and reports benchmark results"""
    
    @staticmethod
    def generate_report(results: List[BenchmarkResult]) -> Dict:
        """Generate comprehensive benchmark report"""
        report = {
            "summary": {},
            "detailed_results": {},
            "comparisons": {},
            "timestamp": time.strftime("%Y-%m-%d %H:%M:%S")
        }
        
        # Group results by configuration and parser
        by_config = {}
        for result in results:
            config_name = result.config.name
            if config_name not in by_config:
                by_config[config_name] = {"sonicsv": [], "libcsv": []}
            by_config[config_name][result.parser].append(result)
        
        # Calculate statistics for each configuration
        for config_name, parsers in by_config.items():
            config_stats = {}
            
            for parser_name, parser_results in parsers.items():
                successful = [r for r in parser_results if r.success]
                
                if successful:
                    throughputs = [r.throughput_mbps for r in successful]
                    runtimes = [r.run_time for r in successful]
                    
                    stats = {
                        "runs": len(parser_results),
                        "successful": len(successful),
                        "failed": len(parser_results) - len(successful),
                        "throughput": {
                            "mean": statistics.mean(throughputs),
                            "median": statistics.median(throughputs),
                            "min": min(throughputs),
                            "max": max(throughputs),
                            "stdev": statistics.stdev(throughputs) if len(throughputs) > 1 else 0.0
                        },
                        "runtime": {
                            "mean": statistics.mean(runtimes),
                            "median": statistics.median(runtimes),
                            "min": min(runtimes),
                            "max": max(runtimes)
                        }
                    }
                else:
                    stats = {
                        "runs": len(parser_results),
                        "successful": 0,
                        "failed": len(parser_results),
                        "throughput": None,
                        "runtime": None,
                        "errors": [r.error for r in parser_results]
                    }
                
                config_stats[parser_name] = stats
            
            report["detailed_results"][config_name] = config_stats
            
            # Calculate comparison if both parsers have successful runs
            if (config_stats.get("sonicsv", {}).get("successful", 0) > 0 and 
                config_stats.get("libcsv", {}).get("successful", 0) > 0):
                
                sonic_throughput = config_stats["sonicsv"]["throughput"]["mean"]
                libcsv_throughput = config_stats["libcsv"]["throughput"]["mean"]
                speedup = sonic_throughput / libcsv_throughput
                
                report["comparisons"][config_name] = {
                    "sonicsv_throughput": sonic_throughput,
                    "libcsv_throughput": libcsv_throughput,
                    "speedup": speedup,
                    "winner": "sonicsv" if speedup > 1.0 else "libcsv"
                }
        
        # Overall summary
        all_comparisons = list(report["comparisons"].values())
        if all_comparisons:
            speedups = [c["speedup"] for c in all_comparisons]
            sonic_wins = sum(1 for c in all_comparisons if c["winner"] == "sonicsv")
            
            report["summary"] = {
                "total_configurations": len(all_comparisons),
                "sonicsv_wins": sonic_wins,
                "libcsv_wins": len(all_comparisons) - sonic_wins,
                "average_speedup": statistics.mean(speedups),
                "median_speedup": statistics.median(speedups),
                "max_speedup": max(speedups),
                "min_speedup": min(speedups)
            }
        
        return report
    
    @staticmethod
    def print_report(report: Dict):
        """Print formatted benchmark report"""
        print("\n" + "="*80)
        print("SONICSV vs LIBCSV BENCHMARK RESULTS")
        print("="*80)
        
        summary = report.get("summary", {})
        if summary:
            print(f"\n📊 OVERALL SUMMARY:")
            print(f"   Configurations tested: {summary['total_configurations']}")
            print(f"   SonicSV wins: {summary['sonicsv_wins']}")
            print(f"   libcsv wins: {summary['libcsv_wins']}")
            print(f"   Average speedup: {summary['average_speedup']:.2f}x")
            print(f"   Median speedup: {summary['median_speedup']:.2f}x")
            print(f"   Max speedup: {summary['max_speedup']:.2f}x")
            print(f"   Min speedup: {summary['min_speedup']:.2f}x")
        
        print(f"\n📈 DETAILED RESULTS:")
        for config_name, comparison in report["comparisons"].items():
            winner = "🚀" if comparison["winner"] == "sonicsv" else "🐌"
            print(f"\n  {winner} {config_name}:")
            print(f"     SonicSV: {comparison['sonicsv_throughput']:.1f} MB/s")
            print(f"     libcsv:  {comparison['libcsv_throughput']:.1f} MB/s")
            print(f"     Speedup: {comparison['speedup']:.2f}x")
        
        print(f"\n⏰ Generated: {report['timestamp']}")
        print("="*80)


def get_benchmark_configs() -> List[BenchmarkConfig]:
    """Define benchmark test configurations"""
    configs = []
    
    # Small files for quick testing
    configs.extend([
        BenchmarkConfig("small_simple", 1, 5000, 5, "simple", 0.1, False, False),
        BenchmarkConfig("small_medium", 1, 3000, 8, "medium", 0.3, True, False),
        BenchmarkConfig("small_complex", 1, 2000, 10, "complex", 0.5, True, True),
    ])
    
    # Medium files
    configs.extend([
        BenchmarkConfig("medium_simple", 10, 50000, 5, "simple", 0.1, False, False),
        BenchmarkConfig("medium_medium", 10, 30000, 8, "medium", 0.3, True, False),
        BenchmarkConfig("medium_complex", 10, 20000, 10, "complex", 0.5, True, True),
    ])
    
    # Large files
    configs.extend([
        BenchmarkConfig("large_simple", 100, 500000, 5, "simple", 0.1, False, False),
        BenchmarkConfig("large_medium", 100, 300000, 8, "medium", 0.3, True, False),
        BenchmarkConfig("large_complex", 100, 200000, 10, "complex", 0.5, True, True),
    ])
    
    # Very large files (up to 1GB)
    configs.extend([
        BenchmarkConfig("xlarge_simple", 500, 2500000, 5, "simple", 0.1, False, False),
        BenchmarkConfig("xlarge_medium", 500, 1500000, 8, "medium", 0.3, True, False),
        BenchmarkConfig("giant_simple", 1000, 5000000, 5, "simple", 0.1, False, False),
    ])
    
    return configs


def main():
    parser = argparse.ArgumentParser(description="SonicSV vs libcsv benchmark suite")
    parser.add_argument("--generate-bench-data", action="store_true",
                       help="Generate benchmark CSV files")
    parser.add_argument("--benchmark", action="store_true",
                       help="Run benchmarks")
    parser.add_argument("--jobs", type=int, default=4,
                       help="Number of parallel jobs (default: 4)")
    parser.add_argument("--runs", type=int, default=3,
                       help="Number of benchmark runs per configuration (default: 3)")
    parser.add_argument("--output", type=str, default="benchmark_results.json",
                       help="Output file for results (default: benchmark_results.json)")
    
    args = parser.parse_args()
    
    if not args.generate_bench_data and not args.benchmark:
        parser.error("Must specify either --generate-bench-data or --benchmark (or both)")
    
    # Create benchmark data directory
    Path("../benchmark_data").mkdir(exist_ok=True)
    
    configs = get_benchmark_configs()
    results = []
    
    if args.generate_bench_data:
        print(f"Generating benchmark data with {args.jobs} jobs...")
        generator = CSVGenerator(args.jobs)
        
        for config in configs:
            filepath = Path(f"../benchmark_data/{config.name}.csv")
            if filepath.exists():
                print(f" {config.name}.csv already exists, skipping")
                continue
                
            try:
                generator.generate_csv_file(config, filepath)
            except KeyboardInterrupt:
                print("\nGeneration interrupted by user")
                sys.exit(1)
            except Exception as e:
                print(f"Error generating {config.name}: {e}")
    
    if args.benchmark:
        print(f"\n🏃 Running benchmarks with {args.runs} runs per test...")
        runner = BenchmarkRunner(args.jobs)
        results = runner.run_benchmarks(configs, args.runs)
        
        # Generate and display report
        report = BenchmarkAnalyzer.generate_report(results)
        BenchmarkAnalyzer.print_report(report)
        
        # Save results to file
        with open(args.output, 'w') as f:
            json.dump(report, f, indent=2)
        print(f"\nResults saved to {args.output}")


if __name__ == "__main__":
    main()