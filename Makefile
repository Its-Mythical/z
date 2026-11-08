# zto2 build. Targets: kernel, iso, test-host, test-boot, clean.

LIMINE_BRANCH ?= v9.x-binary
LIMINE_DIR    := tools/limine
LIMINE_REPO   := https://github.com/limine-bootloader/limine.git

BUILD   := build
KERNEL  := $(BUILD)/zto2.elf
ISO     := $(BUILD)/zto2.iso
ISODIR  := $(BUILD)/iso

CC   ?= gcc
NASM ?= nasm

KCFLAGS := -std=gnu17 -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
           -mno-red-zone -mcmodel=kernel -mno-sse -mno-mmx -mno-80387 \
           -Wall -Wextra -Werror -O2 -g -Iinclude -MMD -MP
KLDFLAGS := -nostdlib -static -no-pie -z max-page-size=0x1000 -T linker.ld

KSRC_C   := $(shell find src -name '*.c' 2>/dev/null | sort)
KSRC_ASM := $(shell find src -name '*.asm' 2>/dev/null | sort)
KOBJ     := $(KSRC_C:%.c=$(BUILD)/obj/%.o) $(KSRC_ASM:%.asm=$(BUILD)/obj/%.o)

.PHONY: all kernel iso test-host test-boot clean limine
all: iso

kernel: $(KERNEL)

$(KERNEL): $(KOBJ) linker.ld
	@mkdir -p $(dir $@)
	$(CC) $(KCFLAGS) $(KLDFLAGS) -o $@ $(KOBJ)

$(BUILD)/obj/%.o: %.c include/limine.h
	@mkdir -p $(dir $@)
	$(CC) $(KCFLAGS) -c $< -o $@

$(BUILD)/obj/%.o: %.asm
	@mkdir -p $(dir $@)
	$(NASM) -f elf64 -g -F dwarf -Iinclude/ $< -o $@

-include $(KOBJ:.o=.d)

# --- Limine ---------------------------------------------------------------
limine: $(LIMINE_DIR)/limine

$(LIMINE_DIR)/limine.h:
	@command -v git >/dev/null 2>&1 || { echo "error: git is required to fetch Limine"; exit 1; }
	git clone --depth=1 -b $(LIMINE_BRANCH) $(LIMINE_REPO) $(LIMINE_DIR) || \
	  { echo "error: could not fetch Limine ($(LIMINE_REPO), branch $(LIMINE_BRANCH)); check network access"; \
	    rm -rf $(LIMINE_DIR); exit 1; }

$(LIMINE_DIR)/limine: $(LIMINE_DIR)/limine.h
	$(MAKE) -C $(LIMINE_DIR)

include/limine.h: $(LIMINE_DIR)/limine.h
	cp $< $@

# --- ISO ------------------------------------------------------------------
iso: $(ISO)

$(ISO): $(KERNEL) limine.conf $(LIMINE_DIR)/limine
	@command -v xorriso >/dev/null 2>&1 || { echo "error: xorriso not found; install xorriso (see README.md)"; exit 1; }
	rm -rf $(ISODIR)
	mkdir -p $(ISODIR)/boot/limine $(ISODIR)/EFI/BOOT
	cp $(KERNEL) $(ISODIR)/boot/zto2.elf
	cp limine.conf $(ISODIR)/boot/limine/limine.conf
	cp $(LIMINE_DIR)/limine-bios.sys $(LIMINE_DIR)/limine-bios-cd.bin \
	   $(LIMINE_DIR)/limine-uefi-cd.bin $(ISODIR)/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(LIMINE_DIR)/BOOTIA32.EFI $(ISODIR)/EFI/BOOT/
	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
	    -no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
	    -apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
	    -efi-boot-part --efi-boot-image --protective-msdos-label \
	    $(ISODIR) -o $@
	$(LIMINE_DIR)/limine bios-install $@

# --- Tests ----------------------------------------------------------------
# Host unit tests: tests/host/test_X.c is built with the sources listed in
# HOST_SRCS_test_X (e.g. HOST_SRCS_test_format := src/lib/format.c), set in
# this Makefile by the task that adds the test. Zero tests is a pass.
HOST_CC     ?= gcc
HOST_CFLAGS := -std=gnu17 -Wall -Wextra -Werror -O0 -g -Iinclude -Itests/host
HOST_TESTS  := $(sort $(basename $(notdir $(wildcard tests/host/test_*.c))))

test-host: $(HOST_TESTS:%=$(BUILD)/host/%)
	@set -e; for t in $(HOST_TESTS); do \
	    echo "== $$t"; $(BUILD)/host/$$t; \
	done; echo "test-host: $(words $(HOST_TESTS)) test(s) passed"


.SECONDEXPANSION:
$(BUILD)/host/%: tests/host/%.c $$(HOST_SRCS_$$*)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) $^ -o $@

# Boots build/zto2.iso (does not build it); override ZTO2_EXPECT to match a
# different serial line. Exit 77 means QEMU is unavailable (skipped).
test-boot:
	@tests/boot/run_qemu.sh

clean:
	rm -rf $(BUILD)
