#!/usr/bin/env bash
# =============================================================================
# setup_10bt1s.sh — 10BASE-T1S adapter setup for AV_Network experiment nodes
#
# Combines:
#   1. microchip_t1s driver load (from ../driver_setup/load.sh logic)
#   2. PLCA configuration via ethtool
#   3. IP address assignment
#
# Usage:
#   sudo ./setup_10bt1s.sh --node-id <0|1|...> --ip <x.x.x.x/mask> [--iface <name>] [--node-cnt <n>]
#
# Examples:
#   sudo ./setup_10bt1s.sh --node-id 0 --ip 192.168.10.11/24   # Nó 0 (Sender)
#   sudo ./setup_10bt1s.sh --node-id 1 --ip 192.168.10.12/24   # Nó 1 (Receiver)
# =============================================================================

set -e

# --------------------------------------------------------------------------
# Defaults
# --------------------------------------------------------------------------
IFACE="eth1"
NODE_CNT=8
TO_TMR=0x20
BURST_CNT=0x0
BURST_TMR=0x80
NODE_ID=""
NODE_IP=""

# Path to the kernel module (relative to this script's repo root)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
KO_FILE="$REPO_ROOT/driver_setup/microchip_t1s.ko"

# --------------------------------------------------------------------------
# Argument parsing
# --------------------------------------------------------------------------
while [[ $# -gt 0 ]]; do
    case "$1" in
        --node-id)  NODE_ID="$2";   shift 2 ;;
        --ip)       NODE_IP="$2";   shift 2 ;;
        --iface)    IFACE="$2";     shift 2 ;;
        --node-cnt) NODE_CNT="$2";  shift 2 ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: sudo $0 --node-id <N> --ip <x.x.x.x/mask> [--iface <eth1>] [--node-cnt <8>]"
            exit 1
            ;;
    esac
done

# Validate required arguments
if [[ -z "$NODE_ID" || -z "$NODE_IP" ]]; then
    echo "[ERROR] --node-id and --ip are required."
    echo "Usage: sudo $0 --node-id <N> --ip <x.x.x.x/mask>"
    exit 1
fi

echo "=============================================="
echo " 10BASE-T1S Setup"
echo " Node ID  : $NODE_ID"
echo " IP       : $NODE_IP"
echo " Interface: $IFACE"
echo " Node Cnt : $NODE_CNT"
echo "=============================================="

# --------------------------------------------------------------------------
# Step 1: Load microchip_t1s driver
# --------------------------------------------------------------------------
echo ""
echo "[1/4] Loading microchip_t1s driver..."

# Unbind devices currently using smsc95xx
DEVICES=$(ls /sys/bus/usb/drivers/smsc95xx 2>/dev/null | grep "^[1-9]" || true)

for dev in $DEVICES; do
    echo "  Unlinking device: $dev"
    echo "$dev" | sudo tee /sys/bus/usb/drivers/smsc95xx/unbind > /dev/null
done

# Unload previous module if loaded
if lsmod | grep -q microchip_t1s; then
    echo "  Unloading existing microchip_t1s module..."
    sudo rmmod microchip_t1s
fi

# Load the module
if [[ ! -f "$KO_FILE" ]]; then
    echo "[ERROR] Kernel module not found at: $KO_FILE"
    echo "        Make sure driver_setup/microchip_t1s.ko exists in the repo."
    exit 1
fi

echo "  Loading: $KO_FILE"
sudo insmod "$KO_FILE"

# Re-bind devices
for dev in $DEVICES; do
    echo "  Re-linking device: $dev"
    echo "$dev" | sudo tee /sys/bus/usb/drivers/smsc95xx/bind > /dev/null
done

echo "  Driver loaded OK."
sleep 1   # Give the kernel a moment to enumerate the interface

# --------------------------------------------------------------------------
# Step 2: Bring interface up
# --------------------------------------------------------------------------
echo ""
echo "[2/4] Bringing up $IFACE..."
sudo ip link set "$IFACE" up
echo "  $IFACE is up."

# --------------------------------------------------------------------------
# Step 3: PLCA configuration
# --------------------------------------------------------------------------
echo ""
echo "[3/4] Configuring PLCA (node-id=$NODE_ID, node-cnt=$NODE_CNT)..."
sudo ethtool --set-plca-cfg "$IFACE" \
    enable on \
    node-id "$NODE_ID" \
    node-cnt "$NODE_CNT" \
    to-tmr "$TO_TMR" \
    burst-cnt "$BURST_CNT" \
    burst-tmr "$BURST_TMR"

sudo ethtool --set-plca-status "$IFACE" on
echo "  PLCA configured."

# --------------------------------------------------------------------------
# Step 4: IP address
# --------------------------------------------------------------------------
echo ""
echo "[4/4] Assigning IP $NODE_IP to $IFACE..."

# Remove existing address if any, ignore errors
sudo ip addr flush dev "$IFACE" 2>/dev/null || true
sudo ip addr add "$NODE_IP" dev "$IFACE"
echo "  IP assigned."

# --------------------------------------------------------------------------
# Done
# --------------------------------------------------------------------------
echo ""
echo "=============================================="
echo " Setup complete!"
echo " Run 'ip addr show $IFACE' to verify."
echo "=============================================="
ip addr show "$IFACE"
