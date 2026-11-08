# zto2: NT-Style Hybrid Operating System Core, Design Spec

Date: 2026-11-08
Status: Approved
Core name: **zto2** (the kernel and executive described here). The name of the full OS built on top of it is still open.

## 1. Intent

Build a real, bootable, non-toy x86_64 operating system, designed core-first. The core is a layered hybrid kernel in the style of Windows NT. It borrows from three families:

- Monolithic: drivers and the network stack run in kernel mode.
- Microkernel: a small kernel core, with the window server and subsystems in user mode.
- Modular: layered drivers behind stable interfaces.

Networking (M2) and a graphical desktop (M3) are required later milestones. They must plug into the core without restructuring it.

Success for Milestone 1: the ISO boots (BIOS and UEFI via Limine), brings up the core, loads a PE user-mode shell, and lets the user type commands and read files from disk using a keyboard and mouse (PS/2 and USB HID).

## 2. Non-goals (for now)

- ARM or 32-bit x86 support.
- Binary compatibility with Windows applications.
- Real SMP scheduling in M1. The kernel boots on one core, but locking is written to be SMP-safe.
- Power management, USB mass storage, audio.

## 3. Layers

From bottom to top:

1. **Boot and HAL.** Limine hands over a 64-bit higher-half kernel (ELF). The HAL wraps the APIC/IOAPIC, timers, CPU feature detection and PCI configuration access. Nothing above the HAL touches raw I/O ports or MMIO for these devices.
2. **Kernel core (`Ke`).** Preemptive scheduler, interrupt dispatch, DPCs, spinlocks, events and wait objects. No knowledge of files, processes or security.
3. **Executive.**
   - `Ob`: object manager (typed objects, handles, namespace).
   - `Mm`: memory manager.
   - `Ps`: process and thread manager.
   - `Se`: security tokens (minimal in M1: a token exists per process).
   - `Io`: I/O manager.
4. **Drivers.** Kernel-mode, layered, communicating through I/O request packets (IRPs).
5. **User mode.** Native syscall stub library, a session manager, subsystem processes (the shell in M1, the window server in M3).

## 4. Memory manager (`Mm`)

- Physical frames come from the bootloader memory map. A per-frame database tracks state (free, active, shared, kernel).
- 4-level paging. Kernel image in the higher half; all RAM mapped in a direct-map region.
- Kernel pools: paged and non-paged, on a slab-style allocator.
- Each process has its own address space, tracked by a tree of virtual address descriptors (VADs).
- Page faults: demand-zero in M1b; copy-on-write added when needed by `fork`-style or shared mappings.
- Section objects provide shared memory between processes. These are required for the M3 window server (shared surfaces).

## 5. Processes, threads, scheduling (`Ps`, `Ke`)

- A process object owns an address space, a handle table and a security token.
- A thread object owns a kernel stack, a user stack, a priority 0 to 31 and a quantum.
- Round-robin within each priority; higher priority preempts. Preemption is driven by the APIC timer through a DPC.
- Wait objects: events, mutexes, semaphores, timers. Threads block on handles via a wait syscall.

## 6. System-call interface and executables

- Entry through `syscall`/`sysret` into a numbered system-service table.
- Services take handles, not raw kernel pointers. User pointers are probed and captured before use. Failures return NT-style status codes.
- User programs are **PE (x86_64) executables**. The kernel includes a PE loader (sections, relocations, imports resolved against the native syscall stub library).
- The kernel image itself remains ELF for the bootloader.

## 7. I/O manager and drivers (`Io`)

- Objects: driver, device and file objects.
- Requests are IRPs passed down a stack of layered drivers (bus, function, filter) and completed back up, synchronously or asynchronously.
- The object manager namespace is NT-style, for example `\Device\Disk0`, `\Device\Keyboard0`, with symbolic links for drive letters.
- Filesystem drivers plug in through a filesystem interface. M1: boot ramdisk, then FAT32 read/write.
- Unrecoverable kernel errors raise a bugcheck: a code on screen and on serial.

### Milestone 1 drivers

- Serial (COM1) debug log
- PCI bus driver
- Framebuffer console
- RAM disk
- virtio-blk and AHCI storage
- PS/2 keyboard and mouse
- xHCI USB host controller with HID keyboard and mouse (last in M1; cut-able if it slips)

### Hooks for later milestones

- Networking: network-adapter driver interface, TCP/IP driver, socket interface.
- GUI: framebuffer surfaces as shared sections, input event queue, user-mode window server.

## 8. Language and build

- C (gnu17) with NASM assembly for the kernel.
- Rust (`no_std`, target `x86_64-unknown-none`, C ABI) for one or two leaf components as an alternative implementation selectable at build time: the PCI bus enumerator and a simple storage driver (RAM disk).
- Build: make; gcc for the kernel; nasm; a PE cross-toolchain (mingw-w64 or clang + lld-link) for user programs; xorriso to produce a hybrid BIOS/UEFI ISO with the Limine bootloader.

## 9. Testing

- Host-side unit tests for pure logic: frame allocator, pool allocator, object manager and handle table, scheduler queues, PE parsing.
- In-kernel self-tests that print PASS/FAIL markers over serial.
- Headless QEMU boot checked by a script that waits for a pass marker on the serial console.
- If QEMU cannot be installed in the build environment, verification is limited to a clean build plus host-side tests, and the ISO is handed over for the user to boot.

## 10. Milestones

| Milestone | Scope |
|---|---|
| M1a | Boot, HAL, serial log, interrupts, scheduler |
| M1b | Mm, Ob, Ps, syscalls, PE loader, user-mode shell |
| M1c | Io, PCI, RAM disk, virtio-blk/AHCI, FAT32 |
| M1d | PS/2 keyboard and mouse, then xHCI USB HID |
| M2 | Network adapter driver, TCP/IP, sockets |
| M3 | Window server, compositor, input routing |

## 11. Risks and assumptions

- The sandbox currently has gcc, nasm, ld and make, but no QEMU, xorriso or GRUB. ISO creation and boot tests depend on installing these or on the user booting the ISO.
- A PE cross-toolchain and Rust toolchain have not been checked yet.
- xHCI USB is the largest single item in M1. It is last so the rest of M1 is unaffected if it slips.
- This is a multi-milestone effort. Only M1a is planned in the first implementation plan.
