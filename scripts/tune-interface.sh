#!/bin/bash
# Tune network interface for optimal reflector performance

if [ $# -lt 1 ]; then
    echo "Usage: $0 <interface>"
    exit 1
fi

IFACE=$1

echo "Tuning interface $IFACE for line-rate performance..."

# Check if interface exists
if ! ip link show "$IFACE" &> /dev/null; then
    echo "Error: Interface $IFACE not found"
    exit 1
fi

# Increase ring buffer sizes
echo "Setting ring buffer sizes..."
sudo ethtool -G "$IFACE" rx 4096 tx 4096 2>/dev/null || echo "  (ring buffer config not supported)"

# Enable multi-queue if available
echo "Configuring multi-queue..."
NUM_CPUS=$(nproc)
sudo ethtool -L "$IFACE" combined "$NUM_CPUS" 2>/dev/null || echo "  (multi-queue config not supported)"

# Disable offloads that interfere with XDP
echo "Disabling interfering offloads..."
sudo ethtool -K "$IFACE" gro off lro off 2>/dev/null || true

# Show current settings
echo ""
echo "Current settings for $IFACE:"
ethtool -g "$IFACE" 2>/dev/null | grep -A 4 "Current hardware"
ethtool -l "$IFACE" 2>/dev/null | grep -A 4 "Current hardware"

echo ""
echo "Interface $IFACE tuned for optimal performance!"
