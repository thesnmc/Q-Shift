# Q-Shift: eBPF-Driven Post-Quantum Cryptographic Live Patching

![eBPF](https://img.shields.io/badge/Kernel-eBPF%20%2F%20XDP-black?logo=linux) ![C](https://img.shields.io/badge/Language-C-blue) ![PQC](https://img.shields.io/badge/Crypto-NIST%20FIPS%20203%20(ML--KEM)-green) ![Status](https://img.shields.io/badge/Status-Operational-success)

**Q-Shift** is a transparent, in-kernel network middleware designed to defeat "Harvest Now, Decrypt Later" (HNDL) attacks. 

By leveraging Extended Berkeley Packet Filter (eBPF) at the eXpress Data Path (XDP) layer, Q-Shift intercepts classical TLS 1.3 handshakes in real-time and injects NIST-standardized Post-Quantum Cryptography (ML-KEM-768). This instantly upgrades unpatchable, legacy backend servers to quantum-resistant standards **without altering a single line of their application code.**

---

### 🚨 The Problem: "Harvest Now, Decrypt Later"
Adversaries and nation-states are actively recording standard Elliptic Curve Cryptography (ECC) and RSA internet traffic today, storing it in massive data centers with the bet that future quantum computers will easily break the encryption. Upgrading enterprise fleets to Post-Quantum standards (like FIPS 203) requires massive application-level rewrites, library updates, and significant downtime.

### 🛡️ The Q-Shift Architecture
Q-Shift pushes the cryptography down to the raw hardware layer (Ring-0) using a fault-tolerant, zero-downtime architecture. It operates entirely offline and independently of the backend software it protects.

#### Phase 1: The Ring-0 Intercept (eBPF/XDP)
A highly optimized kernel hook parses raw Ethernet, IPv4, and TCP headers mathematically to locate TLS 1.3 `ClientHello` messages right at the network card's Receive (RX) ring, before the Linux kernel even allocates an `sk_buff`.

#### Phase 2: The Memory Bridge (AF_XDP)
Because injecting a massive 1,184-byte ML-KEM-768 key violates standard kernel eBPF memory expansion limits, Q-Shift redirects the packet. It is instantly ripped into User-Space RAM via an AF_XDP Shared Memory Ring for manipulation without traditional socket overhead.

#### Phase 3: Live Quantum Entropy (Cisco Outshift)
To maintain zero-latency packet throughput, the User-Space C daemon asynchronously pre-fetches true, hardware-derived quantum entropy from the **Cisco Outshift QRNG API**. If the API is unreachable, the engine gracefully falls back to local entropy generation without interrupting network flow.

#### Phase 4: Dynamic MTU TCP Segmentation
The engine dynamically queries the interface's Maximum Transmission Unit (MTU). If the injected 1,184-byte key causes the packet to exceed hardware limits (e.g., standard 1500-byte Ethernet), Q-Shift executes raw TCP segmentation to cleave the payload into a standard `Vanguard` packet and a `Remnant` overflow packet. It includes underflow protection for massive virtual interfaces like loopback (`lo`).

#### Phase 5: The Re-Entry (RFC 1071)
Q-Shift mathematically calculates and forges the incremental RFC 1071 IP and TCP checksums for the segmented packets. It tags them with a secret `0x7777` IP identification watermark (to prevent loopback broadcast storms) and slams them directly into the Transmit (TX) Ring.

---

### 🚀 Quick Start

**1. Install Dependencies (Ubuntu/Debian):**
```bash
sudo apt-get update
sudo apt-get install clang llvm libbpf-dev libelf-dev libcurl4-openssl-dev
```

**2. Clone and Build:**
```bash
git clone [https://github.com/YOUR_USERNAME/Q-Shift.git](https://github.com/YOUR_USERNAME/Q-Shift.git)
cd Q-Shift
make
```

**3. Execute the Shield:**
Target your physical network interface (e.g., eth0):
```bash
sudo ./qshift_user -i eth0
```

### 🧪 Testing Locally (The Loopback MTU Simulation)
To test the packet segmentation logic locally without a physical network drop, you must throttle the loopback interface to standard Ethernet limits:

```bash
# Terminal 1: Throttle MTU and start the shield
sudo ip link set dev lo mtu 1500
sudo ./qshift_user -i lo

# Terminal 2: Start a silent listener
while true; do sudo nc -l -4 0.0.0.0 443; done

# Terminal 3: Fire a TLS handshake
curl -k -4 [https://127.0.0.1](https://127.0.0.1)
```

*Built for sovereign network infrastructure. Designed for zero-trust environments.*
