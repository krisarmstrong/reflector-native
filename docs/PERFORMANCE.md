# Performance Guide

## Expected Performance

### Linux (AF_XDP)

| NIC Type | Packet Size | Expected PPS | Line Rate @ 10G |
|----------|-------------|--------------|-----------------|
| Intel X710/E810 | 64 bytes | 12-14 Mpps | 85-95% |
| Intel X710/E810 | 512 bytes | 2-2.4 Mpps | 95-100% |
| Intel X710/E810 | 1500 bytes | 812K+ pps | 100% |
| Generic XDP | 64 bytes | 1-3 Mpps | 10-20% |
| Generic XDP | 1500 bytes | 400-600K pps | 50-75% |

### macOS (BPF)

| Packet Size | Expected PPS | Line Rate @ 10G |
|-------------|--------------|-----------------|
| 64 bytes | 0.5-1.5 Mpps | 5-15% |
| 512 bytes | 0.8-1.2 Mpps | 40-60% |
| 1500 bytes | 600-800K pps | 75-100% |

## Linux Optimization

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

## macOS Optimization

### 1. Buffer Size

BPF buffer is pre-configured to 4MB for optimal batching. No tuning available.

### 2. System Tuning

Increase socket buffers:
```bash
sudo sysctl -w kern.ipc.maxsockbuf=16777216
```

### 3. Performance Expectations

macOS limitations:
- No zero-copy (all packets copied)
- No multi-queue support
- Single-threaded packet processing
- BPF API overhead

Best results:
- Use with 1500-byte MTU traffic
- Achieves line rate at 10G with typical frame sizes
- Small packet performance limited to ~1.5 Mpps max

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
