#!/usr/bin/env bash
# =============================================================================
# install_service.sh — Installs the 10BASE-T1S auto-setup systemd service
#
# Run this ONCE on each Raspberry Pi to register the service.
# After installation, the adapter is configured automatically on every boot.
#
# Usage:
#   sudo ./install_service.sh --node-id <N> --ip <x.x.x.x/mask>
#
# Examples:
#   sudo ./install_service.sh --node-id 0 --ip 192.168.10.11/24   # Nó 0 Sender
#   sudo ./install_service.sh --node-id 1 --ip 192.168.10.12/24   # Nó 1 Receiver
# =============================================================================

set -e

NODE_ID=""
NODE_IP=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --node-id) NODE_ID="$2"; shift 2 ;;
        --ip)      NODE_IP="$2"; shift 2 ;;
        *)
            echo "Unknown option: $1"
            echo "Usage: sudo $0 --node-id <N> --ip <x.x.x.x/mask>"
            exit 1
            ;;
    esac
done

if [[ -z "$NODE_ID" || -z "$NODE_IP" ]]; then
    echo "[ERROR] --node-id and --ip are required."
    echo ""
    echo "  Nó 0 (Sender):   sudo $0 --node-id 0 --ip 192.168.10.11/24"
    echo "  Nó 1 (Receiver): sudo $0 --node-id 1 --ip 192.168.10.12/24"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SETUP_SCRIPT="$SCRIPT_DIR/setup_10bt1s.sh"
SERVICE_TEMPLATE="$SCRIPT_DIR/av-network-setup.service"
SERVICE_DEST="/etc/systemd/system/av-network-setup.service"

echo "=============================================="
echo " Installing av-network-setup service"
echo " Node ID : $NODE_ID"
echo " IP      : $NODE_IP"
echo "=============================================="

# Make setup script executable
chmod +x "$SETUP_SCRIPT"

# Generate service file from template with correct values
sed \
    -e "s|NODE_ID_PLACEHOLDER|$NODE_ID|g" \
    -e "s|IP_PLACEHOLDER|$NODE_IP|g" \
    -e "s|ExecStart=.*setup_10bt1s.sh|ExecStart=$SETUP_SCRIPT|g" \
    "$SERVICE_TEMPLATE" > "$SERVICE_DEST"

echo ""
echo "[1/3] Service file written to: $SERVICE_DEST"
cat "$SERVICE_DEST"

# Reload systemd and enable service
echo ""
echo "[2/3] Enabling service..."
systemctl daemon-reload
systemctl enable av-network-setup.service

echo ""
echo "[3/3] Starting service now (first run)..."
systemctl start av-network-setup.service

echo ""
echo "=============================================="
echo " Done! Service status:"
echo "=============================================="
systemctl status av-network-setup.service --no-pager

echo ""
echo "The 10BASE-T1S adapter will now be configured automatically on every boot."
echo "To check status later: systemctl status av-network-setup"
echo "To view logs:          journalctl -u av-network-setup"
