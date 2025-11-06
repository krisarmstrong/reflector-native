# Network Reflector - Native Linux/macOS Implementation

[![CI](https://github.com/krisarmstrong/reflector-native/actions/workflows/ci.yml/badge.svg)](https://github.com/krisarmstrong/reflector-native/actions/workflows/ci.yml)
[![Security](https://github.com/krisarmstrong/reflector-native/actions/workflows/security.yml/badge.svg)](https://github.com/krisarmstrong/reflector-native/actions/workflows/security.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Version](https://img.shields.io/badge/version-1.1.0-blue.svg)](https://github.com/krisarmstrong/reflector-native/releases)

High-performance packet reflector for Fluke/NETSCOUT and NetAlly handheld network test tools.

## Overview

This project provides packet reflection capabilities for ITO (Integrated Test & Optimization) packets on Linux and macOS platforms. The C-based data plane is designed for high performance with zero-copy packet processing where supported.

**Current Version:** 1.1.0 (January 2025) - Enhanced statistics and logging

## Architecture

```
┌─────────────────────────────────────────────────────┐
│  Data Plane (C)                                     │
│  - Linux: AF_PACKET with optimized zero-copy       │
│  - macOS: BPF packet filtering                      │
│  - In-place packet header swapping                  │
│  - Platform abstraction layer                       │
│  - CLI interface with statistics                    │
└─────────────────────────────────────────────────────┘
```

## Current Features

### Linux
- **AF_PACKET**: Optimized packet socket with zero-copy receive
- **Platform compatibility**: Works with all NICs (no special driver needed)
- **Efficient processing**: No malloc in hot path, blocking recv with timeout
- **Tested**: Ubuntu 25.10 with various network adapters

### macOS
- **BPF filtering**: Native packet capture and injection via /dev/bpf
- **Compatibility**: Works with all NICs
- **Tested**: macOS 14+ with Thunderbolt and USB-C adapters
- **Performance**: 99.96% reflection rate at 1 Mbps with 1518-byte frames

### Common
- **Zero-copy reflection**: In-place MAC/IP/UDP header swapping
- **ITO protocol support**: PROBEOT, DATA:OT, LATENCY signatures
- **Verbose logging**: Optional detailed packet processing output
- **Platform abstraction**: Clean separation between platform-specific and common code

## Performance

### Current v1.0.1
| Platform | Test Rate | Packet Size | Result |
|----------|-----------|-------------|---------|
| macOS | 1 Mbps | 1518 bytes | 99.96% (4970/4972 packets) |
| macOS | 1 Gbps | 1518 bytes | Limited to ~50 Mbps max |
| Linux | 1 Mbps | 1518 bytes | Stable operation |

### Known Limitations
- **macOS BPF**: Limited to 10-50 Mbps regardless of hardware (architectural limitation)
- **Linux AF_PACKET**: Better than macOS but not line-rate (suitable for lab testing)
- **See ROADMAP.md** for planned AF_XDP implementation for 10G line-rate on Linux

## Requirements

### Linux
- Linux kernel (tested on Ubuntu 25.10)
- gcc or clang
- make
- sudo/root access (for raw socket operations)

### macOS
- macOS 10.14+ (tested on macOS 14+)
- Xcode Command Line Tools
- sudo/root access (for BPF device access)

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

### Building from Source
```bash
git clone https://github.com/krisarmstrong/reflector-native.git
cd reflector-native
make
```

### Git Hooks
```bash
# Install pre-commit hooks (recommended)
./scripts/install-hooks.sh

# Pre-commit hook runs:
# - Secret detection
# - Large file check
# - Tests on source changes
```

### Running Tests
```bash
# Unit tests
make test

# Manual testing with network tool
sudo ./reflector-macos en0 -v
# Then send ITO packets from LinkRunner/OneTouch
```

### CI/CD
- **GitHub Actions**: Automated builds for Linux and macOS
- **Security scanning**: CodeQL, Gitleaks, cppcheck
- **Release automation**: Tag-based releases with changelogs

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
