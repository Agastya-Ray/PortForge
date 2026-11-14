#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lifted.c"

typedef void (*block)(struct pf_state *);
static const block operations[] = {pf_block_0, pf_block_1, pf_block_2, pf_block_3, pf_block_4, pf_block_5};
static uint64_t next(uint64_t *seed) { *seed ^= *seed << 13; *seed ^= *seed >> 7; *seed ^= *seed << 17; return *seed; }
static uint64_t mix(uint64_t hash, uint64_t word) { return (hash ^ word) * UINT64_C(1099511628211); }
static void require(bool ok) { if (!ok) abort(); }

#if defined(__x86_64__)
#define NATIVE_X86 "true"
static void native(unsigned op, uint16_t status, uint16_t input, uint16_t *cw, uint16_t *sw, uint16_t *word) {
    unsigned char environment[28] = {0};
    environment[0] = 0x7f; environment[1] = 3;
    environment[4] = (uint8_t)status; environment[5] = (uint8_t)(status >> 8);
    environment[8] = environment[9] = 0xff;
    __asm__ volatile("fninit; fldenv %0" : : "m"(environment));
    switch (op) {
    case 0: __asm__ volatile("fninit"); break;
    case 1: __asm__ volatile("fnclex"); break;
    case 2: __asm__ volatile("fnstsw %%ax" : "=a"(*word)); break;
    case 3: __asm__ volatile("fldcw %0" : : "m"(input)); break;
    case 4: __asm__ volatile("fnstcw %0" : "=m"(*word)); break;
    case 5: __asm__ volatile("fnstsw %0" : "=m"(*word)); break;
    default: abort();
    }
    __asm__ volatile("fnstcw %0; fnstsw %1; fnclex; fninit" : "=m"(*cw), "=m"(*sw));
}
#else
#define NATIVE_X86 "false"
#endif

static void faults(void) {
    uint8_t memory[2] = {0x7f, 3};
    for (unsigned op = 3; op < 6; ++op) {
        for (unsigned type = 0; type < 3; ++type) {
            struct pf_state s = {.rip=0x1000, .rflags=0x202, .x87={.control=0x37f, .status=0x4000},
                .memory={memory, 0x2000, type == 0 ? 1 : 2, type != 1}};
            if (type == 1 && op == 3) continue;
            s.gpr[3] = type == 2 ? UINT64_C(0x0000800000000000) : 0x2000;
            struct pf_state before = s;
            uint8_t bytes[2]; memcpy(bytes, memory, 2);
            operations[op](&s);
            require(s.memory_fault != PF_MEM_OK && s.rip == before.rip);
            require(!memcmp(&s.x87, &before.x87, sizeof(s.x87)) && !memcmp(memory, bytes, 2));
            require(s.rflags == before.rflags);
        }
    }
    /* Pending exceptions block waiting FLDCW before even an invalid memory read. */
    struct pf_state s = {.rip=0x1000, .x87={.control=0x37b, .status=0x8084}};
    pf_block_3(&s); require(s.x87_unsupported && s.rip == 0x1000 && s.memory_fault == PF_MEM_OK);
    /* Every no-wait form works with ES+B pending. */
    for (unsigned op=0; op<6; ++op) {
        if(op==3) continue;
        s=(struct pf_state){.rip=0x1000, .x87={.control=0x37b, .status=0x8084}, .memory={memory,0x2000,2,true}};
        s.gpr[3]=0x2000; operations[op](&s);
        require(!s.x87_unsupported && s.rip==0x1002 && s.memory_fault==PF_MEM_OK);
        if(op==1) require(s.x87.status==0);
    }
}

int main(void) {
    const unsigned samples=1000000;
    uint64_t seed=UINT64_C(0x1813db64ac9e8b57), hash=UINT64_C(14695981039346656037);
    faults();
    uint8_t control_bytes[2]={0,0};
    struct pf_state mixed={.rip=0x1000,.x87={.control=0x37f,.status=0x4000},.memory={control_bytes,0x2000,2,true}};
    mixed.gpr[0]=UINT64_C(0x123456789abcdef0);mixed.gpr[3]=0x2000;
    pf_block_6(&mixed);
    require(mixed.rip==0x1006 && mixed.x87.control==0x37f && mixed.gpr[0]==UINT64_C(0x123456789abc4000));
    for(unsigned op=0;op<6;++op) for(unsigned i=0;i<samples;++i) {
        uint16_t input=(uint16_t)next(&seed), status=(uint16_t)next(&seed)&0x7f7f;
        uint8_t memory[2]={(uint8_t)input,(uint8_t)(input>>8)};
        struct pf_state s={.rip=0x1000, .rflags=next(&seed), .x87={.control=0x37f,.status=status}, .memory={memory,0x2000,2,true}};
        s.gpr[0]=next(&seed); s.gpr[3]=0x2000;
        s.pending=(struct pf_pending){.lhs=3,.rhs=4,.result=7,.width=64,.operation=PF_ADD};
        uint64_t rax=s.gpr[0], flags=s.rflags;
        struct pf_pending pending=s.pending;
        operations[op](&s);
        require(!s.x87_unsupported && s.memory_fault==PF_MEM_OK && s.rip==0x1002);
        require(s.rflags==flags && !memcmp(&s.pending,&pending,sizeof(pending)));
        uint16_t word=op==2?(uint16_t)s.gpr[0]:(uint16_t)(memory[0]|(uint16_t)memory[1]<<8);
        if(op==2) require((s.gpr[0]&~UINT64_C(0xffff))==(rax&~UINT64_C(0xffff)));
        else require(s.gpr[0]==rax);
#if defined(__x86_64__)
        uint16_t cw,sw,native_word=input;
        native(op,status,input,&cw,&sw,&native_word);
        if(s.x87.control!=cw || s.x87.status!=sw || word!=native_word) {
            fprintf(stderr,"x87 control op=%u case=%u input=%04x status=%04x got=%04x/%04x/%04x native=%04x/%04x/%04x\n",op,i,input,status,s.x87.control,s.x87.status,word,cw,sw,native_word);return 1;
        }
#endif
        hash=mix(mix(mix(hash,s.x87.control),s.x87.status),word);
    }
    printf("{\"native_x86_oracle\":%s,\"samples_per_encoding\":%u,\"encodings\":6,\"mismatches\":0,\"digest\":\"%016" PRIx64 "\"}\n",NATIVE_X86,samples,hash);
}
