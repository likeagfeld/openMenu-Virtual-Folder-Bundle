#!/bin/bash
#
# Quick build script for openMenu with DC Now feature
# Builds using Docker with KallistiOS toolchain
#

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "========================================="
echo "Building openMenu with DC Now Feature"
echo "========================================="
echo ""

# Check if Docker is available
if ! command -v docker &> /dev/null; then
    echo "Error: Docker is not installed or not in PATH"
    echo "Please install Docker to use this build script"
    echo "Or see BUILD_INSTRUCTIONS.md for manual build steps"
    exit 1
fi

# Pull the Docker image if not available
echo "Checking for Docker image..."
if ! docker image inspect sbstnc/openmenu-dev:0.2.2 &> /dev/null; then
    echo "Pulling Docker image (this may take a few minutes)..."
    docker pull sbstnc/openmenu-dev:0.2.2
fi

echo ""
echo "Starting build..."
echo ""

# Run the build in Docker
# Note: --platform linux/amd64 is required for Apple Silicon Macs (runs under Rosetta 2)
docker run --rm \
  --platform linux/amd64 \
  -v "$SCRIPT_DIR":/workspace \
  -w /workspace/openMenu \
  sbstnc/openmenu-dev:0.2.2 \
  /bin/bash -c "
    set -e
    source /opt/toolchains/dc/kos/environ.sh

    echo 'Building libppp (required for DC Now modem support)...'
    cd /opt/toolchains/dc/kos/addons/libppp
    make clean 2>/dev/null || true
    make
    cd /workspace/openMenu

    echo 'Cleaning previous build...'
    rm -rf cmake-build-dc-release build

    echo 'Configuring with CMake (using dc-release preset)...'
    cmake --preset dc-release

    echo 'Building with Ninja...'
    cmake --build cmake-build-dc-release

    echo ''
    echo 'Build successful!'

    # Copy to standard output location
    mkdir -p build/bin
    cp cmake-build-dc-release/bin/1ST_READ.BIN build/bin/
    ls -lh build/bin/1ST_READ.BIN
  "

echo ""
echo "========================================="
echo "Build Complete!"
echo "========================================="
echo ""
echo "Output binary: openMenu/build/bin/1ST_READ.BIN"
echo ""
echo "Next steps:"
echo "  1. Copy 1ST_READ.BIN to your SD card folder 01"
echo "  2. Boot your Dreamcast with GDEMU"
echo "  3. Press L+R triggers to open DC Now popup"
echo ""
echo "For more information, see BUILD_INSTRUCTIONS.md"
echo ""
