# Networking (Chimera-Net)

Darwin BSD-derived network stack with Intel e1000 Gigabit driver and full TCP/IP support.

## Mbuf Memory Model
Packets are managed using Darwin-style `mbuf_t` structures:
- `m_get(type)`: Allocates a basic mbuf header for control data or small payloads.
- `m_getcl(type)`: Allocates an mbuf with a 2048-byte contiguous cluster buffer for MTU 1500 Ethernet frames.
- `m_freem(m)`: Recursively frees mbuf chains and decrements cluster page reference counts.
- `m_pkthdr`: Top-level packet header containing overall packet length and the receiving interface pointer (`rcvif`).

## Network Interface Layer (`ifnet`)
- `if_attach(ifp)`: Registers a network interface into the global kernel interface list.
- `if_input(ifp, m)`: Demultiplexes incoming frames based on EtherType (`ETHERTYPE_IP`, `ETHERTYPE_ARP`).
- `if_output(ifp, m, dst, ethertype)`: Resolves destination hardware MAC via ARP, prepends Ethernet header, and enqueues to driver transmission ring.

Configured interfaces:
- `lo0`: `127.0.0.1` IPv4 loopback interface.
- `en0`: Intel 82540EM/82545EM PCI Gigabit Ethernet adapter.

## Intel e1000 NIC Driver
- Discovered via PCI bus enumeration; BAR0 MMIO registers mapped to kernel higher-half virtual address space.
- Transmit (TX) and Receive (RX) DMA descriptor rings allocated in contiguous physical memory pages via PMM.
- Ring physical base addresses configured in `REG_RDBAL` / `REG_TDBAL`.
- Hardware interrupts masked via `REG_IMC` to ensure deterministic polling and scheduler-driven servicing.

## TCP/IP Protocol Stack
- **ARP**: Dynamic ARP table with broadcast request generation, cache expiration, and response handling.
- **IPv4**: Header checksum calculation, routing table matching, subnet evaluation.
- **ICMP**: Echo Request processing and Echo Reply generation for `ping`.
- **UDP**: Datagram multiplexing and demultiplexing by port numbers.
- **TCP**: Full state machine (LISTEN, SYN_SENT, SYN_RECEIVED, ESTABLISHED, FIN_WAIT, CLOSE_WAIT, LAST_ACK, TIME_WAIT, CLOSED) with sliding window, sequence/acknowledgement tracking, and connection teardown.
- **DHCP**: Automatic address configuration (`DHCPDISCOVER` -> `DHCPOFFER` -> `DHCPREQUEST` -> `DHCPACK`) with static fallback (`10.0.2.15/24`).

## BSD Socket System Calls
- `socket(domain, type, protocol)`
- `bind(fd, sockaddr, addrlen)`
- `connect(fd, sockaddr, addrlen)`
- `listen(fd, backlog)`
- `accept(fd, sockaddr, &addrlen)`
- `sendto(fd, buf, len, flags, dest, addrlen)`
- `recvfrom(fd, buf, len, flags, src, &addrlen)`
- `getsockname(fd, sockaddr, &addrlen)` / `getpeername(fd, sockaddr, &addrlen)`
