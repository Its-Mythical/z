; Interrupt entry stubs for vectors 0-255 and the common save/restore path.
; Frame layout built here must match TrapFrame in include/zto2/hal.h.

bits 64
default rel

extern HalTrapDispatch

section .text

; Each stub is exactly 16 bytes (push 0 = 2, push imm32 = 5, jmp rel32 <= 5) so
; the table below is computed rather than labelled. imm32 (not imm8) keeps
; vectors >= 128 from sign-extending.
%macro ISR_STUB 2 ; vector, has_error_code
align 16
    %if %2 == 0
    push 0
    %endif
    push dword %1
    jmp near IsrCommon
%endmacro

align 16
HalIsrStubs:
%assign vec 0
%rep 256
    %if (vec == 8) || (vec >= 10 && vec <= 14) || (vec == 17) || (vec == 21) || (vec == 29) || (vec == 30)
    ISR_STUB vec, 1
    %else
    ISR_STUB vec, 0
    %endif
%assign vec vec+1
%endrep

IsrCommon:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    cld
    mov rdi, rsp
    mov rbp, rsp
    and rsp, -16
    call HalTrapDispatch
    mov rsp, rbp
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    add rsp, 16         ; vector + error code
    iretq

section .rodata
align 8
global HalIsrStubTable
HalIsrStubTable:
%assign vec 0
%rep 256
    dq HalIsrStubs + vec * 16
%assign vec vec+1
%endrep

section .note.GNU-stack noalloc noexec nowrite progbits
