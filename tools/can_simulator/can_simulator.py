#!/usr/bin/env python3
"""
can_simulator.py — CAN Bus Replay Simulator
============================================
Replays captured CAN frames from Saleae-exported CSV files onto a SocketCAN
interface (vcan0 or physical CAN), preserving the original inter-frame timing.

The simulator feeds the AV_Network gateway as if a real CAN bus were active.

Usage:
    python3 can_simulator.py --iface vcan0 --file scope_17.csv
    python3 can_simulator.py --iface vcan0 --file scope_17.csv --loop --speed 2.0

CSV format expected (Saleae Logic CAN export):
    Marked,Time,Serial Bus,ID,Type,DLC,Data,CRC,Errors
    ,-500.0ms,Serial1,18FE50A7,Data,8,40 02 41 02 40 02 00 00,3201,
"""

import argparse
import csv
import re
import sys
import time
import can


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

_TIME_RE = re.compile(r"^([+-]?\d+\.?\d*)(ms|us|ns|s)$")

def parse_time_seconds(time_str: str) -> float | None:
    """Convert Saleae time string (e.g. '-500.0ms', '2.928ms', '-3.200us') to seconds."""
    s = time_str.strip()
    if not s:
        return None
    m = _TIME_RE.match(s)
    if not m:
        return None
    value = float(m.group(1))
    unit  = m.group(2)
    multipliers = {"s": 1.0, "ms": 1e-3, "us": 1e-6, "ns": 1e-9}
    return value * multipliers[unit]


def parse_csv(filepath: str) -> list[dict]:
    """
    Parse a Saleae CAN CSV file and return a list of valid frame dicts:
        { "time_s": float, "can_id": int, "dlc": int, "data": bytes }
    Rows with missing/malformed fields are silently skipped.
    """
    frames = []
    with open(filepath, newline="", encoding="utf-8-sig") as f:
        reader = csv.DictReader(f)
        for row in reader:
            # Skip rows without a valid timestamp
            t = parse_time_seconds(row.get("Time", ""))
            if t is None:
                continue

            # Skip non-data rows (Remote frames, error frames, etc.)
            if row.get("Type", "").strip() != "Data":
                continue

            # Parse CAN ID (hex string, extended 29-bit)
            id_str = row.get("ID", "").strip()
            if not id_str:
                continue
            try:
                can_id = int(id_str, 16)
            except ValueError:
                continue

            # Parse DLC
            dlc_str = row.get("DLC", "").strip()
            try:
                dlc = int(dlc_str)
            except ValueError:
                dlc = 8  # default

            # Parse data bytes ("40 02 41 02 ...")
            data_str = row.get("Data", "").strip()
            if not data_str:
                continue
            try:
                data = bytes(int(b, 16) for b in data_str.split())
            except ValueError:
                continue

            # Sanity check: data length must match DLC
            if len(data) < dlc:
                data = data.ljust(dlc, b"\x00")

            frames.append({
                "time_s": t,
                "can_id": can_id,
                "dlc":    dlc,
                "data":   data[:dlc],
            })

    return frames


# ---------------------------------------------------------------------------
# Replay engine
# ---------------------------------------------------------------------------

def replay(frames: list[dict], bus: can.BusABC, speed: float, verbose: bool) -> None:
    """
    Send all frames respecting inter-frame timing.
    speed > 1.0  → faster than real time
    speed < 1.0  → slower than real time
    """
    if not frames:
        print("[simulator] No valid frames to send.", file=sys.stderr)
        return

    print(f"[simulator] Starting replay of {len(frames)} frames at {speed}x speed...")

    prev_time_s = frames[0]["time_s"]

    for i, f in enumerate(frames):
        # Calculate delay from previous frame
        delta = (f["time_s"] - prev_time_s) / speed
        if delta > 0:
            time.sleep(delta)
        prev_time_s = f["time_s"]

        msg = can.Message(
            arbitration_id=f["can_id"],
            is_extended_id=True,   # All ORCA IDs are 29-bit extended
            data=f["data"],
            is_fd=False,
        )

        try:
            bus.send(msg)
            if verbose:
                print(
                    f"[{i+1:5d}/{len(frames)}] "
                    f"t={f['time_s']*1000:+9.3f}ms  "
                    f"ID=0x{f['can_id']:08X}  "
                    f"DLC={f['dlc']}  "
                    f"data={f['data'].hex(' ').upper()}"
                )
        except can.CanError as e:
            print(f"[simulator] Send error on frame {i+1}: {e}", file=sys.stderr)


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="CAN CSV replay simulator → vcan0 / SocketCAN"
    )
    parser.add_argument(
        "--iface", default="vcan0",
        help="SocketCAN interface name (default: vcan0)"
    )
    parser.add_argument(
        "--file", required=True,
        help="Path to Saleae CAN CSV file (e.g. scope_17.csv)"
    )
    parser.add_argument(
        "--loop", action="store_true",
        help="Loop replay indefinitely (Ctrl+C to stop)"
    )
    parser.add_argument(
        "--speed", type=float, default=1.0,
        help="Replay speed multiplier (default: 1.0 = real time)"
    )
    parser.add_argument(
        "--verbose", "-v", action="store_true",
        help="Print each frame as it is sent"
    )
    args = parser.parse_args()

    if args.speed <= 0:
        print("[simulator] --speed must be > 0", file=sys.stderr)
        sys.exit(1)

    print(f"[simulator] Parsing {args.file}...")
    frames = parse_csv(args.file)
    if not frames:
        print("[simulator] No valid frames found in CSV. Exiting.", file=sys.stderr)
        sys.exit(1)

    # Time span of the recording
    duration_ms = (frames[-1]["time_s"] - frames[0]["time_s"]) * 1000
    print(f"[simulator] Loaded {len(frames)} frames  |  span: {duration_ms:.1f} ms")
    print(f"[simulator] Interface: {args.iface}  |  Loop: {args.loop}  |  Speed: {args.speed}x")

    try:
        bus = can.interface.Bus(args.iface, interface="socketcan")
    except OSError as e:
        print(f"[simulator] Failed to open {args.iface}: {e}", file=sys.stderr)
        print("[simulator] Make sure vcan0 is up: sudo ./setup_vcan.sh", file=sys.stderr)
        sys.exit(1)

    iteration = 0
    try:
        while True:
            iteration += 1
            if args.loop:
                print(f"[simulator] --- Iteration {iteration} ---")
            replay(frames, bus, args.speed, args.verbose)
            if not args.loop:
                break
    except KeyboardInterrupt:
        print("\n[simulator] Stopped by user.")
    finally:
        bus.shutdown()
        print(f"[simulator] Done. Total iterations: {iteration}")


if __name__ == "__main__":
    main()
