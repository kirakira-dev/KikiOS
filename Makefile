# KikiOS Makefile
# Build system for KikiOS - an aarch64 operating system
#
# Targets:
#   make              - Build kernel for QEMU (default)
#   make TARGET=pi    - Build kernel for Raspberry Pi Zero 2W
#   make user         - Build userspace programs
#   make install      - Install to disk image
#   make run          - Build, install, and run in QEMU

# Target selection (default: qemu)
TARGET ?= qemu

# Detect host OS
UNAME_S := $(shell uname -s)

# Cross-compiler toolchain (auto-detect based on OS)
ifeq ($(UNAME_S),Darwin)
    CROSS_COMPILE ?= aarch64-elf-
else
    CROSS_COMPILE ?= aarch64-linux-gnu-
endif
export CROSS_COMPILE
CC = $(CROSS_COMPILE)gcc
AS = $(CROSS_COMPILE)as
LD = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy
OBJDUMP = $(CROSS_COMPILE)objdump

# Directories
BOOT_DIR = boot
KERNEL_DIR = kernel
HAL_DIR = kernel/hal
USER_DIR = user
BUILD_DIR = build
SYSROOT = kikios_root

# Target-specific settings
ifeq ($(TARGET),pi-debug)
    HAL_PLATFORM = pizero2w
    CPU = cortex-a53
    LINKER_SCRIPT = linker-pi.ld
    BOOT_SRC = $(BOOT_DIR)/boot-pi.S
    KERNEL_BIN_NAME = kernel8.img
    CFLAGS_TARGET = -DTARGET_PI -DPI_DEBUG_MODE
else ifeq ($(TARGET),pi)
    HAL_PLATFORM = pizero2w
    CPU = cortex-a53
    LINKER_SCRIPT = linker-pi.ld
    BOOT_SRC = $(BOOT_DIR)/boot-pi.S
    KERNEL_BIN_NAME = kernel8.img
    CFLAGS_TARGET = -DTARGET_PI
else
    HAL_PLATFORM = qemu
    CPU = cortex-a72
    LINKER_SCRIPT = linker.ld
    BOOT_SRC = $(BOOT_DIR)/boot.S
    KERNEL_BIN_NAME = kikios.bin
    CFLAGS_TARGET = -DTARGET_QEMU
endif

# Printf output: uart (default) or screen
PRINTF ?= uart
ifeq ($(PRINTF),uart)
    CFLAGS_TARGET += -DPRINTF_UART
endif

# Source files
KERNEL_C_SRCS = $(wildcard $(KERNEL_DIR)/*.c)
KERNEL_S_SRCS = $(wildcard $(KERNEL_DIR)/*.S)
HAL_C_SRCS = $(wildcard $(HAL_DIR)/$(HAL_PLATFORM)/*.c)
HAL_USB_C_SRCS = $(wildcard $(HAL_DIR)/$(HAL_PLATFORM)/usb/*.c)

# Userspace programs (single-file)
USER_PROGS = splash snake tetris desktop calc kikish echo ls cat pwd mkdir touch rm term uptime sysmon textedit files date play music ping fetch viewer vim led \
             clear yes sleep seq whoami hostname uname which basename dirname \
             head tail wc df free ps stat grep find hexdump du cp mv kill lscpu lsusb dmesg mousetest readtest kikicode browser explode kikifetch \
             kotos kinary kuav git winexec kftp wifi

# Object files
BOOT_OBJ = $(BUILD_DIR)/boot.o
KERNEL_C_OBJS = $(patsubst $(KERNEL_DIR)/%.c,$(BUILD_DIR)/%.o,$(KERNEL_C_SRCS))
KERNEL_S_OBJS = $(patsubst $(KERNEL_DIR)/%.S,$(BUILD_DIR)/%.o,$(KERNEL_S_SRCS))
HAL_OBJS = $(patsubst $(HAL_DIR)/$(HAL_PLATFORM)/%.c,$(BUILD_DIR)/hal_%.o,$(HAL_C_SRCS))
HAL_USB_OBJS = $(patsubst $(HAL_DIR)/$(HAL_PLATFORM)/usb/%.c,$(BUILD_DIR)/hal_usb_%.o,$(HAL_USB_C_SRCS))
KERNEL_OBJS = $(KERNEL_C_OBJS) $(KERNEL_S_OBJS) $(HAL_OBJS) $(HAL_USB_OBJS)

# Output files
KERNEL_ELF = $(BUILD_DIR)/kikios.elf
KERNEL_BIN = $(BUILD_DIR)/$(KERNEL_BIN_NAME)
DISK_IMG = disk.img
DISK_SIZE = 1024

# Compiler flags
CFLAGS = -ffreestanding -nostdlib -nostartfiles -mcpu=$(CPU) -mstrict-align -Wall -Wextra -Wno-unused-variable -Wno-unused-function -O3 -I$(KERNEL_DIR) -I$(KERNEL_DIR)/libc $(CFLAGS_TARGET)
TLS_CFLAGS = -ffreestanding -nostdlib -nostartfiles -mcpu=$(CPU) -mstrict-align -O2 -I$(KERNEL_DIR) -I$(KERNEL_DIR)/libc -w $(CFLAGS_TARGET)
ASFLAGS = -mcpu=$(CPU)
LDFLAGS = -nostdlib -T $(LINKER_SCRIPT)

# Userspace compiler flags
USER_CFLAGS = -ffreestanding -nostdlib -nostartfiles -mcpu=$(CPU) -mstrict-align -fPIE -Wall -Wextra -Wno-unused-variable -Wno-unused-function -O3 -I$(USER_DIR)/lib
USER_LDFLAGS = -nostdlib -pie -T user/linker.ld

# QEMU settings (audio/display backends vary by OS)
QEMU = qemu-system-aarch64

# Audio device selection (auto-detect by default)
# Options: sdl, pulseaudio, alsa, coreaudio (macOS), pipewire, etc.
# Usage: make run AUDIODEV=pulseaudio
#
# Display backend selection (auto-detect by default)
# Options: gtk, sdl, cocoa (macOS), none, etc.
# Usage: make run QEMU_DISPLAY_OPT=sdl
ifeq ($(UNAME_S),Darwin)
    AUDIODEV ?= coreaudio
    QEMU_DISPLAY_OPT ?= cocoa
else
    AUDIODEV ?= sdl
    QEMU_DISPLAY_OPT ?= gtk
endif
QEMU_AUDIO = -audiodev $(AUDIODEV),id=audio0
QEMU_DISPLAY = -display $(QEMU_DISPLAY_OPT)
QEMU_FLAGS = -M virt,secure=on -cpu cortex-a72 -m 512M -rtc base=utc,clock=host -global virtio-mmio.force-legacy=false -device ramfb -device virtio-blk-device,drive=hd0 -drive file=$(DISK_IMG),if=none,format=raw,id=hd0 -device virtio-keyboard-device -device virtio-tablet-device -device virtio-sound-device,audiodev=audio0 $(QEMU_AUDIO) -device virtio-net-device,netdev=net0 -netdev user,id=net0 $(QEMU_DISPLAY) -serial stdio -bios $(BUILD_DIR)/kikios.bin
QEMU_FLAGS_NOGRAPHIC = -M virt,secure=on -cpu cortex-a72 -m 512M -rtc base=utc,clock=host -global virtio-mmio.force-legacy=false -device virtio-blk-device,drive=hd0 -drive file=$(DISK_IMG),if=none,format=raw,id=hd0 -device virtio-sound-device,audiodev=audio0 $(QEMU_AUDIO) -device virtio-net-device,netdev=net0 -netdev user,id=net0 -nographic -bios $(BUILD_DIR)/kikios.bin

.PHONY: all clean run run-nographic run-pi user install disk pi pi-debug sync-disk

all: $(KERNEL_BIN)
	@echo ""
	@echo "Built for target: $(TARGET)"
	@echo "Output: $(KERNEL_BIN)"

# ============ Kernel build ============

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(BOOT_OBJ): $(BOOT_SRC) | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/hal_%.o: $(HAL_DIR)/$(HAL_PLATFORM)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/hal_usb_%.o: $(HAL_DIR)/$(HAL_PLATFORM)/usb/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/tls.o: $(KERNEL_DIR)/tls.c | $(BUILD_DIR)
	@echo "Building TLS (this takes a while)..."
	$(CC) $(TLS_CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(KERNEL_DIR)/%.S | $(BUILD_DIR)
	$(CC) $(ASFLAGS) -c $< -o $@

$(KERNEL_ELF): $(BOOT_OBJ) $(KERNEL_OBJS)
	$(LD) $(LDFLAGS) $^ -o $@

$(KERNEL_BIN): $(KERNEL_ELF)
	$(OBJCOPY) -O binary $< $@
	@echo ""
	@echo "========================================="
	@echo "  KikiOS kernel built!"
	@echo "  Target: $(TARGET)"
	@echo "  Binary: $(KERNEL_BIN)"
	@echo "========================================="

# ============ Userspace build ============

$(BUILD_DIR)/user:
	mkdir -p $(BUILD_DIR)/user

$(BUILD_DIR)/user/crt0.o: $(USER_DIR)/lib/crt0.S | $(BUILD_DIR)/user
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/user/crti.o: $(USER_DIR)/lib/crti.S | $(BUILD_DIR)/user
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/user/crtn.o: $(USER_DIR)/lib/crtn.S | $(BUILD_DIR)/user
	$(CC) $(ASFLAGS) -c $< -o $@

$(BUILD_DIR)/user/%.prog.o: $(USER_DIR)/bin/%.c | $(BUILD_DIR)/user
	$(CC) $(USER_CFLAGS) -c $< -o $@

# Single-file programs -> kikios_root/bin/
$(SYSROOT)/bin/%: $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/%.prog.o
	$(LD) $(USER_LDFLAGS) $^ -o $@
	@echo "  Built /bin/$*"

# MicroPython (external build)
$(SYSROOT)/bin/micropython: $(wildcard micropython/ports/kikios/*.c)
	@echo "Building MicroPython..."
	$(MAKE) -C micropython/ports/kikios
	cp micropython/ports/kikios/build/micropython.elf $@
	@echo "  Built /bin/micropython"

# TCC (external build)
$(SYSROOT)/bin/tcc: $(wildcard tinycc/kikios/*.c) $(wildcard tinycc/kikios/*.h)
	@echo "Building TCC..."
	$(MAKE) -C tinycc/kikios clean
	$(MAKE) -C tinycc/kikios
	cp tinycc/kikios/build/tcc $@
	@echo "  Built /bin/tcc"

# DOOM (external build)
$(SYSROOT)/bin/doom: $(wildcard user/bin/doom/*.c) $(wildcard user/bin/doom/*.h)
	@echo "Building DOOM..."
	$(MAKE) -C user/bin/doom
	cp user/bin/doom/build/doom $@
	@echo "  Built /bin/doom"

# CRT files for TCC
CRT_FILES = $(BUILD_DIR)/user/crt0.o $(BUILD_DIR)/user/crti.o $(BUILD_DIR)/user/crtn.o

# Build all userspace programs
USER_BINS = $(patsubst %,$(SYSROOT)/bin/%,$(USER_PROGS)) $(SYSROOT)/bin/micropython $(SYSROOT)/bin/tcc $(SYSROOT)/bin/doom $(SYSROOT)/bin/browser.py

# Copy Python browser script
$(SYSROOT)/bin/browser.py: $(USER_DIR)/bin/browser.py
	cp $< $@

user: $(USER_BINS) $(CRT_FILES)
	@echo ""
	@echo "========================================="
	@echo "  Userspace programs built!"
	@echo "  Output: $(SYSROOT)/bin/"
	@echo "========================================="

# ============ Disk image ============

$(DISK_IMG):
	@echo "Creating FAT32 disk image..."
	dd if=/dev/zero of=$(DISK_IMG) bs=1M count=$(DISK_SIZE)
ifeq ($(UNAME_S),Darwin)
	@DISK_DEV=$$(hdiutil attach -nomount $(DISK_IMG) | head -1 | awk '{print $$1}') && \
		newfs_msdos -F 32 -v VIBEOS $$DISK_DEV && \
		hdiutil detach $$DISK_DEV
else
	@LOOP_DEV=$$(sudo losetup --find --show $(DISK_IMG)) && \
		sudo mkfs.vfat -F 32 -n VIBEOS $$LOOP_DEV && \
		sudo losetup -d $$LOOP_DEV
endif
	@echo "Disk image created: $(DISK_IMG)"

disk: $(DISK_IMG)

# Sync kikios_root/ to QEMU disk image (internal target)
sync-disk: user $(DISK_IMG)
ifeq ($(UNAME_S),Darwin)
	@hdiutil attach $(DISK_IMG) -nobrowse -mountpoint /tmp/kikios_mount > /dev/null
	@rsync -a $(SYSROOT)/ /tmp/kikios_mount/
	@mkdir -p /tmp/kikios_mount/usr/src
	@rsync -a --exclude='*.o' --exclude='*.elf' --exclude='build/' user/ /tmp/kikios_mount/usr/src/user/
	@rsync -a --exclude='*.o' --exclude='build/' tinycc/ /tmp/kikios_mount/usr/src/tinycc/
	@mkdir -p /tmp/kikios_mount/games
	@mkdir -p /tmp/kikios_mount/lib/tcc/include
	@mkdir -p /tmp/kikios_mount/lib/tcc/lib
	@cp -r tinycc/include/* /tmp/kikios_mount/lib/tcc/include/
	@cp tinycc/kikios/tcc_include/* /tmp/kikios_mount/lib/tcc/include/ 2>/dev/null || true
	@cp user/lib/kiki.h /tmp/kikios_mount/lib/tcc/include/
	@cp user/lib/gfx.h /tmp/kikios_mount/lib/tcc/include/ 2>/dev/null || true
	@cp $(BUILD_DIR)/user/crt0.o /tmp/kikios_mount/lib/tcc/lib/crt1.o
	@cp $(BUILD_DIR)/user/crt0.o /tmp/kikios_mount/lib/tcc/lib/Scrt1.o
	@cp $(BUILD_DIR)/user/crti.o /tmp/kikios_mount/lib/tcc/lib/
	@cp $(BUILD_DIR)/user/crtn.o /tmp/kikios_mount/lib/tcc/lib/
	@cp tinycc/kikios/libtcc1.a /tmp/kikios_mount/lib/tcc/lib/
	@cp tinycc/kikios/libc.a /tmp/kikios_mount/lib/tcc/lib/
	@cp tinycc/kikios/libc.a /tmp/kikios_mount/lib/tcc/lib/libc.so
	@cp user/linker.ld /tmp/kikios_mount/lib/tcc/lib/
	@dot_clean /tmp/kikios_mount 2>/dev/null || true
	@find /tmp/kikios_mount -name '._*' -delete 2>/dev/null || true
	@find /tmp/kikios_mount -name '.DS_Store' -delete 2>/dev/null || true
	@rm -rf /tmp/kikios_mount/.fseventsd /tmp/kikios_mount/.Spotlight-V100 /tmp/kikios_mount/.Trashes 2>/dev/null || true
	@hdiutil detach /tmp/kikios_mount > /dev/null
else
	@# Mount disk image and sync files (requires sudo)
	@mkdir -p /tmp/kikios_mount
	@sudo mount -o loop $(DISK_IMG) /tmp/kikios_mount
	@sudo rsync -a --no-owner --no-group $(SYSROOT)/ /tmp/kikios_mount/
	@sudo mkdir -p /tmp/kikios_mount/usr/src
	@sudo rsync -a --no-owner --no-group --exclude='*.o' --exclude='*.elf' --exclude='build/' user/ /tmp/kikios_mount/usr/src/user/
	@sudo rsync -a --no-owner --no-group --exclude='*.o' --exclude='build/' tinycc/ /tmp/kikios_mount/usr/src/tinycc/
	@sudo mkdir -p /tmp/kikios_mount/games
	@sudo mkdir -p /tmp/kikios_mount/lib/tcc/include
	@sudo mkdir -p /tmp/kikios_mount/lib/tcc/lib
	@sudo cp -r tinycc/include/* /tmp/kikios_mount/lib/tcc/include/
	@sudo cp tinycc/kikios/tcc_include/* /tmp/kikios_mount/lib/tcc/include/ 2>/dev/null || true
	@sudo cp user/lib/kiki.h /tmp/kikios_mount/lib/tcc/include/
	@sudo cp user/lib/gfx.h /tmp/kikios_mount/lib/tcc/include/ 2>/dev/null || true
	@sudo cp $(BUILD_DIR)/user/crt0.o /tmp/kikios_mount/lib/tcc/lib/crt1.o
	@sudo cp $(BUILD_DIR)/user/crt0.o /tmp/kikios_mount/lib/tcc/lib/Scrt1.o
	@sudo cp $(BUILD_DIR)/user/crti.o /tmp/kikios_mount/lib/tcc/lib/
	@sudo cp $(BUILD_DIR)/user/crtn.o /tmp/kikios_mount/lib/tcc/lib/
	@sudo cp tinycc/kikios/libtcc1.a /tmp/kikios_mount/lib/tcc/lib/
	@sudo cp tinycc/kikios/libc.a /tmp/kikios_mount/lib/tcc/lib/
	@sudo cp tinycc/kikios/libc.a /tmp/kikios_mount/lib/tcc/lib/libc.so
	@sudo cp user/linker.ld /tmp/kikios_mount/lib/tcc/lib/
	@sudo umount /tmp/kikios_mount
endif

# ============ Run targets ============

run: $(KERNEL_BIN) sync-disk
	$(QEMU) $(QEMU_FLAGS)

run-nographic: $(KERNEL_BIN) sync-disk
	$(QEMU) $(QEMU_FLAGS_NOGRAPHIC)

# Run Pi kernel in QEMU raspi3b (no disk - just tests boot)
run-pi: pi
	$(QEMU) -M raspi3b -kernel $(BUILD_DIR)/kernel8.img -serial stdio -usb -device usb-kbd

# ============ Pi targets ============

pi:
	$(MAKE) TARGET=pi PRINTF=$(PRINTF)

pi-debug:
	$(MAKE) TARGET=pi-debug PRINTF=screen

# Install to Pi SD card
# Usage: make install DISK=/dev/disk4
install: pi user
	@if [ -z "$(DISK)" ]; then \
		echo "Usage: make install DISK=/dev/diskN"; \
		echo ""; \
		echo "Examples:"; \
		echo "  make install DISK=/dev/disk4      # macOS"; \
		echo "  make install DISK=/dev/sda        # Linux"; \
		echo ""; \
		echo "Run 'diskutil list' (macOS) or 'lsblk' (Linux) to find your SD card"; \
		exit 1; \
	fi
	@echo "Installing to $(DISK)..."
	@# Download Pi firmware if not cached
	@mkdir -p $(BUILD_DIR)/firmware
	@if [ ! -f $(BUILD_DIR)/firmware/bootcode.bin ]; then \
		echo "  Downloading Pi boot firmware..."; \
		curl -sL -o $(BUILD_DIR)/firmware/bootcode.bin https://github.com/raspberrypi/firmware/raw/master/boot/bootcode.bin; \
		curl -sL -o $(BUILD_DIR)/firmware/start.elf https://github.com/raspberrypi/firmware/raw/master/boot/start.elf; \
		curl -sL -o $(BUILD_DIR)/firmware/fixup.dat https://github.com/raspberrypi/firmware/raw/master/boot/fixup.dat; \
	fi
	@if [ "$$(uname)" = "Darwin" ]; then \
		echo "  Partitioning disk (MBR + FAT32)..."; \
		diskutil partitionDisk $(DISK) MBR FAT32 VIBEOS 0b; \
		PART=$(DISK)s1; \
		MOUNT=$$(diskutil info $$PART | grep 'Mount Point' | sed 's/.*: *//'); \
		if [ -z "$$MOUNT" ]; then \
			echo "Failed to mount $$PART"; \
			exit 1; \
		fi; \
		echo "  Mounted at $$MOUNT"; \
		COPY="cp"; \
		RSYNC="rsync"; \
		MKDIR="mkdir"; \
	else \
		echo "  Partitioning disk (MBR + FAT32)..."; \
		sudo parted $(DISK) --script mklabel msdos mkpart primary fat32 1MiB 100%; \
		sudo mkfs.vfat -F 32 -n VIBEOS $(DISK)1; \
		mkdir -p /tmp/kikios_sd; \
		sudo mount $(DISK)1 /tmp/kikios_sd; \
		MOUNT=/tmp/kikios_sd; \
		COPY="sudo cp"; \
		RSYNC="sudo rsync"; \
		MKDIR="sudo mkdir"; \
	fi; \
	echo "  Copying boot firmware..."; \
	$$COPY $(BUILD_DIR)/firmware/bootcode.bin $$MOUNT/; \
	$$COPY $(BUILD_DIR)/firmware/start.elf $$MOUNT/; \
	$$COPY $(BUILD_DIR)/firmware/fixup.dat $$MOUNT/; \
	$$COPY firmware/config.txt $$MOUNT/; \
	echo "  Copying kernel..."; \
	$$COPY $(BUILD_DIR)/kernel8.img $$MOUNT/; \
	echo "  Copying userspace..."; \
	$$RSYNC -a $(SYSROOT)/ $$MOUNT/; \
	$$MKDIR -p $$MOUNT/usr/src; \
	$$RSYNC -a --exclude='*.o' --exclude='*.elf' --exclude='build/' user/ $$MOUNT/usr/src/user/; \
	$$RSYNC -a --exclude='*.o' --exclude='build/' tinycc/ $$MOUNT/usr/src/tinycc/; \
	$$MKDIR -p $$MOUNT/lib/tcc/include; \
	$$MKDIR -p $$MOUNT/lib/tcc/lib; \
	$$COPY -r tinycc/include/* $$MOUNT/lib/tcc/include/; \
	$$COPY tinycc/kikios/tcc_include/* $$MOUNT/lib/tcc/include/ 2>/dev/null || true; \
	$$COPY user/lib/kiki.h $$MOUNT/lib/tcc/include/; \
	$$COPY user/lib/gfx.h $$MOUNT/lib/tcc/include/ 2>/dev/null || true; \
	$$COPY $(BUILD_DIR)/user/crt0.o $$MOUNT/lib/tcc/lib/crt1.o; \
	$$COPY $(BUILD_DIR)/user/crt0.o $$MOUNT/lib/tcc/lib/Scrt1.o; \
	$$COPY $(BUILD_DIR)/user/crti.o $$MOUNT/lib/tcc/lib/; \
	$$COPY $(BUILD_DIR)/user/crtn.o $$MOUNT/lib/tcc/lib/; \
	$$COPY tinycc/kikios/libtcc1.a $$MOUNT/lib/tcc/lib/; \
	$$COPY tinycc/kikios/libc.a $$MOUNT/lib/tcc/lib/; \
	$$COPY tinycc/kikios/libc.a $$MOUNT/lib/tcc/lib/libc.so; \
	$$COPY user/linker.ld $$MOUNT/lib/tcc/lib/; \
	if [ "$$(uname)" = "Darwin" ]; then \
		dot_clean $$MOUNT 2>/dev/null || true; \
		find $$MOUNT -name '._*' -delete 2>/dev/null || true; \
		find $$MOUNT -name '.DS_Store' -delete 2>/dev/null || true; \
		diskutil unmount $$MOUNT; \
	else \
		sudo umount $$MOUNT; \
	fi; \
	echo "Done! SD card ready."

# ============ Utility targets ============

debug: $(KERNEL_BIN)
	$(QEMU) $(QEMU_FLAGS) -S -s

disasm: $(KERNEL_ELF)
	$(OBJDUMP) -d $<

clean:
	rm -rf $(BUILD_DIR)
	rm -f $(SYSROOT)/bin/*
	$(MAKE) -C tinycc/kikios clean 2>/dev/null || true
	$(MAKE) -C micropython/ports/kikios clean 2>/dev/null || true

distclean: clean
	rm -f $(DISK_IMG)

check-toolchain:
ifeq ($(UNAME_S),Darwin)
	@which $(CC) > /dev/null 2>&1 || \
	(echo "Cross-compiler not found. Install with: brew install aarch64-elf-gcc" && exit 1)
else
	@which $(CC) > /dev/null 2>&1 || \
	(echo "Cross-compiler not found. Install with:" && \
	 echo "  Ubuntu/Debian: sudo apt install gcc-aarch64-linux-gnu" && \
	 echo "  Fedora: sudo dnf install gcc-aarch64-linux-gnu" && \
	 echo "  Arch: sudo pacman -S aarch64-linux-gnu-gcc" && exit 1)
endif
