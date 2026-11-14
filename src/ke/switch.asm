; void KiSwitchContext(KTHREAD *from /*rdi*/, KTHREAD *to /*rsi*/)
;
; KTHREAD.rsp is the first field (offset 0). Interrupts must be off.
;
; Layout of a switched-out thread's stack, lowest address first, with
; from->rsp pointing at the first entry:
;
;     r15, r14, r13, r12, rbx, rbp    (6 qwords pushed here, popped on resume)
;     return address                  (into KiSwitchContext's caller, or, for a
;                                      thread that has never run, KiThreadStart)
;
; Everything the thread had above that (its C frames, and when it was switched
; from the tick ISR also the ISR's TrapFrame) is untouched and waits for it
; to return through them. Caller-saved registers are dead across the call by
; the SysV ABI, so nothing else needs saving; RFLAGS is not saved here (an ISR
; path gets it back from iretq, KeYield saves it itself).
;
; A new thread's stack is built by KeCreateThread to look exactly like this,
; with r12 = entry, r13 = arg and the return address = KiThreadStart.

bits 64
default rel

extern KiThreadMain

section .text

global KiSwitchContext
KiSwitchContext:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rdi], rsp
    mov rsp, [rsi]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

; First code a new thread runs, reached by the ret above. The ret popped the
; return-address slot, which KeCreateThread placed at 8 mod 16, so rsp is now
; 16-byte aligned, as required immediately before a call (the callee then sees
; rsp+8 aligned at entry). KiThreadMain never returns.
global KiThreadStart
KiThreadStart:
    mov rdi, r12        ; entry
    mov rsi, r13        ; arg
    call KiThreadMain
    ud2

section .note.GNU-stack noalloc noexec nowrite progbits
