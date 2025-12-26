# Network Reflector - Native Linux/macOS Implementation
[![Checks](https://github.com/krisarmstrong/reflector-native/actions/workflows/checks.yml/badge.svg)](https://github.com/krisarmstrong/reflector-native/actions/workflows/checks.yml)
[![CI](https://github.com/krisarmstrong/reflector-native/actions/workflows/ci.yml/badge.svg)](https://github.com/krisarmstrong/reflector-native/actions/workflows/ci.yml)
[![Security](https://github.com/krisarmstrong/reflector-native/actions/workflows/security.yml/badge.svg)](https://github.com/krisarmstrong/reflector-native/actions/workflows/security.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Version](https://img.shields.io/badge/version-2.0.0-blue.svg)](https://github.com/krisarmstrong/reflector-native/releases)
[![Code Quality](https://img.shields.io/badge/code%20quality-A+-brightgreen.svg)](docs/QUALITY_ASSURANCE.md)
[![Test Coverage](https://img.shields.io/badge/coverage-85%25-green.svg)](docs/QUALITY_ASSURANCE.md#code-coverage)
[![Memory Safe](https://img.shields.io/badge/memory-safe-success.svg)](docs/QUALITY_ASSURANCE.md#memory-safety)

High-performance packet reflector for Fluke/NETSCOUT and NetAlly handheld network test tools.

**Quality Assurance**: Comprehensive testing with sanitizers, valgrind, code coverage, static analysis, and automated CI/CD. See [QA Documentation](docs/QUALITY_ASSURANCE.md).

## Overview

This project provides packet reflection capabilities for ITO (Integrated Test & Optimization) packets on Linux and macOS platforms. The C-based data plane is designed for high performance with zero-copy packet processing where supported.

**Current Version:** 2.0.0 (December 2025) - Go control plane, TUI/Web UI, packaging, IPv6/VLAN support

## What's New in v2.0

- **Go Control Plane** - TUI dashboard and embedded Web UI in a single binary
- **Terminal UI** - Real-time stats, signature breakdown, latency histogram
- **Web Dashboard** - React-based UI accessible via `--web` flag
- **YAML Config** - Configuration file support with `--config`
- **IPv6 Support** - Full IPv6 packet reflection with UDP checksum
- **VLAN 802.1Q** - Tagged packet handling
- **NIC Detection** - Runtime recommendations for optimal platform
- **Packaging** - `.deb`, `.rpm`, and macOS `.pkg` installers
- **Service Integration** - systemd and launchd support

## Architecture

```
┌──────────────────────────────────────────────────────────────────────────┐
│                           CLI / Configuration                             │
│  --port 3842  --oui 00:c0:17  --mode all  --dpdk  --latency  --json     │
└─────────────────────────────────────┬────────────────────────────────────┘
                                      │
┌─────────────────────────────────────▼────────────────────────────────────┐
│                        Packet Validation Layer                            │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐      │
│  │ Dst MAC     │→ │ Src OUI     │→ │ UDP Port    │→ │ ITO Sig     │      │
│  │ Check       │  │ 00:c0:17    │  │ 3842        │  │ PROBEOT etc │      │
│  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘      │
└─────────────────────────────────────┬────────────────────────────────────┘
                                      │
┌─────────────────────────────────────▼────────────────────────────────────┐
│                         Reflection Engine                                 │
│  ┌─────────────────────────────────────────────────────────────────┐     │
│  │ Mode: MAC        │ Mode: MAC+IP      │ Mode: ALL (default)      │     │
│  │ Swap: ETH only   │ Swap: ETH + IP    │ Swap: ETH + IP + UDP     │     │
│  │ (Layer 2)        │ (Layer 3)         │ (Layer 4)                │     │
│  └─────────────────────────────────────────────────────────────────┘     │
│  SIMD Optimized: SSE2 (x86_64) / NEON (ARM64)                            │
└─────────────────────────────────────┬────────────────────────────────────┘
                                      │
┌─────────────────────────────────────▼────────────────────────────────────┐
│                       Platform Abstraction Layer                          │
├──────────────────┬──────────────────┬──────────────────┬─────────────────┤
│  DPDK (100G)     │  AF_XDP (40G)    │  AF_PACKET       │  macOS BPF      │
│  --dpdk flag     │  Default Linux   │  Fallback        │  Default macOS  │
│  100+ Gbps       │  10-40 Gbps      │  100-500 Mbps    │  10-50 Mbps     │
│  Poll-mode NIC   │  Zero-copy eBPF  │  Kernel copy     │  /dev/bpf       │
└──────────────────┴──────────────────┴──────────────────┴─────────────────┘
```

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for detailed design documentation.

## Current Features

### Linux
- **DPDK (v1.10+)**: 100G+ line-rate with poll-mode drivers (requires `--dpdk` flag)
- **AF_XDP**: Zero-copy packet I/O with XDP_ZEROCOPY for 10-40 Gbps
- **AF_PACKET fallback**: Automatic fallback for NICs without XDP support
- **Multi-queue support**: Queue-per-worker with RSS flow distribution
- **Multi-client support**: Handle multiple test tools simultaneously (RSS distributes load)
- **Platform compatibility**: Works with all NICs (auto-detects best method)
- **Tested**: Ubuntu 24.04+ with Intel, Mellanox, and various NICs

### macOS
- **BPF filtering**: Native packet capture and injection via /dev/bpf
- **kqueue event-driven I/O**: Non-blocking with efficient event notification
- **Write coalescing**: Batch writes up to 64KB for better throughput
- **Auto-tuning**: Progressive buffer size detection (1MB → 512KB → 256KB)
- **Compatibility**: Works with all NICs
- **Tested**: macOS 14+ with Thunderbolt and USB-C adapters
- **Performance**: 60-75 Mbps sustained throughput

### Packet Filtering (v1.11+)
- **Port filtering**: `--port N` - Filter by UDP port (default: 3842, 0 = any)
- **OUI filtering**: `--oui XX:XX:XX` - Filter by source MAC vendor (default: 00:c0:17 NetAlly)
- **Disable OUI check**: `--no-oui-filter` - Accept packets from any vendor

### Reflection Modes (v1.11+)
- **`--mode mac`**: Swap Ethernet MAC addresses only (Layer 2)
- **`--mode mac-ip`**: Swap MAC + IP addresses (Layer 3)
- **`--mode all`**: Swap MAC + IP + UDP ports (Layer 4, default)

### Common
- **Zero-copy reflection**: In-place header swapping with SIMD (SSE2/NEON)
- **ITO protocol support**: PROBEOT, DATA:OT, LATENCY signatures
- **Statistics**: Real-time pps/Mbps, latency measurements, JSON/CSV output
- **Platform abstraction**: Clean separation between platform-specific and common code

## Performance

### v1.11.0 Performance Matrix

| Platform | Method | Throughput | Line-Rate | Multi-Client |
|----------|--------|------------|-----------|--------------|
| Linux | DPDK | 100+ Gbps | 100G ✅ | RSS load balance |
| Linux | AF_XDP | 10-40 Gbps | 40G ~90% | RSS load balance |
| Linux | AF_PACKET | 100-500 Mbps | No | Single queue |
| macOS | BPF | 10-75 Mbps | No | Single queue |

### Multi-Client Capacity (40G NIC with AF_XDP)

| Clients | Per-Client Speed | Total Bandwidth | Result |
|---------|------------------|-----------------|--------|
| 10 | 1 Gbps | 10 Gbps | ✅ 100% line-rate |
| 4 | 2.5 Gbps | 10 Gbps | ✅ 100% line-rate |
| 8 | 5 Gbps | 40 Gbps | ✅ 84-92% efficiency |
| 4 | 10 Gbps | 40 Gbps | ✅ 84-92% efficiency |

### Known Limitations
- **macOS BPF**: Limited to 10-75 Mbps (kernel architectural limitation)
- **AF_XDP**: Requires XDP-capable NIC and driver (Intel/Mellanox recommended)
- **DPDK**: Requires NIC binding to vfio-pci and hugepages configuration

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

### Basic Usage (C Dataplane Only)
```bash
# macOS
sudo ./reflector-macos en0

# Linux (auto-selects AF_XDP if available)
sudo ./reflector-linux eth0

# Linux with DPDK (100G mode)
sudo ./reflector-linux --dpdk eth0
```

### v2.0 Usage (Go Control Plane with TUI/Web)
```bash
# Build v2.0
make v2

# Run with TUI dashboard (default)
./reflector eth0

# Run with Web UI
./reflector eth0 --web --web-port 8080

# Run with config file
./reflector --config reflector.yaml

# No TUI, just stats to stdout
./reflector eth0 --no-tui
```

### Filtering Options
```bash
# Default: Port 3842, NetAlly OUI (00:c0:17)
sudo ./reflector-linux eth0

# Accept any port (0 = no port filter)
sudo ./reflector-linux eth0 --port 0

# Accept any vendor (disable OUI check)
sudo ./reflector-linux eth0 --no-oui-filter

# Custom OUI (e.g., Fluke Networks)
sudo ./reflector-linux eth0 --oui 00:19:b3
```

### Reflection Modes
```bash
# Full reflection (default) - swap MAC + IP + UDP ports
sudo ./reflector-linux eth0 --mode all

# Layer 3 - swap MAC + IP addresses only
sudo ./reflector-linux eth0 --mode mac-ip

# Layer 2 - swap MAC addresses only
sudo ./reflector-linux eth0 --mode mac
```

### Statistics Options
```bash
# JSON output (for scripting)
sudo ./reflector-linux eth0 --json

# CSV output (for logging)
sudo ./reflector-linux eth0 --csv

# Enable latency measurements
sudo ./reflector-linux eth0 --latency

# Verbose mode (shows packet details)
sudo ./reflector-linux eth0 -v
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
├── cmd/
│   └── reflector/           # Go entry point (v2.0)
│       └── main.go
├── pkg/                     # Go packages (v2.0)
│   ├── config/              # YAML configuration
│   ├── dataplane/           # CGO bindings to C code
│   ├── tui/                 # Terminal UI (tview)
│   └── web/                 # Embedded web server
├── ui/                      # React web UI source
│   ├── src/App.jsx
│   └── package.json
├── src/
│   └── dataplane/           # C data plane implementations
│       ├── common/          # Platform-agnostic code
│       │   ├── packet.c     # ITO validation & reflection (SIMD)
│       │   ├── util.c       # Interface/MAC utilities
│       │   ├── core.c       # Worker thread management
│       │   ├── nic_detect.c # Runtime NIC detection
│       │   └── main.c       # CLI entry point
│       ├── linux_xdp/       # Linux AF_XDP (10-40G)
│       ├── linux_dpdk/      # Linux DPDK (100G+)
│       ├── linux_packet/    # Linux AF_PACKET (fallback)
│       └── macos_bpf/       # macOS BPF
├── include/                 # Header files
│   ├── reflector.h          # Core definitions & API
│   └── platform_config.h    # Platform detection
├── packaging/               # Package build files
│   ├── debian/              # .deb package
│   └── rpm/                 # .rpm spec
├── scripts/service/         # Service files
│   ├── reflector.service    # systemd (Linux)
│   └── com.reflector.plist  # launchd (macOS)
├── tests/                   # Unit tests
├── docs/                    # Documentation
├── .github/workflows/       # CI/CD automation
│   ├── ci.yml               # Build & test
│   ├── package.yml          # Package builds
│   └── security.yml         # Security scanning
├── CHANGELOG.md             # Version history
└── reflector.yaml.example   # Sample config file
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
