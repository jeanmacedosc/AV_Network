
# Soft-Real-Time Priority-Aware Zonal Gateway (RISC-V)

This project implements a software-defined Zonal Gateway on the RISC-V architecture (VisionFive 2). It bridges legacy Controller Area Network (CAN) buses with high-speed 10BASE-T1S Automotive Ethernet, utilizing IEEE 1722 ACF encapsulation and priority-aware scheduling to ensure deterministic behavior for critical control signals.

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

### Prerequisites

Ensure the host machine has the necessary RISC-V toolchain installed for cross-compilation.

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

-----

## 4\. Test Methodology & Time Synchronization

Due to the lack of physical resources to replicate a full zonal architecture, the evaluation relies on a simulated peer (Ubuntu x86) connected via the 10BASE-T1S bus acting as the backbone listener.

### Metrics

The Soft-Real-Time performance is evaluated using two main statistics:

1.  **Gateway Latency:** The internal processing time from CAN frame arrival to IEEE 1722 packing.
2.  **End-to-End Latency:** The total time from the Gateway ingress to the listener (Backbone) reception.

### Time Synchronization (gPTP)

To analyze End-to-End latency consistently, the Gateway and the Backbone listener must share a common time domain.

  * **Mechanism:** IEEE 802.1AS (gPTP) via `linuxptp`.
  * **Topology:**
      * **Master:** Ubuntu x86 Peer.
      * **Slave:** VisionFive 2 Gateway (Running cross-compiled static `ptp4l`).
  * **Constraint:** Due to hardware constraints on the interface, **Software Timestamping** is used. A jitter floor of approximately **50µs** is expected and accounted for in the analysis.
  * **Implementation:** The program uses `CLOCK_REALTIME` (System Clock) instead of `CLOCK_MONOTONIC` to ensure timestamps align with the PTP-synchronized time.

**Command to initialize Master (x86 Host):**

```bash
# -i <interface> | -S (Software Timestamping) | -m (Print messages) | -2 (Layer 2)
sudo ptp4l -i enp2s0 -S -m -2
```

-----

## 5\. Results

*Comparison: Performance with vs. without Priority Real-Time Scheduling.*

