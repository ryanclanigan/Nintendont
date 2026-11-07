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

### Optional ASIX Ethernet

The experimental **ASIX Ethernet** setting supports the **AX88772D** adapter
with USB VID:PID **0b95:1790** and device revision **0300** on an original Wii
running IOS58. Other adapters sharing that VID:PID, including other AX88179
revisions, are not supported by this driver. Connect the adapter and Ethernet
cable before launching Melee. Hotplug and link renegotiation are not supported.

The ASIX driver and TCP/IP stack are compiled into **boot.dol**. There are no
separate ASIX library, driver, or firmware files to install on the SD card, and
no ISO patch or additional IOS installation is needed for this feature.

1. Install a build of this repository containing the **ASIX Ethernet** setting.
   Copy its app folder, including boot.dol, meta.xml and icon.png, into the
   Homebrew Channel's apps directory on your SD card.
2. Copy [slippi_asix.ini.example](kernel/asix/slippi_asix.ini.example) to the
   **root of the device holding Melee**, renamed to **slippi_asix.ini**.
   For Melee on SD, use **SD:/slippi_asix.ini**; for Melee on USB, use
   **USB:/slippi_asix.ini**. CI release packages include the example at their root.
3. Edit the file for your Ethernet subnet. Use an unused static IP address for
   the Wii. For example, with Windows Internet Connection Sharing using
   192.168.137.1 on the laptop's Ethernet interface:

   ~~~ini
   ip=192.168.137.250
   netmask=255.255.255.0
   gateway=192.168.137.1
   ~~~

   IP and netmask are required. Gateway is optional for communication within
   the same subnet and defaults to 0.0.0.0. DHCP and address-conflict detection
   are not implemented; choose an address that is not used by another device.
4. Set **Slippi Networking = Yes** and **ASIX Ethernet = Yes** in Slippi settings,
   then launch Melee. Both switches must be enabled. The ASIX option defaults
   to off; with it off, Nintendont uses its existing IOS networking backend.
5. Connect using the Slippi console mirroring client on the laptop. This uses
   TCP port **51441**, with console discovery broadcasts on UDP port **20582**.
   Both devices must be on the same Ethernet subnet for local discovery.

This backend uses slippi_asix.ini rather than the Wii System Menu's network
profile. To check connectivity, ping the configured Wii IP while Melee is
running. Startup writes **slippi_asix_boot.log**, **slippi_asix_rx.bin** and
**slippi_asix_tx.bin** to the root of the game device. If initialization fails,
record the on-screen code and inspect the boot log; packet captures can be empty
when failure occurs before network traffic starts.

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

The host tests exercise the production driver initialization against simulated
IOS and the socket backend against real lwIP with a simulated network peer:

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
