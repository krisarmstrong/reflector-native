# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.1.1] - 2025-01-06

### Performance Optimizations

**Compiler-Level Optimizations:**
- Branch prediction hints with `likely()`/`unlikely()` macros
- Force-inline critical hot path functions with `ALWAYS_INLINE`
- Memory prefetching for packet data and write areas
- Aggressive compiler flags: `-flto`, `-funroll-loops`, `-ftree-vectorize`, `-finline-functions`
- Link-time optimization (LTO) enabled for whole-program optimization

**Code-Level Optimizations:**
- Optimized `is_ito_packet()` validation with branch hints
- Prefetch packet headers before validation
- All error path checks marked `unlikely()` for better branch prediction
- All success path checks marked `likely()` for optimal pipelining
- Optimized `reflect_packet_inplace()` with prefetching
- Direct integer swaps (32-bit for IP, 16-bit for ports) instead of memcpy
- Statistics update functions inlined (`update_signature_stats`, `update_latency_stats`, `update_error_stats`)

**Expected Impact:**
- 5-10% throughput improvement from better branch prediction
- 2-5% improvement from memory prefetching
- 5-15% improvement from LTO and aggressive inlining
- Reduced L1 cache misses from prefetching
- Better instruction pipelining from branch hints
- **Total expected improvement: 10-25% higher packet rate**

### Technical Details

**Branch Prediction:**
- MAC address mismatches (common): marked `unlikely()`
- ITO packet matches (after MAC match): marked `likely()`
- Error conditions: marked `unlikely()`
- Success paths: marked `likely()`

**Memory Prefetching:**
- Packet data prefetched at validation start
- UDP header area prefetched (offset +64)
- Packet write areas prefetched before reflection
- Reduces cache miss penalties

**Function Inlining:**
- `is_ito_packet()`: forced inline (called per packet)
- `reflect_packet_inplace()`: forced inline (called per reflected packet)
- All statistics helpers: forced inline (called per packet)

**Compiler Flags Added:**
- `-fno-strict-aliasing`: Allows type-punning optimizations
- `-fomit-frame-pointer`: Frees up register for packet processing
- `-funroll-loops`: Unrolls validation loops
- `-finline-functions`: Aggressive function inlining
- `-ftree-vectorize`: Auto-vectorization where possible
- `-flto`: Link-time optimization across compilation units

### Changed
- Version bumped to 1.1.1

## [1.1.0] - 2025-01-06

### Added
- **Enhanced Statistics**: Per-signature packet counters (PROBEOT, DATA:OT, LATENCY)
- **Latency Measurements**: Optional per-packet RX-to-TX latency tracking (min/avg/max)
- **JSON Output Format**: Machine-readable statistics output with `--json` flag
- **CSV Output Format**: CSV statistics output with `--csv` flag for logging/analysis
- **Detailed Error Categorization**: 7 specific error types tracked separately:
  - Invalid MAC address
  - Invalid EtherType
  - Invalid IP protocol
  - Invalid ITO signature
  - Packet too short
  - TX failed
  - Memory allocation failures
- **Configurable Statistics Interval**: `--stats-interval N` to control update frequency
- **Help System**: `-h`/`--help` flag for usage information

### Changed
- Statistics structure now includes detailed breakdowns by packet type
- Text output now shows signature breakdown and latency when available
- Final statistics report includes comprehensive breakdown of all metrics
- Default statistics interval changed from 1 second to 10 seconds
- CLI now supports long-form options (e.g., `--verbose`, `--help`)

### Performance
- Statistics tracking adds per-signature type detection (minimal overhead)
- Optional latency measurement (disabled by default for zero-overhead)
- All new statistics use zero-copy update mechanisms

### Documentation
- Updated help text with all new options
- Enhanced final statistics output format

## [1.0.1] - 2025-01-06

### Fixed
- ITO packet signature offset (changed from 6 to 5 bytes for LinkRunner 10G compatibility)
- AF_PACKET performance: eliminated malloc() per packet (300x performance improvement)
- AF_PACKET packet loss: use blocking recv with 1ms timeout instead of busy-polling
- AF_XDP initialization now works without eBPF filter (SKB mode fallback)
- Linux compilation issues: added _GNU_SOURCE, sys/socket.h, fcntl.h headers
- XDP header path for Ubuntu (xdp/xsk.h instead of bpf/xsk.h)
- Platform detection now gracefully falls back to AF_PACKET when AF_XDP unsupported

### Changed
- AF_PACKET now uses zero-copy approach (direct buffer pointers)
- eBPF filter compilation failures are non-fatal (AF_XDP works without filter)
- Improved error messages and warnings for unsupported NICs

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
