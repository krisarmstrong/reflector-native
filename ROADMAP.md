# Roadmap

This document outlines the planned features and improvements for reflector-native.

## Current Release: v1.2.0 (January 2025)

### Status: Stable - Maximum Performance Optimizations
- ✅ Linux AF_PACKET implementation
- ✅ macOS BPF implementation
- ✅ Platform abstraction layer
- ✅ ITO packet support (PROBEOT, DATA:OT, LATENCY)
- ✅ Zero-copy packet reflection (in-place header swapping)
- ✅ SIMD optimizations (SSE2/SSE3 on x86_64)
- ✅ Batched statistics updates
- ✅ Multi-architecture support (Intel/AMD/ARM64)
- ✅ Enhanced statistics (per-signature, latency, detailed errors)
- ✅ JSON/CSV output formats
- ✅ Aggressive performance warnings for fallback modes
- ✅ Unit tests
- ✅ CI/CD with GitHub Actions
- ✅ Security scanning (CodeQL, Gitleaks, cppcheck)
- ✅ Git hooks and best practices

### Performance Achieved
- **v1.0.1 baseline**: Initial stable release
- **v1.1.0**: Enhanced statistics and logging
- **v1.1.1**: Branch hints, prefetch, LTO (+10-25%)
- **v1.2.0**: SIMD, batched stats (+3-5% more)
- **Cumulative**: 13-30% faster than v1.0.1

### Known Limitations
- macOS limited to 10-50 Mbps (BPF architectural limitation)
- Linux AF_PACKET suitable for lab testing but not line-rate (~50-100 Mbps)
- AF_XDP implementation incomplete (planned for v1.3.0)
- Single worker thread per platform
- CLI-only interface

---

## v1.1.0 - Enhanced Statistics & Logging ✅ RELEASED

### Status: ✅ Completed (January 2025)

**Delivered Features:**
- ✅ Extended packet statistics
  - Per-signature counters (PROBEOT, DATA:OT, LATENCY)
  - Latency measurements (min/avg/max in nanoseconds)
  - 7 detailed error categories
- ✅ JSON output mode for stats
- ✅ CSV output mode for logging
- ✅ Configurable statistics interval (--stats-interval)
- ✅ Help system (-h/--help)

**Documentation:**
- ✅ Updated README with new features
- ✅ CHANGELOG with detailed release notes

---

## v1.1.1 - Compiler Optimizations ✅ RELEASED

### Status: ✅ Completed (January 2025)

**Delivered Optimizations:**
- ✅ Branch prediction hints (likely/unlikely macros)
- ✅ Memory prefetching (PREFETCH_READ/PREFETCH_WRITE)
- ✅ Force-inline hot path functions (ALWAYS_INLINE)
- ✅ Aggressive compiler flags (LTO, loop unrolling, vectorization)
- ✅ Optimized header swapping (direct integer operations)

**Performance Impact:**
- ✅ 10-25% throughput improvement over v1.0.1

---

## v1.2.0 - SIMD & Batched Statistics ✅ RELEASED

### Status: ✅ Completed (January 2025)

**Delivered Optimizations:**
- ✅ SIMD header swapping (SSE2/SSE3 on x86_64)
- ✅ Runtime CPU feature detection
- ✅ ARM64 optimized scalar fallback
- ✅ Batched statistics updates (reduced cache bouncing)
- ✅ Aggressive warnings for suboptimal platforms

**Performance Impact:**
- ✅ Additional 3-5% improvement over v1.1.1
- ✅ Total 13-30% faster than v1.0.1

**User Experience:**
- ✅ Large visual warnings for AF_PACKET fallback
- ✅ macOS BPF architectural limitation warnings
- ✅ Runtime AF_XDP failure diagnostics

**Documentation:**
- ✅ CPU and architecture requirements documented
- ✅ Multi-architecture compatibility clarified

---

## v1.3.0 - Maximum Performance on All Platforms (Q1 2025)

### Status: 📋 In Planning

### Focus: Extract Every Possible Optimization - Leave Nothing on the Table

**Theme:** The ultimate performance release. Push every platform to its absolute maximum capability.

### Linux AF_XDP - Primary Goal (10 Gbps Line-Rate)

**Complete Implementation:**
- [ ] Full AF_XDP with UMEM zero-copy memory management
- [ ] Native XDP mode (hardware offload for supported NICs)
- [ ] Generic XDP fallback (software mode for all NICs)
- [ ] Multi-queue support with per-CPU worker threads
- [ ] eBPF packet filter for kernel-level ITO signature detection
- [ ] CPU affinity and IRQ steering optimization
- [ ] Huge pages for UMEM when available
- [ ] Busy polling for ultra-low latency
- [ ] Batch processing for maximum throughput
- [ ] NUMA-aware memory allocation
- [ ] Lock-free per-queue statistics

**Performance Target:**
- **Native XDP**: 10 Gbps @ 1518-byte frames (812K pps)
- **Native XDP**: 10-14 Mpps @ 64-byte frames
- **Generic XDP**: 1-2 Gbps @ 1518-byte frames
- **Latency**: Sub-microsecond reflection time

**Hardware Support:**
- Intel i40e (X710, XL710, X722) - native XDP
- Intel ice (E810) - native XDP
- Intel ixgbe (82599, X520, X540, X550) - native XDP
- Mellanox mlx5 (ConnectX-4/5/6) - native XDP
- All other NICs - generic XDP fallback

### Linux AF_PACKET - Optimize Fallback (100-200 Mbps)

**When AF_XDP Not Available:**
- [ ] PACKET_FANOUT for multi-queue distribution
- [ ] PACKET_MMAP for zero-copy receive
- [ ] PACKET_QDISC_BYPASS to bypass qdisc layer
- [ ] TPACKET_V3 (latest ring buffer version)
- [ ] Ring buffer tuning (maximize based on available RAM)
- [ ] SO_BUSY_POLL socket option for lower latency
- [ ] Optimal batch sizes for recv/send operations
- [ ] Multi-threaded packet processing

**Performance Target:**
- **Optimized AF_PACKET**: 100-200 Mbps (2x current)

### macOS BPF - Maximize Within Limits (30-50 Mbps)

**Every Possible Optimization:**
- [ ] Maximum BPF buffer size (kernel limit)
- [ ] Optimal read() batch size (empirical testing)
- [ ] Non-blocking I/O with minimal overhead
- [ ] Multiple BPF devices if beneficial
- [ ] Immediate mode vs buffered mode testing
- [ ] Reduced syscall overhead
- [ ] BPF filter optimization

**Performance Target:**
- **Optimized BPF**: 30-50 Mbps sustained (maximize within architectural 50 Mbps limit)

### Cross-Platform Advanced Optimizations

**Apply Everywhere:**
- [ ] ARM64 NEON SIMD instructions (Apple Silicon, AWS Graviton)
- [ ] Additional compiler optimizations (-ffast-math where safe)
- [ ] Profile-guided optimization (PGO) support
- [ ] Cache-line alignment for hot data structures
- [ ] Further branch misprediction reduction
- [ ] TLB miss minimization
- [ ] Instruction cache optimization

### Performance Tooling

**Built-in Measurement:**
- [ ] Accurate pps (packets per second) measurement
- [ ] Latency histograms (p50, p95, p99, p99.9, max)
- [ ] Per-core CPU utilization tracking
- [ ] Cache miss counters (perf events integration)
- [ ] Dropped packet attribution and analysis
- [ ] Comparison mode (before/after benchmarking)

**System Tuning:**
- [ ] Automated Linux tuning script
  - IRQ affinity configuration
  - CPU governor settings (performance mode)
  - Disable CPU power saving (C-states)
  - Enable turbo boost
- [ ] NIC tuning script
  - Ring buffer sizing
  - Interrupt coalescing
  - RSS (Receive Side Scaling) configuration
  - Flow control settings
- [ ] Kernel parameter tuning
  - /proc/sys/net optimizations
  - Hugepage configuration
  - Memory management tuning
- [ ] BIOS settings guide

### Expected Performance Improvements

| Platform | v1.2.0 (Current) | v1.3.0 (Target) | Improvement |
|----------|------------------|-----------------|-------------|
| **Linux AF_XDP (native)** | N/A | 10 Gbps @ 1518B | NEW - 100x+ |
| **Linux AF_XDP (native)** | N/A | 10-14 Mpps @ 64B | NEW - 100x+ |
| **Linux AF_XDP (generic)** | N/A | 1-2 Gbps @ 1518B | NEW - 10-20x |
| **Linux AF_PACKET** | 50-100 Mbps | 100-200 Mbps | 2x |
| **macOS BPF** | 10-50 Mbps | 30-50 Mbps | 1.5x |

### Implementation Priority

**Priority 1 (Must Have):**
1. Complete AF_XDP implementation (native + generic modes)
2. eBPF filter for kernel-level packet filtering
3. Multi-queue support with CPU affinity
4. AF_PACKET optimizations (FANOUT, MMAP, QDISC_BYPASS)
5. macOS BPF buffer and I/O tuning

**Priority 2 (Should Have):**
6. ARM64 NEON SIMD instructions
7. Performance tuning scripts (Linux/NIC)
8. Built-in latency histograms
9. NUMA-aware memory allocation
10. Huge pages support for UMEM

**Priority 3 (Nice to Have):**
11. Profile-guided optimization (PGO)
12. Advanced CPU pinning strategies
13. Detailed performance counters (perf integration)
14. Automated NIC capability detection
15. Runtime performance auto-tuning

### Documentation

**Required:**
- [ ] Comprehensive NIC compatibility matrix
- [ ] AF_XDP deployment and tuning guide
- [ ] Kernel configuration best practices
- [ ] Performance benchmarking methodology
- [ ] Troubleshooting guide for XDP issues
- [ ] Hardware recommendations by use case
- [ ] System tuning checklist

### Testing Requirements

**Hardware:**
- Intel X710 NIC (native XDP testing)
- Mellanox ConnectX NIC (native XDP testing)
- Generic NIC (generic XDP fallback testing)
- Various USB-C/Thunderbolt adapters (AF_PACKET testing)
- Apple Silicon Mac (ARM64 NEON, macOS BPF testing)

**Performance Validation:**
- LinkRunner 10G at various test rates
- 64-byte minimum frames (worst case, maximum pps)
- 1518-byte standard frames (typical case)
- 9000-byte jumbo frames (best case)
- Latency measurements across all frame sizes
- Sustained throughput testing (hours-long tests)
- CPU utilization profiling
- Memory bandwidth analysis

---

## v2.0.0 - Go Control Plane & TUI (Q2 2025)

### Focus: User Interface & Management

**Features:**
- [ ] Go control plane with IPC to C data plane
  - Unix socket communication
  - gRPC API
- [ ] TUI (Terminal UI) interface
  - Real-time statistics dashboard
  - Interface selection
  - Start/stop control
  - Log viewer
- [ ] REST API for remote management
- [ ] Web UI (optional)
  - Statistics visualization
  - Configuration management
  - Live packet inspection

**Architecture:**
```
┌─────────────────────────────────────┐
│  Control Plane (Go)                 │
│  - TUI/Web UI                       │
│  - REST API                         │
│  - Configuration management         │
└────────────┬────────────────────────┘
             │ (Unix socket / gRPC)
┌────────────┴────────────────────────┐
│  Data Plane (C)                     │
│  - AF_XDP/AF_PACKET/BPF             │
│  - Zero-copy reflection             │
└─────────────────────────────────────┘
```

---

## v2.1.0 - Configuration & Flexibility (Q2-Q3 2025)

### Status: 💡 Planned

### Focus: Configuration Management & Multi-Interface Support

**Features:**
- [ ] Configuration file support (YAML/TOML)
  - Interface selection
  - Statistics settings
  - Logging configuration
  - Port overrides
  - Performance tuning parameters
- [ ] Multiple interface support (run on multiple interfaces simultaneously)
- [ ] BPF/XDP filter customization
- [ ] Optional MAC promiscuous mode
- [ ] Systemd service file (Linux)
- [ ] launchd plist (macOS)

**Usability:**
- [ ] Auto-detect suitable network interfaces
- [ ] Dry-run mode (validate config without running)
- [ ] Better error messages with actionable suggestions
- [ ] Configuration validation and schema

---

## v3.0.0 - Advanced Features (Q3-Q4 2025)

### Focus: Extended Protocol & Hardware Support

**Protocol Extensions:**
- [ ] Additional ITO packet types
- [ ] VLAN tag support (802.1Q)
- [ ] QinQ support (802.1ad)
- [ ] MPLS label handling
- [ ] IPv6 support
- [ ] Custom signature configuration

**Hardware Features:**
- [ ] Hardware timestamp support (NIC-level)
- [ ] SR-IOV support
- [ ] DPDK alternative implementation (optional)
- [ ] SmartNIC offload (if available)

**Advanced Features:**
- [ ] Packet rate limiting (throttling)
- [ ] Selective reflection (filter by source, size, etc.)
- [ ] Packet modification (TTL, DSCP, etc.)
- [ ] Load balancing across multiple interfaces
- [ ] Active/standby failover

---

## Future Considerations (2026+)

### Performance
- [ ] DPDK full implementation for extreme performance
- [ ] GPU-accelerated packet processing (experimental)
- [ ] 25G/40G/100G support
- [ ] Kernel module option for even lower latency

### Enterprise Features
- [ ] SNMP MIB for monitoring
- [ ] Syslog integration
- [ ] Authentication/authorization for REST API
- [ ] Multi-tenant support
- [ ] Cloud deployment (containerization)
- [ ] Kubernetes operator

### Protocol Support
- [ ] Reflect other network test protocols
- [ ] Custom protocol plugins (Lua/eBPF scripting)
- [ ] TWAMP (RFC 5357) reflector mode
- [ ] STAMP (RFC 8762) support

### Platform Support
- [ ] Windows native implementation (study original NDIS driver)
- [ ] FreeBSD support
- [ ] ARM64 optimization (Raspberry Pi, AWS Graviton)

---

## Hardware Recommendations by Version

### v1.0 - v1.2 (Current Stable)
- **Linux**: Any network adapter (AF_PACKET mode)
- **macOS**: Any network adapter (BPF mode)
- **Performance**: 10-100 Mbps
- **Use case**: Development, debugging, low-rate lab testing

### v1.3 (Maximum Performance - In Planning)
- **Linux with AF_XDP (Recommended)**:
  - Intel X710/E810 or Mellanox ConnectX-4+ for native XDP (10 Gbps)
  - Any NIC for generic XDP (1-2 Gbps)
- **Linux with AF_PACKET (Fallback)**: Any NIC (100-200 Mbps)
- **macOS**: Any NIC (30-50 Mbps, architectural limit)
- **Use case**: Production line-rate testing, high-performance validation

### v2.x (Go Control Plane)
- Same hardware as v1.3
- **Additional**: Multi-core CPU recommended for TUI + data plane
- **Use case**: Same as v1.3 + remote management, monitoring

### v3.x (Advanced Features)
- **Linux**: High-end NICs with hardware timestamp, SR-IOV
- **Examples**: Intel E810, Mellanox ConnectX-6 Dx
- **Use case**: High-precision timing, VLAN/MPLS, multi-tenant environments

---

## Community Input

We welcome feedback on this roadmap! Please:
- Open GitHub issues for feature requests
- Join discussions on prioritization
- Contribute implementations for planned features
- Share your use cases and requirements

**Priorities may change based on:**
- Community feedback and contributions
- Real-world usage patterns
- Hardware availability for testing
- Emerging protocols and standards

---

## Version Planning Summary

| Version | Focus | Timeline | Status |
|---------|-------|----------|--------|
| v1.0.1 | Initial stable release | Jan 2025 | ✅ Released |
| v1.1.0 | Enhanced statistics & logging | Jan 2025 | ✅ Released |
| v1.1.1 | Compiler optimizations (+10-25%) | Jan 2025 | ✅ Released |
| v1.2.0 | SIMD & batched stats (+3-5%) | Jan 2025 | ✅ Released |
| v1.3.0 | **Maximum performance (AF_XDP, optimize all platforms)** | Q1 2025 | 📋 In Planning |
| v2.0.0 | Go control plane & TUI | Q2 2025 | 💡 Planned |
| v2.1.0 | Configuration & flexibility | Q2-Q3 2025 | 💡 Planned |
| v3.0.0 | Advanced protocol features | Q3-Q4 2025 | 💡 Conceptual |
| v4.0.0+ | Enterprise & future features | 2026+ | 💭 Ideas |

### Release Cadence

**v1.x Series** (C Data Plane - Performance Focus)
- v1.0.1 - v1.2.0: ✅ Completed (13-30% improvement)
- v1.3.0: Maximum performance extraction (target: 100x+ on AF_XDP)

**v2.x Series** (Go Control Plane - UX Focus)
- v2.0.0: TUI and REST API
- v2.1.0: Configuration management

**v3.x+ Series** (Advanced Features)
- Protocol extensions, enterprise features

---

**Last Updated:** January 6, 2025
**Maintainer:** Kris Armstrong
