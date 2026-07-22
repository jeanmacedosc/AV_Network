# CAN Simulator

Replays CAN frames captured from the **Fever FR250 / Nextem ORCA** vehicle onto a virtual SocketCAN interface (`vcan0`), feeding the **AV_Network gateway** as if a real CAN bus were active.

## Files

| File | Description |
|---|---|
| `can_simulator.py` | Main replay script |
| `setup_vcan.sh` | Creates and activates `vcan0` (run once per boot) |
| `requirements.txt` | Python dependency (`python-can`) |

## Setup

### 1. Install Python dependency

```bash
pip3 install -r requirements.txt
```

### 2. Activate vcan0 (once per boot, both Raspberry Pis)

```bash
sudo ./setup_vcan.sh
```

### 3. Verify the interface is up

```bash
ip link show vcan0
# Expected: vcan0: <NOARP,UP,LOWER_UP> ...
```

## Usage

```bash
# Basic replay (single pass)
python3 can_simulator.py --iface vcan0 --file ../../"Fever FR250/CAN Readings/scope_17.csv"

# Loop indefinitely (for sustained experiments)
python3 can_simulator.py --iface vcan0 --file scope_17.csv --loop

# Double speed
python3 can_simulator.py --iface vcan0 --file scope_17.csv --loop --speed 2.0

# Verbose (print each frame)
python3 can_simulator.py --iface vcan0 --file scope_17.csv --verbose

# All options
python3 can_simulator.py --help
```

### Options

| Option | Default | Description |
|---|---|---|
| `--iface` | `vcan0` | SocketCAN interface name |
| `--file` | *(required)* | Path to Saleae CAN CSV file |
| `--loop` | off | Repeat replay indefinitely |
| `--speed` | `1.0` | Replay speed (2.0 = 2× faster) |
| `--verbose` | off | Print each frame to stdout |

## Monitor traffic (on another terminal)

```bash
candump vcan0
```

## Full experiment flow

```
# Node 0 (TX Pi) — Terminal order matters
sudo ./setup_vcan.sh
sudo ../ptp/setup_ptp_master.sh enx<...>          # Terminal 1
sudo ./staging/bin/gateway_arm vcan0 enx<...>     # Terminal 2 (after PTP locks)
python3 tools/can_simulator/can_simulator.py \    # Terminal 3
    --iface vcan0 \
    --file "Fever FR250/CAN Readings/scope_17.csv" \
    --loop

# Node 1 (RX Pi)
sudo ./setup_vcan.sh
sudo ../ptp/setup_ptp_slave.sh enx<...>           # Terminal 1 (wait for lock)
sudo ./staging/bin/gateway_arm vcan0 enx<...>     # Terminal 2
# Results will be written to: e2e_latency.csv
```

## CSV format

The simulator accepts Saleae Logic CAN export format:

```
Marked,Time,Serial Bus,ID,Type,DLC,Data,CRC,Errors
,-500.0ms,Serial1,18FE50A7,Data,8,40 02 41 02 40 02 00 00,3201,
```

Rows with missing/malformed fields are silently skipped.
All IDs are treated as **29-bit Extended CAN** (standard for the ORCA protocol).
