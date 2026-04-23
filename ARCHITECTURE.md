# 🏗️ Architecture & Design Document: Q-Shift
**Version:** 1.1.0 | **Date:** 2026-04-23 | **Author:** Sujay

---

## 1. Executive Summary
This document outlines the architecture for Q-Shift, a sovereign, stateless, Ring-0 network middleware designed to defeat "Harvest Now, Decrypt Later" (HNDL) attacks. Operating entirely at the Network Interface Card (NIC) driver level, Q-Shift intercepts classical TLS 1.3 handshakes in real-time and injects NIST-standardized Post-Quantum Cryptography (ML-KEM-768). The primary objective is to instantly upgrade unpatchable, legacy enterprise infrastructures to quantum-resistant standards via live-patching, requiring zero application-level modifications, zero architectural rewrites, and zero server downtime.

## 2. Architectural Drivers
*What forces shaped this architecture?*
* **Primary Goals:** Line-rate packet processing, zero-downtime Post-Quantum upgrade paths, and absolute data sovereignty.
* **Technical Constraints:** Must operate below the Linux network stack (before `sk_buff` allocation). Must bypass strict eBPF verifier memory limits to successfully inject a massive 1,184-byte payload.
* **Non-Functional Requirements (NFRs):** * **Security/Privacy:** Pure intercept model. Zero payload decryption, zero persistent disk storage, and zero cloud dependency for cryptographic execution.
    * **Reliability:** Must handle unpredictable TCP states (packet drops, retransmissions) gracefully within a fundamentally stateless layer.
    * **Performance:** Sub-millisecond latency. Hardware API network calls must never bottleneck active packet processing.

## 3. System Architecture (The 10,000-Foot View)
Q-Shift is divided into a hyper-optimized kernel-space filter and an asynchronous user-space cryptographic forge.

* **Ring-0 Filter (Data Plane):** Built with eBPF/XDP. It mathematically parses incoming Ethernet/IPv4/TCP headers at the bare metal. It houses an LRU Hash Map to track TCP Sequence states and executes microsecond drop/pass/redirect verdicts.
* **Memory Bridge (Transport Plane):** Utilizes `AF_XDP` Shared Memory (UMEM) rings. This bridges the kernel and user-space, allowing zero-copy memory manipulation to bypass kernel expansion limits.
* **Domain Layer (User-Space C Daemon):** The core intelligence. It houses the Type-Length-Value (TLV) dynamic pointer jumper to parse variable TLS 1.3 packets, the dynamic MTU TCP segmenter, and the RFC 1071 checksum recalculation engine.
* **Hardware Abstraction Layer (Entropy Broker):** A decoupled background daemon that interfaces with external APIs (Cisco Outshift QRNG) to pipe true quantum noise into local RAM (`/dev/shm`), abstracting internet latency away from the primary data plane.

## 4. Design Decisions & Trade-Offs (The "Why")
* **Decision 1: AF_XDP Memory Bridge over In-Kernel Packet Expansion**
    * *Rationale:* The eBPF kernel verifier aggressively restricts memory expansion to prevent infinite loops and kernel panics. Expanding a packet by 1,184 bytes using `bpf_xdp_adjust_tail` physically fails on standard NIC drivers.
    * *Trade-off:* Forces a context switch to User-Space. However, `AF_XDP` mitigates this via zero-copy DMA memory mapping, allowing infinite payload manipulation safety while preserving enterprise-grade throughput.
* **Decision 2: Decoupled Asynchronous Entropy Broker**
    * *Rationale:* Standard `libcurl` API calls take 50-150ms. Executing synchronous API calls inside a live packet-processing loop would cause catastrophic TCP timeouts and network gridlock.
    * *Trade-off:* Increases deployment complexity by requiring two separate daemons and Inter-Process Communication (IPC). But, it guarantees nanosecond hardware entropy reads from RAM and enables a seamless "Fail-Open" to local PRNG if the external API goes offline.
* **Decision 3: Manual TCP Segmentation over IP Fragmentation**
    * *Rationale:* Injecting the ML-KEM key forces the packet past the standard 1500-byte MTU. Relying on native IP Fragmentation is dangerous, as modern enterprise firewalls and middleboxes frequently drop fragmented IP packets outright.
    * *Trade-off:* Requires massive algorithmic complexity to manually recalculate TCP Sequence/Acknowledgment offsets and rebuild TCP headers for the "Vanguard" and "Remnant" packets. However, it ensures 100% reliable packet forwarding across any NAT or router.
* **Decision 4: Stateful LRU Hash Map in a Stateless Environment**
    * *Rationale:* XDP has no concept of "connections." If a forged quantum packet is dropped downstream, the client retransmits. If Q-Shift forges a *new* key for the retransmission, the server drops the handshake.
    * *Trade-off:* Consumes a small footprint of protected kernel memory. However, utilizing a Least Recently Used (LRU) map provides autonomous garbage collection. The kernel instantly recognizes the retransmission and fires the saved payload back via the Fast-Path without ever waking the User-Space daemon.

## 5. Data Flow & Lifecycle
1. **Ingestion (Asynchronous):** The Entropy Broker fetches live quantum noise from Cisco and streams it into the `/dev/shm/qshift_entropy.bin` RAM pipe.
2. **Intercept & Fast-Path Check:** The eBPF hook parses an incoming packet. If it's a TCP retransmission, it pulls the forged payload from the LRU Map and executes `XDP_TX`. If it is a new ClientHello, it redirects to `AF_XDP`.
3. **Dynamic Parsing:** The User-Space TLV parser maps the TLS extensions, vaulting over SNIs to lock onto the exact byte offset of `key_share`.
4. **The Quantum Forge:** The C daemon ingests the pre-fetched entropy, feeds it to `liboqs`, and generates the FIPS 203 ML-KEM-768 public key.
5. **Segmentation & Re-entry:** The payload is injected. MTU is queried. If limits are breached, the packet is cleaved. RFC 1071 checksums are mathematically forged, tagged with `0x7777`, and the Vanguard/Remnant packets are slammed into the TX Ring.

## 6. Security & Privacy Threat Model
* **Data at Rest:** Absolute zero footprint. Payload telemetry is never stored. The eBPF LRU Map is highly volatile and purged dynamically.
* **Data in Transit:** Upgraded via hybrid classical/post-quantum encapsulation (X25519 + ML-KEM-768), ensuring forward secrecy against both current and future cryptographic adversaries.
* **Mitigated Risks:** * *Cloud Lock-In:* Licensed under AGPLv3, legally preventing cloud providers from absorbing the architecture into proprietary, closed-source firewalls.
    * *Verifier Panics:* Utilizes strict bounds checking (`dst + 1 > data_end`) to mathematically prove memory safety to the Linux Kernel.

## 7. Future Architecture Roadmap
* **QUIC / UDP Datagram Support:** Currently, the architecture parses TCP streams. The next evolution involves refactoring the eBPF hook to parse UDP headers and applying Datagram segmentation limits to support HTTP/3 QUIC traffic.
* **Hardware Offloading (SmartNICs):** Transitioning the BPF bytecode execution from the host CPU directly to the network hardware via Data Processing Units (DPUs) like the NVIDIA BlueField, achieving zero-CPU-overhead quantum cryptography.
