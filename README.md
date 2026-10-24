
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
# Makefile arguments

| Argument | Value | Default | Description |
| :--- | :--- | :--- | :--- |
| `DEBUG_SUBSYSTEMS` | `1` | `0` | Enable verbose subsystem traces |
| `ENABLE_OOBE` | `any` | `0` | Enable the Work In Progress OOBE |
| `QEMU_ACCEL` | `any` | `tcg` | Set the mode of acceleration for QEMU |
| `QEMU_DISPLAY` | `any` | `sdl` | Display mode to use in QEMU |
| `QEMU_NETCARD` | `any` | `e1000` | Set the network adapter emulated in QEMU |
| `QEMU_MEMORY` | `any` | `128M` | Set the amount of RAM in QEMU |
| `QEMU_DHCP` | `any` | `10.0.1.0/24,dhcpstart=10.0.1.227` | DHCP Range for QEMU VM |
| `QEMU_EXTRA` | `any` | `null` | Extra arguments to pass to QEMU |

# Default account and sudo

The desktop and new terminals start as `user` (UID/GID 1000). Boot services
retain their privileged identity. The fixed accounts are `root`, `user`,
`system`, and `nobody`.

Use `sudo <command>` to run one command as root. Enter the default root password
`banana` at the hidden password prompt; the terminal returns to its previous
identity after the command. For example, `sudo whoami` prints `root`, and the
following `whoami` prints `user`. Press Ctrl+C to cancel the password prompt.
Only `user` and `root` may use sudo. Account creation and password changes are
not implemented yet. Kernel-mode applications still share kernel privileges;
this does not provide full application isolation.

Run the authentication and terminal password-input regression checks with:

```sh
gcc -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections -Wno-pointer-to-int-cast tools/users/sudo_test.c -o /tmp/nixel-sudo-test
/tmp/nixel-sudo-test
```
