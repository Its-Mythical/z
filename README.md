# zto2

zto2 is the core of an NT-style hybrid operating system, written from scratch for x86_64 in C and NASM. It boots through Limine on BIOS and UEFI and is tested under QEMU. The name of the full OS is still open.

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

GCC, NASM, GNU make, git, xorriso, mtools and QEMU (`qemu-system-x86_64`). Limine (branch `v9.x-binary`) is fetched by the Makefile into `tools/limine` on the first ISO build, so that build needs network access. After that `make clean` keeps it; `make distclean` removes it too.

On Debian or Ubuntu:

```sh
apt-get update
apt-get install -y qemu-system-x86 xorriso mtools nasm
```

(plus `gcc`, `make` and `git` if they are not installed yet).

## Build and test

- `make iso`: build the BIOS+UEFI hybrid ISO `build/zto2.iso` (`make kernel` builds only `build/zto2.elf`)
- `make test-host`: host unit tests (`tests/host/test_X.c`)
- `make test-boot`: boot the existing ISO headless in QEMU and wait for the serial marker `ZTO2-SELFTEST: PASS`. It fails on `ZTO2-SELFTEST: FAIL` or a bugcheck, times out after 30 s, and exits 77 (skipped) if QEMU is missing. `ZTO2_EXPECT` (a regex) overrides the expected line.
- `make check`: build the ISO, then run `test-host` and `test-boot`. A full boot takes a few seconds.
- `make clean`: remove `build/`; `make distclean`: also remove `tools/limine`

### Double-fault regression (manual)

A second boot test deliberately triggers a double fault and expects the kernel to bugcheck with code 0x7F (UNEXPECTED_KERNEL_MODE_TRAP, first parameter 8). It is not part of `make check`, because it needs a special build:

```sh
make iso ZTO2_FAULT_TEST=df
ZTO2_EXPECT='BUGCHECK 0x7f \(0x8,' make test-boot
make iso                      # rebuild the normal ISO
```

The fault test never prints the PASS marker. After a bugcheck the kernel halts, so this run lasts until the runner's 30 s timeout.

## Booting the ISO

In QEMU, with the debug log on your terminal:

```sh
qemu-system-x86_64 -cdrom build/zto2.iso -serial stdio -m 256
```

From a USB stick, which boots on BIOS and UEFI machines. Warning: `dd` overwrites the target device without asking. Check the device name with `lsblk` before you run it, and make sure it is the stick and not a disk you want to keep. Replace `/dev/sdX` with the whole device, not a partition:

```sh
sudo dd if=build/zto2.iso of=/dev/sdX bs=4M conv=fsync status=progress
```

On real hardware the serial log is only visible if the machine has a COM1 port and you attach to it. The kernel has no screen output yet, and it simply halts after the self-tests.
