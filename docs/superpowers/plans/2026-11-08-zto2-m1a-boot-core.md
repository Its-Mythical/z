# zto2 M1a: Boot, HAL, Serial, Interrupts, Scheduler Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Limine-bootable x86_64 kernel that logs over COM1, handles exceptions with a bugcheck, takes APIC timer interrupts, and preemptively schedules threads, verified by a serial PASS marker.

**Architecture:** Higher-half ELF kernel entered directly in long mode by Limine. HAL (`src/hal`) owns serial, GDT/TSS/IDT and the APIC; Ke (`src/ke`) owns spinlocks, DPCs, threads and the scheduler. Pure logic (formatter, ready queue) is host-testable; hardware behavior is proven by an in-kernel self-test in headless QEMU.

**Tech Stack:** C (gnu17), NASM, GNU make, Limine v9 (BIOS+UEFI), xorriso, QEMU.

**Spec:** `docs/superpowers/specs/2026-11-08-nt-hybrid-os-design.md` (sections 3, 5, 8, 9, milestone M1a)

## Global Constraints

- x86_64 only; kernel is C `-std=gnu17` freestanding (`-ffreestanding -mno-red-zone -mcmodel=kernel -mno-sse -mno-mmx`) plus NASM.
- Kernel image is ELF, linked in the higher half (`0xFFFFFFFF80000000`).
- ISO boots via Limine on both BIOS and UEFI.
- Boots on one core; all locks must be SMP-safe (interrupts disabled while a spinlock is held).
- Thread priorities 0 to 31; round-robin within a priority; higher priority preempts; preemption is driven by the APIC timer through a DPC.
- Status codes use NT values: `Z_SUCCESS 0x00000000`, `Z_INVALID_PARAMETER 0xC000000D`, `Z_BUFFER_TOO_SMALL 0xC0000023`, `Z_INSUFFICIENT_RESOURCES 0xC000009A`.
- Debug log is COM1 (`0x3F8`); tests report `ZTO2-SELFTEST: PASS` or `ZTO2-SELFTEST: FAIL <name>` on serial.
- GDT layout is fixed now so M1b `syscall`/`sysret` needs no rework: `0x08` kernel code, `0x10` kernel data, `0x18` user data, `0x20` user code, `0x28` TSS.
- Every commit message ends with the two trailer lines `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>` and `Claude-Session: https://claude.ai/code/session_01KzVmg79pQL4WpuLVpaQRUc`.

## Review Focus

- Bootloader requests return NULL (no HHDM/memmap response): kernel must bugcheck with a message, not fault. Pinned in Task 1.
- `KeFormat` given `%s` with NULL, a buffer too small, `INT64_MIN` with `%lld`, and an unknown specifier: no crash, always NUL-terminated. Pinned in Task 2.
- No UART present or UART never ready: serial writes must give up after a bounded wait, not hang boot. Pinned in Task 2.
- Double fault on a corrupt kernel stack: must reach the bugcheck screen through IST1. Pinned in Task 3.
- All runnable threads blocked or finished: the idle thread must run (never a NULL pick) and a timer tick must not recurse into the scheduler while it is already switching. Pinned in Task 5.

## File Structure

- `Makefile`, `linker.ld`, `limine.conf`: build, link, boot config
- `include/limine.h`: vendored Limine protocol header
- `include/zto2/{types,status,hal,ke}.h`: shared types, status codes, HAL and Ke interfaces
- `src/boot/main.c`: Limine requests and `kmain`
- `src/lib/format.c`: pure formatter (host-testable)
- `src/hal/{serial.c,cpu.c,isr.asm,apic.c}`: serial, GDT/TSS/IDT, interrupt stubs, LAPIC
- `src/ke/{kd.c,bugcheck.c,spinlock.c,ready_queue.c,dpc.c,sched.c,switch.asm,selftest.c}`
- `tests/host/*.c`: host unit tests; `tests/boot/run_qemu.sh`: QEMU boot test

---

### Task 1: Skeleton, build and ISO toolchain

**Files:**
- Create: `Makefile`, `linker.ld`, `limine.conf`, `include/limine.h`, `include/zto2/types.h`, `include/zto2/status.h`, `src/boot/main.c`, `tests/boot/run_qemu.sh`, `.gitignore`
- Test: `tests/boot/run_qemu.sh`

**Interfaces:**
- Produces: `types.h`: `u8,u16,u32,u64,i32,i64,usize`; `status.h`: `typedef i32 ZSTATUS` and the four codes from Global Constraints; `src/boot/main.c`: `void kmain(void)`, `u64 g_HhdmOffset`; Makefile targets `kernel`, `iso`, `test-host`, `test-boot`.

- [ ] **Step 1: Install tools.** Try `apt-get install -y qemu-system-x86 xorriso mtools nasm`. If apt is blocked, record which tools are missing in `README.md` and make `tests/boot/run_qemu.sh` exit 77 (skipped) when `qemu-system-x86_64` is absent.
- [ ] **Step 2: `git init` at repo root; write `.gitignore`** (`build/`, `*.iso`, `tools/limine/`).
- [ ] **Step 3: Write the failing boot test** `tests/boot/run_qemu.sh`: builds nothing; boots `build/zto2.iso` with `qemu-system-x86_64 -cdrom build/zto2.iso -serial stdio -display none -no-reboot -m 256 -device isa-debug-exit,iobase=0xf4,iosize=0x04` under `timeout 30`, and exits 0 only if output contains `ZTO2-SELFTEST: PASS`, exits 77 if QEMU is missing, 1 otherwise.
- [ ] **Step 4: Run `make test-boot`.** Expected: FAIL (no ISO yet) or exit 77 if QEMU is unavailable.
- [ ] **Step 5: Implement the build.** `Makefile` fetches Limine into `tools/limine` from branch `LIMINE_BRANCH ?= v9.x-binary`, copies `limine.h` to `include/`, compiles `src/**/*.c` and `src/**/*.asm`, links with `linker.ld`, and builds a hybrid BIOS/UEFI ISO (`xorriso` plus `limine bios-install`) at `build/zto2.iso` with `limine.conf` (`protocol: limine`, `path: boot():/boot/zto2.elf`).
- [ ] **Step 6: Implement `kmain` in `src/boot/main.c`.** Declares Limine base revision 3 and HHDM, memmap and framebuffer requests; stores `g_HhdmOffset`; if the HHDM or memmap response is NULL, spin in `cli; hlt` after writing the text `ZTO2: missing boot info` directly to port `0xE9` (QEMU debug console), so the failure is visible without serial. Otherwise halt.
- [ ] **Step 7: Run `make iso` then `make test-boot`.** Expected: ISO builds; test still FAIL (no PASS marker yet) or SKIP. Run `make test-host` with zero tests: exit 0.
- [ ] **Step 8: Commit** `build: skeleton, Limine ISO and boot-test runner`.

---

### Task 2: Formatter, serial driver, KdPrint

**Files:**
- Create: `include/zto2/hal.h`, `src/lib/format.c`, `src/hal/serial.c`, `src/ke/kd.c`
- Test: `tests/host/test_format.c`

**Interfaces:**
- Consumes: `types.h`, `status.h`.
- Produces:
  - `size_t KeFormat(char *buf, size_t cap, const char *fmt, va_list ap)`: snprintf semantics (returns the length that would have been written; always NUL-terminates if `cap > 0`). Supports `%c %s %d %u %x %p %lld %llu %llx %%`; `%s` with NULL prints `(null)`; unknown specifier is copied literally.
  - `void HalSerialInit(void)`, `void HalSerialWrite(const char *s, usize n)` in `hal.h`.
  - `void KdPrint(const char *fmt, ...)` in `ke.h` (formats into a 512-byte buffer, writes to serial).

- [ ] **Step 1: Write failing host tests** in `tests/host/test_format.c`: `test_basic` (`"%d %u %x"` with `-5, 7, 255` gives `"-5 7 ff"`), `test_llx` (`0xDEADBEEFCAFEull` gives `"deadbeefcafe"`), `test_int64_min` (`%lld` of `INT64_MIN` gives `"-9223372036854775808"`), `test_null_string` (`%s` NULL gives `"(null)"`), `test_truncation` (cap 4 with `"abcdefgh"` returns 8 and buffer is `"abc"` NUL-terminated), `test_cap_zero` (cap 0 writes nothing, returns the full length), `test_unknown_spec` (`"%q"` gives `"%q"`).
- [ ] **Step 2: Run `make test-host`.** Expected: FAIL (`KeFormat` undefined).
- [ ] **Step 3: Implement `KeFormat`** in `src/lib/format.c` with no libc dependency.
- [ ] **Step 4: Run `make test-host`.** Expected: all PASS.
- [ ] **Step 5: Implement serial** in `src/hal/serial.c`: 115200 8N1 on `0x3F8`; `HalSerialWrite` waits for transmit-holding-empty for at most 100000 polls per byte, and silently drops the byte on timeout (so a missing UART cannot hang boot). Implement `KdPrint` in `src/ke/kd.c`.
- [ ] **Step 6: Call `HalSerialInit()` and `KdPrint("zto2 booting\n")` from `kmain`.** Run `make iso && make test-boot`; expected output contains `zto2 booting` (PASS marker still absent).
- [ ] **Step 7: Commit** `feat: formatter, COM1 serial and KdPrint`.

---

### Task 3: GDT, TSS, IDT, exceptions, bugcheck

**Files:**
- Create: `src/hal/cpu.c`, `src/hal/isr.asm`, `src/ke/bugcheck.c`, `src/ke/selftest.c`
- Modify: `include/zto2/hal.h`, `include/zto2/ke.h`, `src/boot/main.c`
- Test: `src/ke/selftest.c` (`SelfTestBreakpoint`)

**Interfaces:**
- Consumes: `KdPrint`.
- Produces:
  - `typedef struct TrapFrame { u64 r15,r14,r13,r12,r11,r10,r9,r8,rbp,rdi,rsi,rdx,rcx,rbx,rax,vector,error,rip,cs,rflags,rsp,ss; } TrapFrame;`
  - `typedef void (*HalIsr)(TrapFrame *tf);` and `bool HalRegisterInterrupt(u8 vector, HalIsr handler)` (false if taken).
  - `void HalInitCpu(void)`: loads the GDT (layout in Global Constraints), a TSS with `IST1` pointing at a dedicated 16 KiB stack, and an IDT covering vectors 0 to 255.
  - `__attribute__((noreturn)) void KeBugCheck(u32 code, u64 p1, u64 p2, u64 p3, u64 p4)`: prints `*** BUGCHECK 0x%x (p1, p2, p3, p4)` to serial, then `cli; hlt` loop. Codes: `0x0000001E` KMODE_EXCEPTION_NOT_HANDLED, `0x0000007F` UNEXPECTED_KERNEL_MODE_TRAP.
  - `void KeSelfTestRun(void)` and `void KeSelfTestFail(const char *name)` (prints `ZTO2-SELFTEST: FAIL <name>` and halts).

- [ ] **Step 1: Write failing self-test** `SelfTestBreakpoint` in `selftest.c`: registers a handler on vector 3, executes `int3`, and fails with `KeSelfTestFail("breakpoint")` if the handler did not run. `KeSelfTestRun` is called from `kmain` after `HalInitCpu`.
- [ ] **Step 2: Run `make iso && make test-boot`.** Expected: FAIL (no handler or no IDT).
- [ ] **Step 3: Implement `HalInitCpu`** and the 256 stubs in `isr.asm` (push error code 0 where the CPU does not, push vector, jump to a common stub that saves `TrapFrame` and calls C `HalTrapDispatch`). Vector 8 (double fault) uses IST1. Unregistered exception vectors call `KeBugCheck(0x7F, vector, error, rip, 0)`.
- [ ] **Step 4: Add `SelfTestDoubleFault`**, which is not run by default and is enabled by building with `make ZTO2_FAULT_TEST=df`. It corrupts `rsp` to an unmapped address and triggers a fault. Run `make iso ZTO2_FAULT_TEST=df && make test-boot`; expected serial output contains `BUGCHECK 0x7f` and `0x8`, proving IST1 works. Change the runner's expected marker via the `ZTO2_EXPECT` environment variable.
- [ ] **Step 5: Run the default build `make iso && make test-boot`.** Expected: `breakpoint` test passes (full PASS marker comes in Task 6).
- [ ] **Step 6: Commit** `feat: GDT/TSS/IDT, exception dispatch and bugcheck`.

---

### Task 4: Local APIC and timer tick

**Files:**
- Create: `src/hal/apic.c`
- Modify: `include/zto2/hal.h`, `src/ke/selftest.c`, `src/boot/main.c`
- Test: `SelfTestTimerTicks` in `selftest.c`

**Interfaces:**
- Consumes: `HalRegisterInterrupt`, `g_HhdmOffset`.
- Produces: `void HalApicInit(void)` (masks and disables both legacy 8259 PICs, enables the LAPIC with spurious vector `0xFF`), `void HalApicTimerStart(u32 hz)` (calibrated against PIT channel 2 over 10 ms; fires vector `0x40`), `u64 HalTickCount(void)`, `void HalApicEoi(void)`.

- [ ] **Step 1: Write failing self-test** `SelfTestTimerTicks`: starts the timer at 100 Hz, enables interrupts, busy-waits up to 2 seconds of PIT-independent spin count for `HalTickCount() >= 10`, fails with `"timer"` otherwise.
- [ ] **Step 2: Run `make iso && make test-boot`.** Expected: FAIL `timer`.
- [ ] **Step 3: Implement `apic.c`.** Read the LAPIC base from MSR `0x1B`, map through the HHDM, use the divide-by-16 periodic mode, and send EOI at the end of the vector `0x40` handler.
- [ ] **Step 4: Run `make iso && make test-boot`.** Expected: `timer` test passes; the tick count printed over serial is within 20 percent of the expected value after 1 second (log it with `KdPrint`).
- [ ] **Step 5: Commit** `feat: LAPIC init and calibrated periodic timer`.

---

### Task 5: Spinlocks, DPCs, threads and preemptive scheduler

**Files:**
- Create: `include/zto2/ke.h` additions, `src/ke/spinlock.c`, `src/ke/ready_queue.c`, `src/ke/dpc.c`, `src/ke/sched.c`, `src/ke/switch.asm`
- Test: `tests/host/test_ready_queue.c`, `SelfTestPreempt` and `SelfTestPriority` in `selftest.c`

**Interfaces:**
- Consumes: `HalTickCount`, `HalApicEoi`, `HalRegisterInterrupt`, `KdPrint`.
- Produces:
  - `typedef struct KTHREAD { u64 rsp; struct KTHREAD *next; u8 priority; u8 quantum_left; u8 state; void *stack; } KTHREAD;` (`state`: `KT_READY`, `KT_RUNNING`, `KT_FINISHED`).
  - `typedef struct { KTHREAD *head[32]; KTHREAD *tail[32]; u32 bitmap; } KREADY_QUEUE;`
  - `void KeRqInit(KREADY_QUEUE *)`, `void KeRqInsert(KREADY_QUEUE *, KTHREAD *)` (FIFO per priority), `KTHREAD *KeRqPopHighest(KREADY_QUEUE *)` (NULL if empty), `int KeRqHighestPriority(const KREADY_QUEUE *)` (-1 if empty).
  - `typedef struct KSPIN_LOCK { volatile u32 v; } KSPIN_LOCK;`, `u64 KeAcquireSpinLock(KSPIN_LOCK *)` (saves RFLAGS, disables interrupts, returns saved flags), `void KeReleaseSpinLock(KSPIN_LOCK *, u64 saved)`.
  - `typedef struct KDPC { void (*fn)(void *); void *ctx; struct KDPC *next; bool queued; } KDPC;`, `bool KeInsertQueueDpc(KDPC *)` (false if already queued), `void KeDrainDpcs(void)`.
  - `void KeSchedulerInit(void)` (adopts the boot context as the current thread and creates the idle thread at priority 0), `KTHREAD *KeCreateThread(void (*entry)(void *), void *arg, u8 priority)` (NULL on bad priority above 31), `KTHREAD *KeGetCurrentThread(void)`, `void KeYield(void)`, `void KiSwitchContext(KTHREAD *from, KTHREAD *to)` in asm (saves callee-saved registers and RSP). Quantum is 3 ticks at 100 Hz.

- [ ] **Step 1: Write failing host tests** in `test_ready_queue.c`: `test_empty` (`KeRqHighestPriority` is -1, pop returns NULL), `test_fifo_same_priority` (insert A, B at priority 5; pop returns A then B), `test_highest_first` (insert priority 3 then 9; pop returns the 9 first), `test_bitmap_clears` (after popping the only entry at priority 9, `KeRqHighestPriority` is 3).
- [ ] **Step 2: Run `make test-host`.** Expected: FAIL (undefined `KeRq*`).
- [ ] **Step 3: Implement `ready_queue.c`** (no hardware dependencies, compiled for both host and kernel). Run `make test-host`; expected PASS.
- [ ] **Step 4: Write failing self-tests.** `SelfTestPreempt`: two same-priority threads each increment their own counter in a loop that never yields; after `HalTickCount()` advances by 50, both counters must be non-zero. `SelfTestPriority`: a priority 10 thread started while the main test runs at priority 4 must run before the main test resumes. `SelfTestIdle`: with the main test thread blocked in `hlt` waiting on a flag set by a thread that finishes, the system must not fault and the flag must become set (exercises idle and finished-thread handling). `SelfTestCreateBadPriority`: `KeCreateThread(f, 0, 32)` returns NULL.
- [ ] **Step 5: Run `make iso && make test-boot`.** Expected: FAIL on `preempt`.
- [ ] **Step 6: Implement spinlock, DPC and scheduler.** The vector `0x40` handler counts the tick, queues a reschedule DPC, sends EOI, then calls `KeDrainDpcs`; the DPC decrements the current quantum and, when it hits 0 or a higher priority is ready, switches via `KiSwitchContext`. A per-CPU `in_scheduler` flag makes a tick that arrives during a switch only count the tick and return. A finished thread marks itself `KT_FINISHED` and yields; the idle thread loops on `sti; hlt`.
- [ ] **Step 7: Run `make iso && make test-boot`.** Expected: all four self-tests pass.
- [ ] **Step 8: Commit** `feat: spinlocks, DPCs, threads and preemptive scheduler`.

---

### Task 6: Self-test aggregation, boot test gate, README

**Files:**
- Modify: `src/ke/selftest.c`, `src/boot/main.c`, `tests/boot/run_qemu.sh`, `Makefile`
- Create: `README.md`

**Interfaces:**
- Consumes: every `SelfTest*` function from Tasks 3 to 5.
- Produces: `make check` (runs `test-host` then `test-boot`); `kmain` ends by printing `ZTO2-SELFTEST: PASS` and writing `0x10` to port `0xF4` (QEMU exits with status 33).

- [ ] **Step 1: Make the runner strict.** `run_qemu.sh` additionally fails if the output contains `BUGCHECK` or `FAIL` (unless `ZTO2_EXPECT` is set for the fault test), and treats exit status 33 from QEMU as success.
- [ ] **Step 2: Run `make check`.** Expected: FAIL until `kmain` prints the PASS marker.
- [ ] **Step 3: Have `kmain` call `KeSelfTestRun` then print the PASS marker and exit QEMU.** Run `make check`; expected: host tests pass, the boot test passes in under 30 seconds.
- [ ] **Step 4: Write `README.md`**: what zto2 is, the milestone table, the build prerequisites, `make check`, and how to boot the ISO in QEMU or from a USB stick (`-cdrom build/zto2.iso`).
- [ ] **Step 5: Run `make clean && make check` from scratch.** Expected: PASS.
- [ ] **Step 6: Commit** `test: aggregate self-tests and gate on serial PASS marker`.
