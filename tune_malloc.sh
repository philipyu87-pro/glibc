#!/bin/bash
# Malloc Tuning Script for Kunpeng 920
# This script helps test different malloc configurations to find optimal settings

set -e

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Malloc Performance Tuning Guide for Kunpeng 920 ===${NC}"
echo ""

# Check if benchmark exists
BENCHMARK="./malloc_benchmark"
if [ ! -f "$BENCHMARK" ]; then
    echo -e "${YELLOW}Benchmark not found. Compiling...${NC}"
    if [ -f "malloc_benchmark.c" ]; then
        gcc -O2 -pthread -o malloc_benchmark malloc_benchmark.c
        echo -e "${GREEN}Compilation successful${NC}"
    else
        echo -e "${RED}Error: malloc_benchmark.c not found${NC}"
        echo "Please ensure malloc_benchmark.c is in the current directory"
        exit 1
    fi
fi

# Get CPU count
CPU_COUNT=$(nproc)
echo "Detected CPUs: $CPU_COUNT"
echo ""

# Test parameters
THREADS=$CPU_COUNT
ITERATIONS=100000

echo -e "${GREEN}Test Configuration:${NC}"
echo "  Threads: $THREADS"
echo "  Iterations per thread: $ITERATIONS"
echo ""

# Function to run benchmark with specific tunables
run_test() {
    local test_name=$1
    local tunables=$2
    
    echo -e "${YELLOW}--- $test_name ---${NC}"
    if [ -n "$tunables" ]; then
        echo "GLIBC_TUNABLES=$tunables"
        export GLIBC_TUNABLES="$tunables"
    else
        echo "Using default settings (no tunables)"
        unset GLIBC_TUNABLES
    fi
    
    $BENCHMARK -t $THREADS -i $ITERATIONS
    echo ""
}

# Test 1: Default configuration
echo -e "${GREEN}=== Test 1: Default Configuration ===${NC}"
run_test "Default" ""

# Test 2: Increased arena count
echo -e "${GREEN}=== Test 2: Increased Arena Count ===${NC}"
ARENA_MAX=$((CPU_COUNT * 2))
run_test "Arena Max = $ARENA_MAX" "glibc.malloc.arena_max=$ARENA_MAX"

# Test 3: Increased tcache size
echo -e "${GREEN}=== Test 3: Increased Tcache ===${NC}"
run_test "Tcache Count = 10" "glibc.malloc.tcache_count=10"

# Test 4: Larger tcache max size
echo -e "${GREEN}=== Test 4: Larger Tcache Max ===${NC}"
run_test "Tcache Max = 512KB" "glibc.malloc.tcache_max=524288"

# Test 5: Combined optimizations
echo -e "${GREEN}=== Test 5: Combined Optimizations ===${NC}"
COMBINED="glibc.malloc.arena_max=$ARENA_MAX:glibc.malloc.tcache_count=10:glibc.malloc.tcache_max=524288"
run_test "Combined" "$COMBINED"

# Test 6: Aggressive settings
echo -e "${GREEN}=== Test 6: Aggressive Settings ===${NC}"
ARENA_MAX=$((CPU_COUNT * 4))
AGGRESSIVE="glibc.malloc.arena_max=$ARENA_MAX:glibc.malloc.tcache_count=16:glibc.malloc.tcache_max=1048576"
run_test "Aggressive" "$AGGRESSIVE"

echo -e "${GREEN}=== Testing Complete ===${NC}"
echo ""
echo "Analysis and Recommendations:"
echo "1. Compare the throughput numbers from each test"
echo "2. Look for the configuration with highest ops/sec"
echo "3. Consider memory usage vs. performance trade-offs"
echo ""
echo "Recommended tunables for Kunpeng 920 multi-threaded workloads:"
echo "  export GLIBC_TUNABLES=\"glibc.malloc.arena_max=$((CPU_COUNT * 2)):glibc.malloc.tcache_count=10\""
echo ""
echo "For extreme high-concurrency scenarios:"
echo "  export GLIBC_TUNABLES=\"glibc.malloc.arena_max=$((CPU_COUNT * 4)):glibc.malloc.tcache_count=16:glibc.malloc.tcache_max=524288\""
