.code
PUBLIC pf_x87_sample
pf_x87_sample PROC
    fnstcw WORD PTR [rcx+38]
    fldcw WORD PTR [rcx]
    fld QWORD PTR [rcx+8]
    fld QWORD PTR [rcx+16]
    fdivp st(1), st(0)
    fnstsw WORD PTR [rcx+34]
    fstp TBYTE PTR [rcx+24]
    fnstcw WORD PTR [rcx+36]
    fnclex
    fninit
    fldcw WORD PTR [rcx+38]
    ret
pf_x87_sample ENDP
END
