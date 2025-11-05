# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2025-01-05

### Added
- Initial release of reflector-native
- Linux AF_XDP implementation with zero-copy packet I/O
- macOS BPF implementation
- eBPF XDP filter for kernel-level packet classification
- Multi-queue support with per-CPU workers (Linux)
- Platform abstraction layer for cross-platform support
- Real-time statistics and performance monitoring
- In-place packet reflection for ITO packets
- Support for PROBEOT, DATA:OT, and LATENCY signatures
- Comprehensive documentation (Architecture, Performance, Quick Start)
- Build system with Makefile
- Setup and tuning scripts
- Go control plane foundation

### Performance
- Linux: 10G line rate capability (812K+ pps @ 1500 bytes, 10-14 Mpps @ 64 bytes)
- macOS: Up to 800K pps @ 1500 bytes
- Zero memory allocation in packet processing hot path (Linux)
- Multi-threaded packet processing with CPU affinity

### Documentation
- Complete architecture documentation
- Performance tuning guide
- NIC compatibility matrix
- Quick start guide
- Build instructions

[1.0.0]: https://github.com/krisarmstrong/reflector-native/releases/tag/v1.0.0
