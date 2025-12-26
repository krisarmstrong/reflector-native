# Architecture

> **Version:** 1.11.0 | **Last Updated:** December 2025

## System Overview

The Network Reflector is a high-performance packet reflection engine designed for network test tool validation. It receives ITO (Integrated Test & Optimization) packets from handheld network testers and reflects them back with swapped headers.

```
                    ┌─────────────────────────────────────────────────────────────┐
                    │                    TEST ENVIRONMENT                         │
                    └─────────────────────────────────────────────────────────────┘
                                              │
                    ┌─────────────────────────┴─────────────────────────┐
                    │                                                     │
              ┌─────▼─────┐                                        ┌─────▼─────┐
              │ NetAlly   │                                        │ Fluke     │
              │ LinkRunner│  ◄──────── ITO Packets ────────►       │ OneTouch  │
              │ 10G       │                                        │           │
              └─────┬─────┘                                        └─────┬─────┘
                    │                                                     │
                    └─────────────────────────┬───────────────────────────┘
                                              │
                                              ▼
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                              REFLECTOR SERVER                                        │
│                                                                                      │
│  ┌───────────────────────────────────────────────────────────────────────────────┐  │
│  │                         CLI / Configuration Layer                              │  │
│  │                                                                                │  │
│  │  ./reflector-linux eth0 --port 3842 --oui 00:c0:17 --mode all --latency       │  │
│  │                                                                                │  │
│  │  Options:                                                                      │  │
│  │  ├── --port N        Filter UDP port (default: 3842, 0 = any)                 │  │
│  │  ├── --oui XX:XX:XX  Source MAC OUI (default: 00:c0:17 NetAlly)               │  │
│  │  ├── --no-oui-filter Disable OUI filtering                                    │  │
│  │  ├── --mode          Reflection mode: mac | mac-ip | all                      │  │
│  │  ├── --dpdk          Use DPDK (100G mode)                                     │  │
│  │  ├── --latency       Enable latency measurements                              │  │
│  │  └── --json/--csv    Output format                                            │  │
│  └───────────────────────────────────────────────────────────────────────────────┘  │
│                                        │                                             │
│  ┌─────────────────────────────────────▼─────────────────────────────────────────┐  │
│  │                        Packet Validation Pipeline                              │  │
│  │                                                                                │  │
│  │  ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐    │  │
│  │  │ 1. Dst   │ → │ 2. Src   │ → │ 3. IPv4  │ → │ 4. UDP   │ → │ 5. ITO   │    │  │
│  │  │ MAC      │   │ OUI      │   │ Proto    │   │ Port     │   │ Sig      │    │  │
│  │  │ Match    │   │ Check    │   │ Check    │   │ Check    │   │ Match    │    │  │
│  │  └──────────┘   └──────────┘   └──────────┘   └──────────┘   └──────────┘    │  │
│  │                                                                                │  │
│  │  Signatures: PROBEOT | DATA:OT | LATENCY                                      │  │
│  └───────────────────────────────────────────────────────────────────────────────┘  │
│                                        │                                             │
│  ┌─────────────────────────────────────▼─────────────────────────────────────────┐  │
│  │                          Reflection Engine                                     │  │
│  │                                                                                │  │
│  │  ┌────────────────────────────────────────────────────────────────────────┐   │  │
│  │  │ Mode: MAC (L2)     │ Mode: MAC+IP (L3)    │ Mode: ALL (L4) [default]  │   │  │
│  │  ├────────────────────┼──────────────────────┼───────────────────────────┤   │  │
│  │  │ Swap: ETH src/dst  │ Swap: ETH + IP       │ Swap: ETH + IP + UDP      │   │  │
│  │  │ Use case: L2 only  │ Use case: Routing    │ Use case: Full path test  │   │  │
│  │  └────────────────────┴──────────────────────┴───────────────────────────┘   │  │
│  │                                                                                │  │
│  │  SIMD Optimization: SSE2 (x86_64) | NEON (ARM64) | Scalar fallback           │  │
│  └───────────────────────────────────────────────────────────────────────────────┘  │
│                                        │                                             │
│  ┌─────────────────────────────────────▼─────────────────────────────────────────┐  │
│  │                      Platform Abstraction Layer                                │  │
│  │                                                                                │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐           │  │
│  │  │   DPDK      │  │   AF_XDP    │  │  AF_PACKET  │  │  macOS BPF  │           │  │
│  │  │   (Linux)   │  │   (Linux)   │  │   (Linux)   │  │             │           │  │
│  │  ├─────────────┤  ├─────────────┤  ├─────────────┤  ├─────────────┤           │  │
│  │  │ 100+ Gbps   │  │ 10-40 Gbps  │  │ 100-500 Mbps│  │ 10-75 Mbps  │           │  │
│  │  │ Poll-mode   │  │ Zero-copy   │  │ Copy-mode   │  │ Copy-mode   │           │  │
│  │  │ --dpdk flag │  │ Default     │  │ Fallback    │  │ Default     │           │  │
│  │  └─────────────┘  └─────────────┘  └─────────────┘  └─────────────┘           │  │
│  └───────────────────────────────────────────────────────────────────────────────┘  │
│                                        │                                             │
│  ┌─────────────────────────────────────▼─────────────────────────────────────────┐  │
│  │                           NIC / Hardware                                       │  │
│  └───────────────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Hardware Requirements

### CPU Requirements

| Scenario | Minimum Cores | Recommended | Notes |
|----------|---------------|-------------|-------|
| macOS (any) | 1 core | 2 cores | BPF is single-threaded |
| Linux AF_PACKET | 1 core | 2 cores | Single-queue fallback |
| Linux AF_XDP 10G | 2 cores | 4 cores | 1 core per queue |
| Linux AF_XDP 25G | 4 cores | 8 cores | RSS spreads load |
| Linux AF_XDP 40G | 4 cores | 8+ cores | Need 4+ queues |
| Linux DPDK 100G | 4 cores | 8+ cores | Dedicated poll-mode |

**CPU Architecture Support:**
- x86_64 with SSE2: Full SIMD optimization
- ARM64/AArch64: NEON SIMD optimization
- Other: Optimized scalar fallback

### Network Interface Requirements

#### For 10G Line-Rate (AF_XDP)

| Vendor | Chipset | Driver | XDP Support | Notes |
|--------|---------|--------|-------------|-------|
| Intel | X520/X540 | ixgbe | Native | Excellent |
| Intel | X710/XL710 | i40e | Native | Excellent |
| Intel | E810 | ice | Native | Best performance |
| Mellanox | ConnectX-4/5/6 | mlx5 | Native | Excellent |
| Broadcom | BCM57xxx | bnxt | Native | Good |
| Amazon | ENA | ena | Generic | Works in EC2 |

#### For 100G Line-Rate (DPDK)

| Vendor | Chipset | DPDK PMD | Notes |
|--------|---------|----------|-------|
| Intel | E810 | ice | Recommended |
| Mellanox | ConnectX-5/6 | mlx5 | Excellent |
| Broadcom | BCM5xxx | bnxt | Good |

**NICs to Avoid:**
- Realtek consumer NICs (no XDP support)
- USB-to-Ethernet adapters (no XDP, high latency)
- Virtual NICs (limited performance)

---

## Multi-Client Architecture

The reflector can handle multiple simultaneous test tools through RSS (Receive Side Scaling):

```
┌────────────────────────────────────────────────────────────────────────────────────┐
│                            MULTI-CLIENT SCENARIO                                    │
│                                                                                     │
│   ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐                          │
│   │ Client 1 │  │ Client 2 │  │ Client 3 │  │ Client 4 │                          │
│   │ @ 10G    │  │ @ 10G    │  │ @ 10G    │  │ @ 10G    │                          │
│   └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘                          │
│        │             │             │             │                                 │
│        └──────┬──────┴──────┬──────┴──────┬──────┘                                 │
│               │             │             │                                         │
│               ▼             ▼             ▼                                         │
│   ┌───────────────────────────────────────────────────────────────┐               │
│   │                     40G NIC with RSS                           │               │
│   │  ┌─────────────────────────────────────────────────────────┐  │               │
│   │  │         RSS Hash (src IP, dst IP, src port, dst port)   │  │               │
│   │  └─────────────────────────────────────────────────────────┘  │               │
│   │        │             │             │             │            │               │
│   │   ┌────▼────┐   ┌────▼────┐   ┌────▼────┐   ┌────▼────┐      │               │
│   │   │ Queue 0 │   │ Queue 1 │   │ Queue 2 │   │ Queue 3 │      │               │
│   │   └────┬────┘   └────┬────┘   └────┬────┘   └────┬────┘      │               │
│   └────────┼─────────────┼─────────────┼─────────────┼───────────┘               │
│            │             │             │             │                             │
│   ┌────────▼─────────────▼─────────────▼─────────────▼───────────┐               │
│   │                     AF_XDP Sockets                            │               │
│   └────────┬─────────────┬─────────────┬─────────────┬───────────┘               │
│            │             │             │             │                             │
│   ┌────────▼────┐   ┌────▼────┐   ┌────▼────┐   ┌────▼────┐                       │
│   │  Worker 0   │   │ Worker 1│   │ Worker 2│   │ Worker 3│                       │
│   │  (CPU 0)    │   │ (CPU 1) │   │ (CPU 2) │   │ (CPU 3) │                       │
│   │  ~10 Gbps   │   │ ~10 Gbps│   │ ~10 Gbps│   │ ~10 Gbps│                       │
│   └─────────────┘   └─────────┘   └─────────┘   └─────────┘                       │
│                                                                                     │
│   Total Capacity: 4 × 10 Gbps = 40 Gbps (at ~90% efficiency)                       │
└────────────────────────────────────────────────────────────────────────────────────┘
```

### Multi-Client Capacity Examples

| NIC Speed | Platform | # of 1G Clients | # of 10G Clients | Notes |
|-----------|----------|-----------------|------------------|-------|
| 10G | AF_XDP | 10 | 1 | Single 10G fully utilized |
| 25G | AF_XDP | 25 | 2 | Good multi-client |
| 40G | AF_XDP | 40 | 4 | 4 queues recommended |
| 100G | DPDK | 100 | 10 | Full enterprise scale |

---

## Platform-Specific Architecture

### Linux DPDK (100G Mode)

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Application Space                                                       │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │                         reflector-linux --dpdk                      │ │
│  │                                                                     │ │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐           │ │
│  │  │ Worker 0 │  │ Worker 1 │  │ Worker 2 │  │ Worker 3 │           │ │
│  │  │ (lcore 1)│  │ (lcore 2)│  │ (lcore 3)│  │ (lcore 4)│           │ │
│  │  │ Queue 0  │  │ Queue 1  │  │ Queue 2  │  │ Queue 3  │           │ │
│  │  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘           │ │
│  │       │             │             │             │                  │ │
│  │  ┌────▼─────────────▼─────────────▼─────────────▼────────────────┐│ │
│  │  │                     DPDK rte_eth_rx/tx_burst                  ││ │
│  │  └───────────────────────────────────────────────────────────────┘│ │
│  └────────────────────────────────────────────────────────────────────┘ │
│                              │                                           │
│  ┌───────────────────────────▼───────────────────────────────────────┐  │
│  │                     DPDK Memory Pool (mbuf)                        │  │
│  │                     Hugepages (2MB/1GB)                            │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                              │                                           │
├──────────────────────────────┼───────────────────────────────────────────┤
│  User/Kernel Boundary        │                                           │
├──────────────────────────────┼───────────────────────────────────────────┤
│                              │  (BYPASSED - Direct NIC Access)           │
│  ┌───────────────────────────▼───────────────────────────────────────┐  │
│  │                     DPDK Poll-Mode Driver                          │  │
│  │                     (vfio-pci / uio_pci_generic)                   │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                              │                                           │
│  ┌───────────────────────────▼───────────────────────────────────────┐  │
│  │                     100G NIC (Intel E810 / Mellanox CX-6)          │  │
│  └───────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘

Prerequisites:
  1. NIC bound to vfio-pci:  dpdk-devbind.py --bind=vfio-pci 0000:04:00.0
  2. Hugepages configured:   echo 1024 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
  3. IOMMU enabled:          intel_iommu=on (in GRUB)
```

### Linux AF_XDP (10-40G Mode)

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Application Space                                                       │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │                         reflector-linux                             │ │
│  │                                                                     │ │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐           │ │
│  │  │ Worker 0 │  │ Worker 1 │  │ Worker 2 │  │ Worker 3 │           │ │
│  │  │ (CPU 0)  │  │ (CPU 1)  │  │ (CPU 2)  │  │ (CPU 3)  │           │ │
│  │  │ Queue 0  │  │ Queue 1  │  │ Queue 2  │  │ Queue 3  │           │ │
│  │  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘           │ │
│  │       │             │             │             │                  │ │
│  │  ┌────▼─────────────▼─────────────▼─────────────▼────────────────┐│ │
│  │  │                     AF_XDP Sockets (zero-copy)                ││ │
│  │  └───────────────────────────────────────────────────────────────┘│ │
│  └────────────────────────────────────────────────────────────────────┘ │
│                              │                                           │
│  ┌───────────────────────────▼───────────────────────────────────────┐  │
│  │                     UMEM (Shared Memory Region)                    │  │
│  │                     16MB = 4096 frames × 4KB                       │  │
│  │                     mmap'd, zero-copy with kernel                  │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                              │                                           │
├──────────────────────────────┼───────────────────────────────────────────┤
│  Kernel Space                │                                           │
│                              ▼                                           │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │                     XDP/eBPF Filter Program                        │  │
│  │  - Fast MAC check                                                  │  │
│  │  - IPv4/UDP protocol check                                         │  │
│  │  - XDP_REDIRECT to AF_XDP socket                                   │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                              │                                           │
│  ┌───────────────────────────▼───────────────────────────────────────┐  │
│  │                     NIC Driver (i40e/ice/mlx5)                     │  │
│  │                     RSS → Queue Distribution                       │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                              │                                           │
│  ┌───────────────────────────▼───────────────────────────────────────┐  │
│  │                     10/25/40G NIC                                  │  │
│  └───────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
```

### macOS BPF

```
┌─────────────────────────────────────────────────────────────────────────┐
│  Application Space                                                       │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │                         reflector-macos                             │ │
│  │                                                                     │ │
│  │  ┌──────────────────────────────────────────────────────────────┐  │ │
│  │  │                     Worker Thread                             │  │ │
│  │  │                     (single-threaded)                         │  │ │
│  │  └─────────────────────────┬────────────────────────────────────┘  │ │
│  │                            │                                        │ │
│  │  ┌────────────┐       ┌────▼────────┐       ┌────────────┐         │ │
│  │  │ BPF Read   │       │   Process   │       │ BPF Write  │         │ │
│  │  │ /dev/bpf   │  →    │   Packet    │   →   │ /dev/bpf   │         │ │
│  │  │ (1MB buf)  │       │             │       │            │         │ │
│  │  └────────────┘       └─────────────┘       └────────────┘         │ │
│  └────────────────────────────────────────────────────────────────────┘ │
│                              │                                           │
├──────────────────────────────┼───────────────────────────────────────────┤
│  Kernel Space                │                                           │
│                              ▼                                           │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │                     BPF Filter                                     │  │
│  │  - MAC address check                                               │  │
│  │  - IPv4/UDP check                                                  │  │
│  │  - Port check (3842)                                               │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                              │                                           │
│  ┌───────────────────────────▼───────────────────────────────────────┐  │
│  │                     Network Stack (copy mode)                      │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                              │                                           │
│  ┌───────────────────────────▼───────────────────────────────────────┐  │
│  │                     NIC Driver                                     │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                              │                                           │
│  ┌───────────────────────────▼───────────────────────────────────────┐  │
│  │                     NIC (USB/Thunderbolt adapter)                  │  │
│  └───────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘

Performance Limitation: 10-75 Mbps (kernel copy overhead)
```

---

## Running on macOS with VMs

To achieve high performance on a Mac, run Linux in a VM with a passed-through NIC:

### Option 1: UTM/QEMU with USB Passthrough (Easiest)

```
┌─────────────────────────────────────────────────────────────────────────┐
│  macOS Host                                                              │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │  UTM / QEMU Virtual Machine                                         │ │
│  │                                                                     │ │
│  │  ┌──────────────────────────────────────────────────────────────┐  │ │
│  │  │  Ubuntu 24.04 Guest                                           │  │ │
│  │  │                                                               │  │ │
│  │  │  reflector-linux eth0   (AF_XDP if supported)                 │  │ │
│  │  │                                                               │  │ │
│  │  │  USB NIC (passed through)                                     │  │ │
│  │  │  - Realtek USB: 100 Mbps (no XDP)                            │  │ │
│  │  │  - Anker USB-C 2.5G: Up to 2.5 Gbps (no XDP)                 │  │ │
│  │  │  - Thunderbolt dock: Varies                                   │  │ │
│  │  └──────────────────────────────────────────────────────────────┘  │ │
│  └────────────────────────────────────────────────────────────────────┘ │
│                                                                          │
│  Steps:                                                                  │
│  1. Install UTM from App Store                                          │
│  2. Create Ubuntu 24.04 VM (ARM64 for M-series Macs)                    │
│  3. Connect USB Ethernet adapter                                         │
│  4. In UTM: Right-click VM → USB → Select your adapter                  │
│  5. Build and run reflector-linux in the VM                             │
└─────────────────────────────────────────────────────────────────────────┘
```

### Option 2: Parallels Desktop (Better Performance)

```
┌─────────────────────────────────────────────────────────────────────────┐
│  macOS Host (M1/M2/M3)                                                   │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │  Parallels Desktop VM                                               │ │
│  │                                                                     │ │
│  │  ┌──────────────────────────────────────────────────────────────┐  │ │
│  │  │  Ubuntu 24.04 ARM64                                           │  │ │
│  │  │                                                               │  │ │
│  │  │  reflector-linux eth0                                         │  │ │
│  │  │                                                               │  │ │
│  │  │  virtio-net (bridged to physical NIC)                         │  │ │
│  │  │  - Up to 10 Gbps throughput                                   │  │ │
│  │  │  - No XDP (virtio limitation)                                 │  │ │
│  │  │  - AF_PACKET fallback                                         │  │ │
│  │  └──────────────────────────────────────────────────────────────┘  │ │
│  └────────────────────────────────────────────────────────────────────┘ │
│                                                                          │
│  Performance: ~500 Mbps - 2 Gbps (virtio overhead)                      │
└─────────────────────────────────────────────────────────────────────────┘
```

### Option 3: Dedicated Linux Server (Best Performance)

For production 10G-100G testing, use a dedicated Linux server:

```
Recommended Configurations:

Entry Level (10G):
  - Intel NUC or Mini PC
  - Intel X520-DA2 (10G SFP+)
  - Ubuntu 24.04
  - 4 cores, 16GB RAM
  - AF_XDP mode

Mid-Range (25G-40G):
  - Dell R640/R740 or HP DL380
  - Intel X710-DA4 or Mellanox CX-4
  - Ubuntu 22.04 LTS
  - 8+ cores, 32GB RAM
  - AF_XDP mode

High-End (100G):
  - Dell R750 or Supermicro
  - Intel E810 or Mellanox CX-6
  - Ubuntu 22.04 LTS
  - 16+ cores, 64GB RAM, Hugepages
  - DPDK mode
```

---

## Packet Flow Detail

```
                           RECEIVE PATH

 ┌─────────┐    ┌──────────┐    ┌──────────────┐    ┌────────────┐
 │   NIC   │ →  │  Driver  │ →  │ XDP/BPF      │ →  │  Worker    │
 │ RX Ring │    │  RSS     │    │ Filter       │    │  Thread    │
 └─────────┘    └──────────┘    └──────────────┘    └────────────┘
                                                           │
                                                           ▼
                     ┌─────────────────────────────────────────────┐
                     │              VALIDATION CHAIN                │
                     │                                              │
                     │  ┌─────┐  ┌─────┐  ┌─────┐  ┌─────┐  ┌────┐│
                     │  │Dst  │→ │OUI  │→ │IPv4 │→ │Port │→ │Sig ││
                     │  │MAC  │  │     │  │UDP  │  │3842 │  │    ││
                     │  └─────┘  └─────┘  └─────┘  └─────┘  └────┘│
                     │                                              │
                     │  Any check fails → Drop packet               │
                     │  All checks pass → Reflect packet            │
                     └─────────────────────────────────────────────┘
                                         │
                                         ▼
                     ┌─────────────────────────────────────────────┐
                     │              REFLECTION ENGINE               │
                     │                                              │
                     │  Original Packet:                            │
                     │  ┌─────────────────────────────────────────┐│
                     │  │ Dst MAC │ Src MAC │ Dst IP │ Src IP │...││
                     │  │  (us)   │ (tool)  │ (us)   │ (tool) │   ││
                     │  └─────────────────────────────────────────┘│
                     │                    ↓ SWAP                    │
                     │  Reflected Packet:                           │
                     │  ┌─────────────────────────────────────────┐│
                     │  │ Dst MAC │ Src MAC │ Dst IP │ Src IP │...││
                     │  │ (tool)  │  (us)   │ (tool) │  (us)  │   ││
                     │  └─────────────────────────────────────────┘│
                     └─────────────────────────────────────────────┘
                                         │
                                         ▼
                          TRANSMIT PATH (same NIC)

 ┌─────────┐    ┌──────────┐    ┌──────────────┐    ┌────────────┐
 │   NIC   │ ←  │  Driver  │ ←  │ TX Ring      │ ←  │  Worker    │
 │ TX Ring │    │          │    │              │    │  Thread    │
 └─────────┘    └──────────┘    └──────────────┘    └────────────┘
```

---

## Statistics & Monitoring

The reflector collects comprehensive statistics:

```json
{
  "packets": {
    "received": 1234567,
    "reflected": 1234000,
    "dropped": 567
  },
  "bytes": {
    "received": 1852350500,
    "reflected": 1851000000
  },
  "signatures": {
    "probeot": 500000,
    "dataot": 700000,
    "latency": 34000
  },
  "latency": {
    "min_us": 1.2,
    "avg_us": 2.5,
    "max_us": 15.3
  },
  "performance": {
    "pps": 125000,
    "mbps": 1500.5
  }
}
```

---

## Code Organization

```
src/dataplane/
├── common/                     # Platform-agnostic code
│   ├── packet.c                # Validation + SIMD reflection
│   ├── core.c                  # Worker management + stats
│   ├── util.c                  # Interface utilities
│   └── main.c                  # CLI parsing
├── linux_dpdk/                 # DPDK platform (100G)
│   └── dpdk_platform.c         # rte_eth_rx/tx_burst
├── linux_xdp/                  # AF_XDP platform (10-40G)
│   └── xdp_platform.c          # Zero-copy sockets
├── linux_packet/               # AF_PACKET platform (fallback)
│   └── packet_platform.c       # Raw sockets
└── macos_bpf/                  # macOS BPF platform
    └── bpf_platform.c          # /dev/bpf devices
```

---

## Platform Selection Logic

```
Linux:
  if (--dpdk flag && DPDK installed):
      use DPDK (100G mode)
  else if (AF_XDP headers available):
      use AF_XDP (zero-copy)
      on failure: fallback to AF_PACKET
  else:
      use AF_PACKET (copy mode)

macOS:
  use BPF (only option)
```

---

## Version History

| Version | Date | Key Features |
|---------|------|--------------|
| 1.11.0 | Dec 2025 | ITO filtering, reflection modes, OUI check |
| 1.10.0 | Dec 2025 | DPDK 100G support |
| 1.9.0 | Jan 2025 | AF_XDP multi-queue, SIMD optimization |
| 1.0.0 | Jan 2025 | Initial release |
