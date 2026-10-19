pmo

# Architecture

Boot sector and stage-two loader sources live in `src/boot/`. The terminal emulator
lives in `src/system/graphics/terminal/`, and shell command policy lives in
`src/system/shell/`. Both use the kernel interface in `src/kernel/os_api.h`.

Boot orchestration and most window-manager/input code still live in the kernel
and are the next boundaries to extract. Kernel services stay behind
`src/kernel/os_api.h` as those modules move out.

# Building
compile on windows with build.bat
compile on linux with build.sh
# Todo
- [ ] **zainabm** - Add telnet server
- [ ] **zainabm** - Implement memory protection
- [ ] **elyas-code** - Abstractify browser functions
- [ ] **elyas-code** - Abstractify kernel tasks
- [ ] **elyas-code** - Create package manager
- [ ] **elyas-code** - Attempt to implement SSL (ong bro imma kms)
