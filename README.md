# zto2

From-scratch x86_64 NT-style hybrid kernel (C + NASM, Limine, QEMU).

## Build tools

`gcc`, `nasm`, `make`, `git`, `xorriso`, `mtools`, `qemu-system-x86_64`
(Debian/Ubuntu: `apt-get install gcc nasm make git xorriso mtools qemu-system-x86`).
Limine (branch `v9.x-binary`) is fetched into `tools/limine` on first build.

## Targets

- `make kernel`: build `build/zto2.elf`
- `make iso`: build the BIOS+UEFI hybrid ISO `build/zto2.iso`
- `make test-host`: host unit tests (`tests/host/test_X.c`, extra sources via `HOST_SRCS_test_X`)
- `make test-boot`: boot the ISO in QEMU (does not build it); `ZTO2_EXPECT` is a regex, default `ZTO2-SELFTEST: PASS`. Exits 77 (skipped) if QEMU is missing.
