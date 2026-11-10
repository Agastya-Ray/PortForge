.text
.globl pf_fixture
pf_fixture:
    mov pointer(%rip), %rcx
    movabs $payload, %rax
    cmp %rcx, %rax
    jne failed
    mov (%rax), %rax
    ret
failed:
    mov $0, %eax
    ret

.data
pointer: .quad payload
payload: .quad 0x0123456789abcdef
