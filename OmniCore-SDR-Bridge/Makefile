# OmniCore SDR bridge: command-line build of all three CH32H417 images and
# the release files. MounRiver Studio can still build V3F/V5F for debugging;
# the bootloader and the release images come from here.
#
#   make            build/ : *.elf, *.bin, OmniCore-Bridge-<ver>.ocfw, Merge.bin
#   make flash      write Merge.bin with WCH-Link (OpenOCD from MounRiver)
#   make clean
#
# Run from Git Bash (or any POSIX shell). TOOLCHAIN/OPENOCD default to the
# MounRiver Studio 2 install.

MRS      ?= C:/MounRiver/MounRiver_Studio2/resources/app/resources/win32/components/WCH
TOOLCHAIN?= $(MRS)/Toolchain/RISC-V Embedded GCC15/bin
OPENOCD  ?= $(MRS)/OpenOCD/OpenOCD/bin
PYTHON   ?= python

CC      := "$(TOOLCHAIN)/riscv32-wch-elf-gcc"
OBJCOPY := "$(TOOLCHAIN)/riscv32-wch-elf-objcopy"
SIZE    := "$(TOOLCHAIN)/riscv32-wch-elf-size"

BUILD    := build
# Only this directory decides "-dirty": the rest of the repository (Vivado,
# SDK) does not change the CH32 images.
BUILD_ID := $(shell git describe --always --abbrev=7 2>/dev/null)$(shell git diff --quiet HEAD -- . 2>/dev/null || echo -dirty)
BUILD_ID := $(shell echo "$(BUILD_ID)" | cut -c1-15)
ifeq ($(BUILD_ID),)
BUILD_ID := unknown
endif

ARCH   := -march=rv32imac_zba_zbb_zbc_zbs_xw -mabi=ilp32 -msmall-data-limit=8 -msave-restore
CFLAGS := $(ARCH) -fmax-errors=20 -Os -fmessage-length=0 -fsigned-char -ffunction-sections \
          -fdata-sections -fno-common -Wunused -Wuninitialized -g -gdwarf-4 -std=gnu99 -MMD -MP \
          -DOMNICORE_BUILD_ID='"$(BUILD_ID)"'
ASFLAGS:= $(ARCH) -g -x assembler-with-cpp -MMD -MP
LDFLAGS:= $(ARCH) -nostartfiles -Wl,--gc-sections --specs=nano.specs --specs=nosys.specs

PERIPH := $(wildcard SRC/Peripheral/src/*.c) SRC/Core/core_riscv.c Common/Debug/debug.c
INC    := -ICommon -ICommon/Debug -ISRC/Core -ISRC/Peripheral/inc

# V3F stage-0 (same file set as the MRS V3F project).
V3F_SRC := $(wildcard V3F/User/*.c) $(PERIPH) Common/ocfw.c Common/usb_ep3_debug.c \
           Common/usbss_uart1_bridge.c Common/vcxo_discipline.c
V3F_ASM := SRC/Startup/startup_ch32h417_v3f.S
V3F_DEF := -DCore_V3F -IV3F/User $(INC)
V3F_LD  := Common/Ld/V3F/Link_v3f.ld

# V5F application.
APP_SRC := $(wildcard V5F/User/*.c) $(PERIPH) $(wildcard Common/*.c)
APP_ASM := SRC/Startup/startup_ch32h417_v5f.S
APP_DEF := -DCore_V5F -IV5F/User $(INC)
APP_LD  := Common/Ld/V5F/Link_v5f.ld
APP_LIB := -LCommon -lUHSIF -lm

# V5F bootloader.
BOOT_SRC := $(wildcard Boot/User/*.c) $(PERIPH) Common/ocfw.c Common/usb_desc.c \
            Common/ch32h417_usbss_device.c Common/ch32h417_usbss_it.c Common/ch32h417_usbhs_device.c
BOOT_ASM := SRC/Startup/startup_ch32h417_v5f.S
BOOT_DEF := -DCore_V5F -DOMNICORE_BOOTLOADER -DDEBUG=DEBUG_NONE -IBoot/User $(INC)
BOOT_LD  := Boot/Link_boot.ld

objs = $(patsubst %.c,$(BUILD)/$(1)/%.o,$(2)) $(patsubst %.S,$(BUILD)/$(1)/%.o,$(3))
V3F_OBJ  := $(call objs,v3f,$(V3F_SRC),$(V3F_ASM))
APP_OBJ  := $(call objs,app,$(APP_SRC),$(APP_ASM))
BOOT_OBJ := $(call objs,boot,$(BOOT_SRC),$(BOOT_ASM))

VERSION = $(shell sed -n 's/^\#define OMNICORE_BRIDGE_VERSION_\(MAJOR\|MINOR\|PATCH\) *\([0-9]*\).*/\2/p' Common/fw_version.h | paste -sd.)

all: $(BUILD)/Merge.bin

$(BUILD)/v3f/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(V3F_DEF) -c -o $@ $<
$(BUILD)/v3f/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c -o $@ $<
$(BUILD)/app/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(APP_DEF) -c -o $@ $<
$(BUILD)/app/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c -o $@ $<
$(BUILD)/boot/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(BOOT_DEF) -c -o $@ $<
$(BUILD)/boot/%.o: %.S
	@mkdir -p $(dir $@)
	$(CC) $(ASFLAGS) -c -o $@ $<

$(BUILD)/omnicore_v3f.elf: $(V3F_OBJ) $(V3F_LD)
	$(CC) $(LDFLAGS) -T $(V3F_LD) -Wl,-Map,$(@:.elf=.map) -o $@ $(V3F_OBJ) -lm
	$(SIZE) $@
$(BUILD)/omnicore_app.elf: $(APP_OBJ) $(APP_LD)
	$(CC) $(LDFLAGS) -T $(APP_LD) -Wl,-Map,$(@:.elf=.map) -o $@ $(APP_OBJ) $(APP_LIB)
	$(SIZE) $@
$(BUILD)/omnicore_boot.elf: $(BOOT_OBJ) $(BOOT_LD)
	$(CC) $(LDFLAGS) -T $(BOOT_LD) -Wl,-Map,$(@:.elf=.map) -o $@ $(BOOT_OBJ) -lm
	$(SIZE) $@

$(BUILD)/%.bin: $(BUILD)/%.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/Merge.bin: $(BUILD)/omnicore_v3f.bin $(BUILD)/omnicore_boot.bin $(BUILD)/omnicore_app.bin tools/ocfw_pack.py
	$(PYTHON) tools/ocfw_pack.py --version $(VERSION) --build-id "$(BUILD_ID)" \
	    --v3f $(BUILD)/omnicore_v3f.bin --boot $(BUILD)/omnicore_boot.bin --app $(BUILD)/omnicore_app.bin \
	    --ocfw $(BUILD)/OmniCore-Bridge-$(VERSION).ocfw --merge $@

flash: $(BUILD)/Merge.bin
	cd "$(OPENOCD)" && ./openocd.exe -f wch-dual-core.cfg -c "chip_id CH32H417" -c page_erase -c init \
	    -c "targets wch_riscv.cpu.0" -c halt \
	    -c "flash write_image erase $(abspath $(BUILD))/Merge.bin 0x00000000" \
	    -c "verify_image $(abspath $(BUILD))/Merge.bin 0x00000000" -c wlink_reset_resume -c exit

clean:
	rm -rf $(BUILD)

.PHONY: all flash clean
.SECONDARY:
-include $(V3F_OBJ:.o=.d) $(APP_OBJ:.o=.d) $(BOOT_OBJ:.o=.d)
