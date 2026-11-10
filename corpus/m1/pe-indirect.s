.text
.globl pf_fixture
pf_fixture:
    sub $40, %rsp
    test %rdx, %rdx
    jz memory_call
    call *%rcx
    jmp done
memory_call:
    mov %rcx, 24(%rsp)
    call *24(%rsp)
done:
    add $40, %rsp
    ret

.org 0x80, 0xcc
pf_target_add:
    mov %r8, %rax
    add %r9, %rax
    ret

.org 0x90, 0xcc
pf_target_xor:
    mov %r8, %rax
    xor %r9, %rax
    ret
