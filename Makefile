.DEFAULT_GOAL := all

PYTHON ?= python3
NASM ?= nasm
CC ?= gcc
LD ?= ld
OBJCOPY ?= objcopy
QEMU ?= qemu-system-i386

# Customizable QEMU settings
# NVMe alternative: -drive file=os.img,format=raw,if=none,id=nvm0 -device nvme,serial=NIXELNVME01,drive=nvm0
QEMU_DRIVE ?= -drive file=os.img,format=raw,index=0,media=disk
QEMU_ACCEL ?= auto
QEMU_NETCARD ?= e1000
QEMU_MEMORY ?= 512M
# The browser uses RDRAND for TLS entropy; expose it in the default VM CPU.
QEMU_CPU ?= max
QEMU_DHCP ?= 192.168.100.0/24,dhcpstart=192.168.100.227
QEMU_FORWARD ?= ,hostfwd=tcp::2423-:8080,hostfwd=tcp::2121-:21,hostfwd=tcp::20000-:20000,hostfwd=tcp::2323-:2323
QEMU_DISPLAY ?= sdl,gl=off
# SDL can otherwise create an OpenGL renderer even with QEMU GL disabled.
# Use software rendering so startup also works on hosts with broken GLX.
export SDL_RENDER_DRIVER ?= software
# SDL's X11 initialization may probe GLX even for a software renderer.
# Bypass that probe on hosts where GLX context creation raises an X error.
export SDL_VIDEO_X11_FORCE_EGL ?= 1
QEMU_VGA ?= virtio
QEMU_USB ?= -device piix3-usb-uhci,id=uhci -device usb-mouse,bus=uhci.0
QEMU_AUDIO_DRIVER ?= sdl
QEMU_AUDIO_FLAGS = -audiodev $(QEMU_AUDIO_DRIVER),id=audio0 -machine pcspk-audiodev=audio0
QEMU_EXTRA ?=
QEMU_OPL ?= 1
ifeq ($(QEMU_OPL),1)
QEMU_OPL_FLAGS = -device adlib,audiodev=audio0
else
QEMU_OPL_FLAGS =
endif

NOGUI ?= 0
ENABLE_OOBE ?= 0
DEBUG_SUBSYSTEMS ?= 0
# Suppress audio HAL, driver, and audio syscall debug logs.
NO_AUDIO_DEBUG ?= 1
STARTUP_TRACE ?= 0
# 0=normal, 1=input-only runtime, 2=normal without audio, 3=idle runtime.
STARTUP_PROFILE ?= 0

ifeq ($(OS),Windows_NT)
  ifeq ($(origin PYTHON),file)
    PYTHON := python
  endif
  ifeq ($(origin CC),default)
    CC := i686-elf-gcc
  endif
  ifeq ($(origin LD),default)
    LD := i686-elf-ld
  endif
  ifeq ($(origin OBJCOPY),file)
    OBJCOPY := i686-elf-objcopy
  endif
endif

ifeq ($(QEMU_ACCEL),auto)
  QEMU_KVM_AVAILABLE := $(shell $(PYTHON) -c "import os,platform; print(int(platform.system() == 'Linux' and platform.machine().lower() in ('x86_64', 'amd64', 'i386', 'i686') and os.access('/dev/kvm', os.R_OK | os.W_OK)))")
  ifeq ($(QEMU_KVM_AVAILABLE),1)
    QEMU_ACCEL_FLAGS := -accel kvm -accel tcg
  else
    QEMU_ACCEL_FLAGS := -accel tcg
  endif
else
  QEMU_ACCEL_FLAGS := -accel $(QEMU_ACCEL)
endif

NASM_FORMAT := elf32
LD_FORMAT := elf_i386
LDFLAGS := -m $(LD_FORMAT)

ifneq ($(filter-out 0 1,$(DEBUG_SUBSYSTEMS)),)
  $(error DEBUG_SUBSYSTEMS must be 0 or 1)
endif
ifneq ($(filter-out 0 1,$(NO_AUDIO_DEBUG)),)
  $(error NO_AUDIO_DEBUG must be 0 or 1)
endif
ifneq ($(filter-out 0 1,$(STARTUP_TRACE)),)
  $(error STARTUP_TRACE must be 0 or 1)
endif
ifneq ($(filter-out 0 1 2 3,$(STARTUP_PROFILE)),)
  $(error STARTUP_PROFILE must be 0, 1, 2 or 3)
endif
ifneq ($(filter-out 0 1,$(ENABLE_OOBE)),)
  $(error ENABLE_OOBE must be 0 or 1)
endif

ifneq ($(filter-out 0 1,$(NOGUI)),)
  $(error NOGUI must be 0 or 1)
endif

ifeq ($(ENABLE_OOBE),1)
  OOBE_VAL := -DENABLE_OOBE=1
else
  OOBE_VAL :=
endif

CFLAGS := -DSTARTUP_TRACE=$(STARTUP_TRACE) -DSTARTUP_PROFILE=$(STARTUP_PROFILE) \
          -O2 -m32 -ffreestanding -fno-leading-underscore -fno-pie \
          -mno-stack-arg-probe -mno-sse -mno-sse2 -mno-mmx \
          -DDEBUG_SUBSYSTEMS=$(DEBUG_SUBSYSTEMS) -DNO_AUDIO_DEBUG=$(NO_AUDIO_DEBUG) $(OOBE_VAL) -Isrc/include
NASMFLAGS := -f $(NASM_FORMAT) -DSTARTUP_TRACE=$(STARTUP_TRACE)

KERNEL_C := \
  src/kernel/memory.c src/kernel/users.c src/kernel/startup_trace.c src/kernel/main.c src/system/shell/commands.c \
  src/system/shell/network_commands.c src/system/shell/wifi_commands.c src/system/init/main.c \
  src/system/graphics/wm/wm.c src/system/graphics/wm/presentation.c \
  src/system/graphics/widgets.c src/system/graphics/text_layout.c src/system/graphics/settings/settings.c src/system/graphics/session.c \
  src/system/services/input/input.c src/system/services/audio/system_sound.c src/system/services/audio/mixer.c src/system/loader/app_loader.c \
  src/kernel/idt.c src/kernel/paging.c src/kernel/arch_i386.c src/kernel/arch_fpu.c src/kernel/syscall.c src/system/init/splash/startscreen.c \
  src/system/init/splash/logo_data.c src/system/graphics/screenshot/screenshot.c \
  src/system/graphics/image/png.c src/kernel/drivers/display.c src/kernel/hal/video.c \
  src/kernel/drivers/display/vga.c src/kernel/drivers/display/intel_gma.c src/kernel/drivers/display/i915/i915.c src/kernel/drivers/display/qemu.c src/kernel/drivers/display/virtio_gpu.c src/kernel/drivers/display/virgl.c \
  src/kernel/drivers/pci.c src/kernel/drivers/delay.c src/kernel/net/phys/broadcom.c \
  src/kernel/drivers/ata.c src/kernel/net/amd/pcnet.c \
  src/kernel/net/intel/e1000.c src/kernel/net/broadcom/tg3.c src/kernel/net/broadcom/b44.c \
  src/kernel/net/realtek/r8169.c src/kernel/net/wireless/broadcom/b43/b43.c \
  src/kernel/net/wireless/broadcom/b43/b43_firmware.c src/kernel/net/wireless/broadcom/b43/b43_boot.c src/kernel/hal/wifi.c \
  src/kernel/net/wireless/intel/iwlegacy/iwlegacy.c src/kernel/net/wireless/intel/iwlegacy/3945.c \
  src/kernel/net/wireless/intel/iwlegacy/firmware.c src/kernel/net/wireless/intel/iwlegacy/eeprom.c \
  src/kernel/net/wireless/intel/iwlegacy/bsm.c src/kernel/net/wireless/intel/iwlegacy/rx.c \
  src/kernel/net/wireless/intel/iwlegacy/session.c src/kernel/net/wireless/intel/iwlegacy/device.c \
  src/kernel/net/wireless/intel/iwlegacy/command.c src/kernel/net/wireless/intel/iwlegacy/scan.c \
  src/kernel/net/wireless/ieee80211.c src/kernel/net/wireless/intel/iwlegacy/power.c src/kernel/net/wireless/intel/iwlegacy/tx.c src/kernel/net/wireless/intel/iwlegacy/station.c src/kernel/net/wireless/intel/iwlegacy/bss.c src/kernel/net/wireless/intel/iwlegacy/rsn.c src/kernel/net/wireless/intel/iwlegacy/pci_power.c \
  $(wildcard src/kernel/net/wireless/intel/iwlegacy/station/*.c) \
  src/kernel/drivers/serial.c src/kernel/drivers/floppy.c src/kernel/drivers/sb16_driver.c src/kernel/drivers/hda.c src/kernel/drivers/pc_speaker.c \
  src/kernel/drivers/power.c src/kernel/drivers/acpi.c \
  src/kernel/drivers/ps2.c src/kernel/hal/net.c \
  src/kernel/net/ipv4.c src/kernel/net/etherframe.c src/kernel/net/arp.c \
  src/kernel/net/tcp.c src/kernel/net/udp.c src/kernel/net/icmp.c \
  src/kernel/net/ntp.c src/kernel/net/dhcp.c src/kernel/net/dns.c src/kernel/fs.c \
  src/system/graphics/wm/cursor.c src/kernel/drivers/display/font.c \
  src/kernel/keyboard.c src/system/graphics/fonts/univers85ebo_16.c \
  src/system/graphics/wm/desktop.c src/kernel/task.c src/kernel/scheduler.c \
  src/kernel/sched_policy.c src/kernel/sched_wide.c src/kernel/mutex.c \
  src/system/graphics/dialog/dialog.c src/system/graphics/dialog/shutdown.c src/apps/notepad/notepad.c \
  src/apps/viewer/viewer.c src/system/graphics/terminal/terminal.c \
  src/kernel/drivers/rtc.c src/system/graphics/wm/desktop_wallpaper.c \
  src/system/icons/network.h src/kernel/hal/audio.c \
  src/system/icons/sound.h src/system/graphics/wm/start.c \
  src/kernel/hal/usb.c src/kernel/drivers/usb/uhci.c src/kernel/drivers/usb/ehci_handoff.c src/kernel/drivers/usb/ehci.c src/kernel/drivers/usb/mass_storage.c \
  src/kernel/login.c src/kernel/emergency.c \
  src/kernel/drivers/nvme.c src/kernel/drivers/ahci.c \
  src/system/graphics/dialog/crash_dialog.c

KERNEL_ASM := src/kernel/kernel_entry.asm src/kernel/idt_asm.asm \
              src/kernel/task_switch.asm src/kernel/gdt_asm.asm
KERNEL_OBJ := $(KERNEL_ASM:.asm=.o) $(KERNEL_C:.c=.o)
KERNEL_HEADERS := src/kernel/startup_trace.h src/kernel/task.h src/kernel/scheduler.h \
                  src/kernel/mutex.h src/kernel/sched_policy.h src/kernel/sched_wide.h \
                  src/kernel/arch_fpu.h
$(KERNEL_C:.c=.o): CFLAGS += -DNIXEL_KERNEL=1
$(KERNEL_OBJ): $(KERNEL_HEADERS)

.PHONY: FORCE_AUDIO_DEBUG_CONFIG
.audio-debug-config: FORCE_AUDIO_DEBUG_CONFIG
	$(PYTHON) -c "from pathlib import Path; p=Path('$@'); value='$(NO_AUDIO_DEBUG)\n'; p.write_text(value) if not p.exists() or p.read_text() != value else None"

src/kernel/hal/audio.o src/kernel/drivers/hda.o src/kernel/drivers/sb16_driver.o src/kernel/syscall.o: .audio-debug-config src/kernel/drivers/serial.h

STANDARD_APPS := files mines chess calc hexview floppy_debug ping netdiag http_test curl \
                 crash_debug shutdown userhello sound oobe sysdiag reboot setwall \
                 lspci demo3d irc telnetd taskmgr tunnel
APPS := $(STANDARD_APPS) doom syndig qksrv qkftp
APP_BINS := $(addsuffix .bin,$(APPS)) browser.bin fastfetch.bin

.PHONY: all run clean clean-build FORCE_VERSION test-virgl

test-virgl:
	sh src/kernel/drivers/display/tests/run_virgl_tests.sh

src/include/os_version.h: FORCE_VERSION VERSION tools/generate_version.py
	$(PYTHON) tools/generate_version.py

src/system/shell/commands.o src/system/graphics/wm/start.o: src/include/os_version.h
all: os.img

%.o: %.c Makefile
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.asm Makefile
	$(NASM) $(NASMFLAGS) $< -o $@

$(STANDARD_APPS:%=%.bin): %.bin: Makefile src/apps/%/entry.o src/apps/%/main.o
	$(LD) $(LDFLAGS) -Ttext 0xC00000 -e _start $(filter %.o,$^) -o src/apps/$*/$*.tmp
	$(OBJCOPY) -O binary --remove-section=.bss --remove-section=.reloc src/apps/$*/$*.tmp $@
	$(PYTHON) -c "from pathlib import Path; Path('src/apps/$*/$*.tmp').unlink(missing_ok=True)"

src/apps/irc/main.o: src/system/user/sys_api.h src/system/user/window_api.h

.PHONY: test-irc
test-irc:
	sh src/apps/irc/tests/run_tests.sh

src/apps/chess/piece_assets.h: src/apps/chess/embed_assets.py $(wildcard src/apps/chess/assets/*.png)
	$(PYTHON) src/apps/chess/embed_assets.py $@
src/apps/chess/main.o: src/apps/chess/piece_assets.h
src/apps/chess/main.o src/apps/chess/engine.o: src/apps/chess/engine.h
chess.bin: src/apps/chess/engine.o

src/apps/sound/mp3.o: CFLAGS += -Isrc/apps/sound/compat
src/apps/sound/mp3.o: src/apps/sound/mp3.h src/apps/sound/upstream/minimp3.h src/apps/sound/compat/string.h src/apps/sound/compat/stdlib.h
src/apps/sound/main.o: src/apps/sound/mp3.h
sound.bin: src/apps/sound/mp3.o

SYNDIG_DIR := src/apps/syndig
SYNDIG_C := \
    $(SYNDIG_DIR)/main.c \
    $(SYNDIG_DIR)/midi_file.c \
    $(SYNDIG_DIR)/instruments.c \
    $(SYNDIG_DIR)/opl.c \
    $(SYNDIG_DIR)/compat/runtime.c \
    $(SYNDIG_DIR)/compat/div64.c \
    $(SYNDIG_DIR)/upstream/buffer.c \
    $(SYNDIG_DIR)/upstream/channel.c \
    $(SYNDIG_DIR)/upstream/compressor.c \
    $(SYNDIG_DIR)/upstream/envelope.c \
    $(SYNDIG_DIR)/upstream/hertz.c \
    $(SYNDIG_DIR)/upstream/lane.c \
    $(SYNDIG_DIR)/upstream/midi.c \
    $(SYNDIG_DIR)/upstream/oscillator.c \
    $(SYNDIG_DIR)/upstream/polyphony.c
SYNDIG_OBJ := $(SYNDIG_DIR)/entry.o $(SYNDIG_C:.c=.o)
SYNDIG_HEADERS := $(wildcard $(SYNDIG_DIR)/*.h $(SYNDIG_DIR)/compat/*.h $(SYNDIG_DIR)/upstream/*.h)
$(SYNDIG_C:.c=.o): $(SYNDIG_HEADERS)
$(SYNDIG_DIR)/main.o: src/system/graphics/wm/theme.h

syndig.bin: Makefile $(SYNDIG_OBJ)
	$(LD) $(LDFLAGS) --omagic -Ttext 0xC00000 -e _start $(filter %.o,$^) -o $(SYNDIG_DIR)/syndig.tmp
	$(OBJCOPY) -O binary --remove-section=.bss --remove-section=.reloc $(SYNDIG_DIR)/syndig.tmp $@
	$(PYTHON) -c "from pathlib import Path; Path('$(SYNDIG_DIR)/syndig.tmp').unlink(missing_ok=True)"

NIXPLORER_DIR := src/apps/nixplorer
BROWSER_C := $(NIXPLORER_DIR)/browser/main.c \
             $(NIXPLORER_DIR)/engine/net/http.c \
             $(NIXPLORER_DIR)/engine/net/response.c \
             $(NIXPLORER_DIR)/engine/net/url.c \
             $(NIXPLORER_DIR)/engine/platform/tls.c \
             $(NIXPLORER_DIR)/engine/platform/tls_heap.c \
             $(NIXPLORER_DIR)/engine/platform/tcp.c \
             $(NIXPLORER_DIR)/engine/parser/html.c \
             $(NIXPLORER_DIR)/engine/font/font.c \
             $(NIXPLORER_DIR)/engine/dom/document.c \
             $(NIXPLORER_DIR)/engine/style/style.c \
             $(NIXPLORER_DIR)/engine/style/selectors.c \
             $(NIXPLORER_DIR)/engine/layout/box.c \
             $(NIXPLORER_DIR)/engine/layout/flex.c \
             $(NIXPLORER_DIR)/engine/layout/grid.c \
             $(NIXPLORER_DIR)/engine/parser/css_grid.c \
             $(NIXPLORER_DIR)/engine/parser/css_color.c
BROWSER_OBJ := $(NIXPLORER_DIR)/engine/platform/trust.o $(NIXPLORER_DIR)/browser/entry.o $(BROWSER_C:.c=.o)
BROWSER_HEADERS := $(wildcard $(NIXPLORER_DIR)/engine/*/*.h)
$(BROWSER_C:.c=.o): $(BROWSER_HEADERS)
$(NIXPLORER_DIR)/engine/parser/html.o: $(NIXPLORER_DIR)/engine/parser/entities.inc
$(NIXPLORER_DIR)/engine/parser/css_color.o: $(NIXPLORER_DIR)/engine/parser/colors.inc
src/system/graphics/image/png.o: $(NIXPLORER_DIR)/engine/paint/png.c $(NIXPLORER_DIR)/engine/paint/png.h

$(BROWSER_C:.c=.o): src/system/libraries/tls/tls.h src/system/user/sys_api.h
$(NIXPLORER_DIR)/engine/platform/trust.o: $(NIXPLORER_DIR)/engine/platform/trust.S $(NIXPLORER_DIR)/engine/platform/certificates/ca-bundle.pem
	$(CC) $(CFLAGS) -c $< -o $@

browser.bin: Makefile $(NIXPLORER_DIR)/browser/app.ld $(BROWSER_OBJ) src/system/libraries/tls/libnixeltls.a
	$(LD) $(LDFLAGS) --omagic -T $(NIXPLORER_DIR)/browser/app.ld $(filter %.o,$^) src/system/libraries/tls/libnixeltls.a -o $(NIXPLORER_DIR)/browser/browser.tmp
	$(OBJCOPY) -O binary --remove-section=.bss --remove-section=.reloc $(NIXPLORER_DIR)/browser/browser.tmp $@
	$(PYTHON) -c "from pathlib import Path; Path('$(NIXPLORER_DIR)/browser/browser.tmp').unlink(missing_ok=True)"

# QuarkServe
QUARK_DIR := src/apps/qksrv
QUARK_C := \
    $(QUARK_DIR)/main.c \
    $(QUARK_DIR)/config.c \
    $(QUARK_DIR)/requestbuilder.c \
    $(QUARK_DIR)/404.c \
    $(QUARK_DIR)/logger.c \
    $(QUARK_DIR)/dirlisting.c
QUARK_OBJ := $(QUARK_DIR)/entry.o $(QUARK_C:.c=.o)
$(QUARK_C:.c=.o): $(QUARK_DIR)/quark.h $(QUARK_DIR)/logger.h $(QUARK_DIR)/dirlisting.h
qksrv.bin: Makefile $(QUARK_OBJ)
	$(LD) $(LDFLAGS) --omagic -Ttext 0xC00000 -e _start $(filter %.o,$^) -o $(QUARK_DIR)/qksrv.tmp
	$(OBJCOPY) -O binary --remove-section=.bss --remove-section=.reloc $(QUARK_DIR)/qksrv.tmp $@
	$(PYTHON) -c "from pathlib import Path; Path('$(QUARK_DIR)/qksrv.tmp').unlink(missing_ok=True)"

# QuarkFTP
FTP_DIR := src/apps/qkftp
FTP_C := \
    $(FTP_DIR)/main.c \
    $(FTP_DIR)/commands.c
FTP_OBJ := $(FTP_DIR)/entry.o $(FTP_C:.c=.o)
$(FTP_C:.c=.o): $(FTP_DIR)/ftp.h

qkftp.bin: Makefile $(FTP_OBJ)
	$(LD) $(LDFLAGS) --omagic -Ttext 0xC00000 -e _start $(filter %.o,$^) -o $(FTP_DIR)/qkftp.tmp
	$(OBJCOPY) -O binary --remove-section=.bss --remove-section=.reloc $(FTP_DIR)/qkftp.tmp $@
	$(PYTHON) -c "from pathlib import Path; Path('$(FTP_DIR)/qkftp.tmp').unlink(missing_ok=True)"

.PHONY: fastfetch.bin
fastfetch.bin: src/include/os_version.h
	$(MAKE) -C src/apps/fastfetch CC="$(CC)" LD="$(LD)" OBJCOPY="$(OBJCOPY)" NASM="$(NASM)" PYTHON="$(PYTHON)"
	$(PYTHON) -c "from pathlib import Path; Path('fastfetch.bin').write_bytes(Path('src/apps/fastfetch/fastfetch.bin').read_bytes())"

src/boot.bin: Makefile src/boot/boot.asm
	$(NASM) -f bin src/boot/boot.asm -o $@

src/kernel/kernel.bin: Makefile kernel.ld $(KERNEL_OBJ)
	$(LD) $(LDFLAGS) -T kernel.ld $(filter %.o,$^) -o src/kernel/kernel.tmp
	$(OBJCOPY) -O binary --remove-section=.bss --remove-section=.reloc src/kernel/kernel.tmp $@
ifeq ($(STARTUP_TRACE),1)
	$(PYTHON) -c "from pathlib import Path; Path('src/kernel/kernel.trace.elf').write_bytes(Path('src/kernel/kernel.tmp').read_bytes())"
endif
	$(PYTHON) -c "from pathlib import Path; Path('src/kernel/kernel.tmp').unlink(missing_ok=True)"

src/boot_stage2.bin: Makefile src/boot/boot_stage2.asm src/kernel/kernel.bin tools/build_boot_stage2.py tools/disk_layout.py
	$(PYTHON) tools/build_boot_stage2.py --nasm "$(NASM)"

RESOURCE_AUDIO := $(shell $(PYTHON) -c "from pathlib import Path; print(' '.join(p.as_posix() for p in Path('resources').rglob('*') if p.is_file() and p.suffix.lower() in {'.mid', '.midi', '.wav', '.mp3'}))")

# Include directories so adding/removing nested firmware also rebuilds the image.
FIRMWARE_INPUTS := $(shell $(PYTHON) -c "from pathlib import Path; print(' '.join(p.as_posix() for p in Path('firmware').rglob('*')))")

DOOM_WADS := $(wildcard doom1.wad doom.wad doom2.wad)

src/kernel/main.o src/system/init/main.o: src/system/init/nixinit.h

# Rebuild the image when switching between GUI and text-only packaging.
.PHONY: FORCE_IMAGE_CONFIG
.gui-config: FORCE_IMAGE_CONFIG
	$(PYTHON) -c "from pathlib import Path; p=Path('.gui-config'); value='$(NOGUI)'; p.write_text(value) if not p.exists() or p.read_text()!=value else None"

os.img: .gui-config firmware $(FIRMWARE_INPUTS) $(DOOM_WADS) $(RESOURCE_AUDIO) resources Makefile src/boot.bin src/boot_stage2.bin src/kernel/kernel.bin $(APP_BINS) mkext4.py tools/disk_layout.py tools/ext4_builder.py
	$(PYTHON) mkext4.py os.img.new $(if $(filter 1,$(NOGUI)),-nogui,)
	$(PYTHON) -c "from pathlib import Path; Path('os.img.new').replace('os.img')"
	$(MAKE) clean-build

run: os.img
	$(QEMU) $(QEMU_ACCEL_FLAGS) -cpu $(QEMU_CPU) -smp 1 -m $(QEMU_MEMORY) $(QEMU_DRIVE) -netdev user,id=net0,net=$(QEMU_DHCP)$(QEMU_FORWARD) -device $(QEMU_NETCARD),netdev=net0 -vga $(QEMU_VGA) -display $(QEMU_DISPLAY) $(QEMU_AUDIO_FLAGS) -device sb16,audiodev=audio0 $(QEMU_OPL_FLAGS) -serial stdio -rtc base=utc $(QEMU_USB) $(QEMU_EXTRA)

clean-build:
	$(PYTHON) -c "from pathlib import Path; pats=('*.o','*.tmp'); [p.unlink() for pat in pats for p in Path('.').rglob(pat) if p.is_file()]; [p.unlink(missing_ok=True) for p in [Path('src/boot.bin'),Path('src/boot_stage2.bin'),Path('src/kernel/kernel.bin')]+[Path(f'{a}.bin') for a in '$(STANDARD_APPS) doom syndig browser fastfetch'.split()]]"

clean: clean-build
	$(PYTHON) -c "from pathlib import Path; Path('os.img').unlink(missing_ok=True)"

src/kernel/net/broadcom/b44.o: src/kernel/net/broadcom/b44.h src/kernel/net/broadcom/b44_regs.h src/kernel/hal/net_types.h
src/kernel/hal/net.o: src/kernel/net/broadcom/b44.h

src/kernel/drivers/hda.o: src/kernel/drivers/hda.h src/kernel/hal/audio.h
src/kernel/hal/audio.o: src/kernel/drivers/hda.h

$(KERNEL_C:.c=.o): src/kernel/drivers/pci.h src/kernel/drivers/delay.h
src/kernel/net/broadcom/tg3.o: src/kernel/net/broadcom/tg3_regs.h

src/kernel/net/broadcom/tg3.o src/kernel/net/phys/broadcom.o: src/kernel/net/phys/broadcom.h

$(KERNEL_C:.c=.o): src/kernel/hal/net.h src/kernel/hal/net_types.h
src/kernel/net/amd/pcnet.o: src/kernel/net/amd/pcnet.h
src/kernel/net/intel/e1000.o: src/kernel/net/intel/e1000.h
src/kernel/net/realtek/r8169.o: src/kernel/net/realtek/r8169.h
src/kernel/net/broadcom/tg3.o: src/kernel/net/broadcom/tg3.h
src/kernel/hal/net.o: src/kernel/net/amd/pcnet.h src/kernel/net/intel/e1000.h src/kernel/net/broadcom/tg3.h src/kernel/net/realtek/r8169.h

$(KERNEL_C:.c=.o): src/kernel/hal/video.h
src/kernel/hal/video.o src/kernel/drivers/display/vga.o src/kernel/drivers/display/qemu.o: src/kernel/drivers/display/video_drivers.h

$(KERNEL_C:.c=.o): src/kernel/fs.h src/kernel/users.h src/kernel/types.h src/kernel/drivers/display.h
src/kernel/drivers/display/virtio_gpu.o: src/kernel/drivers/display/video_drivers.h

# Native settings controls and their driver interfaces.
src/system/graphics/settings/settings.o: src/system/graphics/settings/settings.h src/kernel/hal/video.h src/kernel/hal/net.h src/kernel/hal/audio.h src/system/graphics/wm/theme.h
src/system/graphics/wm/start.o src/system/graphics/wm/wm.o: src/system/graphics/settings/settings.h src/system/graphics/wm/start.h
src/kernel/hal/video.o: src/kernel/hal/video.h

src/kernel/net/dhcp.o: src/kernel/net/dhcp.h src/kernel/net/udp.h src/kernel/net/ipv4.h
src/kernel/net/ipv4.o src/kernel/hal/net.o: src/kernel/net/ipv4.h src/kernel/net/dhcp.h

src/kernel/drivers/display/intel_gma.o: src/kernel/drivers/display/video_drivers.h src/kernel/hal/video.h src/kernel/drivers/pci.h
src/system/graphics/settings/settings.o: src/kernel/hal/video.h

src/kernel/drivers/display/intel_gma.o src/kernel/drivers/display/i915/i915.o: src/kernel/drivers/display/i915/i915.h src/kernel/drivers/display/i915/i915_regs.h

DOOM_DIR := src/apps/doom
DOOM_ENGINE := \
    dummy am_map doomdef doomstat dstrings d_event d_items d_iwad \
    d_loop d_main d_mode d_net f_finale f_wipe g_game hu_lib \
    hu_stuff info i_cdmus i_endoom i_joystick i_scale i_sound i_system \
    i_timer memio m_argv m_bbox m_cheat m_config m_controls m_fixed \
    m_menu m_misc m_random p_ceilng p_doors p_enemy p_floor p_inter \
    p_lights p_map p_maputl p_mobj p_plats p_pspr p_saveg p_setup \
    p_sight p_spec p_switch p_telept p_tick p_user r_bsp r_data \
    r_draw r_main r_plane r_segs r_sky r_things sha1 sounds \
    statdump st_lib st_stuff s_sound tables v_video wi_stuff w_checksum \
    w_file w_main w_wad z_zone w_file_stdc i_input i_video doomgeneric
DOOM_OBJ := $(DOOM_DIR)/entry.o $(DOOM_DIR)/platform.o $(DOOM_DIR)/audio.o \
            $(DOOM_DIR)/compat/libc.o $(addprefix $(DOOM_DIR)/doomgeneric/,$(addsuffix .o,$(DOOM_ENGINE)))
$(DOOM_OBJ): CFLAGS += -Isrc/apps/doom/compat -DNIXEL_DOOM -DFEATURE_SOUND -DDOOMGENERIC_RESX=320 -DDOOMGENERIC_RESY=200 -fno-builtin -fno-stack-protector
$(DOOM_OBJ): $(wildcard src/apps/doom/compat/*.h src/apps/doom/compat/sys/*.h src/apps/doom/doomgeneric/*.h)
doom.bin: Makefile $(DOOM_OBJ)
	$(LD) $(LDFLAGS) --omagic -Ttext 0xC00000 -e _start $(filter %.o,$^) -o $(DOOM_DIR)/doom.tmp
	$(OBJCOPY) -O binary --remove-section=.bss $(DOOM_DIR)/doom.tmp $@

.PHONY: doom-data
doom-data:
	$(PYTHON) tools/fetch_doom_shareware.py

src/system/services/audio/mixer.o src/system/services/audio/system_sound.o src/kernel/main.o src/kernel/syscall.o src/system/graphics/wm/wm.o src/system/graphics/settings/settings.o: src/system/services/audio/mixer.h src/system/user/audio_api.h
src/kernel/hal/audio.o src/kernel/drivers/hda.o src/kernel/drivers/pc_speaker.o src/system/services/audio/mixer.o: src/kernel/hal/audio.h

.PHONY: test-audio
test-audio:
	sh src/system/services/audio/tests/run_mixer_tests.sh
	sh src/apps/doom/tests/run_audio_tests.sh

.PHONY: test-usb
.PHONY: test-usb-native
test-usb-native:
	sh src/kernel/drivers/usb/tests/run_native_tests.sh

.PHONY: test-usb-handoff
test-usb-handoff:
	sh src/kernel/drivers/usb/tests/run_handoff_tests.sh

test-usb:
	sh src/kernel/drivers/usb/tests/run_tests.sh

.PHONY: test-usb-enumeration
.PHONY: test-usb-reset
test-usb-reset:
	$(PYTHON) tools/tests/test_usb_global_reset.py

test-usb-enumeration:
	$(PYTHON) tools/tests/test_usb_enumeration.py
	$(PYTHON) tools/tests/test_usb_port_changes.py

.PHONY: test-boot-layout
test-boot-layout:
	$(PYTHON) tools/tests/test_disk_layout.py

.PHONY: test-ext4
test-ext4:
	$(PYTHON) tools/tests/test_ext4.py

# Shared native UI palette dependencies.
src/kernel/login.o src/apps/notepad/notepad.o src/apps/viewer/viewer.o src/apps/calc/main.o src/apps/mines/main.o src/apps/hexview/main.o src/apps/files/main.o src/apps/floppy_debug/main.o src/system/graphics/dialog/shutdown.o src/apps/sound/main.o src/apps/chess/main.o src/apps/irc/main.o src/apps/oobe/main.o src/apps/sysdiag/main.o src/apps/nixplorer/browser/main.o: src/system/graphics/wm/theme.h

src/system/graphics/terminal/terminal.o: src/system/graphics/wm/theme.h

# Public ABI headers affect every application, including compatibility wrappers.
$(KERNEL_C:.c=.o) $(STANDARD_APPS:%=src/apps/%/main.o) $(BROWSER_OBJ) $(SYNDIG_OBJ) $(QUARK_OBJ) $(FTP_OBJ) $(DOOM_OBJ): src/system/user/sys_api.h
src/kernel/syscall.o: src/kernel/syscall_policy.h src/kernel/users.h

.PHONY: test-privileges
test-privileges:
	sh src/kernel/tests/run_privilege_tests.sh "$(PYTHON)"

src/system/graphics/dialog/shutdown.o src/system/graphics/session.o src/system/graphics/wm/start.o: src/system/graphics/dialog/shutdown.h

src/kernel/drivers/display/font.o src/kernel/drivers/power.o src/system/graphics/fonts/univers85ebo_16.o src/system/graphics/widgets.o src/system/graphics/wm/wm.o src/system/graphics/wm/presentation.o src/system/graphics/text_layout.o: src/system/graphics/text_layout.h src/kernel/drivers/display/font.h

# Native b43 firmware parsing and boot, exercised with a register model.
.PHONY: test-b43
test-b43:
	$(CC) -std=c99 -Wall -Wextra -Werror src/kernel/net/wireless/broadcom/b43/tests/firmware_test.c src/kernel/net/wireless/broadcom/b43/b43_firmware.c -o /tmp/nixel-test-b43
	/tmp/nixel-test-b43
	$(CC) -std=c99 -Wall -Wextra -Werror src/kernel/net/wireless/broadcom/b43/tests/boot_test.c src/kernel/net/wireless/broadcom/b43/b43_boot.c src/kernel/net/wireless/broadcom/b43/b43_firmware.c -o /tmp/nixel-test-b43-boot
	/tmp/nixel-test-b43-boot

src/kernel/net/wireless/broadcom/b43/b43.o src/kernel/net/wireless/broadcom/b43/b43_firmware.o src/kernel/net/wireless/broadcom/b43/b43_boot.o src/kernel/hal/wifi.o: src/kernel/net/wireless/broadcom/b43/b43.h
src/kernel/net/wireless/broadcom/b43/b43_boot.o: src/kernel/net/wireless/broadcom/b43/b43_boot.h
src/system/shell/commands.o: src/kernel/hal/wifi.h src/kernel/net/wireless/broadcom/b43/b43.h
src/kernel/hal/wifi.o src/system/graphics/settings/settings.o src/system/graphics/wm/wm.o: src/kernel/hal/wifi.h

IWLEGACY_DIR := src/kernel/net/wireless/intel/iwlegacy
IWLEGACY_STATION := $(IWLEGACY_DIR)/station.c $(wildcard $(IWLEGACY_DIR)/station/*.c)
IWLEGACY_NATIVE := $(IWLEGACY_DIR)/3945.c $(IWLEGACY_DIR)/firmware.c $(IWLEGACY_DIR)/eeprom.c $(IWLEGACY_DIR)/bsm.c $(IWLEGACY_DIR)/rx.c $(IWLEGACY_DIR)/session.c $(IWLEGACY_DIR)/command.c $(IWLEGACY_DIR)/scan.c $(IWLEGACY_DIR)/power.c $(IWLEGACY_DIR)/tx.c $(IWLEGACY_DIR)/bss.c $(IWLEGACY_DIR)/rsn.c
$(IWLEGACY_DIR)/iwlegacy.o $(IWLEGACY_DIR)/device.o $(IWLEGACY_DIR)/pci_power.o $(IWLEGACY_STATION:.c=.o) $(IWLEGACY_NATIVE:.c=.o): $(IWLEGACY_DIR)/station/private.h $(IWLEGACY_DIR)/bss.h $(IWLEGACY_DIR)/iwlegacy.h $(IWLEGACY_DIR)/internal.h $(IWLEGACY_DIR)/csr.h $(IWLEGACY_DIR)/prph.h $(IWLEGACY_DIR)/rx.h $(IWLEGACY_DIR)/session.h $(IWLEGACY_DIR)/fh.h $(IWLEGACY_DIR)/command.h $(IWLEGACY_DIR)/scan.h $(IWLEGACY_DIR)/station.h $(IWLEGACY_DIR)/tx.h
src/kernel/hal/net.o src/kernel/hal/wifi.o src/system/shell/commands.o src/system/graphics/settings/settings.o src/system/graphics/wm/wm.o: $(IWLEGACY_DIR)/iwlegacy.h $(IWLEGACY_DIR)/scan.h $(IWLEGACY_DIR)/station.h
.PHONY: test-iwlegacy
test-iwlegacy:
	$(CC) -O2 -fno-builtin -shared -fPIC -DWOLFSSL_USER_SETTINGS -Isrc/kernel/net/wireless/security -Isrc/system/libraries/tls/vendor/wolfssl src/kernel/net/wireless/security/wpa2.c src/kernel/net/wireless/security/ccmp.c src/kernel/net/wireless/security/handshake.c $(addprefix src/system/libraries/tls/vendor/wolfssl/wolfcrypt/src/,$(addsuffix .c,sha hmac hash pwdbased aes)) -o /tmp/nixel-test-iwlegacy-crypto.so
	$(CC) -std=c99 -O2 -Wall -Wextra -Werror $(IWLEGACY_DIR)/tests/3945_test.c $(IWLEGACY_NATIVE) -o /tmp/nixel-test-iwlegacy
	/tmp/nixel-test-iwlegacy
	$(CC) -std=c99 -O2 -Wall -Wextra -Werror -fno-pie -no-pie $(IWLEGACY_DIR)/tests/pci_test.c $(IWLEGACY_NATIVE) -o /tmp/nixel-test-iwlegacy-pci
	/tmp/nixel-test-iwlegacy-pci
	$(CC) -std=c99 -O2 -Wall -Wextra -Werror $(IWLEGACY_DIR)/tests/transport_test.c $(IWLEGACY_NATIVE) -o /tmp/nixel-test-iwlegacy-transport
	/tmp/nixel-test-iwlegacy-transport
	$(CC) -std=c99 -O2 -Wall -Wextra -Werror -fno-pie -no-pie $(IWLEGACY_DIR)/tests/device_test.c $(IWLEGACY_DIR)/pci_power.c $(IWLEGACY_NATIVE) $(IWLEGACY_STATION) src/kernel/net/wireless/ieee80211.c /tmp/nixel-test-iwlegacy-crypto.so -o /tmp/nixel-test-iwlegacy-device
	/tmp/nixel-test-iwlegacy-device
	$(CC) -std=c99 -O2 -Wall -Wextra -Werror $(IWLEGACY_DIR)/tests/scan_test.c $(IWLEGACY_NATIVE) -o /tmp/nixel-test-iwlegacy-scan
	/tmp/nixel-test-iwlegacy-scan
	$(CC) -std=c99 -O2 -Wall -Wextra -Werror $(IWLEGACY_DIR)/tests/power_bridge_test.c $(IWLEGACY_DIR)/power.c src/kernel/net/wireless/ieee80211.c -o /tmp/nixel-test-iwlegacy-power
	/tmp/nixel-test-iwlegacy-power
	$(CC) -std=c99 -O2 -Wall -Wextra -Werror $(IWLEGACY_DIR)/tests/station_test.c $(IWLEGACY_NATIVE) $(IWLEGACY_STATION) src/kernel/net/wireless/ieee80211.c /tmp/nixel-test-iwlegacy-crypto.so -o /tmp/nixel-test-iwlegacy-station
	/tmp/nixel-test-iwlegacy-station
	$(CC) -std=c99 -O2 -Wall -Wextra -Werror $(IWLEGACY_DIR)/tests/bss_test.c $(IWLEGACY_DIR)/bss.c -o /tmp/nixel-test-iwlegacy-bss
	/tmp/nixel-test-iwlegacy-bss
	$(CC) -std=c99 -O2 -Wall -Wextra -Werror $(IWLEGACY_DIR)/tests/station_protocol_test.c $(IWLEGACY_NATIVE) $(IWLEGACY_STATION) src/kernel/net/wireless/ieee80211.c /tmp/nixel-test-iwlegacy-crypto.so -o /tmp/nixel-test-iwlegacy-protocol
	/tmp/nixel-test-iwlegacy-protocol

# Reusable user-space TLS library; applications opt in by linking this archive.
include src/system/libraries/tls/tls.mk


.PHONY: test-browser-https
test-browser-https: browser.bin
	$(PYTHON) src/apps/nixplorer/tests/run_tests.py

.PHONY: test-paging
test-paging:
	CC="$(CC)" LD="$(LD)" NASM="$(NASM)" QEMU="$(QEMU)" $(PYTHON) tools/tests/test_paging.py

# The generic C rule does not track included headers.
src/kernel/paging.o src/kernel/syscall.o: src/kernel/paging.h src/system/user/sys_api.h
src/apps/doom/compat/libc.o: src/system/user/sys_api.h

# Reuse wolfCrypt SHA-1/HMAC/PBKDF2 with a kernel-only, allocation-free config.
# Separate objects prevent the TLS client's settings/allocator from leaking in.
WIFI_SECURITY_DIR := src/kernel/net/wireless/security
WIFI_CRYPTO_VENDOR := src/system/libraries/tls/vendor/wolfssl
WIFI_CRYPTO_OBJECTS := $(addprefix $(WIFI_SECURITY_DIR)/wolf_,$(addsuffix .o,sha hmac hash pwdbased aes))
WIFI_CRYPTO_FLAGS := -U__unix__ -U__linux__ -DWOLFSSL_USER_SETTINGS -I$(WIFI_SECURITY_DIR) -Isrc/system/libraries/tls/compat -I$(WIFI_CRYPTO_VENDOR) -fno-builtin -fno-stack-protector
KERNEL_OBJ += $(WIFI_SECURITY_DIR)/wpa2.o $(WIFI_SECURITY_DIR)/ccmp.o $(WIFI_SECURITY_DIR)/handshake.o $(WIFI_CRYPTO_OBJECTS)
src/kernel/kernel.bin: $(WIFI_SECURITY_DIR)/wpa2.o $(WIFI_SECURITY_DIR)/ccmp.o $(WIFI_SECURITY_DIR)/handshake.o $(WIFI_CRYPTO_OBJECTS)
$(WIFI_SECURITY_DIR)/wpa2.o $(WIFI_SECURITY_DIR)/ccmp.o $(WIFI_SECURITY_DIR)/handshake.o: CFLAGS += $(WIFI_CRYPTO_FLAGS)
$(WIFI_SECURITY_DIR)/wpa2.o: $(WIFI_SECURITY_DIR)/wpa2.c $(WIFI_SECURITY_DIR)/wpa2.h $(WIFI_SECURITY_DIR)/user_settings.h
$(WIFI_SECURITY_DIR)/ccmp.o: $(WIFI_SECURITY_DIR)/ccmp.c $(WIFI_SECURITY_DIR)/ccmp.h $(WIFI_SECURITY_DIR)/wpa2.h $(WIFI_SECURITY_DIR)/user_settings.h
$(WIFI_SECURITY_DIR)/handshake.o: $(WIFI_SECURITY_DIR)/handshake.c $(WIFI_SECURITY_DIR)/handshake.h $(WIFI_SECURITY_DIR)/ccmp.h $(WIFI_SECURITY_DIR)/wpa2.h $(WIFI_SECURITY_DIR)/user_settings.h
$(WIFI_CRYPTO_OBJECTS): $(WIFI_SECURITY_DIR)/user_settings.h Makefile
$(WIFI_SECURITY_DIR)/wolf_%.o: $(WIFI_CRYPTO_VENDOR)/wolfcrypt/src/%.c
	$(CC) $(CFLAGS) $(WIFI_CRYPTO_FLAGS) -MMD -MP -c $< -o $@
-include $(WIFI_CRYPTO_OBJECTS:.o=.d)
.PHONY: test-wifi-crypto
test-wifi-crypto:
	CC="$(CC)" $(PYTHON) $(WIFI_SECURITY_DIR)/tests/test_wpa2.py

src/kernel/net/wireless/ieee80211.o: src/kernel/net/wireless/ieee80211.h

src/kernel/hal/wifi.o src/system/shell/commands.o src/system/graphics/settings/settings.o src/system/graphics/wm/wm.o: $(IWLEGACY_DIR)/scan.h $(IWLEGACY_DIR)/command.h $(IWLEGACY_DIR)/rx.h
$(WIFI_SECURITY_DIR)/%.o: $(WIFI_SECURITY_DIR)/%.c
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@
-include $(WIFI_SECURITY_DIR)/wpa2.d $(WIFI_SECURITY_DIR)/ccmp.d $(WIFI_SECURITY_DIR)/handshake.d

# Dedicated read-only Wi-Fi diagnostics command.
src/system/shell/commands.o src/system/shell/wifi_commands.o: src/system/shell/wifi_commands.h
src/system/shell/wifi_commands.o: src/kernel/hal/wifi.h src/kernel/net/wireless/broadcom/b43/b43.h $(IWLEGACY_DIR)/iwlegacy.h $(IWLEGACY_DIR)/scan.h
.PHONY: test-wifi-status
test-wifi-status:
	$(CC) -std=c99 -O2 -Wall -Wextra -Werror src/system/shell/tests/wifi_status_test.c src/system/shell/wifi_commands.c -o /tmp/nixel-test-wifi-status
	/tmp/nixel-test-wifi-status

.PHONY: test-mmio-paging
test-mmio-paging:
	CC="$(CC)" LD="$(LD)" NASM="$(NASM)" QEMU="$(QEMU)" $(PYTHON) tools/tests/test_mmio_paging.py

$(IWLEGACY_DIR)/iwlegacy.o $(IWLEGACY_DIR)/device.o: src/kernel/paging.h

# Upstream timing-noise collection MUST be built without optimization.
JITTER_DIR := src/kernel/crypto/vendor/jitterentropy
JITTER_C := $(wildcard $(JITTER_DIR)/src/*.c)
ENTROPY_OBJ := src/kernel/crypto/entropy.o src/kernel/crypto/entropy-platform.o src/kernel/crypto/div64.o $(JITTER_C:.c=.o)
$(JITTER_C:.c=.o): CFLAGS += -O0 -fwrapv -DJENT_NIXEL -Isrc/kernel/crypto -I$(JITTER_DIR)
src/kernel/crypto/entropy.o: CFLAGS += -DJENT_NIXEL -Isrc/kernel/crypto -I$(JITTER_DIR)
$(ENTROPY_OBJ): src/kernel/crypto/entropy.h src/kernel/crypto/jitterentropy-platform.h $(JITTER_DIR)/jitterentropy.h $(wildcard $(JITTER_DIR)/src/*.h)
src/kernel/hal/wifi.o: src/kernel/crypto/entropy.h
src/kernel/kernel.bin: $(ENTROPY_OBJ)

src/kernel/crypto/div64.o: src/system/libraries/tls/div64.c
	$(CC) $(CFLAGS) -c $< -o $@

.PHONY: test-entropy test-entropy-qemu
test-entropy:
	CC="$(CC)" $(PYTHON) tools/tests/test_entropy.py
test-entropy-qemu:
	CC="$(CC)" LD="$(LD)" NASM="$(NASM)" QEMU="$(QEMU)" $(PYTHON) tools/tests/test_entropy_qemu.py

.PHONY: test-wifi-network
test-wifi-network:
	$(CC) -std=c99 -O2 -Wall -Wextra -Werror -Wno-pointer-to-int-cast src/kernel/net/tests/wifi_delivery_test.c src/kernel/net/ipv4.c src/kernel/net/udp.c src/kernel/net/dns.c -o /tmp/nixel-wifi-delivery
	/tmp/nixel-wifi-delivery
	$(CC) -std=c99 -O2 -Wall -Wextra -Werror src/kernel/net/tests/dhcp_options_test.c -o /tmp/nixel-dhcp-options
	/tmp/nixel-dhcp-options
