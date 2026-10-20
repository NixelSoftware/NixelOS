pmo

# Building
Install GNU Make, GCC, NASM, binutils, Python, and QEMU, then run `make` to build
`os.img` or `make run` to build and start it in QEMU. Set
`DEBUG_SUBSYSTEMS=1` to enable verbose subsystem traces. Use `make clean` to
remove generated build artifacts.
# Todo
- [ ] **zainabm** - Implement ACPI for reboots
- [x] **zainabm** - Implement proper ACPI
- [ ] **zainabm** - Add telnet server
- [ ] **zainabm** - Implement memory protection
- [ ] **elyas-code** - Abstractify browser functions
- [x] **elyas-code** - Abstractify kernel tasks
- [ ] **elyas-code** - Create package manager
- [ ] **elyas-code** - Attempt to implement SSL (ong bro imma kms)
