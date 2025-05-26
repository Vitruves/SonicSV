#!/usr/bin/env python3
"""
SonicSV Benchmark Runner with Direct Compilation

Compiles benchmarks directly using gcc/g++, generates test data,
and runs comprehensive performance comparisons without external build systems.
"""

import os
import sys
import time
import subprocess
import argparse
import platform
import json
import glob
import threading
import queue
import concurrent.futures
from datetime import datetime
from pathlib import Path

class Terminal:
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    BLUE = '\033[94m'
    BOLD = '\033[1m'
    RESET = '\033[0m'
    
    @staticmethod
    def info(msg): print(f"[*] {msg}")
    
    @staticmethod
    def success(msg): print(f"{Terminal.GREEN}[✓] {msg}{Terminal.RESET}")
    
    @staticmethod
    def warning(msg): print(f"{Terminal.YELLOW}[!] {msg}{Terminal.RESET}")
    
    @staticmethod
    def error(msg): print(f"{Terminal.RED}[✗] {msg}{Terminal.RESET}")
    
    @staticmethod
    def header(msg): print(f"\n{Terminal.BOLD}{msg}{Terminal.RESET}")

class DirectCompiler:
    """Compiles benchmarks directly without external build systems"""
    
    def __init__(self, project_root="."):
        self.project_root = Path(project_root)
        self.benchmark_dir = self.project_root / "benchmark"
        self.bin_dir = self.benchmark_dir / "bin"
        self.compiler_config = self._detect_compilers()
    
    def _detect_compilers(self):
        """Find available C/C++ compilers"""
        compilers = {}
        
        # C compilers (in order of preference)
        for cc in ['gcc', 'clang', 'cc']:
            if self._command_exists(cc):
                compilers['cc'] = cc
                break
        
        # C++ compilers (in order of preference)  
        for cxx in ['g++', 'clang++', 'c++']:
            if self._command_exists(cxx):
                compilers['cxx'] = cxx
                break
        
        if not compilers:
            Terminal.error("No C/C++ compilers found")
            return {}
        
        Terminal.info(f"Using compilers: {compilers}")
        return compilers
    
    def _command_exists(self, command):
        """Check if command exists in PATH"""
        try:
            subprocess.run([command, '--version'], 
                         capture_output=True, check=True)
            return True
        except (subprocess.CalledProcessError, FileNotFoundError):
            return False
    
    def _get_optimization_flags(self):
        """Get platform-specific optimization flags"""
        base_flags = ['-O3', '-DNDEBUG', '-pthread']
        
        # Add feature test macros for POSIX functions
        base_flags.extend(['-D_GNU_SOURCE', '-D_POSIX_C_SOURCE=200809L'])
        
        # Add architecture-specific optimizations
        arch = platform.machine().lower()
        if 'x86_64' in arch or 'amd64' in arch:
            base_flags.extend(['-march=native', '-mtune=native'])
        elif 'arm' in arch or 'aarch64' in arch:
            base_flags.extend(['-mcpu=native'])
        
        # Add platform-specific flags
        if platform.system() == 'Darwin':
            base_flags.append('-Wno-deprecated-declarations')
        
        return base_flags
    
    def _find_source_files(self):
        """Discover benchmark source files"""
        source_patterns = [
            self.benchmark_dir / "bench_*.c",
            self.benchmark_dir / "bench_*.cpp",
            self.benchmark_dir / "**/bench_*.c",
            self.benchmark_dir / "**/bench_*.cpp"
        ]
        
        sources = []
        for pattern in source_patterns:
            sources.extend(glob.glob(str(pattern), recursive=True))
        
        benchmark_sources = {}
        for source_path in sources:
            source = Path(source_path)
            if source.name.startswith('bench_'):
                name = source.stem[6:]  # Remove 'bench_' prefix
                benchmark_sources[name] = source
        
        Terminal.info(f"Found {len(benchmark_sources)} benchmark sources")
        return benchmark_sources
    
    def _find_dependencies(self, benchmark_name, source_file):
        """Find required dependencies for each benchmark"""
        dependencies = {
            'ariasdiniz': {
                'sources': [self.benchmark_dir / "ariasdiniz" / "src" / "parser.c"],
                'includes': [self.benchmark_dir / "ariasdiniz" / "src"],
                'libs': []
            },
            'arrow': {
                'sources': [],
                'includes': [self.benchmark_dir / "arrow" / "cpp" / "src" / "arrow" / "csv" / "api.h", self.benchmark_dir / "arrow" / "cpp" / "src" / "arrow" / "io" / "api.h", self.benchmark_dir / "arrow" / "cpp" / "src" / "arrow" / "util" / "logging.h", self.benchmark_dir / "arrow" / "cpp" / "src" / "arrow" / "compute" / "api.h"],
                'libs': ['arrow']
            },
            'd99kris': {
                'sources': [],
                'includes': [self.benchmark_dir / "d99kris" / "src"],
                'libs': []
            },
            'libcsv': {
                'sources': [],
                'includes': [],
                'libs': ['csv']
            },
            'libcsv_mt': {
                'sources': [],
                'includes': [],
                'libs': ['csv']
            },
            'semitrivial': {
                'sources': [
                    self.benchmark_dir / "semitrivial" / "csv.c",
                    self.benchmark_dir / "semitrivial" / "split.c",
                    self.benchmark_dir / "semitrivial" / "fread_csv_line.c"
                ],
                'includes': [self.benchmark_dir],
                'libs': []
            },
            'sonicsv': {
                'sources': [],
                'includes': ['/usr/local/include/sonicsv', 'include'],
                'libs': ['sonicsv', 'm']
            },
            'sonicsv_batching': {
                'sources': [],
                'includes': ['include'],
                'libs': ['m']
            },
            'sonicsv_mt': {
                'sources': [],
                'includes': ['include'],
                'libs': ['m']
            }
        }
        
        return dependencies.get(benchmark_name, {
            'sources': [], 'includes': [], 'libs': []
        })
    
    def _compile_benchmark(self, name, source_file, dependencies):
        """Compile a single benchmark"""
        output_file = self.bin_dir / f"bench_{name}"
        
        # Determine compiler based on file extension
        is_cpp = source_file.suffix in ['.cpp', '.cc', '.cxx']
        compiler = self.compiler_config['cxx'] if is_cpp else self.compiler_config['cc']
        
        if not compiler:
            Terminal.error(f"No suitable compiler for {source_file}")
            return False
        
        # Build command
        cmd = [compiler]
        
        # Add language standard
        if is_cpp:
            cmd.extend(['-std=c++17'])
        else:
            # Use gnu99 for better compatibility with some libraries
            if name == 'semitrivial':
                cmd.extend(['-std=gnu99'])
            else:
                cmd.extend(['-std=c99'])
        
        # Add optimization flags
        cmd.extend(self._get_optimization_flags())
        
        # Add SonicSV implementation define for SonicSV variants (only if not already in source)
        if name.startswith('sonicsv'):
            # Check if the source file already has the define
            try:
                with open(source_file, 'r') as f:
                    first_line = f.readline().strip()
                    if '#define SONICSV_IMPLEMENTATION' not in first_line:
                        cmd.append('-DSONICSV_IMPLEMENTATION')
            except:
                # If we can't read the file, add the define anyway
                cmd.append('-DSONICSV_IMPLEMENTATION')
        
        # Add include directories
        for include_dir in dependencies.get('includes', []):
            include_path = Path(include_dir)
            if include_path.exists():
                cmd.extend(['-I', str(include_path)])
        
        # Add source files
        cmd.append(str(source_file))
        for dep_source in dependencies.get('sources', []):
            if Path(dep_source).exists():
                cmd.append(str(dep_source))
        
        # Add output
        cmd.extend(['-o', str(output_file)])
        
        # Add libraries
        for lib in dependencies.get('libs', []):
            cmd.extend(['-l', lib])
        
        # Add library search paths
        cmd.extend(['-L/usr/local/lib', '-L/usr/lib'])
        
        Terminal.info(f"Compiling {name}...")
        
        try:
            result = subprocess.run(cmd, capture_output=True, text=True)
            
            if result.returncode == 0:
                Terminal.success(f"Built {name}")
                return True
            else:
                Terminal.warning(f"Failed to build {name}")
                if result.stderr:
                    Terminal.error(f"Compilation errors:\n{result.stderr[:500]}")
                if result.stdout:
                    Terminal.info(f"Compilation output:\n{result.stdout[:300]}")
                return False
                
        except Exception as e:
            Terminal.error(f"Compilation error for {name}: {e}")
            return False
    
    def build_all_benchmarks(self):
        """Build all discovered benchmarks"""
        if not self.compiler_config:
            Terminal.error("No compilers available")
            return {}
        
        # Create output directory
        self.bin_dir.mkdir(parents=True, exist_ok=True)
        
        # Find all benchmark sources
        benchmark_sources = self._find_source_files()
        if not benchmark_sources:
            Terminal.error("No benchmark source files found")
            return {}
        
        compiled_benchmarks = {}
        
        for name, source_file in benchmark_sources.items():
            dependencies = self._find_dependencies(name, source_file)
            
            if self._compile_benchmark(name, source_file, dependencies):
                output_path = self.bin_dir / f"bench_{name}"
                if output_path.exists():
                    compiled_benchmarks[name] = output_path
        
        Terminal.success(f"Successfully built {len(compiled_benchmarks)} benchmarks")
        return compiled_benchmarks

class TestScenarioManager:
    """Manages different test scenarios and configurations"""
    
    def __init__(self, args):
        self.args = args
        self.scenarios = self._generate_test_scenarios()
    
    def _generate_test_scenarios(self):
        """Generate test scenarios based on command line arguments"""
        scenarios = []
        
        # Handle quick vs comprehensive modes
        if self.args.quick:
            rows_list = [100000]
            cols_list = [10]
            cell_sizes = [8]
            content_types = ['mixed']
        elif self.args.comprehensive:
            rows_list = self.args.rows
            cols_list = self.args.cols
            cell_sizes = self.args.cell_size
            content_types = self.args.content
        else:
            # Default: balanced test suite
            rows_list = self.args.rows[:3] if len(self.args.rows) > 2 else self.args.rows
            cols_list = self.args.cols[:4] if len(self.args.cols) > 2 else self.args.cols
            cell_sizes = self.args.cell_size[:3] if len(self.args.cell_size) > 2 else self.args.cell_size
            content_types = self.args.content
        
        # Generate base scenarios
        for rows in rows_list:
            for cols in cols_list:
                for cell_size in cell_sizes:
                    for content_type in content_types:
                        # Basic CSV scenario
                        scenarios.append({
                            'name': f'csv_{content_type}_{rows}r_{cols}c_{cell_size}ch',
                            'rows': rows,
                            'cols': cols,
                            'cell_size': cell_size,
                            'content_type': content_type,
                            'delimiter': ',',
                            'quoted': False,
                            'empty_cells': 0,
                            'file_extension': 'csv'
                        })
                        
                        # Quoted CSV scenario (if enabled)
                        if self.args.test_quoted:
                            scenarios.append({
                                'name': f'csv_quoted_{content_type}_{rows}r_{cols}c_{cell_size}ch',
                                'rows': rows,
                                'cols': cols,
                                'cell_size': cell_size,
                                'content_type': content_type,
                                'delimiter': ',',
                                'quoted': True,
                                'empty_cells': 0,
                                'file_extension': 'csv'
                            })
                        
                        # TSV scenario (if enabled)
                        if self.args.test_tsv:
                            scenarios.append({
                                'name': f'tsv_{content_type}_{rows}r_{cols}c_{cell_size}ch',
                                'rows': rows,
                                'cols': cols,
                                'cell_size': cell_size,
                                'content_type': content_type,
                                'delimiter': '\t',
                                'quoted': False,
                                'empty_cells': 0,
                                'file_extension': 'tsv'
                            })
                        
                        # Empty cells scenario (if enabled)
                        if self.args.test_empty_cells:
                            scenarios.append({
                                'name': f'csv_empty_{content_type}_{rows}r_{cols}c_{cell_size}ch',
                                'rows': rows,
                                'cols': cols,
                                'cell_size': cell_size,
                                'content_type': content_type,
                                'delimiter': ',',
                                'quoted': False,
                                'empty_cells': 10,  # 10% empty cells
                                'file_extension': 'csv'
                            })
        
        # Remove duplicates and sort by estimated complexity
        unique_scenarios = []
        seen_names = set()
        for scenario in scenarios:
            if scenario['name'] not in seen_names:
                unique_scenarios.append(scenario)
                seen_names.add(scenario['name'])
        
        # Sort by file size (rows * cols * cell_size) for logical progression
        unique_scenarios.sort(key=lambda s: s['rows'] * s['cols'] * s['cell_size'])
        
        return unique_scenarios
    
    def get_scenarios(self):
        """Get all test scenarios"""
        return self.scenarios
    
    def get_scenario_summary(self):
        """Get a summary of test scenarios"""
        total_scenarios = len(self.scenarios)
        total_estimated_size = sum(
            (s['rows'] * s['cols'] * s['cell_size'] * 1.2) / (1024 * 1024)  # Rough MB estimate
            for s in self.scenarios
        )
        
        return {
            'total_scenarios': total_scenarios,
            'estimated_total_size_mb': total_estimated_size,
            'scenario_types': list(set(s['file_extension'] for s in self.scenarios)),
            'content_types': list(set(s['content_type'] for s in self.scenarios)),
            'size_range': {
                'min_rows': min(s['rows'] for s in self.scenarios),
                'max_rows': max(s['rows'] for s in self.scenarios),
                'min_cols': min(s['cols'] for s in self.scenarios),
                'max_cols': max(s['cols'] for s in self.scenarios)
            }
        }

class ThreadedCSVManager:
    """Simplified multithreaded CSV generation manager"""
    
    def __init__(self, max_workers=None, max_concurrent_files=3):
        if max_workers is None:
            # Use nproc - 2 for optimal performance while keeping system responsive
            max_workers = max(1, os.cpu_count() - 2)
        self.max_workers = max_workers
        self.max_concurrent_files = max_concurrent_files
        
    def _generate_csv_worker(self, scenario):
        """Worker function for CSV generation"""
        try:
            generator = OptimizedCSVGenerator()
            file_size_mb, filename = generator.generate_from_scenario(scenario)
            return filename, file_size_mb, scenario
        except Exception as e:
            Terminal.error(f"Failed to generate CSV for {scenario['name']}: {e}")
            return None, 0, scenario
    
    def generate_scenarios_parallel(self, scenarios):
        """Generate multiple CSV files in parallel"""
        Terminal.info(f"Starting parallel generation of {len(scenarios)} scenarios...")
        
        # Generate files in parallel batches
        with concurrent.futures.ThreadPoolExecutor(max_workers=self.max_workers) as executor:
            # Submit all generation tasks
            future_to_scenario = {}
            
            for scenario in scenarios:
                future = executor.submit(self._generate_csv_worker, scenario)
                future_to_scenario[future] = scenario
            
            # Collect results as they complete
            completed_scenarios = []
            for future in concurrent.futures.as_completed(future_to_scenario):
                scenario = future_to_scenario[future]
                try:
                    filename, file_size_mb, scenario_data = future.result(timeout=300)
                    if filename:
                        completed_scenarios.append((filename, file_size_mb, scenario_data))
                        Terminal.success(f"Generated: {filename} ({file_size_mb:.2f} MB)")
                    else:
                        Terminal.error(f"Failed to generate: {scenario['name']}")
                except Exception as e:
                    Terminal.error(f"Generation error for {scenario['name']}: {e}")
        
        return completed_scenarios
    
    def shutdown(self):
        """Shutdown the manager - simplified version"""
        pass  # No complex cleanup needed in simplified version

class OptimizedCSVGenerator:
    """Memory-efficient CSV generator with deterministic output"""
    
    def __init__(self, columns=10, cell_length=8, max_workers=None):
        self.columns = columns
        self.cell_length = cell_length
        self.write_buffer_size = 256 * 1024
        if max_workers is None:
            max_workers = max(1, os.cpu_count() - 2)
        self.max_workers = max_workers
    
    def _generate_cell_content(self, row_index, col_index, content_style):
        """Generate deterministic cell content"""
        seed_value = (row_index * self.columns + col_index) % 100000
        
        content_generators = {
            'numeric': lambda s: f"{s % (10 ** self.cell_length):0{self.cell_length}d}",
            'alphabetic': lambda s: ''.join(
                chr(ord('a') + (s + i) % 26) for i in range(self.cell_length)
            ),
            'mixed': lambda s: ''.join(
                'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789'[(s + i) % 62]
                for i in range(self.cell_length)
            )
        }
        
        return content_generators[content_style](seed_value)
    
    def _generate_chunk_worker(self, args):
        """Worker function to generate a chunk of CSV rows"""
        start_row, end_row, content_style, use_quotes, empty_cell_percentage, delimiter = args
        
        chunk_lines = []
        for row_idx in range(start_row, end_row):
            row_cells = []
            
            for col_idx in range(self.columns):
                # Occasionally create empty cells
                if (empty_cell_percentage > 0 and 
                    (row_idx * self.columns + col_idx) % (100 // empty_cell_percentage) == 0):
                    cell_content = ""
                else:
                    cell_content = self._generate_cell_content(row_idx, col_idx, content_style)
                    if use_quotes and delimiter == ',':  # Only quote CSV, not TSV
                        cell_content = f'"{cell_content}"'
                
                row_cells.append(cell_content)
            
            chunk_lines.append(delimiter.join(row_cells))
        
        return start_row, chunk_lines

    def generate_csv_file(self, file_path, row_count, content_style='mixed', 
                         use_quotes=False, empty_cell_percentage=0, delimiter=','):
        """Generate CSV file with specified characteristics using parallel workers"""
        file_type = "TSV" if delimiter == '\t' else "CSV"
        Terminal.info(f"Generating {row_count:,} × {self.columns} {file_type} file using {self.max_workers} workers...")
        
        generation_start = time.perf_counter()
        
        # Calculate chunk sizes for parallel generation
        chunk_size = max(1000, row_count // self.max_workers)
        chunks = []
        
        for start_row in range(0, row_count, chunk_size):
            end_row = min(start_row + chunk_size, row_count)
            chunks.append((start_row, end_row, content_style, use_quotes, empty_cell_percentage, delimiter))
        
        Terminal.info(f"Splitting generation into {len(chunks)} chunks of ~{chunk_size:,} rows each")
        
        # Generate chunks in parallel
        chunk_results = {}
        
        if len(chunks) > 1 and self.max_workers > 1:
            # Parallel generation
            with concurrent.futures.ThreadPoolExecutor(max_workers=self.max_workers) as executor:
                future_to_chunk = {
                    executor.submit(self._generate_chunk_worker, chunk_args): chunk_args[0] 
                    for chunk_args in chunks
                }
                
                completed_chunks = 0
                for future in concurrent.futures.as_completed(future_to_chunk):
                    start_row = future_to_chunk[future]
                    try:
                        chunk_start, chunk_lines = future.result()
                        chunk_results[chunk_start] = chunk_lines
                        completed_chunks += 1
                        
                        progress_percent = (completed_chunks / len(chunks)) * 100
                        Terminal.info(f"Generation progress: {progress_percent:.0f}% ({completed_chunks}/{len(chunks)} chunks)")
                        
                    except Exception as e:
                        Terminal.error(f"Chunk generation failed for row {start_row}: {e}")
                        return 0
        else:
            # Sequential generation for small files or single worker
            for chunk_args in chunks:
                start_row, chunk_lines = self._generate_chunk_worker(chunk_args)
                chunk_results[start_row] = chunk_lines
                
                progress_percent = (start_row / row_count) * 100
                Terminal.info(f"Generation progress: {progress_percent:.0f}%")
        
        # Write results to file in correct order
        Terminal.info("Writing chunks to file...")
        with open(file_path, 'w', buffering=self.write_buffer_size) as output_file:
            # Write header row
            header_columns = [f"field_{i:02d}" for i in range(self.columns)]
            output_file.write(delimiter.join(header_columns) + '\n')
            
            # Write data chunks in order
            for start_row in sorted(chunk_results.keys()):
                for line in chunk_results[start_row]:
                    output_file.write(line + '\n')
        
        generation_time = time.perf_counter() - generation_start
        file_size_mb = os.path.getsize(file_path) / (1024 * 1024)
        generation_rate = file_size_mb / generation_time
        
        Terminal.success(f"Generated {file_size_mb:.2f} MB in {generation_time:.1f}s "
                        f"({generation_rate:.1f} MB/s) using {self.max_workers} workers")
        return file_size_mb
    
    def generate_from_scenario(self, scenario):
        """Generate a test file from a scenario configuration"""
        self.columns = scenario['cols']
        self.cell_length = scenario['cell_size']
        
        filename = f"{scenario['name']}.{scenario['file_extension']}"
        
        return self.generate_csv_file(
            filename,
            row_count=scenario['rows'],
            content_style=scenario['content_type'],
            use_quotes=scenario['quoted'],
            empty_cell_percentage=scenario['empty_cells'],
            delimiter=scenario['delimiter']
        ), filename

class PerformanceBenchmarker:
    """Executes and measures benchmark performance"""
    
    def __init__(self, benchmark_executables):
        self.benchmark_executables = benchmark_executables
    
    def _execute_single_run(self, benchmark_name, csv_file_path, timeout_seconds=180):
        """Execute one benchmark run and collect metrics"""
        executable_path = self.benchmark_executables[benchmark_name]
        file_size_mb = os.path.getsize(csv_file_path) / (1024 * 1024)
        
        try:
            execution_start = time.perf_counter()
            
            process_result = subprocess.run(
                [str(executable_path), str(csv_file_path)],
                capture_output=True,
                text=True,
                timeout=timeout_seconds
            )
            
            execution_duration = time.perf_counter() - execution_start
            
            if process_result.returncode == 0:
                throughput_rate = file_size_mb / execution_duration if execution_duration > 0 else 0
                return {
                    'success': True,
                    'execution_time_seconds': execution_duration,
                    'throughput_mb_per_second': throughput_rate,
                    'file_size_mb': file_size_mb,
                    'process_output_length': len(process_result.stdout),
                    'process_error_length': len(process_result.stderr)
                }
            else:
                return {
                    'success': False,
                    'error_type': 'non_zero_exit',
                    'exit_code': process_result.returncode,
                    'stderr_preview': process_result.stderr[:500]
                }
                
        except subprocess.TimeoutExpired:
            return {'success': False, 'error_type': 'timeout', 'timeout_seconds': timeout_seconds}
        except Exception as execution_error:
            return {'success': False, 'error_type': 'exception', 'error_message': str(execution_error)}
    
    def run_comprehensive_benchmark(self, csv_file_path, iterations_per_benchmark=3):
        """Execute complete benchmark suite with statistical analysis"""
        Terminal.header(f"Performance Benchmark Suite: {Path(csv_file_path).name}")
        
        file_size_mb = os.path.getsize(csv_file_path) / (1024 * 1024)
        Terminal.info(f"Test file size: {file_size_mb:.2f} MB")
        
        comprehensive_results = {}
        
        for benchmark_name in sorted(self.benchmark_executables.keys()):
            Terminal.info(f"Benchmarking {benchmark_name}...")
            
            iteration_results = []
            successful_runs = 0
            
            for iteration_number in range(iterations_per_benchmark):
                run_result = self._execute_single_run(benchmark_name, csv_file_path)
                
                if run_result.get('success'):
                    iteration_results.append(run_result)
                    successful_runs += 1
                    throughput = run_result['throughput_mb_per_second']
                    Terminal.info(f"  Iteration {iteration_number + 1}: {throughput:.2f} MB/s")
                else:
                    error_info = run_result.get('error_type', 'unknown')
                    Terminal.warning(f"  Iteration {iteration_number + 1}: FAILED ({error_info})")
            
            # Calculate performance statistics
            if iteration_results:
                throughput_values = [result['throughput_mb_per_second'] for result in iteration_results]
                execution_times = [result['execution_time_seconds'] for result in iteration_results]
                
                comprehensive_results[benchmark_name] = {
                    'successful_iterations': successful_runs,
                    'total_iterations': iterations_per_benchmark,
                    'average_throughput_mb_per_second': sum(throughput_values) / len(throughput_values),
                    'peak_throughput_mb_per_second': max(throughput_values),
                    'minimum_throughput_mb_per_second': min(throughput_values),
                    'average_execution_time_seconds': sum(execution_times) / len(execution_times),
                    'fastest_execution_time_seconds': min(execution_times),
                    'slowest_execution_time_seconds': max(execution_times),
                    'file_size_mb': file_size_mb,
                    'detailed_results': iteration_results
                }
                
                avg_throughput = comprehensive_results[benchmark_name]['average_throughput_mb_per_second']
                Terminal.success(f"{benchmark_name}: {avg_throughput:.2f} MB/s average")
            else:
                comprehensive_results[benchmark_name] = {
                    'successful_iterations': 0,
                    'total_iterations': iterations_per_benchmark,
                    'failure_reason': 'all_iterations_failed'
                }
                Terminal.error(f"{benchmark_name}: Complete failure")
        
        return comprehensive_results

class BenchmarkReporter:
    """Generates reports and summaries from benchmark results"""
    
    @staticmethod
    def save_json_report(results_data, output_file_path):
        """Save comprehensive results to JSON file"""
        with open(output_file_path, 'w') as json_file:
            json.dump(results_data, json_file, indent=2, default=str)
        Terminal.success(f"JSON report saved: {output_file_path}")
    
    @staticmethod
    def generate_text_summary(results_data, summary_file_path):
        """Create human-readable performance summary"""
        with open(summary_file_path, 'w') as summary_file:
            summary_file.write("CSV Parser Performance Analysis\n")
            summary_file.write("=" * 60 + "\n\n")
            
            # System and test information
            system_info = results_data['execution_environment']
            summary_file.write(f"Execution Environment:\n")
            summary_file.write(f"  Platform: {system_info['platform_name']} {system_info['platform_release']}\n")
            summary_file.write(f"  Architecture: {system_info['machine_architecture']}\n")
            summary_file.write(f"  CPU Cores: {system_info['cpu_core_count']}\n\n")
            
            test_configuration = results_data['test_parameters']
            summary_file.write(f"Test Configuration:\n")
            summary_file.write(f"  Data Rows: {test_configuration['row_count']:,}\n")
            summary_file.write(f"  Data Columns: {test_configuration['column_count']}\n")
            summary_file.write(f"  File Size: {test_configuration['file_size_mb']:.2f} MB\n")
            summary_file.write(f"  Content Type: {test_configuration['content_type']}\n")
            summary_file.write(f"  Quoted Fields: {test_configuration['quoted_fields']}\n")
            summary_file.write(f"  Benchmark Iterations: {test_configuration['iterations_count']}\n\n")
            
            # Performance rankings
            benchmark_data = results_data['benchmark_performance']
            successful_benchmarks = {
                name: data for name, data in benchmark_data.items()
                if data.get('successful_iterations', 0) > 0
            }
            
            if successful_benchmarks:
                ranked_benchmarks = sorted(
                    successful_benchmarks.items(),
                    key=lambda item: item[1]['average_throughput_mb_per_second'],
                    reverse=True
                )
                
                summary_file.write("Performance Rankings:\n")
                summary_file.write("-" * 60 + "\n")
                
                for rank_position, (parser_name, performance_data) in enumerate(ranked_benchmarks, 1):
                    avg_throughput = performance_data['average_throughput_mb_per_second']
                    success_percentage = (performance_data['successful_iterations'] / 
                                        performance_data['total_iterations']) * 100
                    
                    summary_file.write(f"{rank_position:2d}. {parser_name:20} "
                                     f"{avg_throughput:8.2f} MB/s "
                                     f"({success_percentage:5.1f}% success)\n")
                
                # Detailed performance metrics
                summary_file.write(f"\nDetailed Performance Metrics:\n")
                summary_file.write("-" * 60 + "\n")
                
                for parser_name, performance_data in ranked_benchmarks:
                    summary_file.write(f"\n{parser_name}:\n")
                    summary_file.write(f"  Average Throughput: {performance_data['average_throughput_mb_per_second']:8.2f} MB/s\n")
                    summary_file.write(f"  Peak Throughput:    {performance_data['peak_throughput_mb_per_second']:8.2f} MB/s\n")
                    summary_file.write(f"  Minimum Throughput: {performance_data['minimum_throughput_mb_per_second']:8.2f} MB/s\n")
                    summary_file.write(f"  Average Time:       {performance_data['average_execution_time_seconds']:8.3f} seconds\n")
                    summary_file.write(f"  Fastest Time:       {performance_data['fastest_execution_time_seconds']:8.3f} seconds\n")
                    summary_file.write(f"  Success Rate:       {performance_data['successful_iterations']}/{performance_data['total_iterations']} runs\n")
            else:
                summary_file.write("No successful benchmark runs recorded.\n")
        
        Terminal.success(f"Summary report generated: {summary_file_path}")
    
    @staticmethod
    def generate_multi_scenario_summary(results_data, summary_file_path):
        """Create comprehensive multi-scenario performance summary"""
        with open(summary_file_path, 'w') as summary_file:
            summary_file.write("Multi-Scenario CSV Parser Performance Analysis\n")
            summary_file.write("=" * 80 + "\n\n")
            
            # System and test information
            system_info = results_data['execution_environment']
            summary_file.write(f"Execution Environment:\n")
            summary_file.write(f"  Platform: {system_info['platform_name']} {system_info['platform_release']}\n")
            summary_file.write(f"  Architecture: {system_info['machine_architecture']}\n")
            summary_file.write(f"  CPU Cores: {system_info['cpu_core_count']}\n\n")
            
            test_config = results_data['test_configuration']
            summary_file.write(f"Test Configuration:\n")
            summary_file.write(f"  Total Scenarios: {test_config['total_scenarios']}\n")
            summary_file.write(f"  Test Mode: {test_config['test_mode']}\n")
            summary_file.write(f"  Iterations per Benchmark: {test_config['iterations_per_benchmark']}\n\n")
            
            # Aggregate performance across all scenarios
            all_parser_results = {}
            
            for scenario_name, scenario_data in results_data['scenario_results'].items():
                scenario_config = scenario_data['scenario_config']
                benchmark_results = scenario_data['benchmark_results']
                
                for parser_name, parser_data in benchmark_results.items():
                    if parser_data.get('successful_iterations', 0) > 0:
                        if parser_name not in all_parser_results:
                            all_parser_results[parser_name] = []
                        
                        all_parser_results[parser_name].append({
                            'scenario': scenario_name,
                            'throughput': parser_data['average_throughput_mb_per_second'],
                            'file_size': scenario_data['file_size_mb'],
                            'rows': scenario_config['rows'],
                            'cols': scenario_config['cols'],
                            'content_type': scenario_config['content_type']
                        })
            
            # Calculate aggregate statistics
            summary_file.write("Overall Performance Summary:\n")
            summary_file.write("-" * 80 + "\n")
            
            parser_aggregates = {}
            for parser_name, results in all_parser_results.items():
                throughputs = [r['throughput'] for r in results]
                parser_aggregates[parser_name] = {
                    'avg_throughput': sum(throughputs) / len(throughputs),
                    'max_throughput': max(throughputs),
                    'min_throughput': min(throughputs),
                    'scenarios_completed': len(results),
                    'total_scenarios': test_config['total_scenarios']
                }
            
            # Sort by average throughput
            ranked_parsers = sorted(
                parser_aggregates.items(),
                key=lambda x: x[1]['avg_throughput'],
                reverse=True
            )
            
            for rank, (parser_name, stats) in enumerate(ranked_parsers, 1):
                completion_rate = (stats['scenarios_completed'] / stats['total_scenarios']) * 100
                summary_file.write(f"{rank:2d}. {parser_name:20} "
                                 f"Avg: {stats['avg_throughput']:8.2f} MB/s "
                                 f"Max: {stats['max_throughput']:8.2f} MB/s "
                                 f"({completion_rate:5.1f}% scenarios)\n")
            
            # Detailed scenario breakdown
            summary_file.write(f"\nDetailed Scenario Results:\n")
            summary_file.write("-" * 80 + "\n")
            
            for scenario_name, scenario_data in results_data['scenario_results'].items():
                scenario_config = scenario_data['scenario_config']
                summary_file.write(f"\nScenario: {scenario_name}\n")
                summary_file.write(f"  Configuration: {scenario_config['rows']:,} rows × {scenario_config['cols']} cols, "
                                 f"{scenario_config['content_type']} content, {scenario_data['file_size_mb']:.2f} MB\n")
                
                scenario_results = scenario_data['benchmark_results']
                successful_parsers = {
                    name: data for name, data in scenario_results.items()
                    if data.get('successful_iterations', 0) > 0
                }
                
                if successful_parsers:
                    ranked_scenario = sorted(
                        successful_parsers.items(),
                        key=lambda x: x[1]['average_throughput_mb_per_second'],
                        reverse=True
                    )
                    
                    for rank, (parser_name, parser_data) in enumerate(ranked_scenario, 1):
                        throughput = parser_data['average_throughput_mb_per_second']
                        summary_file.write(f"    {rank}. {parser_name:15} {throughput:8.2f} MB/s\n")
                else:
                    summary_file.write("    No successful results for this scenario\n")
        
        Terminal.success(f"Multi-scenario summary generated: {summary_file_path}")
    
    @staticmethod
    def display_multi_scenario_summary(results_data):
        """Show formatted multi-scenario results in terminal"""
        Terminal.header("MULTI-SCENARIO BENCHMARK RESULTS")
        
        # Display test configuration
        config = results_data['test_configuration']
        env = results_data['execution_environment']
        print(f"Test Suite: {config['total_scenarios']} scenarios ({config['test_mode']} mode)")
        print(f"Platform: {env['platform_name']} ({env['cpu_core_count']} cores)")
        print()
        
        # Aggregate results across all scenarios
        all_parser_results = {}
        
        for scenario_name, scenario_data in results_data['scenario_results'].items():
            benchmark_results = scenario_data['benchmark_results']
            
            for parser_name, parser_data in benchmark_results.items():
                if parser_data.get('successful_iterations', 0) > 0:
                    if parser_name not in all_parser_results:
                        all_parser_results[parser_name] = []
                    
                    all_parser_results[parser_name].append(
                        parser_data['average_throughput_mb_per_second']
                    )
        
        if not all_parser_results:
            Terminal.warning("No successful benchmark results to display")
            return
        
        # Calculate and display aggregate statistics
        print(f"{'Rank':<5} {'Parser':<20} {'Avg Throughput':<15} {'Max Throughput':<15} {'Scenarios':<12}")
        print("-" * 75)
        
        parser_stats = {}
        for parser_name, throughputs in all_parser_results.items():
            parser_stats[parser_name] = {
                'avg': sum(throughputs) / len(throughputs),
                'max': max(throughputs),
                'count': len(throughputs)
            }
        
        ranked_results = sorted(
            parser_stats.items(),
            key=lambda x: x[1]['avg'],
            reverse=True
        )
        
        for rank, (parser_name, stats) in enumerate(ranked_results, 1):
            completion_rate = (stats['count'] / config['total_scenarios']) * 100
            display_color = Terminal.GREEN if rank == 1 else Terminal.RESET
            
            print(f"{display_color}{rank:<5} {parser_name:<20} "
                  f"{stats['avg']:>10.2f} MB/s {stats['max']:>10.2f} MB/s "
                  f"{stats['count']:>3d}/{config['total_scenarios']} ({completion_rate:4.1f}%){Terminal.RESET}")
        
        # Show best performing scenario for top parser
        if ranked_results:
            top_parser = ranked_results[0][0]
            best_scenario = None
            best_throughput = 0
            
            for scenario_name, scenario_data in results_data['scenario_results'].items():
                if top_parser in scenario_data['benchmark_results']:
                    parser_result = scenario_data['benchmark_results'][top_parser]
                    if parser_result.get('successful_iterations', 0) > 0:
                        throughput = parser_result['average_throughput_mb_per_second']
                        if throughput > best_throughput:
                            best_throughput = throughput
                            best_scenario = scenario_name
            
            if best_scenario:
                print(f"\n{Terminal.GREEN}Best Performance: {top_parser} achieved {best_throughput:.2f} MB/s on {best_scenario}{Terminal.RESET}")

    @staticmethod
    def display_terminal_summary(results_data):
        """Show formatted results in terminal (legacy single-scenario method)"""
        Terminal.header("BENCHMARK RESULTS SUMMARY")
        
        benchmark_data = results_data['benchmark_performance']
        successful_benchmarks = {
            name: data for name, data in benchmark_data.items()
            if data.get('successful_iterations', 0) > 0
        }
        
        if not successful_benchmarks:
            Terminal.warning("No successful benchmark results to display")
            return
        
        # Display test configuration
        config = results_data['test_parameters']
        print(f"Test Dataset: {config['row_count']:,} rows × {config['column_count']} columns "
              f"({config['file_size_mb']:.2f} MB)")
        print(f"Platform: {results_data['execution_environment']['platform_name']} "
              f"({results_data['execution_environment']['cpu_core_count']} cores)")
        print()
        
        # Sort and display results
        ranked_results = sorted(
            successful_benchmarks.items(),
            key=lambda item: item[1]['average_throughput_mb_per_second'],
            reverse=True
        )
        
        print(f"{'Rank':<5} {'Parser':<20} {'Throughput':<15} {'Avg Time':<12} {'Success':<10}")
        print("-" * 70)
        
        for rank_position, (parser_name, performance_data) in enumerate(ranked_results, 1):
            avg_throughput = performance_data['average_throughput_mb_per_second']
            avg_time = performance_data['average_execution_time_seconds']
            success_rate = (performance_data['successful_iterations'] / 
                          performance_data['total_iterations']) * 100
            
            display_color = Terminal.GREEN if rank_position == 1 else Terminal.RESET
            print(f"{display_color}{rank_position:<5} {parser_name:<20} "
                  f"{avg_throughput:>10.2f} MB/s {avg_time:>9.3f}s "
                  f"{success_rate:>7.1f}%{Terminal.RESET}")

def main():
    argument_parser = argparse.ArgumentParser(
        description="SonicSV Benchmark Runner with Direct Compilation",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    
    # Compilation options
    argument_parser.add_argument("--skip-build", action='store_true',
                               help="Skip compilation phase")
    
    # Test data configuration
    argument_parser.add_argument("--rows", type=int, nargs='*', 
                               default=[10000, 100000, 1000000, 10000000],
                               help="Number of rows in test CSV (multiple values supported)")
    argument_parser.add_argument("--cols", type=int, nargs='*',
                               default=[5, 10, 20],
                               help="Number of columns in test CSV (multiple values supported)")
    argument_parser.add_argument("--cell-size", type=int, nargs='*',
                               default=[4, 8, 16],
                               help="Character length of each cell (multiple values supported)")
    argument_parser.add_argument("--content", choices=['numeric', 'alphabetic', 'mixed'],
                               nargs='*', default=['numeric', 'mixed'],
                               help="Type of cell content (multiple values supported)")
    argument_parser.add_argument("--test-quoted", action='store_true',
                               help="Include quoted field tests")
    argument_parser.add_argument("--test-tsv", action='store_true',
                               help="Include TSV (tab-separated) tests")
    argument_parser.add_argument("--test-empty-cells", action='store_true',
                               help="Include tests with empty cells")
    argument_parser.add_argument("--quick", action='store_true',
                               help="Run quick test suite (smaller datasets)")
    argument_parser.add_argument("--comprehensive", action='store_true',
                               help="Run comprehensive test suite (all combinations)")
    
    # Execution parameters
    argument_parser.add_argument("--iterations", type=int, default=3,
                               help="Benchmark iterations per parser")
    argument_parser.add_argument("--timeout", type=int, default=180,
                               help="Timeout per benchmark (seconds)")
    argument_parser.add_argument("--generation-workers", type=int, 
                               default=max(1, os.cpu_count() - 2),
                               help=f"Number of parallel CSV generation workers (default: {max(1, os.cpu_count() - 2)} = nproc-2)")
    argument_parser.add_argument("--max-concurrent-files", type=int, default=3,
                               help="Maximum number of CSV files to keep in memory")
    argument_parser.add_argument("--no-parallel-generation", action='store_true',
                               help="Disable parallel CSV generation")
    
    # Output configuration
    argument_parser.add_argument("--output", default="benchmark_results.json",
                               help="JSON results output file")
    argument_parser.add_argument("--summary", default="benchmark_summary.txt",
                               help="Text summary output file")
    argument_parser.add_argument("--keep-csv", action='store_true',
                               help="Preserve generated test CSV file")
    
    parsed_arguments = argument_parser.parse_args()
    
    # Display execution environment
    Terminal.header("CSV Parser Benchmark Suite")
    Terminal.info(f"Platform: {platform.system()} {platform.release()}")
    Terminal.info(f"Architecture: {platform.machine()}")
    Terminal.info(f"Available CPU Cores: {os.cpu_count()}")
    
    # Compilation phase
    compiled_benchmarks = {}
    if not parsed_arguments.skip_build:
        compiler = DirectCompiler()
        compiled_benchmarks = compiler.build_all_benchmarks()
        
        if not compiled_benchmarks:
            Terminal.error("No benchmarks compiled successfully")
            return 1
    else:
        # Discover existing benchmarks
        bin_directory = Path("benchmark/bin")
        if bin_directory.exists():
            for executable_file in bin_directory.glob("bench_*"):
                if executable_file.is_file() and os.access(executable_file, os.X_OK):
                    benchmark_name = executable_file.name[6:]  # Remove 'bench_' prefix
                    compiled_benchmarks[benchmark_name] = executable_file
        
        if not compiled_benchmarks:
            Terminal.error("No existing benchmark executables found")
            return 1
    
    # Generate test scenarios
    scenario_manager = TestScenarioManager(parsed_arguments)
    scenarios = scenario_manager.get_scenarios()
    scenario_summary = scenario_manager.get_scenario_summary()
    
    # Display test plan
    Terminal.header("Test Execution Plan")
    Terminal.info(f"Total scenarios: {scenario_summary['total_scenarios']}")
    Terminal.info(f"Estimated total data size: {scenario_summary['estimated_total_size_mb']:.1f} MB")
    Terminal.info(f"File types: {', '.join(scenario_summary['scenario_types'])}")
    Terminal.info(f"Content types: {', '.join(scenario_summary['content_types'])}")
    Terminal.info(f"Size range: {scenario_summary['size_range']['min_rows']:,}-{scenario_summary['size_range']['max_rows']:,} rows, "
                 f"{scenario_summary['size_range']['min_cols']}-{scenario_summary['size_range']['max_cols']} columns")
    
    # Execute benchmarks with threaded CSV management
    performance_benchmarker = PerformanceBenchmarker(compiled_benchmarks)
    all_scenario_results = {}
    
    if parsed_arguments.no_parallel_generation or len(scenarios) == 1:
        # Sequential generation and execution
        Terminal.info("Using sequential CSV generation")
        csv_generator = OptimizedCSVGenerator(max_workers=1)
        generated_files = []
        
        try:
            for i, scenario in enumerate(scenarios, 1):
                Terminal.header(f"Scenario {i}/{len(scenarios)}: {scenario['name']}")
                
                # Generate test file for this scenario
                file_size_mb, filename = csv_generator.generate_from_scenario(scenario)
                generated_files.append(filename)
                
                # Run benchmarks on this scenario
                scenario_results = performance_benchmarker.run_comprehensive_benchmark(
                    filename,
                    iterations_per_benchmark=parsed_arguments.iterations
                )
                
                # Store results with scenario context
                all_scenario_results[scenario['name']] = {
                    'scenario_config': scenario,
                    'file_size_mb': file_size_mb,
                    'benchmark_results': scenario_results
                }
                
                # Cleanup immediately if not keeping files
                if not parsed_arguments.keep_csv:
                    if os.path.exists(filename):
                        os.remove(filename)
                        Terminal.info(f"Cleaned up: {filename}")
                
                Terminal.success(f"Completed scenario {i}/{len(scenarios)}")
        
        finally:
            # Final cleanup
            if not parsed_arguments.keep_csv:
                for filename in generated_files:
                    if os.path.exists(filename):
                        os.remove(filename)
    
    else:
        # True build-test-remove cycle: generate one file at a time
        Terminal.info(f"Using build-test-remove cycle with {parsed_arguments.generation_workers} generation workers")
        csv_generator = OptimizedCSVGenerator(max_workers=parsed_arguments.generation_workers)
        
        for scenario_index, scenario in enumerate(scenarios, 1):
            Terminal.header(f"Scenario {scenario_index}/{len(scenarios)}: {scenario['name']}")
            
            try:
                # BUILD: Generate CSV file for this scenario
                file_size_mb, filename = csv_generator.generate_from_scenario(scenario)
                
                # TEST: Run benchmarks on this file
                scenario_results = performance_benchmarker.run_comprehensive_benchmark(
                    filename,
                    iterations_per_benchmark=parsed_arguments.iterations
                )
                
                # Store results
                all_scenario_results[scenario['name']] = {
                    'scenario_config': scenario,
                    'file_size_mb': file_size_mb,
                    'benchmark_results': scenario_results
                }
                
                # REMOVE: Cleanup immediately after benchmarking
                if not parsed_arguments.keep_csv:
                    try:
                        if os.path.exists(filename):
                            os.remove(filename)
                            Terminal.info(f"Cleaned up: {filename}")
                    except Exception as cleanup_error:
                        Terminal.warning(f"Failed to cleanup {filename}: {cleanup_error}")
                
                Terminal.success(f"Completed scenario {scenario_index}/{len(scenarios)}")
                
            except Exception as e:
                Terminal.error(f"Error processing scenario {scenario['name']}: {e}")
                # Cleanup on error too
                if not parsed_arguments.keep_csv:
                    try:
                        if 'filename' in locals() and os.path.exists(filename):
                            os.remove(filename)
                            Terminal.info(f"Cleaned up after error: {filename}")
                    except:
                        pass
    
    # Compile comprehensive results
    final_results_data = {
        'execution_timestamp': datetime.now().isoformat(),
        'execution_environment': {
            'platform_name': platform.system(),
            'platform_release': platform.release(),
            'machine_architecture': platform.machine(),
            'cpu_core_count': os.cpu_count(),
            'python_version': platform.python_version()
        },
        'test_configuration': {
            'total_scenarios': len(scenarios),
            'scenario_summary': scenario_summary,
            'iterations_per_benchmark': parsed_arguments.iterations,
            'timeout_seconds': parsed_arguments.timeout,
            'test_mode': 'comprehensive' if parsed_arguments.comprehensive else 
                       'quick' if parsed_arguments.quick else 'balanced',
            'parallel_generation': not parsed_arguments.no_parallel_generation and len(scenarios) > 1,
            'generation_workers': parsed_arguments.generation_workers,
            'max_concurrent_files': parsed_arguments.max_concurrent_files
        },
        'scenario_results': all_scenario_results,
        'compiled_benchmarks': list(compiled_benchmarks.keys())
    }
    
    # Generate output reports
    BenchmarkReporter.save_json_report(final_results_data, parsed_arguments.output)
    BenchmarkReporter.generate_multi_scenario_summary(final_results_data, parsed_arguments.summary)
    BenchmarkReporter.display_multi_scenario_summary(final_results_data)
    
    Terminal.success("Multi-scenario benchmark execution completed successfully")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())