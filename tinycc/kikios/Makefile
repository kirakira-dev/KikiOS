# TCC for KikiOS - Makefile

# Cross compiler (inherit from parent or auto-detect)
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    CROSS_COMPILE ?= aarch64-elf-
else
    CROSS_COMPILE ?= aarch64-linux-gnu-
endif
CC = $(CROSS_COMPILE)gcc
AS = $(CROSS_COMPILE)as
AR = $(CROSS_COMPILE)ar
LD = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy

# Output
TARGET = tcc
BUILD = build

# Directories
KIKIOS_ROOT = ../..
TCC_ROOT = ..
PORT_DIR = .

# Compiler flags
CFLAGS = -O0 -g -Wall -Wno-unused-variable -Wno-unused-function
CFLAGS += -ffreestanding -nostdlib -nostdinc
CFLAGS += -fPIE -fno-builtin
CFLAGS += -mstrict-align
# FPU is enabled in KikiOS, so we can use floating-point

# libgcc for soft-float long double functions
LIBGCC = $(shell $(CC) -print-file-name=libgcc.a)

# Include paths - our headers first, then TCC's internal headers
CFLAGS += -I$(PORT_DIR)/include
CFLAGS += -I$(PORT_DIR)
CFLAGS += -I$(TCC_ROOT)
CFLAGS += -I$(TCC_ROOT)/include

# Defines for TCC
CFLAGS += -DTCC_TARGET_ARM64
CFLAGS += -DCONFIG_TCC_STATIC
CFLAGS += -DCONFIG_TCC_PIE
CFLAGS += -DONE_SOURCE=0
CFLAGS += -DTCC_VERSION=\"0.9.28-kikios\"
CFLAGS += -DCONFIG_TCCDIR=\"/lib/tcc\"
CFLAGS += -DCONFIG_TCC_SYSINCLUDEPATHS=\"/lib/tcc/include\"
CFLAGS += -DCONFIG_TCC_LIBPATHS=\"/lib/tcc/lib\"
CFLAGS += -DCONFIG_TCC_CRTPREFIX=\"/lib/tcc/lib\"
CFLAGS += -DCONFIG_TCC_ELFINTERP=\"\"
CFLAGS += -DCONFIG_TCC_CROSSPREFIX=\"\"
CFLAGS += -DTCC_LIBTCC1=\"libtcc1.a\"

# Linker flags
LDFLAGS = -nostdlib -pie -T $(KIKIOS_ROOT)/user/linker.ld

# TCC source files (not using ONE_SOURCE, compile separately)
# Note: tccrun.c (JIT execution) is excluded - requires mmap
TCC_SRCS = \
	$(TCC_ROOT)/tcc.c \
	$(TCC_ROOT)/libtcc.c \
	$(TCC_ROOT)/tccpp.c \
	$(TCC_ROOT)/tccgen.c \
	$(TCC_ROOT)/tccdbg.c \
	$(TCC_ROOT)/tccelf.c \
	$(TCC_ROOT)/tccasm.c \
	$(TCC_ROOT)/arm64-gen.c \
	$(TCC_ROOT)/arm64-link.c \
	$(TCC_ROOT)/arm64-asm.c

# Port source files
PORT_SRCS = \
	$(PORT_DIR)/tcc_main.c \
	$(PORT_DIR)/tcc_libc.c

# Assembly files
ASM_SRCS = \
	$(PORT_DIR)/setjmp.S

# CRT
CRT = $(KIKIOS_ROOT)/user/lib/crt0.o

# Object files
TCC_OBJS = $(patsubst $(TCC_ROOT)/%.c,$(BUILD)/tcc_%.o,$(TCC_SRCS))
PORT_OBJS = $(patsubst $(PORT_DIR)/%.c,$(BUILD)/port_%.o,$(PORT_SRCS))
ASM_OBJS = $(patsubst $(PORT_DIR)/%.S,$(BUILD)/%.o,$(ASM_SRCS))

ALL_OBJS = $(TCC_OBJS) $(PORT_OBJS) $(ASM_OBJS)

.PHONY: all clean

all: $(BUILD)/$(TARGET) libtcc1.a libc.a

$(BUILD):
	mkdir -p $(BUILD)

# TCC source flags (rename main to tcc_main)
TCC_CFLAGS = $(CFLAGS) -Dmain=tcc_main

# Compile TCC sources
$(BUILD)/tcc_%.o: $(TCC_ROOT)/%.c | $(BUILD)
	$(CC) $(TCC_CFLAGS) -c $< -o $@

# Compile port sources (no main renaming)
$(BUILD)/port_tcc_main.o: $(PORT_DIR)/tcc_main.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/port_tcc_libc.o: $(PORT_DIR)/tcc_libc.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# Assemble
$(BUILD)/%.o: $(PORT_DIR)/%.S | $(BUILD)
	$(CC) -c $< -o $@

# Link
$(BUILD)/$(TARGET): $(CRT) $(ALL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBGCC)

# Make sure CRT exists
$(CRT):
	$(MAKE) -C $(KIKIOS_ROOT) user/lib/crt0.o

# Runtime libraries for TCC-compiled programs
libtcc1.o: libtcc1.c
	$(CC) $(CFLAGS) -c $< -o $@

libtcc1.a: libtcc1.o
	$(AR) rcs $@ $<

libc_stub.o: libc_stub.c
	$(CC) $(CFLAGS) -c $< -o $@

libc.a: libc_stub.o
	$(AR) rcs $@ $<

clean:
	rm -rf $(BUILD) *.o *.a
