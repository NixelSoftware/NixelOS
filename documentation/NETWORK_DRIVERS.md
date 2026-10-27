# Network Driver and Networking Guide

This document describes the network interfaces currently in the kernel, which adapters are selected at boot, and which parts of the networking stack are usable. It is based on the implementation in `src/kernel`; support here means code is present and wired up, not that every physical card or network has been tested.

## Which interface should I use?

There are two related APIs:

* A **device driver** sends and receives Ethernet frames. Driver code uses `src/kernel/drivers/net.h` conventions and is selected through `src/kernel/kernel_net.c`.
* A **kernel client or app** generally uses the protocols above the device layer. Apps receive the `os_api_t` table in `src/kernel/os_api.h`, which exposes polling, IPv4 send/receive registration, ICMP, DNS, and diagnostics. It does not expose the raw `sys_net_send` or receive-handler hooks.

For new kernel protocol code, use the Ethernet, ARP, IPv4, UDP, or TCP modules in `src/kernel/net`. For a new hardware adapter, implement the driver operations and add detection/dispatch in `kernel_net.c`. Avoid having an app directly probe PCI or access device I/O ports.

## Driver operations

The common interface is in `src/kernel/drivers/net.h`:

| Function | Meaning |
| --- | --- |
| `net_init(io_base)` | Initialize the PCnet driver. `0` asks it to find its supported PCI function; returns nonzero on success. |
| `net_is_ready()` | Report the driver's ready state. |
| `net_shutdown()` | Stop the device. |
| `net_poll()` | Process completed TX descriptors and received frames. Call regularly; this is the current RX/TX completion path. |
| `net_send(frame, length)` | Queue one Ethernet frame. Returns nonzero when accepted, zero on invalid input, unavailable device, or busy TX descriptor. |
| `net_set_receive_handler(callback)` | Install the callback invoked for each received Ethernet frame. |
| `net_get_mac_address()` | Return the MAC address packed into a `uint64_t` in network byte order (most significant byte is the first MAC octet). |
| `net_set_ip_address(ip)` / `net_get_ip_address()` | Store/read the configured IPv4 address in the driver. Routing configuration is also kept by the IPv4 layer. |
| `net_get_diagnostics(out)` / `net_reset_diagnostics()` | Read/reset driver counters and descriptor state. `out` must point to a valid `net_diagnostics_t`. |

The common maximum Ethernet frame constant is `NET_MAX_FRAME_SIZE` (1518 bytes). PCnet uses eight 1536-byte RX/TX buffers. A driver send call copies the caller's bytes before returning, so callers may reuse their frame buffer after a successful return. The receive callback receives a pointer into a ring buffer; consume or copy it during the callback because the descriptor is returned to hardware after the callback returns.

`net_receive_handler_t` has this form:

```c
typedef uint8_t (*net_receive_handler_t)(const uint8_t* frame,
                                         uint32_t length);
```

Keep this callback bounded and nonblocking. It runs from `net_poll()` and the buffer is owned by the driver. The current network stack dispatches from this callback synchronously.

## Drivers currently selected by the kernel

`sys_net_init()` scans PCI bus/device/function numbers and selects the first matching device in `kernel_net.c`.

| Adapter path | Current state |
| --- | --- |
| AMD PCnet-PCI II, PCI ID `1022:2000` | Wired to `drivers/net.c`. Initializes descriptor rings, polls RX/TX, sends frames, and reports driver diagnostics. |
| Intel e1000 family IDs listed in `kernel_net.c` | Wired to `drivers/net_e1000.c`. Uses RX/TX descriptor rings and polling, with limited diagnostics. The code checks link state in `e1000_is_ready()`. |
| AMD PCnet-FAST III, PCI ID `1022:2001` | Listed in PCI matching, but the active PCnet initializer searches only for `1022:2000`. Detection may select this device and then initialization can fail. This needs alignment before treating `2001` as supported. |
| `drivers/net_pcnet32.c` interface | Separate/older PCnet implementation. It is not the implementation selected by `kernel_net.c`; the active PCnet path is `drivers/net.c`. |

The e1000 match table currently lists `8086:100E`, `100F`, `1004`, `1000`, and `10D3`. This is a small explicit ID list, not generic support for every Intel adapter sold as an e1000. Both active paths are polled rather than interrupt driven. The kernel main loop calls the network poll function regularly.

The adapter initialization argument to `sys_net_init(uint16_t io)` is currently ignored; the selector performs PCI discovery itself. The PCnet driver can also be initialized directly with an I/O base, but normal kernel startup uses automatic discovery.

## Broadcom Tg3 bring-up

Detection still uses the original PCI table. The driver implementation
now uses the hardware's 32-byte RX descriptors, 16-byte TX descriptors,
80-byte status block, and 512-entry rings. It uses eight RX buffers and up to
seven outstanding TX buffers to stay within the kernel's static memory limit.

Polling follows DMA status producer/consumer indices. TX rejects requests when
its buffers are occupied and counts completion only after the NIC consumes a
descriptor. RX checks descriptor type, buffer index, errors and length, strips
the FCS, and reposts buffers in producer order. Link polling configures copper
MII/GMII and duplex after autonegotiation. Initialization now checks BAR access,
MAC validity, MDIO failures, PHY reset and DMA stop timeouts. `netdiag` exposes
PHY and traffic counters; serial messages start with `[TG3]` and `[DHCP]`.

The implementation remains experimental on physical hardware. Chip-specific
core reset/firmware handshakes, NVRAM MAC fallback and silicon errata are not
fully implemented. It expects firmware to have loaded a valid MAC into the
MAC registers. Host tests and successful compilation do not establish that a
physical Tg3 adapter can acquire a lease. QEMU has no Tg3 model in the tested
installation; the QEMU boot regression uses e1000.

Run the host DMA/ring tests on Linux x86 with:

```sh
cc -O2 -fno-pie -no-pie -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast tests/net_tg3_test.c -o /tmp/net_tg3_test
/tmp/net_tg3_test
```

These tests emulate DMA/MMIO memory, not physical PCI or MDIO transactions.
Register and descriptor layouts were checked against the Linux
[Tg3 definitions](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/broadcom/tg3.h)
and [driver](https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/broadcom/tg3.c).

## Kernel network wrapper

`src/kernel/kernel_net.h` exposes the active-device wrapper functions to kernel code:

* `sys_net_init`, `sys_net_is_ready`, `sys_net_shutdown`
* `sys_net_poll`, `sys_net_send`
* `sys_net_set_receive_handler`
* `sys_net_get_mac_address`, `sys_net_set_ip_address`, `sys_net_get_ip_address`
* `sys_net_get_diagnostics`

Use the wrapper instead of calling a specific driver directly so the active device is respected. `sys_net_send` accepts an Ethernet frame, not an IP payload. For ordinary app development, use the `os_api_t` network calls instead: `net_poll`, `net_is_ready`, `net_get_diagnostics`, `ipv4_send`, `ipv4_set_handler`, `ipv4_get_local_ip`, `dns_resolve`, and the ICMP helpers. See `documentation/APP_DEVELOPMENT.md` for the full app API guide.

## Protocol layers and present limitations

| Layer / module | Present implementation |
| --- | --- |
| Ethernet (`etherframe.c`) | Builds/parses Ethernet II frames and dispatches by EtherType. RX accepts frames addressed to the local MAC or broadcast. |
| ARP (`arp.c`) | Basic IPv4/ Ethernet ARP request/reply handling and a 128-entry cache. Resolution sends a request and synchronously polls while waiting for a reply. |
| IPv4 (`ipv4.c`) | Basic header/checksum validation, local/broadcast destination filtering, protocol handler dispatch, and route selection using subnet/gateway. Fragmented packets are dropped; there is no reassembly. |
| ICMP (`icmp.c`) | Echo request/reply support for ping-style use. |
| UDP (`udp.c`) | Small socket table, bind/listen/connect, send, and callback delivery. Payloads are bounded by `UDP_MAX_PAYLOAD` (1460 bytes). |
| DHCP (`dhcp.c`) | Startup discover/request flow and lease fields for address, mask, gateway, DNS, and duration. On failure, kernel startup installs a static fallback configuration. Lease renewal and full DHCP lifecycle are not implemented. |
| DNS (`dns.c`) | Synchronous hostname lookup returning IPv4 A records. It always queries `1.1.1.1`; the DNS server learned from DHCP is not used here. It has fixed packet/time limits and no cache or retry strategy. |
| TCP (`tcp.c`) | A TCP socket implementation exists with connect/listen/bind/send/disconnect interfaces. The regular kernel initialization path shown in `main.c` initializes ICMP and UDP but does not call `tcp_init()`. Treat TCP as not integrated into normal boot until that is addressed. |

These are small OS components, not a complete standards-grade stack. In particular, do not assume support for IP fragments, broad DNS record types, DNS configuration from DHCP, TCP initialization in the boot path, or interrupt-driven network processing.

## Diagnostics and debugging

`net_diagnostics_t` in `drivers/net.h` includes readiness, I/O base, register snapshots, TX/RX ring indexes, send/completion/error counters, the last RX length, and descriptor snapshots. `netdiag` prints a subset. Both active drivers populate the common counters, but hardware-specific status meanings differ and e1000 does not decode every possible transmit error cause; zero is not proof that no hardware error occurred.

### Initialization failure codes

The `Init failure` number printed by `netdiag` comes from `net_diagnostics_t.init_failure`. The shared constants are declared in `drivers/net.h` as `NET_INIT_FAILURE_*`; PCnet and e1000 use these values:

| Code | Meaning | What to check |
| --- | --- | --- |
| `0` (`NET_INIT_FAILURE_NONE`) | No initialization failure was recorded. This does not by itself prove that a link is up or traffic works. | Check `State`, link status, MAC, and RX/TX counters too. |
| `1` (`NET_INIT_FAILURE_DISCOVERY`) | Device discovery failed or a usable device BAR was not found. For PCnet, discovery did not find the expected `1022:2000` device with a usable I/O BAR. | Confirm the adapter is visible on PCI and has a usable BAR. The selector lists `1022:2001`, but the active PCnet finder currently searches only for `1022:2000`. |
| `2` (`NET_INIT_FAILURE_REGISTER_ACCESS`) | Register access failed. PCnet observed `0xFFFF` after switching to 32-bit I/O mode; e1000 observed an all-ones STATUS read. `last_error_status` is set to `0xFFFF`. | Recheck BAR decoding, PCI I/O or memory enablement, and whether the device model supports the selected access mode. |
| `3` (`NET_INIT_FAILURE_TIMEOUT`) | Hardware initialization timed out. PCnet did not observe CSR0 `IDON`; e1000's CTRL reset bit did not clear within the wait loop. `last_error_status` captures the relevant raw status snapshot. | For PCnet, check the initialization block and ring addresses/alignment/DMA addressability. For e1000, check reset behavior and the captured STATUS value. |

The e1000 driver uses the same numeric meanings: `1` for no matching/usable PCI device or BAR, `2` for an invalid register interface, and `3` when the reset bit does not clear before initialization continues. On e1000, `last_error_status` contains a raw STATUS-register snapshot (or `0xFFFF` for an all-ones read); it is not PCnet CSR0. A zero failure code means initialization completed, but link state is reported separately by `State`.

Future drivers should use the `NET_INIT_FAILURE_*` constants and reserve `NET_INIT_FAILURE_NONE` for successful initialization/no recorded init failure. If a device has additional failure conditions, map them to the closest shared category and put hardware-specific detail in its raw status/serial diagnostics; do not silently invent different meanings for codes 1–3. Update this guide if the shared meanings ever need to change.

Other fields that help with failures:

* `last_error_status` is a raw device register snapshot (`CSR0` for PCnet, `STATUS` for e1000) or the `0xFFFF` sentinel for an all-ones register read. It is not a numbered error code. Interpret it according to the selected adapter.
* `tx_attempts`, `tx_submitted`, `tx_busy`, and `tx_completed` distinguish rejected sends, ring pressure, and completions. `tx_errors` is incremented when the driver reports a transmit descriptor error; e1000 currently records completion status but does not decode a complete set of hardware TX error causes.
* `rx_frames` counts valid frames accepted by the driver (whether or not a callback is installed). `rx_invalid` counts invalid descriptors/lengths and `rx_errors` counts hardware-reported receive errors where the driver exposes them.

Useful checks while adding a driver:

1. Confirm PCI vendor/device ID and BAR decoding before enabling bus mastering or touching device registers.
2. Check `sys_net_init()` and `sys_net_is_ready()` results, then inspect the MAC and diagnostics.
3. Verify that a transmitted frame advances the TX ring and that polling observes completion.
4. Verify that RX validates descriptor status and length before calling the common handler, then returns ownership to the device.
5. Test ARP and DHCP before higher protocols. A working link alone does not show that IPv4 routing or DNS works.

The repository's `netdiag` app and kernel serial messages are the available built-in debugging aids. There is no general packet capture tool or automated hardware-driver test harness in the current tree.
