# Q-Shift: eBPF-Driven Post-Quantum Cryptographic Live Patching

![eBPF](https://img.shields.io/badge/Kernel-eBPF%20%2F%20XDP-black?logo=linux) 
![C](https://img.shields.io/badge/Language-C-blue) 
![PQC](https://img.shields.io/badge/Crypto-NIST%20FIPS%20203%20(ML--KEM)-green) 
![Status](https://img.shields.io/badge/Status-Operational-success)

Q-Shift is a transparent, in-kernel network middleware designed to defeat "Harvest Now, Decrypt Later" (HNDL) attacks.

By leveraging Extended Berkeley Packet Filter (eBPF) at the eXpress Data Path (XDP) layer, Q-Shift intercepts classical TLS 1.3 handshakes in real-time and injects NIST-standardized Post-Quantum Cryptography (ML-KEM-768). This instantly upgrades unpatchable, legacy backend servers to quantum-resistant standards without altering a single line of their application code.

## 🌐 Core Philosophy: Sovereign & Stateless

Q-Shift is built on strict privacy-by-design principles. It operates entirely as a stateless middleware. No payloads are decrypted, no traffic is stored, and no sensitive data ever touches a cloud provider. It is designed for sovereign infrastructure environments where keeping data and economic value localized and tightly controlled is the highest priority.

## 🚨 The Problem: "Harvest Now, Decrypt Later"

Adversaries and nation-states are actively recording standard Elliptic Curve Cryptography (ECC) and RSA internet traffic today, storing it in massive data centers with the bet that future quantum computers will easily break the encryption. Upgrading enterprise fleets to Post-Quantum standards requires massive application-level rewrites, library updates, and significant downtime.

## 🛡️ The Q-Shift Architecture

Q-Shift pushes the cryptography down to the raw hardware layer (Ring-0) using a fault-tolerant, zero-downtime architecture.

### Phase 1: The Ring-0 Intercept (eBPF/XDP)
A highly optimized kernel hook parses raw Ethernet, IPv4, and TCP headers mathematically to locate TLS 1.3 ClientHello messages right at the network card's Receive (RX) ring, before the Linux kernel even allocates an `sk_buff`.

### Phase 2: The Memory Bridge (AF_XDP)
Because injecting a massive 1,184-byte ML-KEM-768 key violates standard kernel eBPF memory expansion limits, Q-Shift redirects the packet. It is instantly ripped into User-Space RAM via an `AF_XDP` Shared Memory Ring for manipulation without traditional socket overhead.

### Phase 3: The FIPS 203 Quantum Forge (liboqs)
To guarantee mathematical perfection, Q-Shift does not rely on local pseudo-randomness. The engine pre-fetches true, hardware-derived quantum entropy directly from the Cisco Outshift QRNG API. This physical quantum noise is fed into the bleeding-edge `liboqs` Open Quantum Safe engine to forge a strict, NIST FIPS 203 compliant ML-KEM-768 keypair in real-time.

### Phase 4: Dynamic MTU TCP Segmentation
The engine dynamically queries the interface's Maximum Transmission Unit (MTU). If the injected 1,184-byte key causes the packet to exceed hardware limits (e.g., standard 1500-byte Ethernet), Q-Shift executes raw TCP segmentation to cleave the payload into a standard Vanguard packet and a Remnant overflow packet.

### Phase 5: The Re-Entry (RFC 1071)
Q-Shift mathematically recalculates the incremental RFC 1071 IP and TCP checksums for the segmented packets. It tags them with a secret `0x7777` IP identification watermark to prevent broadcast storms, and slams them directly into the Transmit (TX) Ring.

---

## 🚀 Installation & Build Guide

Q-Shift requires the Open Quantum Safe library (`liboqs`) compiled directly from source to ensure FIPS 203 compliance.

### 1. Install Core Dependencies (Ubuntu/Debian):
```bash
sudo apt update
sudo apt install clang llvm libbpf-dev libelf-dev libcurl4-openssl-dev cmake gcc libssl-dev -y
```

### 2. Build & Install the Quantum Forge (liboqs):
```bash
cd ~
git clone [https://github.com/open-quantum-safe/liboqs.git](https://github.com/open-quantum-safe/liboqs.git)
cd liboqs
mkdir build && cd build
cmake -DOQS_USE_OPENSSL=1 ..
make -j4
sudo make install
sudo ldconfig  # Refresh the kernel library cache
```

### 3. Clone & Compile Q-Shift:
```bash
cd ~
git clone [https://github.com/YOUR_USERNAME/Q-Shift.git](https://github.com/YOUR_USERNAME/Q-Shift.git)
cd Q-Shift
```

### 4. Configure Cisco QRNG Authentication:
Before compiling, you must provide your Cisco Outshift API key to enable hardware entropy.

1. Open `qshift_user.c`.
2. Locate the HTTP header block.
3. Replace `YOUR_CISCO_OUTSHIFT_API_KEY_HERE` with your actual token.

### 5. Build the Shield:
```bash
make clean
make
```

---

## 🧪 Local Loopback Testing

To watch the forge work in real-time, you can simulate a client-server connection entirely on your local machine using three terminal windows.

**Terminal 1: Launch the Shield**
```bash
# Attach the eBPF hook to the loopback interface
sudo ./qshift_user -i lo
```

**Terminal 2: The Dummy Server**
```bash
# Open a raw network socket to hold port 443 open
sudo nc -lnvp 443
```

**Terminal 3: The Client Handshake**
```bash
# Fire a TLS 1.3 ClientHello packet
curl -k -v [https://127.0.0.1](https://127.0.0.1)
```

*Look at Terminal 1. You will see Q-Shift instantly intercept the packet, authenticate with Cisco, forge the FIPS 203 key, and inject it into the payload before `nc` ever sees it.*

---

## 🏗️ Production Implementation Strategy

Q-Shift is designed to act as an invisible cryptographic gateway. To implement this in a real-world enterprise environment:

* **Deploy at the Edge:** Run Q-Shift on the Edge Router, Reverse Proxy (like an Nginx/HAProxy box), or the physical Linux Gateway bridging the external internet to your internal network.
* **Target the External Interface:** Run the daemon targeting the public-facing NIC (e.g., `sudo ./qshift_user -i eth0`).

**The Result:** All incoming external traffic is mathematically upgraded to Post-Quantum standards at Ring-0. The internal legacy backend servers (which only understand classical RSA/ECC) remain completely untouched, oblivious, and secure.

> *Built for sovereign network infrastructure. Designed for zero-trust environments.*
> TheSNMC
