# 🚀 Q-Shift
> A stateless, Ring-0 eBPF network middleware for instant Post-Quantum Cryptographic live-patching.

[![License](https://img.shields.io/badge/License-TheSNMC-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20eBPF-lightgrey)]()
[![Architecture](https://img.shields.io/badge/Architecture-Ring--0%20%7C%20Stateless-success)]()

---

## 📖 Overview
The cryptographic bedrock of the internet is facing an existential threat known as "Harvest Now, Decrypt Later" (HNDL). Adversaries are actively warehousing standard RSA/ECC encrypted traffic with the certainty that Cryptographically Relevant Quantum Computers (CRQCs) will eventually shatter classical encryption. Upgrading enterprise fleets to Post-Quantum standards usually requires massive application-level rewrites, library updates, and significant downtime. 

Q-Shift is designed to solve this entirely at the hardware/OS layer. It is a transparent, in-kernel network middleware that leverages Extended Berkeley Packet Filter (eBPF) at the eXpress Data Path (XDP) layer. Q-Shift intercepts classical TLS 1.3 handshakes in real-time and injects NIST-standardized Post-Quantum Cryptography (ML-KEM-768) directly into the packets. This instantly upgrades unpatchable, legacy backend servers to quantum-resistant standards without altering a single line of their application code.

**The Core Mandate:** Q-Shift operates on strict privacy-by-design and sovereign infrastructure principles. It operates entirely statelessly, ensuring payloads are never decrypted and critical key material never relies on centralized cloud orchestration for its execution flow.

## ✨ Key Features
* **Ring-0 Fast-Path & TCP State Machine:** Utilizes an eBPF LRU (Least Recently Used) Hash Map to track TCP Sequence Numbers. If a packet drops, the kernel hook instantly catches the TCP retransmission and fires the pre-forged quantum payload back from memory in nanoseconds, bypassing User-Space entirely.
* **Dynamic TLS 1.3 Pointer Jumper:** Replaces hardcoded offsets with a strict, bounds-checked Type-Length-Value (TLV) parsing engine. It dynamically vaults over variable-length SNIs and cipher suites to lock onto the exact `key_share` extension byte offset in microseconds.
* **Decoupled Async Entropy Broker:** Completely abstracts quantum hardware API logic. A background broker pre-fetches true quantum noise from the Cisco Outshift QRNG API and pipes it into a `/dev/shm` RAM pipe, ensuring the Ring-0 packet filter is never bottlenecked by network latency.
* **FIPS 203 Quantum Forge:** Feeds true physical quantum entropy into the `liboqs` engine to generate flawless, NIST-compliant ML-KEM-768 keypairs on the fly. Gracefully fails-open to local PRNG if the hardware API drops.
* **Dynamic TCP Segmentation:** Automatically queries hardware MTU. If the 1,184-byte quantum key injection exceeds physical interface limits, the engine mathematically cleaves the payload into a standard Vanguard packet and a Remnant overflow packet, forging valid RFC 1071 checksums for both.

## 🛠️ Tech Stack
* **Language:** C / eBPF Bytecode
* **Framework:** libbpf / xdp-tools
* **Environment:** Linux Kernel (Ubuntu 24.04 / WSL2) / Clang / LLVM
* **Key Libraries/APIs:** Open Quantum Safe (`liboqs`), OpenSSL (`libcrypto`), Cisco Outshift QRNG API

## ⚙️ Architecture & Data Flow
Q-Shift operates as a multi-stage, high-velocity pipeline intercepting packets before the kernel network stack allocates an `sk_buff`.

* **Input:** The Ring-0 XDP filter parses raw Ethernet/IPv4/TCP headers. If a TLS 1.3 ClientHello is detected, it is vaulted into User-Space RAM via an `AF_XDP` Shared Memory (UMEM) bridge, bypassing strict kernel memory-expansion limits.
* **Processing:** The C daemon dynamically parses the TLS payload, reads pre-fetched hardware quantum entropy from the Async Broker, and injects a mathematically forged ML-KEM-768 key into the `key_share` extension. If the new size breaches the hardware MTU, the packet is segmented.
* **Output:** Incremental RFC 1071 checksums are recalculated, an IP identification watermark (`0x7777`) is applied, and the fortified packets are slammed directly back into the NIC's Transmit (TX) ring.

## 🔒 Privacy & Data Sovereignty
* **Data Collection:** Absolute zero. Q-Shift operates as a stateless proxy at the byte level. It never decrypts payloads, stores user data, or logs IP addresses.
* **Permissions Required:** Requires `CAP_SYS_ADMIN` and `CAP_NET_ADMIN` (Root) to bind eBPF programs to the physical Network Interface Card (NIC) and manage raw memory frames.
* **Cloud Connectivity:** Strictly offline-first architecture. The Entropy Broker fetches external quantum noise, but if isolated, the entire gateway gracefully falls back to local mathematical PRNG without dropping active network traffic. The AGPLv3 license legally prevents cloud providers from closing the source on network-level modifications.

## 🚀 Getting Started

### Prerequisites
* Linux OS (Ubuntu/Debian recommended) with Kernel 5.4+
* Root privileges for XDP attachment
* Clang, LLVM, libbpf-dev, libelf-dev, libcurl4-openssl-dev
* Compiled `liboqs` with OpenSSL hardware acceleration enabled

### Installation

1. **Clone the repository:**
   ```bash
   git clone [https://github.com/thesnmc/Q-Shift.git](https://github.com/thesnmc/Q-Shift.git)
   cd Q-Shift
   ```

2. **Build and install the Quantum Forge (liboqs):**
   *(See the detailed `ARCHITECTURE.md` for `liboqs` compilation instructions)*

3. **Configure the Cisco QRNG Entropy Broker:**
   * Open `entropy_broker.c`.
   * Replace `YOUR_CISCO_OUTSHIFT_API_KEY_HERE` with your active token.

4. **Compile the Q-Shift Gateway:**
   ```bash
   make clean
   make
   ```

5. **Ignite the Shield (Targeting Loopback for testing):**
   ```bash
   # Launch the background broker
   ./entropy_broker > /dev/null 2>&1 &

   # Attach the eBPF hook
   sudo ./qshift_user -i lo
   ```

## 🤝 Contributing
Contributions, issues, and feature requests are welcome. Feel free to check the issues page if you want to contribute to the sovereign architecture.

## 📄 License
See the LICENSE file for details.  
Built by an independent systems architect in Chennai, India.
