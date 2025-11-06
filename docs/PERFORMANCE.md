# Performance Guide

> **Note:** This document describes both current v1.0.1 performance and planned future performance with AF_XDP (v2.0+).
> Sections marked with 🚀 indicate future planned features. See [ROADMAP.md](../ROADMAP.md) for timeline.

## Current Performance (v1.0.1)

### Linux (AF_PACKET)

| Packet Size | Test Rate | Result |
|-------------|-----------|---------|
| 1518 bytes | 1 Mbps | Stable operation |
| 1518 bytes | 10 Mbps | Limited performance |

**Current Implementation:**
- AF_PACKET socket-based capture
- Single-threaded processing
- Works with all network adapters
- Suitable for lab testing and low-rate scenarios

### macOS (BPF)

| Packet Size | Test Rate | Result |
|-------------|-----------|---------|
| 1518 bytes | 1 Mbps | 99.96% success (4970/4972 packets) |
| 1518 bytes | 1 Gbps | Limited to ~50 Mbps max |

**Current Limitations:**
- BPF architectural limitations
- Maximum ~10-50 Mbps regardless of hardware
- Suitable for lab testing only

---

## 🚀 Planned Performance (v2.0+ with AF_XDP)

### Linux (AF_XDP) - Planned v2.0

| NIC Type | Packet Size | Expected PPS | Line Rate @ 10G |
|----------|-------------|--------------|-----------------|
| Intel X710/E810 | 64 bytes | 12-14 Mpps | 85-95% |
| Intel X710/E810 | 512 bytes | 2-2.4 Mpps | 95-100% |
| Intel X710/E810 | 1500 bytes | 812K+ pps | 100% |
| Generic XDP | 64 bytes | 1-3 Mpps | 10-20% |
| Generic XDP | 1500 bytes | 400-600K pps | 50-75% |

### macOS (BPF) - No Change Planned

macOS will continue using BPF in v2.0+ due to platform limitations.
Performance improvements will come from optimizations within BPF constraints.

---

## Current Optimization (v1.0.1)

### Linux AF_PACKET

Current implementation requires minimal tuning:
- Ensure interface is UP
- Use verbose mode (`-v`) for debugging
- Monitor with system tools

### macOS BPF

No tuning available - architectural limitations apply.

---

## 🚀 Planned Linux Optimization (v2.0+ with AF_XDP)

### 1. NIC Selection

**Native XDP (Recommended):**
- Intel i40e: X710, XL710, X722
- Intel ice: E810
- Intel ixgbe: 82599, X520, X540, X550
- Mellanox mlx5: ConnectX-4, ConnectX-5, ConnectX-6

Check driver:
```bash
ethtool -i eth0 | grep driver
```

### 2. Kernel Configuration

**Minimum kernel: 5.3+** (for AF_XDP)

Check version:
```bash
uname -r
```

Enable huge pages:
```bash
echo 512 | sudo tee /proc/sys/vm/nr_hugepages
```

### 3. Interface Tuning

Run tuning script:
```bash
sudo ./scripts/tune-interface.sh eth0
```

Manual tuning:
```bash
# Increase ring buffers
sudo ethtool -G eth0 rx 4096 tx 4096

# Enable multi-queue RSS
sudo ethtool -L eth0 combined 4

# Disable GRO/LRO
sudo ethtool -K eth0 gro off lro off

# Check settings
ethtool -g eth0
ethtool -l eth0
ethtool -k eth0 | grep -E "gro|lro"
```

### 4. CPU Affinity

For best performance, pin IRQs to specific CPUs:

```bash
# Find IRQs for interface
grep eth0 /proc/interrupts

# Set IRQ affinity (example for IRQ 125 to CPU 0)
echo 1 | sudo tee /proc/irq/125/smp_affinity
```

### 5. System Tuning

Disable power saving:
```bash
# Set CPU governor to performance
for cpu in /sys/devices/system/cpu/cpu*/cpufreq/scaling_governor; do
    echo performance | sudo tee $cpu
done
```

Increase socket buffer sizes:
```bash
sudo sysctl -w net.core.rmem_max=134217728
sudo sysctl -w net.core.wmem_max=134217728
```

### 6. Verification

Check XDP program is loaded:
```bash
sudo bpftool prog show
sudo bpftool map show
```

Monitor XDP statistics:
```bash
# While reflector is running
sudo bpftool prog show | grep xdp
```

## Current macOS Optimization (v1.0.1)

### System Tuning

Increase socket buffers (optional):
```bash
sudo sysctl -w kern.ipc.maxsockbuf=16777216
```

### Performance Expectations

Current v1.0.1 limitations:
- No zero-copy (all packets copied)
- No multi-queue support
- Single-threaded packet processing
- BPF API overhead
- Maximum ~10-50 Mbps throughput

Best results:
- Use with 1 Mbps test rates
- 1518-byte frames work well
- Suitable for lab/development testing

**Note:** For production line-rate testing, use Linux with planned AF_XDP implementation (v2.0+).

## Benchmarking

### Test with iperf3

Sender (generate UDP traffic):
```bash
iperf3 -c <reflector-ip> -u -b 10G -l 1400 -t 60
```

### Test with pktgen

For small packet testing:
```bash
# Load pktgen module
sudo modprobe pktgen

# Configure (see Linux kernel docs)
```

### Monitor Statistics

Watch reflector output:
```bash
sudo ./reflector-linux eth0 -v
```

Expected output:
```
[10.0s] RX: 8120000 pkts (12180000000 bytes) | Reflected: 8120000 pkts | 812000 pps, 9750 Mbps
```

## Troubleshooting Performance

### Low throughput on Linux

1. **Check XDP mode:**
   ```bash
   ip link show eth0 | grep xdp
   ```
   Should show "xdpdrv" (native) not "xdpgeneric"

2. **Check CPU usage:**
   ```bash
   top -H -p $(pgrep reflector)
   ```
   Should show multiple threads near 100% on busy system

3. **Check for drops:**
   ```bash
   ethtool -S eth0 | grep -i drop
   ```

4. **Verify multi-queue:**
   ```bash
   ethtool -l eth0
   ```

### Generic XDP fallback

If native XDP not available, performance will be reduced. Upgrade:
- Kernel to 5.3+
- NIC driver to latest version
- Consider NIC replacement

### Memory allocation failures

If "Failed to allocate UMEM":
```bash
# Enable huge pages
echo 512 | sudo tee /proc/sys/vm/nr_hugepages

# Check huge pages
cat /proc/meminfo | grep Huge
```

## Production Deployment

For production use:
1. Run performance benchmark before deployment
2. Monitor statistics during operation
3. Set up monitoring/alerting for packet drops
4. Document NIC model and performance characteristics
5. Keep kernel and NIC drivers updated
6. Consider redundant interfaces for high availability
