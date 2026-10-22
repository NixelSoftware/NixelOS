
# Todo
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
Install [MSYS2]((https://www.msys2.org/))
Download [i686-elf-tools](https://github.com/lordmilko/i686-elf-tools)
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
# Makefile arguments

| Argument | Value | Default | Description |
| :--- | :--- | :--- | :--- |
| `DEBUG_SUBSYSTEMS` | `1` | `0` | Enable verbose subsystem traces |
| `ENABLE_OOBE` | `1` | `0` | Enable the Work In Progress OOBE |
