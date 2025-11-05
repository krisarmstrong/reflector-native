# Build Instructions

## Quick Build

### macOS (Current Platform)
```bash
cd /Users/krisarmstrong/Developer/projects/reflector-native
make
```

This will create: `reflector-macos`

### Linux
```bash
# Install dependencies first
./scripts/setup-linux.sh

# Build
make

# Install (optional)
sudo make install
```

This will create: `reflector-linux` and `src/xdp/filter.bpf.o`

## Build Requirements

### macOS
- Xcode Command Line Tools
- No additional dependencies

### Linux
- gcc or clang
- make
- clang + llvm (for eBPF compilation)
- libbpf-dev
- libxdp-dev
- libelf-dev
- zlib-dev

## Build Outputs

```
reflector-native/
├── reflector-macos      # macOS binary
├── reflector-linux      # Linux binary
├── src/xdp/filter.bpf.o # eBPF program (Linux only)
└── src/dataplane/       # Object files
```

## Testing the Build

### macOS
```bash
# Test (requires sudo)
sudo ./reflector-macos en0

# Should output:
# Network Reflector v1.0.0
# Starting on interface: en0
# ...
```

### Linux
```bash
# Test (requires sudo)
sudo ./reflector-linux eth0

# Verify XDP program loaded
sudo bpftool prog show | grep xdp
```

## Troubleshooting

### macOS: "cannot execute binary file"
```bash
# Check architecture
file reflector-macos

# Should show: Mach-O 64-bit executable arm64 (or x86_64)
```

### Linux: "BPF object file not found"
```bash
# Compile eBPF program manually
cd src/xdp
clang -O2 -target bpf -c filter.bpf.c -o filter.bpf.o
```

### Linux: "undefined reference to xsk_socket__create"
```bash
# Install libxdp
sudo apt install libxdp-dev  # Debian/Ubuntu
sudo dnf install libxdp-devel  # Fedora
```

## Clean Build

```bash
make clean
make
```

## Cross-Compilation

Not currently supported. Build on target platform.

## Installation

```bash
# Install to /usr/local/bin
sudo make install

# Uninstall
sudo make uninstall
```

## Building from Source Archive

If you receive a tarball:

```bash
tar xzf reflector-native.tar.gz
cd reflector-native
make
```

## Development Build

For development with debug symbols:

```bash
# Edit Makefile, change:
CFLAGS := -Wall -Wextra -O3 -march=native -pthread

# To:
CFLAGS := -Wall -Wextra -g -O0 -pthread

# Build
make clean && make
```

## Static Build (Linux)

For portable binaries:

```bash
# Edit Makefile, add to LDFLAGS:
LDFLAGS += -static

# May require static libraries installed
sudo apt install libbpf-dev:static
```
