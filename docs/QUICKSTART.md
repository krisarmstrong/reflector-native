# Quick Start Guide

## Linux

### Prerequisites
```bash
# Install dependencies
cd scripts
chmod +x setup-linux.sh
./setup-linux.sh
```

### Build
```bash
make
```

### Run
```bash
# Tune interface (optional but recommended)
sudo ./scripts/tune-interface.sh eth0

# Start reflector
sudo ./reflector-linux eth0
```

## macOS

### Prerequisites
```bash
# Install Xcode Command Line Tools
xcode-select --install
```

### Build
```bash
make
```

### Run
```bash
sudo ./reflector-macos en0
```

## Verification

To test the reflector, use a Fluke/NETSCOUT/NetAlly handheld tool:

1. Start reflector on target interface
2. Run OneTouch/LinkRunner/AirCheck test
3. Observer packets being reflected
4. Check statistics in reflector output

## Performance Tips

### Linux
- Use modern NIC with native XDP support (Intel i40e, ice, ixgbe)
- Run `tune-interface.sh` script before starting
- Check `ethtool -i <iface>` for XDP support
- Monitor with `bpftool prog show`

### macOS
- Performance limited by BPF architecture
- Best results with larger packet sizes (1500 bytes)
- Small packets (64-512 bytes) will not achieve line rate

## Troubleshooting

### Linux: "Failed to load BPF object"
- Install clang: `sudo apt install clang`
- Rebuild XDP program: `make clean && make`

### Linux: "Permission denied"
- Run with sudo: `sudo ./reflector-linux eth0`
- Check interface exists: `ip link show`

### macOS: "Failed to open BPF device"
- Run with sudo: `sudo ./reflector-macos en0`
- Check interface: `ifconfig en0`

### No packets reflected
- Verify interface is UP: `ip link show` or `ifconfig`
- Check MAC address matches test tool target
- Verify ITO packets are being sent to this interface
- Use `-v` flag for verbose output
