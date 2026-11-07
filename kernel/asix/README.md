# Experimental AX88772D networking

Enable **Slippi Networking: Yes** and **ASIX Ethernet: Yes** in Slippi settings.
The option defaults to off. It selects a driver and lwIP stack in Nintendont's
ARM kernel; it does not install an IOS or change the Wii System Menu. No ISO
patch is needed. Leave it off to use the existing IOS networking backend.

Only USB **0b95:1790, bcdDevice 0300 (AX88772D)** is accepted. The original
AX88179 and AX88179A can share that USB ID and are deliberately rejected.
Connect the adapter and cable before starting a game. USB high speed and a
negotiated 10/100 Ethernet link are required. Bulk endpoints 82 (IN) and 03 (OUT)
are validated against the USB configuration descriptor before use.

## Static configuration

Copy slippi_asix.ini.example to **/slippi_asix.ini at the root of the device
holding the game** (SD for a game on SD, USB for a game on USB). This is separate
from the app directory and Wii System Settings.

    ip=192.168.137.250
    netmask=255.255.255.0
    gateway=192.168.137.1

These example addresses match Windows Internet Connection Sharing on the test
laptop. Choose a unique IP in the laptop/router's Ethernet subnet. Gateway is
optional for communication within that subnet and defaults to 0.0.0.0.
IP and netmask are required. Invalid, missing, oversized or duplicate settings
stop initialization rather than silently choosing an address. DHCP and address
conflict detection are not implemented in this backend yet.

The existing Slippi TCP protocol remains on port **51441**. Discovery retains
its 48-byte SLIP_READY UDP broadcast on port **20582**, using the adapter's MAC.
There is one TCP client and one UDP discovery socket; this bridge implements
only the socket operations Slippi uses. All raw lwIP calls and USB completions
are serialized across the two Slippi threads. Socket waits and idle network
cycles pump USB and lwIP. Sends queue the entire message or report failure;
receive-window credit follows application reads. Descriptor generations prevent
stale clients from accessing a reconnect.

## Validation and remaining hardware work

The earlier standalone PowerPC probe v0.9 passed a physical original-Wii test:
262144 echoed bytes verified by the laptop in 1.19 seconds. That validates the
adapter register sequence and 8 KiB USB receive strategy, not this ARM integration.
The host socket test uses this backend and real lwIP. It checks a fragmented
message prefix, 250000 replay bytes, transmit backpressure and loss recovery,
UDP discovery, reset/reconnect, FIN, and invalid static configuration rejection.

Run the host test with: sh kernel/asix/tests/run.sh
Windows: kernel/asix/tests/run.ps1 -Compiler gcc
TinyCC on Windows needs -CompilerInclude pointing at its include directory.
Build with Nintendont's supported toolchain and make. The existing GitHub CI uses
nikhilnarayana/devkitpro-slippi:7. New lwIP sources use GNU C11; the original
kernel remains GNU89. A 128 KiB lwIP heap is allocated only when enabled.

The integrated boot.dol still needs physical validation: menu navigation and
persistence, boot from SD, ping during Melee, Slippi discovery/handshake,
complete-match mirroring, reconnect, and simultaneous USB replay writing.
Hotplug/replug, link renegotiation and Wii U vWii are not supported/tested here.
USB enumeration consumes the initial VEN snapshot before the existing storage
hotplug watcher starts, without registering a competing watcher.

Network initialization error codes: -101 config, -102 mutex, -103 netif,
-104 heap; -1/-2 USB service/enumeration, -3 missing adapter, -4 resume,
-5 revision, -6 endpoints, -7 USB configuration, -8 MAC, -9 link/register
initialization, -10 completion queue. Check the config and cable first if boot
stops at network initialization. USB transfer errors stop the custom transport.

## Source and licensing

lwIP **2.2.1** is vendored at commit
77dcd25a72509eb83f72b033d219b1d40cd8eb95 from
https://github.com/lwip-tcpip/lwip (tag STABLE-2_2_1_RELEASE).
Its BSD notice is in lwip/COPYING and remains in source files.
The Starlet port is GPL-2.0-or-later, compatible with Nintendont. The register
and framing reference is https://github.com/ASIXElectronics/asix-usb-nic-linux-driver
at commit 42feb1252fe0669845a8d6714bcc77bfc19eea1d. See hardware.c for attribution.

## Diagnostic build 0.2

A three-second Starlet preflight runs after initialization and before Slippi/disc
threads start. It pumps USB/lwIP and requests the configured gateway via ARP.
It saves /slippi_asix_boot.log plus /slippi_asix_rx.bin and /slippi_asix_tx.bin
on the game device. The binary files contain the first raw USB transfer in each
direction, without a capture-container header; an empty RX file means no positive
receive completion was captured. The log records queue/submit/completion results,
buffer addresses, frame validation and input errors. File writes occur before
normal game threads begin, rather than adding concurrent FatFS calls.

This is instrumentation for the reported integrated ping/discovery failure.
It does not establish or claim a fix for that failure.

## Loader packaging fix 0.3

Build 0.2 contained a 197981-byte kernel ZIP but main.o passed the old
197143-byte length to unzip_data. Binary data generation also updates headers
with compile-time sizes, and parallel make did not order C compilation after
those header updates. The loader now explicitly waits for embedded binary
objects, and archive extraction checks every open/info/read/CRC/allocation result.
A regression check confirms an updated ZIP rebuilds main.o in a parallel build.
The corrected loader passes the full 197981-byte length. Network preflight and
diagnostic filenames remain the same as 0.2; physical boot still needs retesting.
