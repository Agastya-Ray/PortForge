    .text
    .globl pf_fixture
pf_fixture:
    cmpq %rdx, %rcx
    jne .Ldifferent
    call .Ladd
    cmpq %rdx, %rcx
    je .Ldone
    jmp .Ldifferent
.Ldone:
    leaq (%rcx), %rax
    ret
.Ladd:
    addq %r9, %rcx
    ret
    ud2
.Ldifferent:
    subq %r8, %rcx
    jmp .Ldone
