
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
