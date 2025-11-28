# RISC-V toolchain
```bash
sudo apt update
sudo apt install \
  crossbuild-essential-riscv64 \
  gcc-riscv64-linux-gnu \
  g++-riscv64-linux-gnu
```

# TCP dump to .pcap analysis:
```bash
# -r read from file; -xx full packet in hexa; -vv verbose
tcpdump -r capture.pcap -xx -vv
```