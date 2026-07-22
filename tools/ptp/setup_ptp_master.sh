#!/bin/bash
# setup_ptp_master.sh — Configure this node as PTP Grandmaster (Node 0 / TX side)
#
# Uses linuxptp (ptp4l + phc2sys) with Software Timestamping (-S),
# which is required for USB-based 10BASE-T1S adapters that lack hardware PHC.
#
# The gateway uses CLOCK_REALTIME for all timestamps, so phc2sys synchronizes
# the system clock to the PTP time domain (or vice versa for software mode).
#
# Usage:
#   sudo ./setup_ptp_master.sh <interface>
#   sudo ./setup_ptp_master.sh enx9c956eb58a56
#
# Dependencies:
#   sudo apt install linuxptp

set -e

IFACE=${1:?"Usage: $0 <10BASE-T1S interface name>"}

echo "[PTP Master] Interface: $IFACE"
echo "[PTP Master] Starting ptp4l as Grandmaster (Layer 2, Software Timestamping)..."

# Start ptp4l in background
# -i <iface>   : network interface
# -S           : use software timestamping (required for USB adapters)
# -m           : print messages to stdout
# -2           : use Layer 2 (IEEE 802.3) transport
sudo ptp4l -i "$IFACE" -S -m -2 &
PTP4L_PID=$!

echo "[PTP Master] ptp4l PID: $PTP4L_PID"
echo "[PTP Master] Waiting 2s before starting phc2sys..."
sleep 2

# Sync CLOCK_REALTIME from the PTP hardware clock
# -s CLOCK_REALTIME : treat system clock as source in software mode
# -c $IFACE         : target is the PTP clock on the interface
# -w                : wait for ptp4l to be in synchronized state
# -m                : print messages
sudo phc2sys -s "$IFACE" -c CLOCK_REALTIME -w -m &
PHC2SYS_PID=$!

echo "[PTP Master] phc2sys PID: $PHC2SYS_PID"
echo ""
echo "[PTP Master] Running. Press Ctrl+C to stop both processes."
echo "[PTP Master] PIDs: ptp4l=$PTP4L_PID  phc2sys=$PHC2SYS_PID"

# Wait for both background processes
wait $PTP4L_PID $PHC2SYS_PID
