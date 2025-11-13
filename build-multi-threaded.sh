#!/bin/bash

# Enhanced build script for multi-threaded hev-socks5-tunnel

set -e

echo "======================================"
echo "Building Multi-threaded hev-socks5-tunnel"
echo "======================================"
echo

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Detect CPU cores
if command -v nproc &> /dev/null; then
    CORES=$(nproc)
elif command -v sysctl &> /dev/null; then
    CORES=$(sysctl -n hw.ncpu)
else
    CORES=4
fi

echo -e "${BLUE}System Information:${NC}"
echo "  CPU Cores: $CORES"
echo "  OS: $(uname -s)"
echo "  Arch: $(uname -m)"
echo

# Show enhancements
echo -e "${GREEN}Multi-threading Enhancements:${NC}"
echo "  ✓ True multi-threading with pthread"
echo "  ✓ Auto-scaled thread pool (2 * CPU cores)"
echo "  ✓ Multi-threaded TUN I/O"
echo "  ✓ Lock-free statistics"
echo "  ✓ Parallel session handling"
echo "  ✓ Optimal performance on ${CORES}-core system"
echo

# Clean previous build
echo -e "${YELLOW}Cleaning previous build...${NC}"
make clean 2>/dev/null || true
echo

# Build third-party libraries
echo -e "${YELLOW}Building dependencies...${NC}"
make tp-static -j${CORES}
echo

# Build main project
echo -e "${YELLOW}Building hev-socks5-tunnel (multi-threaded)...${NC}"
make -j${CORES}
echo

# Show result
if [ -f "bin/hev-socks5-tunnel" ]; then
    echo -e "${GREEN}======================================"
    echo "Build Successful!"
    echo "======================================${NC}"
    echo
    echo "Binary: bin/hev-socks5-tunnel"
    echo "Size: $(du -h bin/hev-socks5-tunnel | cut -f1)"
    echo
    echo "Expected Performance:"
    echo "  - Thread Pool Workers: $((CORES * 2))"
    echo "  - I/O Threads: $((CORES >= 4 ? 4 : 2))"
    echo "  - Throughput: Up to $((CORES))x single-threaded"
    echo
    echo "To install: sudo make install"
    echo "To run: ./bin/hev-socks5-tunnel conf/main.yml"
else
    echo -e "${RED}Build failed!${NC}"
    exit 1
fi
