/* Linux x86-64 SysV only.  Both current s2n routines reserve 488 bytes,
   push six registers, and make no nested calls.  Inspect before C resumes. */
.intel_syntax noprefix
.text
.globl fd_test_signing_stack
.type fd_test_signing_stack,@function
.macro CHECK reg, magic, bit
    movabs r11, \magic
    cmp \reg, r11
    je .Lgood\@
    or QWORD PTR [rdx], \bit
.Lgood\@:
.endm

/* Arguments: function, output, scalar, report. */
fd_test_signing_stack:
    push rbx
    push rbp
    push r12
    push r13
    push r14
    push r15
    sub rsp, 40
    mov [rsp], rdi
    mov [rsp+8], rsi
    mov [rsp+16], rdx
    mov [rsp+24], rcx
    mov [rsp+32], rsp

    /* Poison exactly the future callee scratch, not saved registers. */
    sub rsp, 544
    mov rdi, rsp
    movabs rax, 0xa5a5a5a5a5a5a5a5
    mov ecx, 61
    cld
    rep stosq
    add rsp, 544
    movabs rbx, 0x1122334455667788
    movabs rbp, 0x2233445566778899
    movabs r12, 0x33445566778899aa
    movabs r13, 0x445566778899aabb
    movabs r14, 0x5566778899aabbcc
    movabs r15, 0x66778899aabbccdd
    mov rdi, [rsp+8]
    mov rsi, [rsp+16]
    call QWORD PTR [rsp]

    /* Reclaim the region immediately.  No pushes or calls before inspection. */
    sub rsp, 544
    mov rdx, [rsp+568]
    xor eax, eax
    xor r9d, r9d
    xor ecx, ecx
.Lscan:
    mov r8, [rsp+rcx*8]
    or rax, r8
    test r8, r8
    setne r10b
    movzx r10d, r10b
    add r9, r10
    inc ecx
    cmp ecx, 61
    jne .Lscan
    mov [rdx+8], rax
    mov [rdx+16], r9
    mov QWORD PTR [rdx], 0
    lea r10, [rsp+544]
    mov r11, [rsp+576]
    mov [rdx+24], r11
    mov [rdx+32], r10
    cmp r10, r11
    je .Lstack_ok
    or QWORD PTR [rdx], 64
.Lstack_ok:
    CHECK rbx, 0x1122334455667788, 1
    CHECK rbp, 0x2233445566778899, 2
    CHECK r12, 0x33445566778899aa, 4
    CHECK r13, 0x445566778899aabb, 8
    CHECK r14, 0x5566778899aabbcc, 16
    CHECK r15, 0x66778899aabbccdd, 32
    add rsp, 584
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbp
    pop rbx
    ret
.size fd_test_signing_stack,.-fd_test_signing_stack
.section .note.GNU-stack,"",@progbits
