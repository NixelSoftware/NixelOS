
# Todo
- [ ] **someone** - Truncate filenames until clicked on the desktop
- [X] **zainabm** - Implement ACPI for reboots
- [x] **zainabm** - Implement proper ACPI
- [ ] **zainabm** - Add telnet server
- [x] **zainabm** - Implement memory protection
- [ ] **elyas-code** - Abstractify browser functions
- [x] **elyas-code** - Abstractify kernel tasks
- [ ] **elyas-code** - Create package manager
- [ ] **elyas-code** - Attempt to implement SSL (ong bro imma kms)

## Prerequisites

Install the required build toolchain (`gcc`, `make`, `ld`), `nasm`, and `qemu` using your distribution's package manager:

### Debian / Ubuntu / Linux Mint
```bash
sudo apt update
sudo apt install -y build-essential nasm qemu-system-x86
```

### Arch Linux / Manjaro
```bash
sudo pacman -S --needed base-devel nasm qemu-system-x86
```

### Fedora / RHEL
```bash
sudo dnf install -y @development-tools nasm qemu-system-x86
```

### openSUSE (Tumbleweed / Leap)
```bash
sudo zypper install -t pattern devel_basis
sudo zypper install -y nasm qemu-x86
```

### Alpine Linux
```bash
sudo apk add build-base nasm qemu-system-x86_64
```

### Void Linux
```bash
sudo xbps-install -S base-devel nasm qemu-system-x86_64
```

### Windows
Install [MSYS2](https://www.msys2.org/) and Download [i686-elf-tools](https://github.com/lordmilko/i686-elf-tools).
In MINGW2, Install MAKE
```cmd
pacman -S mingw-w64-x86_64-make
```

Add both to your path

# Building
Install GNU Make, GCC, NASM, binutils, Python, and QEMU, then run `make` to build
`os.img` or `make run` to build and start it in QEMU. Set
`DEBUG_SUBSYSTEMS=1` to enable verbose subsystem traces. Use `make clean` to
remove generated build artifacts.

`make run` automatically prefers KVM hardware acceleration on Linux/x86 when
`/dev/kvm` is readable and writable, with TCG software emulation as a fallback.
Other hosts use TCG. To require KVM, run `make run QEMU_ACCEL=kvm`; to force
software emulation for debugging, use `make run QEMU_ACCEL=tcg`.
The kernel currently uses one CPU, so increasing QEMU's CPU count does not
distribute guest work. RAM remains configurable with `QEMU_MEMORY=256M` if an
application needs more than the default 128 MiB.

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