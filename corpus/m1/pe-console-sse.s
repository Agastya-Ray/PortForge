.intel_syntax noprefix
.text
.globl pf_sse_sample
pf_sse_sample:
    stmxcsr DWORD PTR [rcx]
    ldmxcsr DWORD PTR [rdx]
    movss xmm0, DWORD PTR [rdx+8]
    movss xmm1, DWORD PTR [rdx+12]
    ucomiss xmm0, xmm1
    setz BYTE PTR [rcx+4]
    setp BYTE PTR [rcx+5]
    setb BYTE PTR [rcx+6]
    stmxcsr DWORD PTR [rcx+16]

    movss xmm0, DWORD PTR [rdx+16]
    comiss xmm0, DWORD PTR [rdx+20]
    setz BYTE PTR [rcx+7]
    setp BYTE PTR [rcx+8]
    setb BYTE PTR [rcx+9]
    stmxcsr DWORD PTR [rcx+20]

    ldmxcsr DWORD PTR [rdx]
    movsd xmm0, QWORD PTR [rdx+24]
    ucomisd xmm0, QWORD PTR [rdx+32]
    setz BYTE PTR [rcx+10]
    setp BYTE PTR [rcx+11]
    setb BYTE PTR [rcx+12]
    stmxcsr DWORD PTR [rcx+24]

    ldmxcsr DWORD PTR [rdx+4]
    movsd xmm0, QWORD PTR [rdx+40]
    movsd xmm1, QWORD PTR [rdx+48]
    comisd xmm0, xmm1
    setz BYTE PTR [rcx+13]
    setp BYTE PTR [rcx+14]
    setb BYTE PTR [rcx+15]
    stmxcsr DWORD PTR [rcx+28]
    ldmxcsr DWORD PTR [rcx]
    ret
