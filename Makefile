# zto2 build. Targets: kernel, iso, test-host, test-runner, test-boot, test-df, check, clean, distclean.

# Limine is pinned to one release (tag and the commit it must resolve to), so a
# fresh clone cannot drift with the upstream branch. Only 'make iso' fetches it,
# for the bootloader binaries. include/limine.h is committed and authoritative
# for the kernel build; the ISO build checks it against the pinned release.
LIMINE_TAG    := v9.6.7-binary
LIMINE_COMMIT := ee5d29cd0a8034612dcd1df3f00052480db785c5
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

# Optional boot fault tests: make iso ZTO2_FAULT_TEST=df defines
# ZTO2_FAULT_TEST_df (see src/ke/selftest.c) and ZTO2_FAULT_TEST, which makes
# KeBugCheck exit QEMU (src/ke/bugcheck.c). Not set in the default build.
ZTO2_FAULT_TEST ?=
ifneq ($(ZTO2_FAULT_TEST),)
KCFLAGS += -DZTO2_FAULT_TEST_$(ZTO2_FAULT_TEST)=1 -DZTO2_FAULT_TEST=1
endif

KLDFLAGS := -nostdlib -static -no-pie -z max-page-size=0x1000 -T linker.ld

KSRC_C   := $(shell find src -name '*.c' 2>/dev/null | sort)
KSRC_ASM := $(shell find src -name '*.asm' 2>/dev/null | sort)
KOBJ     := $(KSRC_C:%.c=$(BUILD)/obj/%.o) $(KSRC_ASM:%.asm=$(BUILD)/obj/%.o)

.PHONY: all kernel iso test-host test-runner test-boot test-df check clean distclean limine FORCE
all: iso

kernel: $(KERNEL)

$(KERNEL): $(KOBJ) linker.ld
	@mkdir -p $(dir $@)
	$(CC) $(KCFLAGS) $(KLDFLAGS) -o $@ $(KOBJ)

# Rewritten only when the compiler flags change, so objects rebuild on a flag switch.
FLAGSTAMP := $(BUILD)/kcflags.stamp
$(FLAGSTAMP): FORCE
	@mkdir -p $(dir $@)
	@echo '$(KCFLAGS)' | cmp -s - $@ || echo '$(KCFLAGS)' > $@
FORCE:

$(BUILD)/obj/%.o: %.c include/limine.h $(FLAGSTAMP)
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
	git clone --depth=1 -b $(LIMINE_TAG) $(LIMINE_REPO) $(LIMINE_DIR) || \
	  { echo "error: could not fetch Limine ($(LIMINE_REPO), tag $(LIMINE_TAG)); check network access"; \
	    rm -rf $(LIMINE_DIR); exit 1; }
	@[ "$$(git -C $(LIMINE_DIR) rev-parse HEAD)" = "$(LIMINE_COMMIT)" ] || \
	  { echo "error: tag $(LIMINE_TAG) no longer resolves to $(LIMINE_COMMIT); refusing the fetched Limine"; \
	    rm -rf $(LIMINE_DIR); exit 1; }

$(LIMINE_DIR)/limine: $(LIMINE_DIR)/limine.h
	$(MAKE) -C $(LIMINE_DIR)

# Run by the ISO build, the only place tools/limine is used: the checkout must
# be the pinned commit and its header must equal the committed include/limine.h.
define LIMINE_VERIFY
@[ "$$(git -C $(LIMINE_DIR) rev-parse HEAD 2>/dev/null)" = "$(LIMINE_COMMIT)" ] || \
  { echo "error: $(LIMINE_DIR) is not Limine $(LIMINE_TAG) ($(LIMINE_COMMIT)); run 'make distclean' and rebuild"; exit 1; }
@cmp -s include/limine.h $(LIMINE_DIR)/limine.h || \
  { echo "error: include/limine.h differs from $(LIMINE_DIR)/limine.h ($(LIMINE_TAG)); update the committed header to match the pinned Limine"; exit 1; }
endef

# --- ISO ------------------------------------------------------------------
iso: $(ISO)

$(ISO): $(KERNEL) limine.conf $(LIMINE_DIR)/limine
	@command -v xorriso >/dev/null 2>&1 || { echo "error: xorriso not found; install xorriso (see README.md)"; exit 1; }
	$(LIMINE_VERIFY)
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
HOST_SRCS_test_format := src/lib/format.c
HOST_SRCS_test_ready_queue := src/ke/ready_queue.c
HOST_TESTS  := $(sort $(basename $(notdir $(wildcard tests/host/test_*.c))))

test-host: $(HOST_TESTS:%=$(BUILD)/host/%)
	@set -e; for t in $(HOST_TESTS); do \
	    echo "== $$t"; $(BUILD)/host/$$t; \
	done; echo "test-host: $(words $(HOST_TESTS)) test(s) passed"


.SECONDEXPANSION:
$(BUILD)/host/%: tests/host/%.c $$(HOST_SRCS_$$*)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) $^ -o $@

# Boots build/zto2.iso (does not build it) and requires the PASS marker plus an
# 'ok' line for every name in tests/boot/expected_selftests.txt; overriding
# ZTO2_EXPECT (a regex) matches a different serial line instead. Exit 77 means
# QEMU is unavailable (skipped).
test-boot:
	@tests/boot/run_qemu.sh

# Self-test of the boot-test runner against a stub QEMU (needs no QEMU or ISO).
test-runner:
	@tests/boot/test_runner.sh

# Double-fault regression: builds the fault ISO (ZTO2_FAULT_TEST=df), expects a
# bugcheck 0x7f with first parameter 8 (#DF reaching KeBugCheck on the IST1
# stack), then rebuilds the normal ISO whatever the outcome.
test-df:
	@rc=0; \
	$(MAKE) --no-print-directory iso ZTO2_FAULT_TEST=df && \
	    ZTO2_EXPECT='BUGCHECK 0x7f \(0x8,' tests/boot/run_qemu.sh || rc=$$?; \
	$(MAKE) --no-print-directory iso || { [ $$rc -ne 0 ] || rc=$$?; }; \
	exit $$rc

# Builds the ISO, then runs the host tests, the runner self-test, the boot test
# and the double-fault test (which leaves the normal ISO in place).
check: iso
	$(MAKE) --no-print-directory test-host
	$(MAKE) --no-print-directory test-runner
	$(MAKE) --no-print-directory test-boot
	$(MAKE) --no-print-directory test-df

# clean keeps the fetched Limine so a rebuild needs no network.
clean:
	rm -rf $(BUILD)

distclean: clean
	rm -rf $(LIMINE_DIR)
