#!/bin/bash
# setup_vcan.sh — Activates the vcan0 virtual CAN interface.
# Run once per boot before starting the gateway or simulator.
# Usage: sudo ./setup_vcan.sh

set -e

echo "[vcan] Loading vcan kernel module..."
sudo modprobe vcan

if ip link show vcan0 &>/dev/null; then
    echo "[vcan] vcan0 already exists, bringing it up..."
    sudo ip link set up vcan0
else
    echo "[vcan] Creating vcan0 interface..."
    sudo ip link add dev vcan0 type vcan
    sudo ip link set up vcan0
fi

echo "[vcan] Done."
ip link show vcan0
