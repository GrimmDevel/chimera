# networking (xiu-net)

darwin bsd-derived network stack with intel e1000 driver and full tcp/ip.

## mbuf memory model
network packets use darwin `mbuf_t` structures:
- `m_get(type)`: allocates basic mbuf header.
- `m_getcl(type)`: allocates mbuf with 2048-byte cluster buffer for full ethernet MTU frames.
- `m_freem(m)`: frees mbuf chain and decrements cluster refcount.
- `m_pkthdr`: packet header containing total length and incoming interface pointer `rcvif`.

## interface layer (`ifnet`)
- `if_attach(ifp)`: registers network interface into kernel table.
- `if_input(ifp, m)`: demultiplexes incoming frames by ethertype (`ETHERTYPE_IP`, `ETHERTYPE_ARP`).
- `if_output(ifp, m, dst, ethertype)`: prepends ethernet header, resolves mac via arp, and sends via driver.

configured interfaces:
- `lo0`: 127.0.0.1 loopback interface
- `en0`: intel 82540em/82545em pci gigabit ethernet

## intel e1000 nic driver
- pci BAR0 mapped via hhdm.
- dma descriptor rings (rx/tx) allocated as contiguous physical memory pages via `pmm_alloc_pages()`.
- physical addresses written to `REG_RDBAL` / `REG_TDBAL`.
- all hardware interrupts masked via `REG_IMC` to avoid unhandled irq storms during polling/scheduling.

## tcp/ip protocol stack
- **arp**: dynamic arp cache with request broadcasting and timeout expiration.
- **ipv4**: checksum calculation, routing table lookup, subnet mask evaluation.
- **icmp**: echo request parsing and ping reply generation.
- **udp**: connectionless datagram transport with port demux.
- **tcp**: rfc-compliant state machine (listen, 3-way handshake, seq/ack tracking, sliding window, fin/ack teardown).
- **dhcp**: automated boot discovery (`DHCPDISCOVER` -> `DHCPOFFER` -> `DHCPREQUEST` -> `DHCPACK`) with static qemu fallback (`10.0.2.15 / 24`).

## socket syscalls
userspace socket operations map directly to kernel `socket_t` objects in `fileproc_t`:
- `socket(domain, type, protocol)`
- `bind(fd, sockaddr, addrlen)`
- `connect(fd, sockaddr, addrlen)`
- `listen(fd, backlog)`
- `accept(fd, sockaddr, &addrlen)`
- `sendto(fd, buf, len, flags, dest, addrlen)`
- `recvfrom(fd, buf, len, flags, src, &addrlen)`
