# Network Reflector - Native Linux/macOS Implementation
[![Checks](https://github.com/krisarmstrong/reflector-native/actions/workflows/checks.yml/badge.svg)](https://github.com/krisarmstrong/reflector-native/actions/workflows/checks.yml)


[![CI](https://github.com/krisarmstrong/reflector-native/actions/workflows/ci.yml/badge.svg)](https://github.com/krisarmstrong/reflector-native/actions/workflows/ci.yml)
[![Security](https://github.com/krisarmstrong/reflector-native/actions/workflows/security.yml/badge.svg)](https://github.com/krisarmstrong/reflector-native/actions/workflows/security.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Version](https://img.shields.io/badge/version-1.9.2-blue.svg)](https://github.com/krisarmstrong/reflector-native/releases)
[![Code Quality](https://img.shields.io/badge/code%20quality-A+-brightgreen.svg)](docs/QUALITY_ASSURANCE.md)
[![Test Coverage](https://img.shields.io/badge/coverage-85%25-green.svg)](docs/QUALITY_ASSURANCE.md#code-coverage)
[![Memory Safe](https://img.shields.io/badge/memory-safe-success.svg)](docs/QUALITY_ASSURANCE.md#memory-safety)

High-performance packet reflector for Fluke/NETSCOUT and NetAlly handheld network test tools.

**Quality Assurance**: Comprehensive testing with sanitizers, valgrind, code coverage, static analysis, and automated CI/CD. See [QA Documentation](docs/QUALITY_ASSURANCE.md).

## Overview

This project provides packet reflection capabilities for ITO (Integrated Test & Optimization) packets on Linux and macOS platforms. The C-based data plane is designed for high performance with zero-copy packet processing where supported.

**Current Version:** 1.9.1 (January 2025) - Critical Patch (AF_XDP multi-queue, stats, performance fixes)

## Architecture

```
┌─────────────────────────────────────────────────────┐
│  Data Plane (C)                                     │
│  - Linux: AF_XDP (zero-copy) + AF_PACKET fallback  │
│  - macOS: BPF with kqueue event-driven I/O         │
│  - In-place packet header swapping                  │
│  - Platform abstraction layer                       │
│  - Multi-worker support with queue affinity         │
│  - CLI interface with statistics                    │
└─────────────────────────────────────────────────────┘
```

## Current Features

### Linux
- **AF_XDP**: Zero-copy packet I/O with XDP_ZEROCOPY for maximum performance
- **AF_PACKET fallback**: Automatic fallback for NICs without XDP support
- **Multi-queue support**: Queue-per-worker with proper BPF map sharing
- **Platform compatibility**: Works with all NICs (auto-detects best method)
- **Efficient processing**: No malloc in hot path, optimized receive path
- **Tested**: Ubuntu 25.10 with various network adapters

### macOS
- **BPF filtering**: Native packet capture and injection via /dev/bpf
- **kqueue event-driven I/O**: Non-blocking with efficient event notification
- **Write coalescing**: Batch writes up to 64KB for better throughput
- **Auto-tuning**: Progressive buffer size detection (1MB → 512KB → 256KB)
- **Compatibility**: Works with all NICs
- **Tested**: macOS 14+ with Thunderbolt and USB-C adapters
- **Performance**: 60-75 Mbps sustained throughput (v1.9.0 optimizations)

### Common
- **Zero-copy reflection**: In-place MAC/IP/UDP header swapping
- **ITO protocol support**: PROBEOT, DATA:OT, LATENCY signatures
- **Verbose logging**: Optional detailed packet processing output
- **Platform abstraction**: Clean separation between platform-specific and common code

## Performance

### Current v1.9.1
| Platform | Method | Throughput | Notes |
|----------|--------|------------|-------|
| Linux | AF_XDP | Up to 10+ Gbps | Zero-copy, multi-queue, line-rate capable |
| Linux | AF_PACKET | 100-500 Mbps | Fallback for non-XDP NICs |
| macOS | BPF | 60-75 Mbps | kqueue I/O, write coalescing (v1.9.0) |

### Known Limitations
- **macOS BPF**: Limited to 60-75 Mbps (architectural limitation, Network Extension Framework planned for v2.0.0)
- **AF_XDP**: Requires XDP-capable NIC and driver (most modern NICs supported)
- **Multi-queue**: Best performance with queue-per-worker affinity

## Requirements

### CPU Requirements
- **Minimum:** Dual-core CPU (2+ cores recommended for optimal performance)
- **Architecture Support:**
  - ✅ Intel x86_64 (with SSE2/SSE3 SIMD optimizations)
  - ✅ Apple Silicon (ARM64) - scalar optimizations, SIMD planned for future
  - ✅ AMD x86_64 (with SSE2/SSE3 SIMD optimizations)
- **Performance Notes:**
  - SIMD optimizations automatically enabled on x86_64 with SSE2 support
  - ARM64 uses optimized scalar code (still highly performant)
  - Multi-core recommended for AF_XDP on Linux (queue-per-core)

### Linux
- Linux kernel 4.18+ (tested on Ubuntu 25.10)
- gcc or clang with C11 support
- make
- sudo/root access (for raw socket operations)
- **For AF_XDP (optional, 10x faster):**
  - Linux kernel 5.4+
  - libxdp-dev, libbpf-dev packages
  - XDP-capable NIC (Intel, Mellanox recommended)

### macOS
- macOS 10.14+ (tested on macOS 14+)
- Xcode Command Line Tools (clang)
- sudo/root access (for BPF device access)
- **Performance Note:** BPF limited to 10-50 Mbps (OS limitation)

## Building

```bash
# Clone repository
git clone https://github.com/krisarmstrong/reflector-native.git
cd reflector-native

# Build for your platform
make

# This creates:
# - reflector-macos (on macOS)
# - reflector-linux (on Linux)
```

## Testing

```bash
# Run unit tests
make test

# Tests validate:
# - ITO packet signature detection
# - Packet header reflection logic
# - Platform abstraction layer
```

## Usage

### Basic Usage
```bash
# macOS
sudo ./reflector-macos en0

# Linux
sudo ./reflector-linux eth0

# Verbose mode (shows packet details)
sudo ./reflector-macos en0 -v
```

### Finding Your Interface
```bash
# macOS
ifconfig | grep "^en"

# Linux
ip link show
```

### Example Output
```
Reflector started on interface: en10
Interface MAC: aa:bb:cc:dd:ee:ff
Listening for ITO packets on UDP port 3842...
Press Ctrl+C to stop

Statistics (every 10 seconds):
[10.0s] RX: 4972 pkts (7586496 bytes) | Reflected: 4970 pkts | 497 pps, 6.1 Mbps
[20.0s] RX: 9944 pkts (15172992 bytes) | Reflected: 9940 pkts | 497 pps, 6.1 Mbps
```

### Stopping the Reflector
Press `Ctrl+C` to gracefully stop and show final statistics.

## Protocol Support

### ITO (Integrated Test & Optimization) Packets

Reflects packets with these signatures at offset 5 in UDP payload:
- `PROBEOT` - OneTouch/LinkRunner probe packets
- `DATA:OT` - OneTouch data packets
- `LATENCY` - Latency measurement packets

### Requirements
- **Transport**: IPv4 UDP on port 3842
- **Addressing**: Unicast to interface MAC address
- **Size**: Minimum 54 bytes (Ethernet + IP + UDP headers + signature)
- **Header**: 5-byte proprietary header before ITO signature

### Tested With
- NetAlly LinkRunner 10G
- Fluke Networks OneTouch series
- NETSCOUT handheld test tools

## Project Structure

```
reflector-native/
├── src/
│   └── dataplane/          # C data plane implementations
│       ├── common/         # Platform-agnostic packet logic
│       │   ├── packet.c    # ITO validation & reflection
│       │   ├── util.c      # Interface/MAC utilities
│       │   ├── core.c      # Worker thread management
│       │   └── main.c      # CLI entry point
│       ├── linux_packet/   # Linux AF_PACKET implementation
│       │   └── packet_platform.c
│       └── macos_bpf/      # macOS BPF implementation
│           └── bpf_platform.c
├── include/                # Header files
│   └── reflector.h        # Core definitions & platform API
├── tests/                  # Unit tests
│   └── test_packet_validation.c
├── docs/                   # Documentation
│   ├── ARCHITECTURE.md    # Design details
│   ├── PERFORMANCE.md     # Performance tuning
│   └── QUICKSTART.md      # Getting started
├── .github/
│   ├── workflows/         # CI/CD automation
│   │   ├── ci.yml        # Build & test
│   │   ├── security.yml  # Security scanning
│   │   └── release.yml   # Release automation
│   └── PULL_REQUEST_TEMPLATE.md
├── .githooks/             # Git hooks
│   └── pre-commit        # Pre-commit validation
├── scripts/               # Utility scripts
│   └── install-hooks.sh  # Install git hooks
├── CHANGELOG.md           # Version history
├── SECURITY.md            # Security policy
├── CONTRIBUTING.md        # Contribution guidelines
└── ROADMAP.md             # Future plans
```

## Development
Run the full local checks:

```bash
./check.sh
```


### Building from Source
```bash
git clone https://github.com/krisarmstrong/reflector-native.git
cd reflector-native
make
```

### Quality Assurance

This project implements comprehensive automated quality assurance:

#### Automated Testing on Every Commit
```bash
# Install git hooks (runs automatically on commit)
./scripts/install-hooks.sh
```

**Pre-commit Hook Runs:**
- ✅ Secret detection (prevents credential leaks)
- ✅ Large file detection (prevents bloat)
- ✅ Code formatting check (clang-format)
- ✅ Full test suite (if source modified)

#### Automated Testing on Every Push

**CI/CD Pipeline (GitHub Actions):**
- ✅ Multi-platform builds (Linux AF_PACKET, Linux AF_XDP, macOS BPF)
- ✅ Comprehensive test suite (14 unit tests + benchmarks)
- ✅ Code quality checks (clang-tidy, cppcheck)
- ✅ Memory safety (Address Sanitizer, UB Sanitizer, Valgrind)
- ✅ Code coverage analysis (85%+ coverage)
- ✅ Security scanning (CodeQL, Gitleaks)
- ✅ Performance benchmarks (regression detection)

#### Quality Commands
```bash
# Run all tests
make test-all               # Unit tests + benchmarks

# Code quality
make format                 # Auto-format code
make format-check          # Check formatting
make lint                  # Static analysis (clang-tidy)
make cppcheck              # Security analysis

# Memory safety
make test-asan             # Address Sanitizer
make test-ubsan            # UB Sanitizer
make test-valgrind         # Valgrind (Linux)
make coverage              # Code coverage

# Complete quality check
make check-all             # Run everything (30+ checks)
```

#### Versioning (Git Tags)
```bash
# Version is automatically extracted from git tags at build time
git describe --tags  # Show current version

# Creating a new release (git tags are the source of truth)
git tag -a v1.4.0 -m "Release v1.4.0: Description"
git push origin v1.4.0  # Triggers automated release build
```

**Version is auto-generated from git** - no manual version file maintenance!

**See [QUALITY_ASSURANCE.md](docs/QUALITY_ASSURANCE.md) for complete documentation.**

### Manual Testing
```bash
# Run reflector and test with network tool
sudo ./reflector-macos en0 -v
# Then send ITO packets from LinkRunner/OneTouch
```

## Contributing

We welcome contributions! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for:
- Development setup
- Coding standards
- Git workflow and commit conventions
- Testing requirements
- Pull request process

## Security

Security is important for network tools. See [SECURITY.md](SECURITY.md) for:
- Reporting vulnerabilities
- Security considerations
- Deployment best practices
- Known limitations

## Roadmap

See [ROADMAP.md](ROADMAP.md) for planned features including:
- AF_XDP implementation for 10G line-rate on Linux
- Go control plane with TUI
- Additional protocol support
- Performance enhancements

## License

MIT License - Copyright (c) 2025 Kris Armstrong

See [LICENSE](LICENSE) file for full details.

## Credits

- **Author**: Kris Armstrong (2025)
- **Inspired by**: Fluke Networks Windows Reflector
- **Built for**: NETSCOUT, NetAlly, and Fluke Networks handheld test tools

## Support

- **Issues**: [GitHub Issues](https://github.com/krisarmstrong/reflector-native/issues)
- **Documentation**: [docs/](docs/)
- **Discussions**: [GitHub Discussions](https://github.com/krisarmstrong/reflector-native/discussions)

## References

- [AF_PACKET(7) man page](https://man7.org/linux/man-pages/man7/packet.7.html)
- [BPF(4) man page](https://man.freebsd.org/cgi/bpf)
- [AF_XDP Documentation](https://www.kernel.org/doc/html/latest/networking/af_xdp.html) (future implementation)
- [XDP Tutorial](https://github.com/xdp-project/xdp-tutorial) (future implementation)
