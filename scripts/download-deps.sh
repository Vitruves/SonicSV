#!/usr/bin/env bash

readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
readonly BENCHMARK_DIR="${SCRIPT_DIR}/../benchmark"

declare -A REPOSITORIES=(
    ["semitrivial"]="https://github.com/semitrivial/csv_parser"
    ["d99kris"]="https://github.com/d99kris/rapidcsv"
    ["p-ranav"]="https://github.com/p-ranav/csv2"
    ["ariasdiniz"]="https://github.com/ariasdiniz/csv_parser"
    ["vincentlaucsb"]="https://github.com/vincentlaucsb/csv-parser"
    ["arrow"]="https://github.com/apache/arrow"
    ["libcsv"]="https://github.com/rgamble/libcsv"
    ["rapidcsv"]="https://github.com/d99kris/rapidcsv"
)

log_info() { echo "[*] $*"; }
log_success() { echo "[✓] $*"; }
log_error() { echo "[✗] $*" >&2; }

clone_repository() {
    local name="$1" url="$2"
    local target_dir="${BENCHMARK_DIR}/${name}"
    
    if [[ -d "$target_dir" ]]; then
        log_success "$name already exists"
        return 0
    fi
    
    printf "Cloning %-15s... " "$name"
    if git clone --depth=1 --quiet "$url" "$target_dir" 2>/dev/null; then
        echo "✓"
    else
        echo "✗"
        log_error "Failed to clone $name"
        return 1
    fi
}

main() {
    log_info "Downloading third-party parsers..."
    mkdir -p "$BENCHMARK_DIR"
    
    local total=${#REPOSITORIES[@]} current=0 failed=0
    
    for name in "${!REPOSITORIES[@]}"; do
        ((current++))
        printf "[%d/%d] " "$current" "$total"
        clone_repository "$name" "${REPOSITORIES[$name]}" || ((failed++))
    done
    
    if ((failed > 0)); then
        log_error "$failed repositories failed to download"
        exit 1
    fi
    
    log_success "All repositories downloaded successfully"
}

main "$@"