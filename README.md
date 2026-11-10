# Introduction
![NixelOS Banner](http://nixelos.org/static/images/nixelos_banner_wip.png)<br>
NixelOS is a simple and light operating system made to run on older machines while having a lot of neat features that you wouldn't have on an older OS.
NixelOS is currently a work in progress but you are able to compile and use an alpha version of NixelOS with the instructions on this page

## Prerequisites

Install the required build toolchain (`gcc`, `make`, `ld`), `nasm`, and `qemu` using your distribution's package manager:

### Debian / Ubuntu / Linux Mint
```bash
sudo apt update
sudo apt install -y build-essential nasm qemu-system-x86 e2fsprogs
```

### Arch Linux / Manjaro
```bash
sudo pacman -S --needed base-devel nasm qemu-system-x86 e2fsprogs
```

### Fedora / RHEL
```bash
sudo dnf install -y @development-tools nasm qemu-system-x86 e2fsprogs
```

### openSUSE (Tumbleweed / Leap)
```bash
sudo zypper install -t pattern devel_basis
sudo zypper install -y nasm qemu-x86 e2fsprogs
```

### Alpine Linux
```bash
sudo apk add build-base nasm qemu-system-x86_64 e2fsprogs
```

### Void Linux
```bash
sudo xbps-install -S base-devel nasm qemu-system-x86_64 e2fsprogs
```

### Windows
Install [MSYS2](https://www.msys2.org/) and download [i686-elf-tools](https://github.com/lordmilko/i686-elf-tools).
In the MSYS2 shell, install GNU Make:
```sh
pacman -S mingw-w64-x86_64-make
```

Add Make and the cross-toolchain's `bin` directory to your Windows `PATH`,
along with Python 3.8+, NASM, and QEMU. The compiler must produce ELF objects
for NixelOS.

For image creation, no extra tools are needed: when `mke2fs` and `debugfs`
are unavailable, `mkext4.py` writes the ext4 filesystem directly in Python.

Then build from PowerShell or Command Prompt in the checkout:

```powershell
mingw32-make
mingw32-make run
```

Compilation uses the Windows tools. Native Windows e2fsprogs tools on `PATH`
are used when present, but they are optional. Tool choices can be overridden,
for example `mingw32-make PYTHON=python CC=i686-elf-gcc`.

# Building
Install GNU Make, GCC, NASM, binutils, Python, and QEMU (e2fsprogs is optional),
then run `make` to build `os.img` or `make run` to build and start it in QEMU.
Set `DEBUG_SUBSYSTEMS=1` to enable verbose subsystem traces. Use `make clean` to
remove generated build artifacts.

`make run` automatically prefers KVM hardware acceleration on Linux/x86 when
`/dev/kvm` is readable and writable, with TCG software emulation as a fallback.
Other hosts use TCG. To require KVM, run `make run QEMU_ACCEL=kvm`; to force
software emulation for debugging, use `make run QEMU_ACCEL=tcg`.
The kernel currently uses one CPU, so increasing QEMU's CPU count does not
distribute guest work. RAM remains configurable with `QEMU_MEMORY=256M` if an
application needs more than the default 128 MiB.

## Filesystem layout

The image uses ext4, built without root access by `mkext4.py`. It uses
`mke2fs` and `debugfs` from e2fsprogs when available, and otherwise writes the
filesystem directly, so Windows builds need no extra filesystem tools.

| Path | Contents |
| :--- | :--- |
| `/home/user` | Regular user's home: Desktop, Documents, Downloads, Pictures, Music |
| `/root` | Root's private home |
| `/usr/bin` | Application binaries; searched when launching a bare command |
| `/usr/share/nixel` | Bundled audio and images |
| `/usr/share/games/doom` | Doom WAD files |
| `/etc` | System configuration, including `quark.cfg` |
| `/var/log` | Logs, including `quark.log` |
| `/var/lib/nixel` | Persistent OS state |
| `/var/www` | Default HTTP and FTP content |
| `/tmp` | Shared temporary files with sticky permissions |

Terminals start in the signed-in user's home. `cd` (or `cd ~`) returns there;
child applications inherit the shell's working directory. Personal desktop
settings are under `~/.config/desktop`, Doom settings under `~/.config/doom`,
notes under `~/Documents`, and screenshots under `~/Pictures`.

See [filesystem implementation and limits](documentation/filesystem.md).
Run `make test-ext4 test-boot-layout` to check filesystem operations and boot
layout, and `python3 tools/test_boot.py` after building for QEMU boot checks.

# Makefile arguments

| Argument | Value | Default | Description |
| :--- | :--- | :--- | :--- |
| `DEBUG_SUBSYSTEMS` | `1` | `0` | Enable verbose subsystem traces |
| `ENABLE_OOBE` | `any` | `0` | Enable the Work In Progress OOBE |
| `QEMU_ACCEL` | `auto` or accelerator name | `auto` | Prefer accessible Linux/x86 KVM with TCG fallback, or require the specified accelerator |
| `QEMU_DISPLAY` | `any` | `sdl` | Display mode to use in QEMU |
| `QEMU_NETCARD` | `any` | `e1000` | Set the network adapter emulated in QEMU |
| `QEMU_MEMORY` | `any` | `128M` | Set the amount of RAM in QEMU |
| `QEMU_DHCP` | `any` | `10.0.1.0/24,dhcpstart=10.0.1.227` | DHCP Range for QEMU VM |
| `QEMU_EXTRA` | `any` | `null` | Extra arguments to pass to QEMU |

# Boot arguments

| Argument | Value | Default | Description |
| :--- | :--- | :--- | :--- |
| `DEBUG` | `1` | `0` | Enable serial debugging |
| `VERBOSE` | `1` | `0` | Enable on-screen boot logging |

## Supported Hardware

### Audio Devices

| Device | Controller / Architecture | Interface |
| :--- | :--- | :--- |
| **Intel High Definition Audio** | Intel ICH8 (82801H) | PCI |
| **Sound Blaster 16** | Creative SB16 DSP | ISA / Legacy |

---

### Network Interface Cards (NICs)

#### Intel (e1000 Family)
| Device Model | PCI Vendor ID | PCI Device ID |
| :--- | :--- | :--- |
| Intel 82540EM Gigabit Ethernet | `0x8086` | `0x100E` |
| Intel 82545EM Gigabit Ethernet | `0x8086` | `0x100F` |
| Intel 82543GC Gigabit Ethernet | `0x8086` | `0x1004` |
| Intel 82542 Gigabit Ethernet | `0x8086` | `0x1000` |
| Intel 82574L Gigabit Ethernet | `0x8086` | `0x10D3` |

#### AMD (PCNet Family)
| Device Model | PCI Vendor ID | PCI Device ID |
| :--- | :--- | :--- |
| AMD PCNet-PCI II (Am79C970A) | `0x1022` | `0x2000` |
| AMD PCNet-FAST III (Am79C973) | `0x1022` | `0x2001` |

#### Broadcom B44 Fast Ethernet
| Device Model | PCI Vendor ID | PCI Device ID |
| :--- | :--- | :--- |
| Broadcom BCM4401 Fast Ethernet | `0x14E4` | `0x4401` |
| Broadcom BCM4401-B0 Fast Ethernet | `0x14E4` | `0x170C` |
| Broadcom BCM4401-B1 Fast Ethernet | `0x14E4` | `0x4402` |

#### Broadcom Tigon3 / NetXtreme Gigabit *(Experimental / WIP)*
> **Note:** The `tg3` driver is currently undergoing fixes and may exhibit instability.

| Device Model | PCI Vendor ID | PCI Device ID |
| :--- | :--- | :--- |
| Broadcom BCM5721 NetXtreme Gigabit | `0x14E4` | `0x1659` |
| Broadcom BCM5722 NetXtreme Gigabit | `0x14E4` | `0x165A` |
| Broadcom BCM5751 NetXtreme Gigabit | `0x14E4` | `0x1677` |
| Broadcom BCM5754 NetXtreme Gigabit | `0x14E4` | `0x167A` |
| Broadcom BCM5755 NetXtreme Gigabit | `0x14E4` | `0x167B` |
| Broadcom BCM57760 NetXtreme Gigabit | `0x14E4` | `0x16B0` |
| Broadcom BCM57765 NetXtreme Gigabit | `0x14E4` | `0x16B4` |
| Broadcom BCM57780 Gigabit Ethernet | `0x14E4` | `0x1673` |
| Broadcom BCM57780 NetLink Gigabit | `0x14E4` | `0x1690` |
| Broadcom BCM57780 NetLink Gigabit | `0x14E4` | `0x1692` |
| Broadcom BCM57785 NetLink Gigabit | `0x14E4` | `0x16B1` |
| Broadcom BCM57786 NetLink Gigabit | `0x14E4` | `0x16B5` |
| Broadcom BCM57788 NetLink Gigabit | `0x14E4` | `0x1691` |

## Chess

Launch `chess.bin` to play White against a basic computer opponent. Click a
piece and a highlighted square to move. Includes undo and promotion choices.
See [Chess controls and engine tests](src/apps/chess/README.md).


## OS version

Run `ver`, run `fastfetch`, or open **Start → Help** to see the OS version and
build ID. Edit `VERSION` to change the release number (initially `0.1.0`).
The build generates `src/include/os_version.h` from that file and the latest
commit's committer Unix timestamp (`git log -1 --format=%ct`). Rebuilding a
commit keeps its build ID; uncommitted edits do not change it. Source archives
without Git metadata report build ID `0` (unknown). The standalone Fastfetch
build uses the same generator.

## TLS library

Nixel includes a freestanding TLS 1.2/1.3 client library with a user-space TCP
adapter. Build it with `make tls` and run interoperability checks with
`make test-tls`. See [TLS integration and platform requirements](src/system/libraries/tls/README.md).

Build a headless image with `python3 build.py -nogui` (or `make NOGUI=1`).
Use `python3 build.py` or `make NOGUI=0` to restore GUI packaging. The headless
image omits both component manifests and boots to text login and systemshell.
Graphics code remains linked into the kernel; the text console uses the
framebuffer. This option does not configure a serial-only console.
