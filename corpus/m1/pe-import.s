    .text
    .globl pf_fixture
pf_fixture:
    leaq -40(%rsp), %rsp
    cmpq %rdx, %rcx
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
