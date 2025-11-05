# Network Reflector - Native Linux/macOS Implementation

High-performance packet reflector for Fluke/NETSCOUT and NetAlly handheld network test tools.

## Overview

This project provides line-rate packet reflection capabilities for ITO (Integrated Test & Optimization) packets on Linux and macOS platforms. It's designed to achieve 10G line rate performance on Linux using AF_XDP zero-copy technology.

## Architecture

```
┌─────────────────────────────────────────────────────┐
│  Control Plane (Go)                                 │
│  - TUI/CLI interface                                │
│  - Configuration management                          │
│  - Statistics collection/display                    │
│  - IPC with dataplane                               │
└────────────────┬────────────────────────────────────┘
                 │ (Unix socket)
┌────────────────┴────────────────────────────────────┐
│  Data Plane (C)                                     │
│  - Linux: AF_XDP with eBPF filtering                │
│  - macOS: BPF packet filtering                      │
│  - Zero-copy packet reflection                      │
│  - Multi-queue support (Linux)                      │
└─────────────────────────────────────────────────────┘
```

## Features

### Linux (AF_XDP)
- **Line-rate performance**: Up to 10G with modern NICs
- **Zero-copy**: Shared memory between kernel and userspace
- **Multi-queue**: Per-CPU processing threads
- **eBPF filtering**: Kernel-level packet classification
- **No kernel module**: Uses standard kernel features (5.3+)

### macOS (BPF)
- **BPF filtering**: Native packet capture and injection
- **Performance**: Line rate with 1500-byte frames
- **Compatibility**: Works with all NICs

## Performance Targets

| Platform | Packet Size | Expected Performance | Line Rate % |
|----------|-------------|---------------------|-------------|
| Linux (modern NIC) | 64 bytes | 10-14 Mpps | 85-100% |
| Linux (modern NIC) | 1500 bytes | 812K+ pps | 100% |
| macOS | 64 bytes | 0.5-1.5 Mpps | 5-15% |
| macOS | 1500 bytes | 600-800K pps | 75-100% |

## Requirements

### Linux
- Kernel 5.3+ (for AF_XDP)
- libbpf-dev
- libxdp-dev
- clang (for eBPF compilation)
- NIC with XDP support (Intel i40e, ice, ixgbe, or Mellanox mlx5)

### macOS
- macOS 10.14+
- Xcode Command Line Tools

### Both
- Go 1.21+ (for control plane)
- gcc or clang

## Building

```bash
# Linux
make linux

# macOS
make macos

# Both (if on Linux)
make all
```

## Installation

```bash
sudo make install
```

## Usage

### Start Reflector
```bash
# Linux
sudo reflector-native -i eth0

# macOS
sudo reflector-native -i en0

# With control UI
reflector-ui
```

### Configuration
```bash
# Enable reflection on interface
reflector-ctl enable eth0

# Disable reflection
reflector-ctl disable eth0

# Show statistics
reflector-ctl stats

# Show all interfaces
reflector-ctl list
```

## NIC Tuning (Linux)

For optimal performance:

```bash
# Increase ring buffer sizes
sudo ethtool -G eth0 rx 4096 tx 4096

# Enable multi-queue (RSS)
sudo ethtool -L eth0 combined 4

# Disable interfering offloads
sudo ethtool -K eth0 gro off lro off

# Pin IRQs to CPUs
sudo ./scripts/set_irq_affinity.sh eth0
```

## Supported NICs

### Linux (Native XDP)
- Intel X710/XL710 (i40e driver)
- Intel E810 (ice driver)
- Intel 82599/X520/X540 (ixgbe driver)
- Mellanox ConnectX-4/5 (mlx5 driver)

### Linux (Generic XDP)
- All NICs (with reduced performance)

### macOS
- All NICs via BPF

## Protocol Support

Currently reflects ITO (Integrated Test & Optimization) packets with signatures:
- `PROBEOT` - OneTouch probe packets
- `DATA:OT` - OneTouch data packets
- `LATENCY` - Latency measurement packets

Packets must be:
- IPv4 UDP
- Addressed to interface MAC
- Minimum 54 bytes

## Project Structure

```
reflector-native/
├── src/
│   ├── dataplane/          # C data plane implementations
│   │   ├── common/         # Shared packet parsing/reflection
│   │   ├── linux_xdp/      # Linux AF_XDP implementation
│   │   └── macos_bpf/      # macOS BPF implementation
│   ├── control/            # Go control plane
│   │   ├── ui/             # TUI interface
│   │   ├── stats/          # Statistics aggregation
│   │   └── config/         # Configuration management
│   └── xdp/                # eBPF programs
│       └── filter.bpf.c    # XDP filter
├── include/                # Header files
├── docs/                   # Documentation
├── scripts/                # Utility scripts
└── build/                  # Build output
```

## Development

### Building eBPF (Linux)
```bash
cd src/xdp
clang -O2 -target bpf -c filter.bpf.c -o filter.bpf.o
```

### Testing
```bash
# Run test suite
make test

# Performance benchmark
make benchmark
```

## Contributing

This project is designed for network testing and diagnostic purposes.

## License

See LICENSE file for details.

## Credits

- Original Windows Reflector: Fluke Networks
- Linux/macOS Native Implementation: Kris Armstrong (2025)
- Built for NETSCOUT/NetAlly handheld network test tools

## References

- [AF_XDP Documentation](https://www.kernel.org/doc/html/latest/networking/af_xdp.html)
- [BPF Documentation](https://www.freebsd.org/cgi/man.cgi?bpf)
- [XDP Tutorial](https://github.com/xdp-project/xdp-tutorial)
