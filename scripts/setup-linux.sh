#!/bin/bash
# Setup script for Linux dependencies

set -e

echo "Installing dependencies for Linux reflector..."

# Detect package manager
if command -v apt-get &> /dev/null; then
    sudo apt-get update
    sudo apt-get install -y \
        build-essential \
        clang \
        llvm \
        libbpf-dev \
        libxdp-dev \
        libelf-dev \
        zlib1g-dev \
        pkg-config
elif command -v dnf &> /dev/null; then
    sudo dnf install -y \
        gcc \
        make \
        clang \
        llvm \
        libbpf-devel \
        libxdp-devel \
        elfutils-libelf-devel \
        zlib-devel
elif command -v pacman &> /dev/null; then
    sudo pacman -S --needed \
        base-devel \
        clang \
        llvm \
        libbpf \
        libxdp \
        elfutils \
        zlib
else
    echo "Unsupported package manager. Please install dependencies manually:"
    echo "  - build tools (gcc, make)"
    echo "  - clang, llvm"
    echo "  - libbpf-dev, libxdp-dev"
    echo "  - libelf-dev, zlib-dev"
    exit 1
fi

echo "Dependencies installed successfully!"
echo ""
echo "Next steps:"
echo "  1. Run 'make' to build"
echo "  2. Run 'sudo make install' to install"
echo "  3. Run 'sudo reflector <interface>' to start"
