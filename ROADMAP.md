# Roadmap

This document outlines the planned features and improvements for reflector-native.

## Current Release: v1.0.1 (January 2025)

### Status: Stable
- ✅ Linux AF_PACKET implementation
- ✅ macOS BPF implementation
- ✅ Platform abstraction layer
- ✅ ITO packet support (PROBEOT, DATA:OT, LATENCY)
- ✅ Zero-copy packet reflection (in-place header swapping)
- ✅ Unit tests
- ✅ CI/CD with GitHub Actions
- ✅ Security scanning (CodeQL, Gitleaks, cppcheck)
- ✅ Git hooks and best practices

### Known Limitations
- macOS limited to 10-50 Mbps (BPF architectural limitation)
- Linux AF_PACKET suitable for lab testing but not line-rate
- Single-threaded operation
- CLI-only interface

---

## v1.1.0 - Enhanced Testing & Monitoring (Q1 2025)

### Focus: Observability and Testing

**Features:**
- [ ] Extended packet statistics
  - Per-signature counters (PROBEOT vs DATA:OT vs LATENCY)
  - Latency measurements (RX to TX time)
  - Error categorization
- [ ] JSON output mode for stats
- [ ] Prometheus metrics exporter
- [ ] Packet capture mode (pcap export for debugging)
- [ ] Configurable statistics interval
- [ ] Performance benchmarking suite

**Documentation:**
- [ ] Performance tuning guide
- [ ] Troubleshooting flowcharts
- [ ] Integration examples with monitoring tools

---

## v1.2.0 - Configuration & Flexibility (Q1 2025)

### Focus: Configuration Management

**Features:**
- [ ] Configuration file support (YAML/TOML)
  - Interface selection
  - Statistics settings
  - Logging configuration
  - Port overrides
- [ ] Multiple interface support (run on multiple interfaces)
- [ ] BPF filter customization
- [ ] Optional MAC promiscuous mode
- [ ] Systemd service file (Linux)
- [ ] launchd plist (macOS)

**Usability:**
- [ ] Auto-detect suitable network interfaces
- [ ] Dry-run mode (validate config without running)
- [ ] Better error messages with suggestions

---

## v2.0.0 - AF_XDP for Line-Rate (Q2 2025)

### Focus: High Performance on Linux

**Major Features:**
- [ ] AF_XDP implementation (zero-copy kernel bypass)
  - Native XDP mode for supported NICs
  - Generic XDP fallback
  - Multi-queue support
  - Per-queue worker threads
  - CPU affinity optimization
- [ ] eBPF packet filter
  - Kernel-level ITO signature detection
  - Early packet filtering
  - Hardware offload where available
- [ ] UMEM shared memory management
- [ ] Batch packet processing
- [ ] Runtime NIC capability detection

**Performance Targets:**
- 10G line-rate with 1500-byte packets
- 10-14 Mpps with 64-byte packets on Intel/Mellanox NICs
- Sub-microsecond reflection latency

**Hardware Support:**
- Intel i40e (X710, XL710, X722)
- Intel ice (E810)
- Intel ixgbe (82599, X520, X540, X550)
- Mellanox mlx5 (ConnectX-4/5/6)

**Documentation:**
- [ ] NIC compatibility matrix
- [ ] Performance tuning guide for AF_XDP
- [ ] Benchmarking methodology

---

## v2.1.0 - Go Control Plane (Q2-Q3 2025)

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

### v1.x (Current)
- **Linux**: Any network adapter
- **macOS**: Any network adapter
- **Use case**: Lab testing, low-rate scenarios

### v2.x (AF_XDP)
- **Linux**: Intel X710/E810 or Mellanox ConnectX-4+ recommended
- **Alternatives**: Any XDP-capable NIC (degraded performance with generic XDP)
- **Use case**: Production line-rate testing up to 10G

### v3.x (Advanced)
- **Linux**: High-end NICs with hardware timestamp, SR-IOV
- **Examples**: Intel E810, Mellanox ConnectX-6 Dx
- **Use case**: High-precision timing, multi-tenant environments

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
| v1.0.1 | Current stable release | Jan 2025 | ✅ Released |
| v1.1.0 | Observability | Q1 2025 | 📋 Planned |
| v1.2.0 | Configuration | Q1 2025 | 📋 Planned |
| v2.0.0 | AF_XDP line-rate | Q2 2025 | 📋 Planned |
| v2.1.0 | Go control plane | Q2-Q3 2025 | 📋 Planned |
| v3.0.0 | Advanced features | Q3-Q4 2025 | 💡 Conceptual |
| v4.0.0+ | Future | 2026+ | 💭 Ideas |

---

**Last Updated:** January 2025
**Maintainer:** Kris Armstrong
