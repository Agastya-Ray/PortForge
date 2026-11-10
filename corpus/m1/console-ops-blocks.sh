blocks=(
    4831c8          # XOR RAX, RCX
    25ffffff7f      # AND EAX, 0x7fffffff
    09c8            # OR EAX, ECX
    85c8            # TEST EAX, ECX
    4839c8ffc0      # CMP RAX, RCX; INC EAX
    4839c848ffc9    # CMP RAX, RCX; DEC RCX
    4839da0f42c1    # CMP RDX, RBX; CMOVB EAX, ECX
    4839da480f4fc1  # CMP RDX, RBX; CMOVG RAX, RCX
    8339ff          # CMP dword [RCX], -1
    480301          # ADD RAX, [RCX]
    0f1f4000        # multi-byte NOP
    5459            # PUSH RSP; POP RCX
    505c            # PUSH RAX; POP RSP
)
