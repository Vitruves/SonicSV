package main

import (
	"bufio"
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"math/rand"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
)

const (
	ColorReset  = "\033[0m"
	ColorRed    = "\033[31m"
	ColorGreen  = "\033[32m"
	ColorYellow = "\033[33m"
	ColorBlue   = "\033[34m"
	ColorBold   = "\033[1m"
)

type Terminal struct{}

func (t Terminal) Info(msg string) {
	fmt.Printf("[*] %s\n", msg)
}

func (t Terminal) Success(msg string) {
	fmt.Printf("%s[✓] %s%s\n", ColorGreen, msg, ColorReset)
}

func (t Terminal) Warning(msg string) {
	fmt.Printf("%s[!] %s%s\n", ColorYellow, msg, ColorReset)
}

func (t Terminal) Error(msg string) {
	fmt.Printf("%s[✗] %s%s\n", ColorRed, msg, ColorReset)
}

func (t Terminal) Header(msg string) {
	fmt.Printf("\n%s%s%s\n", ColorBold, msg, ColorReset)
}

type BenchmarkConfig struct {
	TargetSizes     []int
	Cols            int
	CellSize        int
	ContentType     string
	Iterations      int
	Timeout         int
	OutputJSON      string
	OutputSummary   string
	KeepFiles       bool
	MaxWorkers      int
	SkipBuild       bool
	TestQuoted      bool
	TestTSV         bool
	TestEmptyCells  bool
}

type Scenario struct {
	Name         string
	Rows         int
	Cols         int
	CellSize     int
	ContentType  string
	Delimiter    string
	Quoted       bool
	EmptyCells   int
	Extension    string
	TargetSizeMB int
}

type BenchmarkResult struct {
	Success                bool    `json:"success"`
	ExecutionTimeSeconds   float64 `json:"execution_time_seconds,omitempty"`
	ThroughputMBPerSecond  float64 `json:"throughput_mb_per_second,omitempty"`
	FileSizeMB             float64 `json:"file_size_mb,omitempty"`
	ErrorType              string  `json:"error_type,omitempty"`
	ErrorMessage           string  `json:"error_message,omitempty"`
}

type ParserResults struct {
	SuccessfulIterations          int               `json:"successful_iterations"`
	TotalIterations              int               `json:"total_iterations"`
	AverageThroughputMBPerSecond float64           `json:"average_throughput_mb_per_second"`
	PeakThroughputMBPerSecond    float64           `json:"peak_throughput_mb_per_second"`
	MinimumThroughputMBPerSecond float64           `json:"minimum_throughput_mb_per_second"`
	AverageExecutionTimeSeconds  float64           `json:"average_execution_time_seconds"`
	FastestExecutionTimeSeconds  float64           `json:"fastest_execution_time_seconds"`
	SlowestExecutionTimeSeconds  float64           `json:"slowest_execution_time_seconds"`
	FileSizeMB                   float64           `json:"file_size_mb"`
	DetailedResults              []BenchmarkResult `json:"detailed_results"`
}

type ScenarioResults struct {
	ScenarioConfig    Scenario                    `json:"scenario_config"`
	FileSizeMB        float64                     `json:"file_size_mb"`
	BenchmarkResults  map[string]ParserResults    `json:"benchmark_results"`
}

type ComprehensiveResults struct {
	ExecutionTimestamp   string                     `json:"execution_timestamp"`
	ExecutionEnvironment map[string]interface{}     `json:"execution_environment"`
	TestConfiguration    map[string]interface{}     `json:"test_configuration"`
	ScenarioResults      map[string]ScenarioResults `json:"scenario_results"`
	CompiledBenchmarks   []string                   `json:"compiled_benchmarks"`
}
type Compiler struct {
	projectRoot    string
	benchmarkDir   string
	binDir         string
	compilerConfig map[string]string
	terminal       Terminal
}

func NewCompiler(projectRoot string) *Compiler {
	benchmarkDir := filepath.Join(projectRoot, "benchmark")
	binDir := filepath.Join(benchmarkDir, "bin")
	
	c := &Compiler{
		projectRoot:  projectRoot,
		benchmarkDir: benchmarkDir,
		binDir:       binDir,
		terminal:     Terminal{},
	}
	
	c.compilerConfig = c.detectCompilers()
	return c
}

func (c *Compiler) detectCompilers() map[string]string {
	compilers := make(map[string]string)
	
	for _, cc := range []string{"gcc", "clang", "cc"} {
		if c.commandExists(cc) {
			compilers["cc"] = cc
			break
		}
	}
	
	for _, cxx := range []string{"g++", "clang++", "c++"} {
		if c.commandExists(cxx) {
			compilers["cxx"] = cxx
			break
		}
	}
	
	if len(compilers) == 0 {
		c.terminal.Error("No C/C++ compilers found")
		return nil
	}
	
	c.terminal.Info(fmt.Sprintf("Using compilers: %v", compilers))
	return compilers
}

func (c *Compiler) commandExists(command string) bool {
	cmd := exec.Command(command, "--version")
	return cmd.Run() == nil
}

func (c *Compiler) getOptimizationFlags() []string {
	baseFlags := []string{"-O3", "-DNDEBUG", "-pthread"}
	
	switch runtime.GOARCH {
	case "amd64":
		baseFlags = append(baseFlags, "-march=native", "-mtune=native")
	case "arm64":
		baseFlags = append(baseFlags, "-mcpu=native")
	}
	
	if runtime.GOOS == "darwin" {
		baseFlags = append(baseFlags, "-Wno-deprecated-declarations")
	}
	
	return baseFlags
}

func (c *Compiler) getBenchmarkDependencies(benchmarkName string) map[string]interface{} {
	dependencies := map[string]map[string]interface{}{
		"ariasdiniz": {
			"sources":  []string{filepath.Join(c.benchmarkDir, "ariasdiniz", "src", "parser.c")},
			"includes": []string{filepath.Join(c.benchmarkDir, "ariasdiniz", "src")},
			"libs":     []string{},
		},
		"arrow": {
			"sources":  []string{},
			"includes": []string{filepath.Join(c.benchmarkDir, "arrow", "cpp", "src", "arrow", "csv", "api.h"), filepath.Join(c.benchmarkDir, "arrow", "cpp", "src", "arrow", "io", "api.h"), filepath.Join(c.benchmarkDir, "arrow", "cpp", "src", "arrow", "util", "logging.h"), filepath.Join(c.benchmarkDir, "arrow", "cpp", "src", "arrow", "compute", "api.h")},
			"libs":     []string{"arrow"},
		},
		"d99kris": {
			"sources":  []string{},
			"includes": []string{filepath.Join(c.benchmarkDir, "d99kris", "src")},
			"libs":     []string{},
		},
		"libcsv": {
			"sources":  []string{},
			"includes": []string{},
			"libs":     []string{"csv"},
		},
		"libcsv_mt": {
			"sources":  []string{},
			"includes": []string{},
			"libs":     []string{"csv"},
		},
		"rapidcsv": {
			"sources":  []string{},
			"includes": []string{filepath.Join(c.benchmarkDir, "rapidcsv", "src")},
			"libs":     []string{},
		},
		"semitrivial": {
			"sources": []string{
				filepath.Join(c.benchmarkDir, "semitrivial", "csv.c"),
				filepath.Join(c.benchmarkDir, "semitrivial", "split.c"),
				filepath.Join(c.benchmarkDir, "semitrivial", "fread_csv_line.c"),
			},
			"includes": []string{filepath.Join(c.benchmarkDir, "semitrivial")},
			"libs":     []string{},
		},
		"sonicsv": {
			"sources":  []string{},
			"includes": []string{filepath.Join(c.projectRoot, "include")},
			"libs":     []string{"m"},
		},
		"sonicsv_batching": {
			"sources":  []string{},
			"includes": []string{filepath.Join(c.projectRoot, "include")},
			"libs":     []string{"m"},
		},
		"sonicsv_mt": {
			"sources":  []string{},
			"includes": []string{filepath.Join(c.projectRoot, "include")},
			"libs":     []string{"m"},
		},
		"sonicsv_streaming": {
			"sources":  []string{},
			"includes": []string{filepath.Join(c.projectRoot, "include")},
			"libs":     []string{"m"},
		},
	}
	
	if deps, exists := dependencies[benchmarkName]; exists {
		return deps
	}
	
	return map[string]interface{}{
		"sources":  []string{},
		"includes": []string{},
		"libs":     []string{},
	}
}

func (c *Compiler) findSourceFiles() map[string]string {
	sources := make(map[string]string)
	
	patterns := []string{
		filepath.Join(c.benchmarkDir, "bench_*.c"),
		filepath.Join(c.benchmarkDir, "bench_*.cpp"),
	}
	
	for _, pattern := range patterns {
		matches, err := filepath.Glob(pattern)
		if err != nil {
			continue
		}
		
		for _, match := range matches {
			base := filepath.Base(match)
			if strings.HasPrefix(base, "bench_") {
				name := strings.TrimPrefix(base, "bench_")
				name = strings.TrimSuffix(name, filepath.Ext(name))
				sources[name] = match
			}
		}
	}
	
	c.terminal.Info(fmt.Sprintf("Found %d benchmark sources", len(sources)))
	return sources
}

func (c *Compiler) checkLibraryAvailability(libName string) bool {
	testProgram := fmt.Sprintf(`
#include <stdio.h>
int main() { return 0; }
`)
	
	tmpFile, err := os.CreateTemp("", "libcheck_*.c")
	if err != nil {
		return false
	}
	defer os.Remove(tmpFile.Name())
	defer tmpFile.Close()
	
	tmpFile.WriteString(testProgram)
	tmpFile.Close()
	
	compiler := c.compilerConfig["cc"]
	if compiler == "" {
		return false
	}
	
	outputFile := tmpFile.Name() + ".out"
	defer os.Remove(outputFile)
	
	cmd := exec.Command(compiler, tmpFile.Name(), "-l"+libName, "-o", outputFile)
	err = cmd.Run()
	
	return err == nil
}

func (c *Compiler) compileBenchmark(name, sourceFile string) bool {
	outputFile := filepath.Join(c.binDir, "bench_"+name)
	
	isCpp := strings.HasSuffix(sourceFile, ".cpp") || strings.HasSuffix(sourceFile, ".cc")
	var compiler string
	var ok bool
	
	if isCpp {
		compiler, ok = c.compilerConfig["cxx"]
	} else {
		compiler, ok = c.compilerConfig["cc"]
	}
	
	if !ok {
		c.terminal.Error(fmt.Sprintf("No suitable compiler for %s", sourceFile))
		return false
	}
	
	dependencies := c.getBenchmarkDependencies(name)
	
	// Check library availability first
	if libs, exists := dependencies["libs"]; exists {
		if libSlice, ok := libs.([]string); ok {
			for _, lib := range libSlice {
				if lib != "m" && !c.checkLibraryAvailability(lib) {
					c.terminal.Warning(fmt.Sprintf("Library '%s' not found for %s, skipping", lib, name))
					return false
				}
			}
		}
	}
	
	cmd := []string{compiler}
	
	// Language standard
	if isCpp {
		cmd = append(cmd, "-std=c++17")
	} else {
		if name == "semitrivial" {
			cmd = append(cmd, "-std=gnu99")
		} else {
			cmd = append(cmd, "-std=c99")
		}
	}
	
	// Optimization flags
	cmd = append(cmd, c.getOptimizationFlags()...)
	
	// SonicSV implementation define
	if strings.HasPrefix(name, "sonicsv") {
		cmd = append(cmd, "-DSONICSV_IMPLEMENTATION")
	}
	
	// Include directories
	if includes, exists := dependencies["includes"]; exists {
		if includeSlice, ok := includes.([]string); ok {
			for _, includeDir := range includeSlice {
				if _, err := os.Stat(includeDir); err == nil {
					cmd = append(cmd, "-I", includeDir)
				}
			}
		}
	}
	
	// Source files
	cmd = append(cmd, sourceFile)
	
	// Additional source files
	if sources, exists := dependencies["sources"]; exists {
		if sourceSlice, ok := sources.([]string); ok {
			for _, depSource := range sourceSlice {
				if _, err := os.Stat(depSource); err == nil {
					cmd = append(cmd, depSource)
				}
			}
		}
	}
	
	// Output file
	cmd = append(cmd, "-o", outputFile)
	
	// Library search paths
	cmd = append(cmd, "-L/usr/local/lib", "-L/usr/lib")
	
	// Libraries
	if libs, exists := dependencies["libs"]; exists {
		if libSlice, ok := libs.([]string); ok {
			for _, lib := range libSlice {
				cmd = append(cmd, "-l"+lib)
			}
		}
	}
	
	c.terminal.Info(fmt.Sprintf("Compiling %s...", name))
	
	execCmd := exec.Command(cmd[0], cmd[1:]...)
	output, err := execCmd.CombinedOutput()
	
	if err != nil {
		c.terminal.Warning(fmt.Sprintf("Failed to build %s", name))
		if len(output) > 0 {
			maxLen := 500
			if len(output) < maxLen {
				maxLen = len(output)
			}
			c.terminal.Error(fmt.Sprintf("Compilation errors:\n%s", string(output[:maxLen])))
		}
		return false
	}
	
	c.terminal.Success(fmt.Sprintf("Built %s", name))
	return true
}

func (c *Compiler) BuildAllBenchmarks() map[string]string {
	if c.compilerConfig == nil {
		c.terminal.Error("No compilers available")
		return nil
	}
	
	os.MkdirAll(c.binDir, 0755)
	
	sources := c.findSourceFiles()
	if len(sources) == 0 {
		c.terminal.Error("No benchmark source files found")
		return nil
	}
	
	compiled := make(map[string]string)
	
	for name, sourceFile := range sources {
		if c.compileBenchmark(name, sourceFile) {
			outputPath := filepath.Join(c.binDir, "bench_"+name)
			if _, err := os.Stat(outputPath); err == nil {
				compiled[name] = outputPath
			}
		}
	}
	
	c.terminal.Success(fmt.Sprintf("Successfully built %d benchmarks", len(compiled)))
	return compiled
}

type CSVGenerator struct {
	maxWorkers int
	terminal   Terminal
}

func NewCSVGenerator(maxWorkers int) *CSVGenerator {
	if maxWorkers <= 0 {
		maxWorkers = max(1, runtime.NumCPU()-2)
	}
	
	return &CSVGenerator{
		maxWorkers: maxWorkers,
		terminal:   Terminal{},
	}
}

func (g *CSVGenerator) generateCellContent(rowIndex, colIndex, cellLength int, contentType string) string {
	seed := (rowIndex*1000 + colIndex) % 100000
	rng := rand.New(rand.NewSource(int64(seed)))
	
	switch contentType {
	case "numeric":
		format := fmt.Sprintf("%%0%dd", cellLength)
		return fmt.Sprintf(format, seed%(pow(10, cellLength)))
	case "alphabetic":
		result := make([]byte, cellLength)
		for i := 0; i < cellLength; i++ {
			result[i] = byte('a' + rng.Intn(26))
		}
		return string(result)
	case "mixed":
		chars := "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"
		result := make([]byte, cellLength)
		for i := 0; i < cellLength; i++ {
			result[i] = chars[rng.Intn(len(chars))]
		}
		return string(result)
	}
	return ""
}

func (g *CSVGenerator) calculateRowsForTarget(targetMB, cols, cellSize int, delimiter string) int {
	headerSize := cols * 10
	bytesPerRow := cols*cellSize + (cols-1)*len(delimiter) + 1
	targetBytes := targetMB * 1024 * 1024
	
	rows := (targetBytes - headerSize) / bytesPerRow
	return max(1000, rows)
}

func (g *CSVGenerator) generateChunk(startRow, endRow, cols, cellSize int, contentType, delimiter string, quoted bool, emptyCellPercent int) []string {
	lines := make([]string, 0, endRow-startRow)
	
	for row := startRow; row < endRow; row++ {
		cells := make([]string, cols)
		
		for col := 0; col < cols; col++ {
			var content string
			
			if emptyCellPercent > 0 && (row*cols+col)%(100/emptyCellPercent) == 0 {
				content = ""
			} else {
				content = g.generateCellContent(row, col, cellSize, contentType)
				if quoted && delimiter == "," {
					content = fmt.Sprintf(`"%s"`, content)
				}
			}
			
			cells[col] = content
		}
		
		lines = append(lines, strings.Join(cells, delimiter))
	}
	
	return lines
}

func (g *CSVGenerator) GenerateFromScenario(scenario Scenario) (float64, string, error) {
	filename := fmt.Sprintf("%s.%s", scenario.Name, scenario.Extension)
	
	g.terminal.Info(fmt.Sprintf("Generating %s (%d MB target)...", filename, scenario.TargetSizeMB))
	
	start := time.Now()
	
	file, err := os.Create(filename)
	if err != nil {
		return 0, "", err
	}
	defer file.Close()
	
	writer := bufio.NewWriterSize(file, 256*1024)
	defer writer.Flush()
	
	headers := make([]string, scenario.Cols)
	for i := 0; i < scenario.Cols; i++ {
		headers[i] = fmt.Sprintf("field_%02d", i)
	}
	writer.WriteString(strings.Join(headers, scenario.Delimiter) + "\n")
	
	chunkSize := max(1000, scenario.Rows/g.maxWorkers)
	
	if scenario.Rows > 10000 && g.maxWorkers > 1 {
		type chunkResult struct {
			startRow int
			lines    []string
		}
		
		chunks := make(chan chunkResult, g.maxWorkers)
		var wg sync.WaitGroup
		
		for startRow := 0; startRow < scenario.Rows; startRow += chunkSize {
			endRow := min(startRow+chunkSize, scenario.Rows)
			
			wg.Add(1)
			go func(start, end int) {
				defer wg.Done()
				lines := g.generateChunk(start, end, scenario.Cols, scenario.CellSize, 
					scenario.ContentType, scenario.Delimiter, scenario.Quoted, scenario.EmptyCells)
				chunks <- chunkResult{start, lines}
			}(startRow, endRow)
		}
		
		go func() {
			wg.Wait()
			close(chunks)
		}()
		
		results := make(map[int][]string)
		for chunk := range chunks {
			results[chunk.startRow] = chunk.lines
		}
		
		for startRow := 0; startRow < scenario.Rows; startRow += chunkSize {
			if lines, exists := results[startRow]; exists {
				for _, line := range lines {
					writer.WriteString(line + "\n")
				}
			}
		}
	} else {
		lines := g.generateChunk(0, scenario.Rows, scenario.Cols, scenario.CellSize,
			scenario.ContentType, scenario.Delimiter, scenario.Quoted, scenario.EmptyCells)
		
		for _, line := range lines {
			writer.WriteString(line + "\n")
		}
	}
	
	writer.Flush()
	file.Close()
	
	stat, err := os.Stat(filename)
	if err != nil {
		return 0, "", err
	}
	
	sizeMB := float64(stat.Size()) / (1024 * 1024)
	duration := time.Since(start)
	rate := sizeMB / duration.Seconds()
	
	g.terminal.Success(fmt.Sprintf("Generated %s: %.2f MB in %.1fs (%.1f MB/s)", 
		filename, sizeMB, duration.Seconds(), rate))
	
	return sizeMB, filename, nil
}

type Benchmarker struct {
	executables map[string]string
	terminal    Terminal
}

func NewBenchmarker(executables map[string]string) *Benchmarker {
	return &Benchmarker{
		executables: executables,
		terminal:    Terminal{},
	}
}

func (b *Benchmarker) executeSingleRun(benchmarkName, csvFile string, timeout int) BenchmarkResult {
	executable := b.executables[benchmarkName]
	
	stat, err := os.Stat(csvFile)
	if err != nil {
		return BenchmarkResult{
			Success:      false,
			ErrorType:    "file_stat_error",
			ErrorMessage: err.Error(),
		}
	}
	
	fileSizeMB := float64(stat.Size()) / (1024 * 1024)
	
	start := time.Now()
	
	ctx := context.Background()
	if timeout > 0 {
		var cancel context.CancelFunc
		ctx, cancel = context.WithTimeout(ctx, time.Duration(timeout)*time.Second)
		defer cancel()
	}
	
	cmd := exec.CommandContext(ctx, executable, csvFile)
	err = cmd.Run()
	
	duration := time.Since(start).Seconds()
	
	if err != nil {
		if ctx.Err() == context.DeadlineExceeded {
			return BenchmarkResult{
				Success:   false,
				ErrorType: "timeout",
			}
		}
		return BenchmarkResult{
			Success:      false,
			ErrorType:    "execution_error",
			ErrorMessage: err.Error(),
		}
	}
	
	throughput := fileSizeMB / duration
	
	return BenchmarkResult{
		Success:               true,
		ExecutionTimeSeconds:  duration,
		ThroughputMBPerSecond: throughput,
		FileSizeMB:            fileSizeMB,
	}
}

func (b *Benchmarker) RunComprehensiveBenchmark(csvFile string, iterations int, timeout int) map[string]ParserResults {
	b.terminal.Header(fmt.Sprintf("Performance Benchmark: %s", filepath.Base(csvFile)))
	
	stat, _ := os.Stat(csvFile)
	fileSizeMB := float64(stat.Size()) / (1024 * 1024)
	b.terminal.Info(fmt.Sprintf("Test file size: %.2f MB", fileSizeMB))
	
	results := make(map[string]ParserResults)
	
	for benchmarkName := range b.executables {
		b.terminal.Info(fmt.Sprintf("Benchmarking %s...", benchmarkName))
		
		var successfulRuns []BenchmarkResult
		
		for i := 0; i < iterations; i++ {
			result := b.executeSingleRun(benchmarkName, csvFile, timeout)
			
			if result.Success {
				successfulRuns = append(successfulRuns, result)
				b.terminal.Info(fmt.Sprintf("  Iteration %d: %.2f MB/s", i+1, result.ThroughputMBPerSecond))
			} else {
				b.terminal.Warning(fmt.Sprintf("  Iteration %d: FAILED (%s)", i+1, result.ErrorType))
			}
		}
		
		if len(successfulRuns) > 0 {
			throughputs := make([]float64, len(successfulRuns))
			times := make([]float64, len(successfulRuns))
			
			for i, run := range successfulRuns {
				throughputs[i] = run.ThroughputMBPerSecond
				times[i] = run.ExecutionTimeSeconds
			}
			
			results[benchmarkName] = ParserResults{
				SuccessfulIterations:          len(successfulRuns),
				TotalIterations:              iterations,
				AverageThroughputMBPerSecond:  average(throughputs),
				PeakThroughputMBPerSecond:     maxFloat64(throughputs...),
				MinimumThroughputMBPerSecond:  minFloat64(throughputs...),
				AverageExecutionTimeSeconds:   average(times),
				FastestExecutionTimeSeconds:   minFloat64(times...),
				SlowestExecutionTimeSeconds:   maxFloat64(times...),
				FileSizeMB:                    fileSizeMB,
				DetailedResults:               successfulRuns,
			}
			
			avgThroughput := results[benchmarkName].AverageThroughputMBPerSecond
			b.terminal.Success(fmt.Sprintf("%s: %.2f MB/s average", benchmarkName, avgThroughput))
		} else {
			results[benchmarkName] = ParserResults{
				SuccessfulIterations: 0,
				TotalIterations:      iterations,
			}
			b.terminal.Error(fmt.Sprintf("%s: Complete failure", benchmarkName))
		}
	}
	
	return results
}

type ScenarioManager struct {
	config   BenchmarkConfig
	terminal Terminal
}

func NewScenarioManager(config BenchmarkConfig) *ScenarioManager {
	return &ScenarioManager{
		config:   config,
		terminal: Terminal{},
	}
}

func (sm *ScenarioManager) GenerateScenarios() []Scenario {
	var scenarios []Scenario
	
	generator := NewCSVGenerator(1)
	
	for _, targetMB := range sm.config.TargetSizes {
		rows := generator.calculateRowsForTarget(targetMB, sm.config.Cols, sm.config.CellSize, ",")
		
		scenario := Scenario{
			Name:         fmt.Sprintf("csv_%s_%dmb_%dr_%dc", sm.config.ContentType, targetMB, rows, sm.config.Cols),
			Rows:         rows,
			Cols:         sm.config.Cols,
			CellSize:     sm.config.CellSize,
			ContentType:  sm.config.ContentType,
			Delimiter:    ",",
			Quoted:       false,
			EmptyCells:   0,
			Extension:    "csv",
			TargetSizeMB: targetMB,
		}
		scenarios = append(scenarios, scenario)
		
		if sm.config.TestQuoted {
			quotedScenario := scenario
			quotedScenario.Name = fmt.Sprintf("csv_quoted_%s_%dmb_%dr_%dc", sm.config.ContentType, targetMB, rows, sm.config.Cols)
			quotedScenario.Quoted = true
			scenarios = append(scenarios, quotedScenario)
		}
		
		if sm.config.TestTSV {
			tsvRows := generator.calculateRowsForTarget(targetMB, sm.config.Cols, sm.config.CellSize, "\t")
			tsvScenario := scenario
			tsvScenario.Name = fmt.Sprintf("tsv_%s_%dmb_%dr_%dc", sm.config.ContentType, targetMB, tsvRows, sm.config.Cols)
			tsvScenario.Rows = tsvRows
			tsvScenario.Delimiter = "\t"
			tsvScenario.Extension = "tsv"
			scenarios = append(scenarios, tsvScenario)
		}
		
		if sm.config.TestEmptyCells {
			emptyScenario := scenario
			emptyScenario.Name = fmt.Sprintf("csv_empty_%s_%dmb_%dr_%dc", sm.config.ContentType, targetMB, rows, sm.config.Cols)
			emptyScenario.EmptyCells = 10
			scenarios = append(scenarios, emptyScenario)
		}
	}
	
	return scenarios
}

type Reporter struct {
	terminal Terminal
}

func NewReporter() *Reporter {
	return &Reporter{terminal: Terminal{}}
}

func (r *Reporter) SaveJSONReport(results ComprehensiveResults, outputFile string) error {
	data, err := json.MarshalIndent(results, "", "  ")
	if err != nil {
		return err
	}
	
	err = os.WriteFile(outputFile, data, 0644)
	if err != nil {
		return err
	}
	
	r.terminal.Success(fmt.Sprintf("JSON report saved: %s", outputFile))
	return nil
}

func (r *Reporter) GenerateTextSummary(results ComprehensiveResults, outputFile string) error {
	file, err := os.Create(outputFile)
	if err != nil {
		return err
	}
	defer file.Close()
	
	writer := bufio.NewWriter(file)
	defer writer.Flush()
	
	writer.WriteString("Multi-Scenario CSV Parser Performance Analysis\n")
	writer.WriteString(strings.Repeat("=", 80) + "\n\n")
	
	env := results.ExecutionEnvironment
	writer.WriteString("Execution Environment:\n")
	writer.WriteString(fmt.Sprintf("  Platform: %v %v\n", env["platform"], env["arch"]))
	writer.WriteString(fmt.Sprintf("  CPU Cores: %v\n", env["cpu_cores"]))
	writer.WriteString(fmt.Sprintf("  Go Version: %v\n", env["go_version"]))
	writer.WriteString("\n")
	
	config := results.TestConfiguration
	writer.WriteString("Test Configuration:\n")
	writer.WriteString(fmt.Sprintf("  Total Scenarios: %v\n", config["total_scenarios"]))
	writer.WriteString(fmt.Sprintf("  Target Sizes: %v MB\n", config["target_sizes_mb"]))
	writer.WriteString(fmt.Sprintf("  Columns: %v\n", config["columns"]))
	writer.WriteString(fmt.Sprintf("  Cell Size: %v\n", config["cell_size"]))
	writer.WriteString(fmt.Sprintf("  Content Type: %v\n", config["content_type"]))
	writer.WriteString(fmt.Sprintf("  Iterations: %v\n", config["iterations"]))
	writer.WriteString("\n")
	
	allParserResults := make(map[string][]float64)
	
	for _, scenarioData := range results.ScenarioResults {
		for parserName, parserData := range scenarioData.BenchmarkResults {
			if parserData.SuccessfulIterations > 0 {
				if _, exists := allParserResults[parserName]; !exists {
					allParserResults[parserName] = make([]float64, 0)
				}
				allParserResults[parserName] = append(allParserResults[parserName], parserData.AverageThroughputMBPerSecond)
			}
		}
	}
	
	writer.WriteString("Overall Performance Summary:\n")
	writer.WriteString(strings.Repeat("-", 80) + "\n")
	
	type parserAggregate struct {
		name           string
		avgThroughput  float64
		maxThroughput  float64
		minThroughput  float64
		scenariosCount int
	}
	
	var aggregates []parserAggregate
	totalScenarios := len(results.ScenarioResults)
	
	for parserName, throughputs := range allParserResults {
		agg := parserAggregate{
			name:           parserName,
			avgThroughput:  average(throughputs),
			maxThroughput:  maxFloat64(throughputs...),
			minThroughput:  minFloat64(throughputs...),
			scenariosCount: len(throughputs),
		}
		aggregates = append(aggregates, agg)
	}
	
	sort.Slice(aggregates, func(i, j int) bool {
		return aggregates[i].avgThroughput > aggregates[j].avgThroughput
	})
	
	for rank, agg := range aggregates {
		completionRate := float64(agg.scenariosCount) / float64(totalScenarios) * 100
		writer.WriteString(fmt.Sprintf("%2d. %-20s Avg: %8.2f MB/s Max: %8.2f MB/s (%5.1f%% scenarios)\n",
			rank+1, agg.name, agg.avgThroughput, agg.maxThroughput, completionRate))
	}
	
	writer.WriteString("\nDetailed Scenario Results:\n")
	writer.WriteString(strings.Repeat("-", 80) + "\n")
	
	var sortedScenarios []string
	for scenarioName := range results.ScenarioResults {
		sortedScenarios = append(sortedScenarios, scenarioName)
	}
	
	sort.Strings(sortedScenarios)
	
	for _, scenarioName := range sortedScenarios {
		scenarioData := results.ScenarioResults[scenarioName]
		config := scenarioData.ScenarioConfig
		
		writer.WriteString(fmt.Sprintf("\nScenario: %s\n", scenarioName))
		writer.WriteString(fmt.Sprintf("  Configuration: %d rows × %d cols, %s content, %.2f MB\n",
			config.Rows, config.Cols, config.ContentType, scenarioData.FileSizeMB))
		
		var scenarioResults []struct {
			name       string
			throughput float64
		}
		
		for parserName, parserData := range scenarioData.BenchmarkResults {
			if parserData.SuccessfulIterations > 0 {
				scenarioResults = append(scenarioResults, struct {
					name       string
					throughput float64
				}{parserName, parserData.AverageThroughputMBPerSecond})
			}
		}
		
		sort.Slice(scenarioResults, func(i, j int) bool {
			return scenarioResults[i].throughput > scenarioResults[j].throughput
		})
		
		if len(scenarioResults) > 0 {
			for rank, result := range scenarioResults {
				writer.WriteString(fmt.Sprintf("    %d. %-15s %8.2f MB/s\n",
					rank+1, result.name, result.throughput))
			}
		} else {
			writer.WriteString("    No successful results for this scenario\n")
		}
	}
	
	writer.Flush()
	r.terminal.Success(fmt.Sprintf("Summary report generated: %s", outputFile))
	return nil
}

func (r *Reporter) DisplayMultiScenarioSummary(results ComprehensiveResults) {
	r.terminal.Header("MULTI-SCENARIO BENCHMARK RESULTS")
	
	config := results.TestConfiguration
	env := results.ExecutionEnvironment
	
	fmt.Printf("Test Suite: %v scenarios\n", config["total_scenarios"])
	fmt.Printf("Platform: %v (%v cores)\n", env["platform"], env["cpu_cores"])
	fmt.Println()
	
	allParserResults := make(map[string][]float64)
	
	for _, scenarioData := range results.ScenarioResults {
		for parserName, parserData := range scenarioData.BenchmarkResults {
			if parserData.SuccessfulIterations > 0 {
				if _, exists := allParserResults[parserName]; !exists {
					allParserResults[parserName] = make([]float64, 0)
				}
				allParserResults[parserName] = append(allParserResults[parserName], parserData.AverageThroughputMBPerSecond)
			}
		}
	}
	
	if len(allParserResults) == 0 {
		r.terminal.Warning("No successful benchmark results to display")
		return
	}
	
	fmt.Printf("%-5s %-20s %-15s %-15s %-12s\n", "Rank", "Parser", "Avg Throughput", "Max Throughput", "Scenarios")
	fmt.Println(strings.Repeat("-", 75))
	
	type parserStat struct {
		name      string
		avg       float64
		max       float64
		scenarios int
	}
	
	var stats []parserStat
	totalScenarios := len(results.ScenarioResults)
	
	for parserName, throughputs := range allParserResults {
		stat := parserStat{
			name:      parserName,
			avg:       average(throughputs),
			max:       maxFloat64(throughputs...),
			scenarios: len(throughputs),
		}
		stats = append(stats, stat)
	}
	
	sort.Slice(stats, func(i, j int) bool {
		return stats[i].avg > stats[j].avg
	})
	
	for rank, stat := range stats {
		completionRate := float64(stat.scenarios) / float64(totalScenarios) * 100
		color := ""
		reset := ""
		if rank == 0 {
			color = ColorGreen
			reset = ColorReset
		}
		
		fmt.Printf("%s%-5d %-20s %10.2f MB/s %10.2f MB/s %3d/%d (%.1f%%)%s\n",
			color, rank+1, stat.name, stat.avg, stat.max, stat.scenarios, totalScenarios, completionRate, reset)
	}
	
	if len(stats) > 0 {
		topParser := stats[0]
		bestThroughput := topParser.max
		bestScenario := ""
		
		for scenarioName, scenarioData := range results.ScenarioResults {
			if parserData, exists := scenarioData.BenchmarkResults[topParser.name]; exists {
				if parserData.SuccessfulIterations > 0 && parserData.PeakThroughputMBPerSecond == bestThroughput {
					bestScenario = scenarioName
					break
				}
			}
		}
		
		if bestScenario != "" {
			fmt.Printf("\n%sBest Performance: %s achieved %.2f MB/s on %s%s\n",
				ColorGreen, topParser.name, bestThroughput, bestScenario, ColorReset)
		}
	}
 }
 
 // Utility functions
 func min(a, b int) int {
	if a < b {
		return a
	}
	return b
 }
 
 func max(a, b int) int {
	if a > b {
		return a
	}
	return b
 }
 
 func minFloat64(values ...float64) float64 {
	if len(values) == 0 {
		return 0
	}
	result := values[0]
	for _, v := range values[1:] {
		if v < result {
			result = v
		}
	}
	return result
 }
 
 func maxFloat64(values ...float64) float64 {
	if len(values) == 0 {
		return 0
	}
	result := values[0]
	for _, v := range values[1:] {
		if v > result {
			result = v
		}
	}
	return result
 }
 
 func average(values []float64) float64 {
	if len(values) == 0 {
		return 0
	}
	sum := 0.0
	for _, v := range values {
		sum += v
	}
	return sum / float64(len(values))
 }
 
 func pow(base, exp int) int {
	result := 1
	for i := 0; i < exp; i++ {
		result *= base
	}
	return result
 }
 
 func getKeys(m map[string]string) []string {
	keys := make([]string, 0, len(m))
	for k := range m {
		keys = append(keys, k)
	}
	sort.Strings(keys)
	return keys
 }
 
 func main() {
	config := BenchmarkConfig{}
	
	var targetSizesStr string
	flag.StringVar(&targetSizesStr, "sizes", "1,10,100,1000", "Target file sizes in MB (comma-separated)")
	flag.IntVar(&config.Cols, "cols", 10, "Number of columns")
	flag.IntVar(&config.CellSize, "cell-size", 8, "Character length per cell")
	flag.StringVar(&config.ContentType, "content", "mixed", "Content type: numeric, alphabetic, mixed")
	flag.IntVar(&config.Iterations, "iterations", 3, "Number of benchmark iterations")
	flag.IntVar(&config.Timeout, "timeout", 180, "Timeout per benchmark (seconds)")
	flag.StringVar(&config.OutputJSON, "output", "benchmark_results.json", "JSON output file")
	flag.StringVar(&config.OutputSummary, "summary", "benchmark_summary.txt", "Summary output file")
	flag.BoolVar(&config.KeepFiles, "keep-files", false, "Keep generated test files")
	flag.IntVar(&config.MaxWorkers, "workers", 0, "Maximum parallel workers (0 = auto)")
	flag.BoolVar(&config.SkipBuild, "skip-build", false, "Skip compilation phase")
	flag.BoolVar(&config.TestQuoted, "test-quoted", false, "Include quoted field tests")
	flag.BoolVar(&config.TestTSV, "test-tsv", false, "Include TSV tests")
	flag.BoolVar(&config.TestEmptyCells, "test-empty", false, "Include empty cell tests")
	flag.Parse()
	
	sizeStrings := strings.Split(targetSizesStr, ",")
	config.TargetSizes = make([]int, 0, len(sizeStrings))
	for _, sizeStr := range sizeStrings {
		size, err := strconv.Atoi(strings.TrimSpace(sizeStr))
		if err != nil {
			log.Fatalf("Invalid size: %s", sizeStr)
		}
		config.TargetSizes = append(config.TargetSizes, size)
	}
	
	if config.MaxWorkers == 0 {
		config.MaxWorkers = max(1, runtime.NumCPU()-2)
	}
	
	terminal := Terminal{}
	
	terminal.Header("CSV Parser Benchmark Suite")
	terminal.Info(fmt.Sprintf("Platform: %s %s", runtime.GOOS, runtime.GOARCH))
	terminal.Info(fmt.Sprintf("CPU Cores: %d", runtime.NumCPU()))
	terminal.Info(fmt.Sprintf("Target sizes: %v MB", config.TargetSizes))
	terminal.Info(fmt.Sprintf("Workers: %d", config.MaxWorkers))
	
	var compiledBenchmarks map[string]string
	if !config.SkipBuild {
		compiler := NewCompiler(".")
		compiledBenchmarks = compiler.BuildAllBenchmarks()
		
		if len(compiledBenchmarks) == 0 {
			terminal.Error("No benchmarks compiled successfully")
			return
		}
	} else {
		compiledBenchmarks = make(map[string]string)
		binDir := "benchmark/bin"
		
		entries, err := os.ReadDir(binDir)
		if err != nil {
			terminal.Error("Cannot read benchmark directory")
			return
		}
		
		for _, entry := range entries {
			if strings.HasPrefix(entry.Name(), "bench_") && !entry.IsDir() {
				name := strings.TrimPrefix(entry.Name(), "bench_")
				path := filepath.Join(binDir, entry.Name())
				
				if stat, err := os.Stat(path); err == nil && stat.Mode()&0111 != 0 {
					compiledBenchmarks[name] = path
				}
			}
		}
		
		if len(compiledBenchmarks) == 0 {
			terminal.Error("No existing benchmark executables found")
			return
		}
	}
	
	terminal.Success(fmt.Sprintf("Found %d benchmarks: %v", len(compiledBenchmarks), getKeys(compiledBenchmarks)))
	
	scenarioManager := NewScenarioManager(config)
	scenarios := scenarioManager.GenerateScenarios()
	
	terminal.Header("Test Execution Plan")
	terminal.Info(fmt.Sprintf("Total scenarios: %d", len(scenarios)))
	
	totalEstimatedMB := 0
	for _, scenario := range scenarios {
		totalEstimatedMB += scenario.TargetSizeMB
	}
	terminal.Info(fmt.Sprintf("Total estimated size: %d MB", totalEstimatedMB))
	
	csvGenerator := NewCSVGenerator(config.MaxWorkers)
	benchmarker := NewBenchmarker(compiledBenchmarks)
	allScenarioResults := make(map[string]ScenarioResults)
	
	for i, scenario := range scenarios {
		terminal.Header(fmt.Sprintf("Scenario %d/%d: %s", i+1, len(scenarios), scenario.Name))
		
		fileSizeMB, filename, err := csvGenerator.GenerateFromScenario(scenario)
		if err != nil {
			terminal.Error(fmt.Sprintf("Failed to generate scenario %s: %v", scenario.Name, err))
			continue
		}
		
		benchmarkResults := benchmarker.RunComprehensiveBenchmark(filename, config.Iterations, config.Timeout)
		
		allScenarioResults[scenario.Name] = ScenarioResults{
			ScenarioConfig:   scenario,
			FileSizeMB:       fileSizeMB,
			BenchmarkResults: benchmarkResults,
		}
		
		if !config.KeepFiles {
			if err := os.Remove(filename); err != nil {
				terminal.Warning(fmt.Sprintf("Failed to remove %s: %v", filename, err))
			} else {
				terminal.Info(fmt.Sprintf("Cleaned up: %s", filename))
			}
		}
		
		terminal.Success(fmt.Sprintf("Completed scenario %d/%d", i+1, len(scenarios)))
	}
	
	results := ComprehensiveResults{
		ExecutionTimestamp: time.Now().Format(time.RFC3339),
		ExecutionEnvironment: map[string]interface{}{
			"platform":   runtime.GOOS,
			"arch":       runtime.GOARCH,
			"cpu_cores":  runtime.NumCPU(),
			"go_version": runtime.Version(),
		},
		TestConfiguration: map[string]interface{}{
			"total_scenarios":      len(scenarios),
			"target_sizes_mb":      config.TargetSizes,
			"columns":              config.Cols,
			"cell_size":            config.CellSize,
			"content_type":         config.ContentType,
			"iterations":           config.Iterations,
			"timeout_seconds":      config.Timeout,
			"max_workers":          config.MaxWorkers,
			"test_quoted":          config.TestQuoted,
			"test_tsv":             config.TestTSV,
			"test_empty_cells":     config.TestEmptyCells,
		},
		ScenarioResults:    allScenarioResults,
		CompiledBenchmarks: getKeys(compiledBenchmarks),
	}
	
	reporter := NewReporter()
	
	if err := reporter.SaveJSONReport(results, config.OutputJSON); err != nil {
		terminal.Error(fmt.Sprintf("Failed to save JSON report: %v", err))
	}
	
	if err := reporter.GenerateTextSummary(results, config.OutputSummary); err != nil {
		terminal.Error(fmt.Sprintf("Failed to generate summary: %v", err))
	}
	
	reporter.DisplayMultiScenarioSummary(results)
	
	terminal.Success("Multi-scenario benchmark execution completed successfully")
 }