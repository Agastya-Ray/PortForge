#include "pe-console-api.h"

struct sample {
    uint64_t control, a, b;
    unsigned char result[10];
    uint16_t status, actual_control, initial_control;
};
void pf_x87_sample(struct sample *value);

void pf_entry(void) {
    static const uint64_t inputs[][2] = {
        {UINT64_C(0x401c000000000000),UINT64_C(0x4008000000000000)},
        {UINT64_C(0xc01c000000000000),UINT64_C(0x4008000000000000)},
        {UINT64_C(0x401c000000000000),UINT64_C(0xc008000000000000)},
        {0,UINT64_C(0xc008000000000000)},
        {UINT64_C(0x3ff0000000000000),0},
        {0,0},
        {1,UINT64_C(0x4000000000000000)},
        {UINT64_C(0x7fefffffffffffff),1},
    };
    static const unsigned precision[]={0,0x200,0x300};
    unsigned char output[12*8*16];
    unsigned offset=0;
    for(unsigned p=0;p<3;++p) for(unsigned rounding=0;rounding<4;++rounding) {
        for(unsigned i=0;i<8;++i) {
            struct sample value;
            value.control=0x7f|precision[p]|(rounding<<10);
            value.a=inputs[i][0];value.b=inputs[i][1];
            pf_x87_sample(&value);
            if(value.initial_control!=0x27f) ExitProcess(87);
            const unsigned char *bytes=(const unsigned char *)&value+24;
            for(unsigned j=0;j<16;++j) output[offset++]=bytes[j];
        }
    }
    uint32_t written;
    if(!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),output,sizeof(output),&written,0)||written!=sizeof(output)) ExitProcess(88);
    ExitProcess(0);
}
