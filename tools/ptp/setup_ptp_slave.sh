#!/bin/bash
# setup_ptp_slave.sh — Configure this node as PTP Slave (Node 1 / RX side)
#
# Synchronizes CLOCK_REALTIME to the PTP Grandmaster (Node 0).
# The gateway on Node 1 uses CLOCK_REALTIME for the RX timestamp, and
# the deserialized TX timestamp (from Node 0) is also in CLOCK_REALTIME —
# so the difference (RX - TX) gives the true End-to-End latency.
#
# Expected sync accuracy with software timestamping: ~50–200µs jitter floor.
# This is accounted for in the analysis (see tools/analysis/).
#
# Usage:
#   sudo ./setup_ptp_slave.sh <interface>
#   sudo ./setup_ptp_slave.sh enx9c956eb58a56
#
# Dependencies:
#   sudo apt install linuxptp

set -e

IFACE=${1:?"Usage: $0 <10BASE-T1S interface name>"}

echo "[PTP Slave]  Interface: $IFACE"
echo "[PTP Slave]  Starting ptp4l as Slave (Layer 2, Software Timestamping)..."
echo "[PTP Slave]  Waiting for Grandmaster on the 10BASE-T1S bus..."

# Start ptp4l in slave-only mode
# -s : slave-only mode (never become master)
sudo ptp4l -i "$IFACE" -S -m -2 -s &
PTP4L_PID=$!

echo "[PTP Slave]  ptp4l PID: $PTP4L_PID"
echo "[PTP Slave]  Waiting for clock lock before starting phc2sys..."
sleep 5

# Once locked, sync CLOCK_REALTIME from the PTP-disciplined interface clock
sudo phc2sys -s "$IFACE" -c CLOCK_REALTIME -w -m &
PHC2SYS_PID=$!

echo "[PTP Slave]  phc2sys PID: $PHC2SYS_PID"
echo ""
echo "[PTP Slave]  Running. Watch the offset value in ptp4l output."
echo "[PTP Slave]  When offset < 1000ns, the clocks are synchronized."
echo "[PTP Slave]  PIDs: ptp4l=$PTP4L_PID  phc2sys=$PHC2SYS_PID"

wait $PTP4L_PID $PHC2SYS_PID
