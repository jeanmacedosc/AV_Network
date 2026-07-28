
# Soft-Real-Time Priority-Aware Zonal Gateway

This project implements a software-defined Zonal Gateway supporting multiple architectures including **ARM (Raspberry Pi 4/5)** and **RISC-V (VisionFive 2)**. It bridges legacy Controller Area Network (CAN) buses with high-speed 10BASE-T1S Automotive Ethernet, utilizing IEEE 1722 ACF encapsulation and priority-aware scheduling to ensure deterministic behavior for critical control signals.

## 1\. System Architecture

The gateway implements a **Bidirectional Observer/Observed pattern**. It is updated by CAN and ETH (10BASE-T1S) interfaces upon data reception and notifies the egress interfaces to handle redirection:

  * **CAN $\to$ ETH:** IEEE 1722 ACF Encapsulation.
  * **ETH $\to$ CAN:** Direct Burst.

### Priority Handling

The system functions as a **Priority Aware Gateway**, utilizing three distinct Ring Buffers classified by CAN ID ranges to ensure Quality of Service (QoS):

  * **CRITICAL Queue:** CAN IDs `0x000` to `0x100`
  * **HIGH Queue:** CAN IDs `0x101` to `0x400`
  * **LOW Queue:** All remaining IDs

### Data Flow Logic

1.  **CAN Ingress:**

      * The Gateway assigns a timestamp immediately upon frame arrival.
      * It verifies the priority based on the configured ID ranges (`Gateway::configure_routes()`).
      * The frame is inserted into the respective queue.
      * **Critical Path:** If the frame is CRITICAL, it immediately unlocks the `Gateway::egress_loop()` thread to pack and dispatch the frame, minimizing latency.

2.  **Ethernet (10BASE-T1S) Ingress:**

      * The Gateway filters Ethernet frames matching **EthType 0x22F0** (IEEE 1722 AVTP) and **Subtype 0x03** (ACF).
      * It parses the frame to determine how many CAN messages are encapsulated.
      * Messages are unpacked and burst out to the registered CAN interfaces.

-----

## 2\. Software Optimizations

To achieve Soft-Real-Time performance on a standard Linux kernel, the following key architectural improvements were implemented:

1.  **CPU Isolation:** The main Gateway process is pinned to an isolated CPU core to prevent context switching and "cold cache" effects.
2.  **Memory Locking:** All process memory is locked (`mlockall`) to prevent the OS from swapping out ring buffers to disk.
3.  **Real-Time Scheduling:** The process runs under a `SCHED_FIFO` Real-Time policy with high priority.
4.  **Zero-Copy Buffers:** Queues utilize buffer rings with `mmap` to avoid unnecessary memory copying between kernel and user space.
5.  **Event-Driven I/O:** The kernel notifies the application exactly when a packet arrives (via `poll`/interrupts), ensuring receiver threads do not waste CPU cycles busy-waiting.

-----

## 3\. Environment Setup

### Compilation

**Option A: Native Compilation (Raspberry Pi / ARM)**
For native development on standard Linux distributions (e.g., Raspberry Pi OS):
```bash
sudo apt update
sudo apt install build-essential git
g++ -std=c++17 -O2 -pthread -Iinclude -Isrc -Isrc/hal -Isrc/observer src/main.cpp src/gateway.cpp src/hal/can_iface.cpp src/hal/eth_iface.cpp -o gateway
```

**Option B: Cross-Compilation (VisionFive 2 / RISC-V)**
Ensure the host machine has the necessary RISC-V toolchain installed for cross-compilation:
```bash
sudo apt update
sudo apt install \
  crossbuild-essential-riscv64 \
  gcc-riscv64-linux-gnu \
  g++-riscv64-linux-gnu
```

### Serial Connection

To visualize the VisionFive 2 console:

```bash
sudo minicom -D /dev/ttyUSB0 -b 115200
```

### Network Interface Configuration (10BASE-T1S)

Configure the 10BASE-T1S interface and the Physical Layer Collision Avoidance (PLCA) parameters.

*Note: The interface name (e.g., `enx9c956eb58a56`) may vary. Verify the assigned identifier using `ip a`.*

```bash
# Bring up the interface
sudo ip link set up enx9c956eb58a56

# Configure PLCA (Node ID 1, 8 Nodes, Timer 0x20)
sudo ethtool \
  --set-plca-cfg enx9c956eb58a56 enable on \
  node-id 1 \
  node-cnt 8 \
  to-tmr 0x20 \
  burst-cnt 0x0 \
  burst-tmr 0x80
```

### Running the Gateway

The Gateway binary accepts the network interfaces as arguments. The C++ code automatically detects whether an interface is CAN or Ethernet based on the `can` string prefix.

**Scenario A: Raspberry Pi Topology (Virtual CAN + USB 10BASE-T1S)**
In this environment, CAN traffic is simulated via a virtual interface (`vcan0`) and the 10BASE-T1S network is attached via a USB adapter (`eth1`).
```bash
# Run with root privileges to allow SCHED_FIFO real-time priority
sudo ./gateway vcan0 eth1
```

**Scenario B: VisionFive 2 Topology (Physical CAN + Native/USB 10BASE-T1S)**
In the original environment, a physical CAN interface (`can0` or `can1`) is bridged to the 10BASE-T1S interface (e.g., `enx9c956eb58a56`).
```bash
sudo ./gateway can0 enx9c956eb58a56
```

-----

## 4\. Test Methodology & Time Synchronization

The evaluation methodology supports different network topologies depending on available hardware. The End-to-End network traverses the 10BASE-T1S bus, while out-of-band standard Gigabit Ethernet is used to synchronize the system clocks.

### Metrics

The Soft-Real-Time performance is evaluated using two main statistics:

1.  **Gateway Latency:** The internal processing time from CAN frame arrival to IEEE 1722 packing. Measures pure C++ performance.
2.  **End-to-End Latency:** The total time from the Gateway CAN ingress to the listener Ethernet reception. Measures full network stack and USB/physical bus overhead.

### Time Synchronization (gPTP)

To analyze End-to-End latency consistently, the Gateway nodes must share a common time domain.

  * **Mechanism:** IEEE 802.1AS (gPTP) via `linuxptp`.
  * **Implementation:** The program uses `CLOCK_REALTIME` (System Clock) to ensure timestamps align with the PTP-synchronized time.

**Topology A: 2x Raspberry Pi (Hardware Timestamping)**
  * **Master:** Node 0 (Sender) | **Slave:** Node 1 (Receiver)
  * **Advantage:** Raspberry Pi Gigabit interfaces support **Hardware Timestamping**, allowing for sub-microsecond precision with virtually zero jitter.
  * **Command:** `sudo ptp4l -i eth0 -m -2` (Master) / `sudo ptp4l -i eth0 -m -2 -s` (Slave).

**Topology B: VisionFive 2 & x86 PC (Software Timestamping)**
  * **Master:** Ubuntu x86 Peer | **Slave:** VisionFive 2 Gateway (Running cross-compiled static `ptp4l`).
  * **Constraint:** Due to hardware constraints on certain interfaces, **Software Timestamping** (`-S`) may be required. A jitter floor of approximately **50µs** is expected and must be accounted for in the analysis.
  * **Command:** `sudo ptp4l -i enp2s0 -S -m -2`

### Automated Experiment Scripts (Raspberry Pi)

For the Raspberry Pi topology, two interactive bash scripts (`run_node0.sh` and `run_node1.sh`) are provided in the root directory to automate the entire experiment lifecycle, including network setup, PTP synchronization, Gateway execution, and CAN simulation.

1.  **Run Receiver (Node 1):**
    ```bash
    chmod +x run_node1.sh
    ./run_node1.sh
    ```
2.  **Run Sender (Node 0):**
    ```bash
    chmod +x run_node0.sh
    ./run_node0.sh
    ```
    *Note: Do NOT press ENTER to start the CAN simulator on Node 0 until PTP is fully synchronized on Node 1.*

### Verifying PTP Synchronization (Golden Lock)

Before starting the CAN simulation, it is critical to ensure that the PTP hardware clocks have stabilized and the Linux system clock (`CLOCK_REALTIME`) is perfectly locked to the PHC. Failure to do so will result in artificial "clock step" artifacts in the End-to-End latency results.

On Node 1 (Receiver), monitor the `phc2sys` logs:
```bash
tail -f /tmp/phc2sys.log
```
**What to look for:**
*   Wait for the `offset` value to drop and stabilize.
*   A "Golden Lock" is achieved when the `offset` consistently oscillates between **-50 and +50 nanoseconds** (sub-microsecond precision).
*   Once this state is reached, it is safe to start the CAN simulator on Node 0.

-----

```bash
# -i <interface> | -S (Software Timestamping) | -m (Print messages) | -2 (Layer 2)
sudo ptp4l -i enp2s0 -S -m -2
```

-----

## 5\. Results

*Comparison: Performance with vs. without Priority Real-Time Scheduling.*

