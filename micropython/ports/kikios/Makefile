# MicroPython port for KikiOS
# Builds a PIE binary for KikiOS userspace

include ../../py/mkenv.mk

# Cross-compilation for aarch64 (inherit from parent or auto-detect)
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    CROSS_COMPILE ?= aarch64-elf-
else
    CROSS_COMPILE ?= aarch64-linux-gnu-
endif

# qstr definitions (must come before including py.mk)
QSTR_DEFS = qstrdefsport.h

# Disable some features for minimal size
MICROPY_ROM_TEXT_COMPRESSION ?= 0

# include py core make definitions
include $(TOP)/py/py.mk
# extmod removed - we don't use extended modules

INC += -I.
INC += -I$(TOP)
INC += -I$(BUILD)
INC += -I$(TOP)/extmod
INC += -I$(TOP)/lib
INC += -I../../../user/lib
INC += -I../../../kernel/libc

# Compiler flags for KikiOS userspace (PIE for ELF loading)
CFLAGS += $(INC) -ffreestanding -nostdlib -nostartfiles
CFLAGS += -mcpu=cortex-a72 -mstrict-align
CFLAGS += -fPIE
CFLAGS += -Wall -Wno-unused-variable -Wno-unused-function -std=gnu99
CFLAGS += -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers
CFLAGS += -Wno-implicit-function-declaration -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast
CFLAGS += -Wno-int-conversion
CFLAGS += -O0 -DNDEBUG
CFLAGS += -fdata-sections -ffunction-sections
CFLAGS += -include stddef.h -include stdint.h -include stdbool.h -include string.h -include errno.h -include stdio.h -include setjmp.h -include math.h

# Linker flags (override inherited LDFLAGS from parent make)
LDFLAGS = -nostdlib -pie -T ../../../user/linker.ld
LDFLAGS += --gc-sections

# Find libgcc for soft-float support
LIBGCC := $(shell $(CC) -print-libgcc-file-name)

# Source files
SRC_C = \
	main.c \
	mphalport.c \
	modkiki.c \
	stubs.c \
	shared/readline/readline.c \
	shared/runtime/pyexec.c \
	shared/runtime/stdout_helpers.c \
	shared/runtime/gchelper_generic.c \

# Extended modules (json, random, heapq, re)
# Note: modre.c #includes the re1.5 .c files directly
SRC_EXTMOD_C = \
	extmod/modjson.c \
	extmod/modrandom.c \
	extmod/modheapq.c \
	extmod/modre.c \

SRC_QSTR += shared/readline/readline.c shared/runtime/pyexec.c modkiki.c $(SRC_EXTMOD_C)

# Assembly sources
SRC_S = setjmp.S

# Use crt0 from KikiOS userspace
CRT0 = ../../../user/lib/crt0.S
CRT0_OBJ = $(BUILD)/crt0.o

OBJ += $(PY_CORE_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_EXTMOD_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_S:.S=.o))

all: $(BUILD)/micropython.elf

$(BUILD):
	$(Q)mkdir -p $@

$(CRT0_OBJ): $(CRT0) | $(BUILD)
	$(CC) -c $< -o $@

$(BUILD)/%.o: %.S | $(BUILD)
	$(ECHO) "AS $<"
	$(Q)$(CC) -c $< -o $@

$(BUILD)/micropython.elf: $(CRT0_OBJ) $(OBJ)
	$(ECHO) "LINK $@"
	$(Q)$(LD) $(LDFLAGS) -o $@ $^ $(LIBGCC)
	$(Q)$(SIZE) $@

include $(TOP)/py/mkrules.mk
