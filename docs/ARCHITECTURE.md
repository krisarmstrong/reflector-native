# Architecture

> **Note:** This document describes both the current v1.0.1 implementation and planned future architecture (v2.0+).
> Sections marked with 🚀 indicate future planned features. See [ROADMAP.md](../ROADMAP.md) for timeline.

## Current Architecture (v1.0.1)

The reflector is a **single-tier C application** with platform-specific implementations:

```
┌─────────────────────────────────────────────────────┐
│  Reflector Application (C)                          │
│                                                     │
│  ┌──────────────────────────────────────┐          │
│  │  CLI Interface                       │          │
│  │  - Argument parsing                  │          │
│  │  - Statistics display                │          │
│  │  - Signal handling                   │          │
│  └──────────────┬───────────────────────┘          │
│                 │                                    │
│  ┌──────────────▼───────────────────────┐          │
│  │  Core Engine                         │          │
│  │  - Worker thread                     │          │
│  │  - Statistics aggregation            │          │
│  └──────────────┬───────────────────────┘          │
│                 │                                    │
│  ┌──────────────▼───────────────────────┐          │
│  │  Platform Abstraction Layer          │          │
│  │  - platform_ops_t interface          │          │
│  │  - init/cleanup/recv/send/release    │          │
│  └─────────┬────────────────┬───────────┘          │
│            │                │                       │
│  ┌─────────▼──────┐  ┌─────▼──────────┐           │
│  │ Linux          │  │ macOS          │           │
│  │ AF_PACKET      │  │ BPF            │           │
│  └────────────────┘  └────────────────┘           │
└─────────────────────────────────────────────────────┘
```

### Current Implementation Details

**Linux (AF_PACKET):**
- Raw packet socket with ETH_P_ALL protocol
- Blocking recv with 1ms timeout
- Zero-copy receive (direct buffer access)
- In-place packet modification

**macOS (BPF):**
- /dev/bpf device for packet capture/injection
- 4MB read buffer for batch processing
- BPF filter for ITO packets
- Single-threaded processing

**Common (Platform-agnostic):**
- ITO packet validation
- In-place header swapping (MAC/IP/UDP)
- Statistics tracking
- Logging and debugging

---

## 🚀 Future Architecture (v2.0+)

The planned architecture introduces a **two-tier system**:

1. **Data Plane (C)**: High-performance packet I/O and reflection
2. **Control Plane (Go)**: Configuration, monitoring, and UI

### Planned Two-Tier Architecture

```
┌─────────────────────────────────────────────────────┐
│  Control Plane (Go)                                 │
│  - TUI/Web UI                                       │
│  - REST API                                         │
│  - Configuration management                          │
│  - Statistics collection/display                    │
└────────────────┬────────────────────────────────────┘
                 │ (Unix socket / gRPC)
┌────────────────┴────────────────────────────────────┐
│  Data Plane (C)                                     │
│  - Linux: AF_XDP with eBPF filtering                │
│  - macOS: BPF packet filtering                      │
│  - Zero-copy packet reflection                      │
│  - Multi-queue support (Linux)                      │
└─────────────────────────────────────────────────────┘
```

## 🚀 Planned Data Plane Architecture (v2.0+)

### Linux (AF_XDP) - Planned

```
┌─────────────────────────────────────────────────┐
│  Application (reflector-linux)                  │
│                                                 │
│  ┌──────────────┐  ┌──────────────┐           │
│  │ Worker 0     │  │ Worker 1     │  ...       │
│  │ (CPU 0)      │  │ (CPU 1)      │           │
│  │ Queue 0      │  │ Queue 1      │           │
│  └──────┬───────┘  └──────┬───────┘           │
│         │                  │                    │
│  ┌──────▼──────────────────▼─────────┐        │
│  │     AF_XDP Sockets (zero-copy)     │        │
│  └──────┬──────────────────┬─────────┘        │
└─────────┼──────────────────┼──────────────────┘
          │                  │
┌─────────▼──────────────────▼──────────────────┐
│  Kernel Space                                  │
│                                                │
│  ┌───────────────────────────────────┐        │
│  │  XDP eBPF Program                 │        │
│  │  - Fast packet filtering          │        │
│  │  - MAC address check              │        │
│  │  - Protocol check (IPv4 UDP)      │        │
│  │  - ITO signature check            │        │
│  │  - XDP_REDIRECT → AF_XDP socket   │        │
│  │  - XDP_PASS → normal stack        │        │
│  └─────────────┬─────────────────────┘        │
│                │                                │
│  ┌─────────────▼─────────────────────┐        │
│  │  UMEM (shared memory)             │        │
│  │  - Zero-copy packet buffers       │        │
│  │  - 16MB pre-allocated             │        │
│  │  - 4096-byte frames               │        │
│  └───────────────────────────────────┘        │
│                │                                │
│  ┌─────────────▼─────────────────────┐        │
│  │  NIC Driver (i40e/ice/ixgbe)      │        │
│  │  - Multi-queue RSS                │        │
│  │  - XDP native mode                │        │
│  └───────────────────────────────────┘        │
└────────────────┬───────────────────────────────┘
                 │
┌────────────────▼───────────────────────────────┐
│  Hardware (NIC)                                │
└────────────────────────────────────────────────┘
```

### macOS (BPF) - Current Implementation

> **Note:** This is the current v1.0.1 implementation (single-threaded, 4MB buffer).

```
┌─────────────────────────────────────────────────┐
│  Application (reflector-macos)                  │
│                                                 │
│  ┌──────────────┐                              │
│  │ Worker 0     │                              │
│  │ (single)     │                              │
│  └──────┬───────┘                              │
│         │                                       │
│  ┌──────▼──────────┐  ┌──────────────┐        │
│  │ BPF Read (4MB)  │  │ BPF Write    │        │
│  │ /dev/bpf0       │  │ /dev/bpf1    │        │
│  └──────┬──────────┘  └──────▲───────┘        │
└─────────┼────────────────────┼─────────────────┘
          │                    │
┌─────────▼────────────────────┼─────────────────┐
│  Kernel Space                │                 │
│                              │                 │
│  ┌──────────────────┐        │                 │
│  │ BPF Filter       │        │                 │
│  │ - MAC check      │        │                 │
│  │ - IPv4 check     │        │                 │
│  │ - UDP check      │        │                 │
│  └────────┬─────────┘        │                 │
│           │                  │                 │
│  ┌────────▼──────────────────┴─────┐          │
│  │  Network Stack                   │          │
│  └────────┬─────────────────────────┘          │
│           │                                     │
│  ┌────────▼─────────────────────────┐          │
│  │  NIC Driver                       │          │
│  └───────────────────────────────────┘          │
└────────────────┬───────────────────────────────┘
                 │
┌────────────────▼───────────────────────────────┐
│  Hardware (NIC)                                │
└────────────────────────────────────────────────┘
```

## Packet Flow

### Receive Path

1. **Packet arrives at NIC**
2. **NIC places packet in RX ring** (via DMA)
3. **XDP program runs** (Linux only)
   - Parses Ethernet/IP/UDP headers
   - Checks for ITO signature
   - If match: XDP_REDIRECT to AF_XDP socket
   - If not: XDP_PASS to normal stack
4. **Worker thread polls AF_XDP/BPF**
   - Reads batch of packets (up to 64)
   - Zero-copy on Linux (direct UMEM access)
   - Copy on macOS (BPF buffer)
5. **Userspace validation**
   - Re-validate ITO packet (defense in depth)
   - Call `is_ito_packet()`

### Reflection Path

1. **In-place header swapping**
   - Swap Ethernet MAC: src ↔ dst
   - Swap IP addresses: src ↔ dst
   - Swap UDP ports: src ↔ dst
2. **Submit to TX ring**
   - Linux: Same UMEM buffer (zero-copy)
   - macOS: Write via BPF device
3. **Kick TX if needed**
   - Linux: sendto() with MSG_DONTWAIT
   - macOS: write() system call
4. **NIC transmits packet**

## Threading Model

### Linux (Multi-threaded)

- **One worker per RX queue**
- Each worker:
  - Binds to specific queue
  - Pins to specific CPU (matching IRQ affinity)
  - Independent AF_XDP socket
  - Own UMEM region
- **No shared state** between workers
- **Lock-free** packet processing

### macOS (Single-threaded)

- **One worker** (BPF device limitation)
- Processes packets sequentially
- No CPU pinning available

## Memory Management

### Linux AF_XDP

**UMEM (User Memory):**
- 16MB total (4096 frames × 4KB each)
- Allocated via mmap()
- Shared with kernel (zero-copy)
- Registered with xsk_umem__create()

**Ring Buffers:**
- RX ring: Receive packets from kernel
- TX ring: Send packets to kernel
- Fill queue: Provide free buffers to kernel
- Completion queue: Reclaim sent buffers

**Frame Management:**
- Pre-allocated at startup
- Recycled after transmission
- No runtime allocation in hot path

### macOS BPF

**Buffers:**
- 4MB read buffer (batch reads)
- 4MB write buffer (batch writes)
- Allocated via malloc()

**Packet Handling:**
- Packets copied from kernel to read buffer
- Reflected packets copied to write buffer
- No zero-copy available

## Platform Abstraction

### Interface: `platform_ops_t`

```c
struct platform_ops_t {
    int (*init)(reflector_ctx_t *rctx, worker_ctx_t *wctx);
    void (*cleanup)(worker_ctx_t *wctx);
    int (*recv_batch)(worker_ctx_t *wctx, packet_t *pkts, int max);
    int (*send_batch)(worker_ctx_t *wctx, packet_t *pkts, int num);
    void (*release_batch)(worker_ctx_t *wctx, packet_t *pkts, int num);
};
```

### Implementations

- **Linux**: `xdp_platform.c` → AF_XDP
- **macOS**: `bpf_platform.c` → BPF

### Runtime Selection

```c
#ifdef __linux__
    platform_ops = get_xdp_platform_ops();
#elif defined(__APPLE__)
    platform_ops = get_bpf_platform_ops();
#endif
```

## Statistics

### Per-Worker Stats

- packets_received
- packets_reflected
- bytes_received
- bytes_reflected
- tx_errors
- rx_invalid

### Aggregation

Statistics aggregated across all workers in `reflector_get_stats()`

## Performance Characteristics

### Linux AF_XDP

**Advantages:**
- Zero-copy RX/TX
- Multi-queue scaling
- eBPF filtering in kernel
- CPU affinity optimization

**Achieves:**
- 10G line rate with 1500-byte packets
- 10-14 Mpps with 64-byte packets (NIC dependent)

### macOS BPF

**Limitations:**
- Copy-based (no zero-copy)
- Single-threaded
- No multi-queue
- Higher syscall overhead

**Achieves:**
- 10G line rate with 1500-byte packets
- 0.5-1.5 Mpps with 64-byte packets

## Code Organization

```
src/
├── dataplane/
│   ├── common/           # Shared code
│   │   ├── packet.c      # ITO validation & reflection
│   │   ├── util.c        # Interface helpers
│   │   ├── core.c        # Worker thread management
│   │   └── main.c        # CLI entry point
│   ├── linux_xdp/
│   │   └── xdp_platform.c  # AF_XDP implementation
│   └── macos_bpf/
│       └── bpf_platform.c  # BPF implementation
├── xdp/
│   └── filter.bpf.c      # eBPF XDP filter (Linux)
└── control/
    └── main.go           # Go control plane
```

## 🚀 Future Enhancements

> **See [ROADMAP.md](../ROADMAP.md) for detailed timeline and priorities.**

Potential additions (v2.0+):
1. AF_XDP support (v2.0 - for 10G line-rate on Linux)
2. Go control plane with TUI/Web UI (v2.1)
3. DPDK support (v3.0 - for extreme performance)
4. Configuration file support (v1.2)
5. REST API for remote control (v2.1)
6. Packet capture/logging (v1.1)
7. Custom filter expressions (v3.0)
8. Hardware timestamp support (v3.0)
9. Systemd/launchd integration (v1.2)
