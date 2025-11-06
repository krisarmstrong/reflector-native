# Quick Start Guide

> **Current Version:** v1.0.1 - See [ROADMAP.md](../ROADMAP.md) for planned features.

## Linux (v1.0.1)

### Prerequisites
```bash
# Ubuntu/Debian
sudo apt update
sudo apt install build-essential make gcc

# Fedora/RHEL
sudo dnf install make gcc
```

### Build
```bash
git clone https://github.com/krisarmstrong/reflector-native.git
cd reflector-native
make
```

### Run
```bash
# Find your interface
ip link show

# Start reflector
sudo ./reflector-linux eth0

# Use -v for verbose output
sudo ./reflector-linux eth0 -v
```

## macOS (v1.0.1)

### Prerequisites
```bash
# Install Xcode Command Line Tools
xcode-select --install
```

### Build
```bash
git clone https://github.com/krisarmstrong/reflector-native.git
cd reflector-native
make
```

### Run
```bash
# Find your interface
ifconfig | grep "^en"

# Start reflector
sudo ./reflector-macos en0

# Use -v for verbose output
sudo ./reflector-macos en0 -v
```

## Verification

To test the reflector, use a Fluke/NETSCOUT/NetAlly handheld tool:

1. Start reflector on target interface
2. Run OneTouch/LinkRunner/AirCheck test
3. Observer packets being reflected
4. Check statistics in reflector output

## Current Performance (v1.0.1)

### Linux (AF_PACKET)
- Works with all network adapters
- Suitable for lab testing and low-rate scenarios (1-10 Mbps)
- No special tuning required
- See [ROADMAP.md](../ROADMAP.md) for planned AF_XDP implementation (v2.0)

### macOS (BPF)
- Maximum ~10-50 Mbps throughput (architectural limitation)
- Best results at 1 Mbps with 1518-byte frames
- 99.96% reflection success rate at low rates
- Suitable for lab testing only

## Troubleshooting

### Linux: "Permission denied"
```bash
# Must run with sudo for raw socket access
sudo ./reflector-linux eth0
```

### Linux: "No such device"
```bash
# Check available interfaces
ip link show

# Make sure interface is UP
sudo ip link set eth0 up
```

### macOS: "Failed to open BPF device"
```bash
# Must run with sudo for BPF device access
sudo ./reflector-macos en0

# Check available interfaces
ifconfig

# Make sure interface exists and is UP
ifconfig en0
```

### No packets reflected
1. **Check interface is UP:**
   ```bash
   # Linux
   ip link show eth0

   # macOS
   ifconfig en0
   ```

2. **Use verbose mode to see what's happening:**
   ```bash
   sudo ./reflector-macos en0 -v
   ```

3. **Verify test tool is sending to correct IP/MAC:**
   - Test tool should target the reflector's interface IP
   - Packets should be unicast to interface MAC
   - Port should be UDP 3842

4. **Check firewall:**
   ```bash
   # macOS - disable firewall temporarily
   sudo pfctl -d

   # Linux - check iptables
   sudo iptables -L -n
   ```

5. **Verify ITO packet format:**
   - Must have 5-byte header before signature
   - Signature must be PROBEOT, DATA:OT, or LATENCY
   - Use tcpdump to inspect packets:
     ```bash
     sudo tcpdump -i en0 -X udp port 3842
     ```

## Getting Help

- **Issues**: [GitHub Issues](https://github.com/krisarmstrong/reflector-native/issues)
- **Docs**: [Full documentation](../README.md)
- **Architecture**: [ARCHITECTURE.md](ARCHITECTURE.md)
- **Performance**: [PERFORMANCE.md](PERFORMANCE.md)
