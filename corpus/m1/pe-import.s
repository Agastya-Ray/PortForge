    .text
    .globl pf_fixture
pf_fixture:
    leaq -40(%rsp), %rsp
    .ifdef PF_WINE
    cmpq %r9, %r8
    .else
    cmpq %rdx, %rcx
    .endif
    je .Lordinal
    call *pf_iat(%rip)
    jmp .Ldone
.Lordinal:
    call *pf_iat+8(%rip)
.Ldone:
    leaq 40(%rsp), %rsp
    ret

    .data
    .balign 8
pf_iat:
    .zero 256
