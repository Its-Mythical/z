# zto2

zto2 is the core of an NT-style hybrid operating system, written from scratch for x86_64 in C and NASM. It boots through Limine; the ISO is a BIOS+UEFI hybrid. The automated tests boot it under QEMU with BIOS firmware. The UEFI path was checked by hand once with OVMF and is not covered by the automated tests. The name of the full OS is still open.

## Milestones

| Milestone | Scope | Status |
|---|---|---|
| M1a | Boot, HAL, serial log, interrupts, scheduler | implemented |
| M1b | Mm, Ob, Ps, syscalls, PE loader, user-mode shell | planned |
| M1c | Io, PCI, RAM disk, virtio-blk/AHCI, FAT32 | planned |
| M1d | PS/2 keyboard and mouse, then xHCI USB HID | planned |
| M2 | Network adapter driver, TCP/IP, sockets | planned |
| M3 | Window server, compositor, input routing | planned |

What M1a gives you today: a kernel that boots from the ISO, logs over COM1 (115200 8N1), runs its own interrupts (GDT/TSS/IDT, LAPIC timer), bugchecks on unrecoverable errors, and runs a preemptive priority scheduler with an idle thread. On boot it runs a set of in-kernel self-tests and reports the result on serial. There is no memory manager, user mode, filesystem or input yet.

## Build prerequisites

An x86_64 Linux host with GCC (the kernel is compiled with the host `gcc`; there is no cross compiler), NASM, GNU make, git, xorriso, mtools and QEMU (`qemu-system-x86_64`). Limine is pinned to release `v9.6.7-binary` and is fetched by any `make iso` into `tools/limine` the first time, so that build needs network access. `make kernel` and `make test-host` do not use Limine or the network: the kernel is built against the committed `include/limine.h`, and the ISO build checks that it matches the pinned release. `make clean` keeps `tools/limine`; `make distclean` removes it too.

On Debian or Ubuntu:

```sh
apt-get update
apt-get install -y qemu-system-x86 xorriso mtools nasm
```

(plus `gcc`, `make` and `git` if they are not installed yet).

## Build and test

- `make iso`: build the BIOS+UEFI hybrid ISO `build/zto2.iso` (`make kernel` builds only `build/zto2.elf`)
- `make test-host`: host unit tests (`tests/host/test_X.c`)
- `make test-boot`: boot the existing ISO headless in QEMU. It requires the serial marker `ZTO2-SELFTEST: PASS` and a `ZTO2-SELFTEST: ok <name>` line for every self-test listed in `tests/boot/expected_selftests.txt` (add a name there when you add a self-test). It needs QEMU to exit with status 0 or 33 (the kernel's clean exit), fails on `ZTO2-SELFTEST: FAIL`, on a bugcheck and on a hang (30 s timeout), and exits 77 (skipped) if QEMU is missing. Setting `ZTO2_EXPECT` (a regex) replaces the default check; a timeout is then accepted only if the regex mentions `BUGCHECK`.
- `make test-runner`: checks the boot-test runner itself against a stub QEMU (no QEMU or ISO needed).
- `make test-df`: double-fault regression, see below.
- `make check`: build the ISO, then run `test-host`, `test-runner`, `test-boot` and `test-df`. It takes about ten seconds.
- `make clean`: remove `build/`; `make distclean`: also remove `tools/limine`

### Double-fault regression

`make test-df` builds a special ISO (`ZTO2_FAULT_TEST=df`) whose self-tests end by deliberately triggering a double fault, expects the kernel to bugcheck with code 0x7F (UNEXPECTED_KERNEL_MODE_TRAP, first parameter 8) on the dedicated IST1 stack, and then rebuilds the normal ISO, also if the test failed. In that build only, `KeBugCheck` exits QEMU after printing, so the test takes a few seconds; the normal build still halts after a bugcheck. The test fails if the PASS marker is ever printed. It is part of `make check`.

## Booting the ISO

In QEMU, with the debug log on your terminal:

```sh
qemu-system-x86_64 -cdrom build/zto2.iso -serial stdio -m 256
```

From a USB stick (the image is meant to boot on both BIOS and UEFI machines, but only BIOS is covered by the tests). Warning: `dd` overwrites the target device without asking. Check the device name with `lsblk` before you run it, and make sure it is the stick and not a disk you want to keep. Replace `/dev/sdX` with the whole device, not a partition:

```sh
sudo dd if=build/zto2.iso of=/dev/sdX bs=4M conv=fsync status=progress
```

On real hardware the serial log is only visible if the machine has a COM1 port and you attach to it. The kernel has no screen output yet, and it simply halts after the self-tests.
