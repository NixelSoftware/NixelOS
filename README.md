pmo

# Architecture

Boot sector and stage-two loader sources live in `src/boot/`. The terminal emulator
lives in `src/system/graphics/terminal/`, shell command policy lives in
`src/system/shell/`, and boot sequencing lives in `src/system/init/`. Window
registration and focus stacking live in `src/system/graphics/wm/`. These system
components use interfaces from `src/kernel/`.

Hardware setup remains in the kernel. Window drawing and the input event loop
also remain in `src/kernel/main.c` and are the next boundaries to extract.

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
