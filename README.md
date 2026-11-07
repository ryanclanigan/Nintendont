Download the `.zip` from the [latest release](https://github.com/project-slippi/Nintendont/releases/latest)

## Nintendont Slippi
This is a fork of [FIX94/Nintendont](https://github.com/FIX94/Nintendont) specifically
used for supporting Project Slippi.

## Users
See [README.md](https://github.com/FIX94/Nintendont/blob/master/README.md) for standard Nintendont if you're new to Nintendont.

### Replays
See documentation in [SLIPPI.md](SLIPPI.md) for more information.

### Kadano's Wii softmodding guide
I recommend using [Kadano's guide](https://docs.google.com/document/d/1iaPI7Mb5fCzsLLLuEeQuR9-BeR8AOwvHyU-FM8GKmEs) if you're new to Wii homebrew. Many guides in the wild are out of date or weren't very good to begin with.

### Installation Summary
1. Download the `.zip` from the [latest release](https://github.com/project-slippi/Nintendont/releases/latest). Unzip to the root of your SD card such that `boot.dol`, `meta.xml`, and `icon.png` all end up under `/apps/Nintendont Melee`.
2. Copy your vanilla Melee (NTSC 1.02) ISO (as well as any special versions like [Training Mode](https://www.patreon.com/UnclePunch)) to `/games/`.
3. Combine with [Priiloader](http://wiibrew.org/wiki/Priiloader) and [Nintendont Forwarder for Priiloader](https://github.com/jmlee337/Nintendont-Forwarder-for-Priiloader) to autoboot from power-on to Nintendont.
4. Turn on autoboot in Nintendont settings to autoboot all the way to Melee.

### Automatic ASIX Ethernet

With **Slippi Networking** enabled, Nintendont can automatically select its
built-in USB 2.0 ASIX driver on an original Wii running IOS58. No additional
menu toggle is needed. This is experimental family coverage, not a guarantee
for every retail adapter or board configuration.

| Chip family | Driver path | Physical Wii validation |
| --- | --- | --- |
| AX88172 | Raw Ethernet, board-specific GPIO reset | Needed |
| AX88172A | Legacy framing, internal/external PHY selection | Needed |
| AX88772 / AX88772A | Legacy framing, chipcode-specific reset | Needed |
| AX88760 Ethernet function | AX88772A-compatible identity/protocol | Needed |
| AX88772B / AX88772C | Legacy header mode, software checksums | Needed |
| AX88178 | Legacy framing, Marvell or RTL8211CL external PHY | Needed |
| AX88178A | USB 2.0 gigabit register/framing path | Needed |
| AX88772D / AX88772E | Existing revision-0300 register/framing path | One revision-0300 adapter verified; D vs E is not distinguishable by these IDs |

Detection uses the explicit IDs in [device_list.h](kernel/asix/device_list.h),
including known legacy OEM IDs, and validates the USB device descriptor.
AX88772D/E requires **0b95:1790, bcdDevice 0300**. AX88178A requires
**0b95:178a, bcdDevice 0100**. The legacy AX88772 chipcode selects original,
A or B/C initialization even when the advertised model name differs.
Bulk endpoints are discovered from one interface/alternate setting rather than
assuming all adapters have the same endpoints.

USB 3.0 chipsets (AX88179/A/B and AX88279 variants) are excluded, including when
connected at USB 2.0 speed. Unknown OEM IDs are left to IOS rather than guessed.
Special MII/RMII bridge configurations, custom external-PHY boards, and AX88178
PHY modes other than Marvell/RTL8211CL are not implemented. AX88772B/C currently
requires the standard internal Ethernet MAC mode. Gigabit families negotiate
10/100/1000 links, but this does not make the Wii's USB bus run at gigabit speed.

With **Slippi Networking = Yes**, recognized USB 2.0 ASIX adapters now use
**DHCP automatically** when no slippi_asix.ini exists. The router (or laptop
internet-sharing DHCP server) supplies the IP, subnet mask, gateway and DNS.
There is no additional ASIX toggle. With networking off, the driver is inactive.
Unrecognized adapters use existing IOS networking; that fallback does not imply
IOS supports every otherwise unrecognized device.

Connect the adapter and cable before launching Melee. Hotplug and runtime link
renegotiation are not supported. DHCP renewal and rebinding do continue while
the game runs; an expired address is cleared while the client tries again.
Address changes or a lost router can interrupt an existing mirroring connection.

The driver and lwIP stack are compiled into **boot.dol**. There are no separate
ASIX library, driver, or firmware files to install, and no ISO patch or
additional IOS installation is needed for this feature.

1. Install a build of this repository containing ASIX DHCP support. Copy its app
   folder into the Homebrew Channel's apps directory on your SD card.
2. For automatic setup, leave **slippi_asix.ini absent**, or use a file containing
   **dhcp=1**. If upgrading from a static setup, rename/remove the old file or
   replace it with the DHCP example. The location is the **root of the device
   holding Melee**: SD:/slippi_asix.ini for an SD game, USB:/slippi_asix.ini for a
   USB game. CI release packages include
   [slippi_asix.ini.example](kernel/asix/slippi_asix.ini.example) at their root.
3. Connect Ethernet to a router with DHCP enabled (or a laptop actively sharing
   internet with a DHCP server). Set **Slippi Networking = Yes** and launch Melee.
4. Use Slippi console discovery to connect. The assigned IP can change; read
   **address=** in slippi_asix_boot.log on the game device to find it for ping.
   Mirroring uses TCP **51441**, with discovery broadcasts on UDP **20582**;
   the laptop and Wii must be on the same subnet for discovery.

Startup DHCP requests ask the server to broadcast its offer/ACK until the Wii
has an address, for servers that cannot unicast to an unconfigured client.
Lease renewals retain normal unicast behavior.

DHCP waits up to 30 seconds after the link is ready, including retries and
address-conflict checks. A timeout reports **No router IP** on the TV and writes
diagnostics; it does not guess a static address. Link failure, unsupported
hardware, or a disabled DHCP server still needs to be fixed.

Static configuration remains available. Existing files keep their behavior:

~~~ini
ip=192.168.137.250
netmask=255.255.255.0
gateway=192.168.137.1
~~~

Use an unused IP and the actual subnet. IP and netmask are required in static
mode; gateway is optional for local mirroring. An optional dns= address defaults
to gateway. A static file may include dhcp=0. Do not combine dhcp=1 with static
IP/netmask/gateway/DNS fields. Invalid or unreadable files report an error.
The custom stack uses DHCP or this file rather than the Wii System Menu profile;
the native IOS path keeps its existing network settings.

Startup writes **slippi_asix_boot.log**, **slippi_asix_rx.bin** and
**slippi_asix_tx.bin** to the game device root. A bounded
**slippi_asix_rx_trace.bin** additionally records up to 32 KiB of incoming raw
USB groups, each prefixed with its little-endian u32 length. This preserves
replies arriving after the first packet; trace overflow is counted in the log.
The log records the assigned IP,
DHCP state, DNS, family, revision, endpoints and I/O results. Captures may be
empty when initialization fails before traffic. DHCP and the additional adapter
families still need physical Wii validation in this build.

## Developers
See [README.md](https://github.com/FIX94/Nintendont/blob/master/README.md) for standard Nintendont if you're new to Nintendont.

### Build Requirements
You will need to install part of the devkitPro toolchain to build Nintendont. Visit [the getting started page](https://devkitpro.org/wiki/Getting_Started) and find the instructions for your system. I recommend installing GBA, Wii, and GC packages. GBA is required to get `devkitARM` which isn't provided in the others. You effectively need `devkitARM` `devkitPPC` and `libogc`.

### ASIX build dependencies

ASIX support uses this repository's driver in kernel/asix and **lwIP 2.2.1**,
whose source is already included under kernel/asix/lwip. No external ASIX SDK,
prebuilt ASIX library, or separate lwIP installation is required. The kernel
Makefile compiles and links these sources into the ARM kernel, which the loader
embeds in boot.dol. The new sources compile as GNU C11, while the existing kernel
continues to use GNU89. The devkitARM/devkitPPC/libogc requirements above still
apply; use the Docker image below for the toolchain used by CI.

The vendored lwIP source is pinned to commit
77dcd25a72509eb83f72b033d219b1d40cd8eb95 (STABLE-2_2_1_RELEASE).
Its BSD license is included in [kernel/asix/lwip/COPYING](kernel/asix/lwip/COPYING).
Preserve that notice when distributing builds or source.

The USB register sequences and legacy identity/GPIO mappings are adapted from
[Linux v6.18 ASIX sources](https://github.com/torvalds/linux/tree/v6.18/drivers/net/usb)
and the [official ASIX driver](https://github.com/ASIXElectronics/asix-usb-nic-linux-driver)
at 42feb1252fe0669845a8d6714bcc77bfc19eea1d. Two additional OEM identities come from
[ASIX legacy driver](https://github.com/ASIXElectronics/ax88772c-772b-772a-linux-driver)
at e87b8576e6fa3accd373cff326850ab0a216df36. Attribution is retained in the source.
No Linux module, firmware download, or EEPROM programming is used on the Wii.

The host tests exercise the production driver initialization against simulated
IOS for every implemented family, endpoint selection, MII ownership/link failures,
USB 3.0 exclusion, all legacy header/payload split positions, malformed aggregates,
and TX padding at USB packet boundaries. Full DHCP startup tests cover no-file
auto configuration, dropped discovers/retries, wrong transaction IDs, NAK and
address-conflict recovery, IP/router/DNS, renewal, rebinding, expiry and timeout.
The socket tests use real lwIP with
a simulated network peer (250,000 replay bytes, loss/backpressure, discovery,
reconnect and FIN):

~~~sh
sh kernel/asix/tests/run.sh
~~~

On Windows with a host GCC compiler:

~~~powershell
./kernel/asix/tests/run.ps1 -Compiler gcc
~~~

TinyCC users should also pass -CompilerInclude pointing to TinyCC's include
directory. Host tests do not replace testing USB communication and mirroring
on a physical Wii.

### Building with Docker
We maintain a docker image for building on CI which can also be used for building locally without the need to install devkitPro. Simply run `docker run --volume=${PWD}:/work --workdir=/work nikhilnarayana/devkitpro-slippi:7 make` from the root repo directory.

### Building on Windows
To build on windows, you need to run the `Incremental Build.bat` file in the Nintendont root.

### Troubleshooting
libwinpthread-1.dll error:
```
make -C multidol
make[1]: Entering directory '../Nintendont/multidol'
 ASSEMBLE    crt0.S
 COMPILE     cache.c
 COMPILE     main.c
 COMPILE     global.c
 COMPILE     apploader.c
 COMPILE     dip.c
 COMPILE     utils.c
 LINK        multidol_ldr.elf
collect2.exe: error: ld returned 53 exit status
make[1]: *** [Makefile:44: multidol_ldr.elf] Error 1
make[1]: Leaving directory '../Nintendont/multidol'
make: *** [Makefile:34: multidol] Error 2
```
For some reason the devkitPro installer might not install things correctly. In order to fix the above error, you will need to go to your `devkitPPC/bin` directory, grab the file called `libwinpthread-1.dll`, and copy it to `devkitPPC/powerpc-eabi/bin/`, this should fix the issue.
