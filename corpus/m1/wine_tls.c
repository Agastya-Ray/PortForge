#include <windows.h>
#include <stdint.h>
#define LITTLEENDIAN 1
#define THREAD_LOCAL _Thread_local
#include "softfloat.h"

struct task { HANDLE ready, start; unsigned id; };
/* Keep ELF TLS lowering in the ordinary C ABI. GCC Wine callbacks use
 * ms_abi, whose TLS lowering can otherwise reuse RSI/RDI incorrectly. */
static __attribute__((noinline)) DWORD thread_body(void *argument) {
    const struct task *task=argument;
    DWORD result=0;
    if(extF80_roundingPrecision!=80 || softfloat_roundingMode!=softfloat_round_near_even || softfloat_exceptionFlags!=0 || softfloat_detectTininess!=softfloat_tininess_afterRounding) result=1;
    extF80_roundingPrecision=(uint_fast8_t)(32+task->id);
    softfloat_roundingMode=(uint_fast8_t)(10+task->id);
    softfloat_exceptionFlags=(uint_fast8_t)(20+task->id);
    softfloat_detectTininess=(uint_fast8_t)(40+task->id);
    if(!SetEvent(task->ready) || WaitForSingleObject(task->start,5000)!=WAIT_OBJECT_0) return 2;
    if(extF80_roundingPrecision!=32+task->id || softfloat_roundingMode!=10+task->id || softfloat_exceptionFlags!=20+task->id || softfloat_detectTininess!=40+task->id) result=3;
    return result;
}

static DWORD WINAPI worker(void *argument) {
    return thread_body(argument);
}

int main(void) {
    struct task tasks[4]={{0}};
    HANDLE threads[4]={0};
    HANDLE start=CreateEventW(NULL,TRUE,FALSE,NULL);
    unsigned count=0;
    int result=1;
    if(!start) return 1;
    for(unsigned i=0;i<4;++i) {
        tasks[i]=(struct task){CreateEventW(NULL,TRUE,FALSE,NULL),start,i};
        if(!tasks[i].ready) goto done;
        threads[i]=CreateThread(NULL,0,worker,&tasks[i],0,NULL);
        if(!threads[i]) goto done;
        ++count;
    }
    for(unsigned i=0;i<4;++i) if(WaitForSingleObject(tasks[i].ready,5000)!=WAIT_OBJECT_0) goto done;
    if(extF80_roundingPrecision!=80 || softfloat_roundingMode!=softfloat_round_near_even || softfloat_exceptionFlags!=0 || softfloat_detectTininess!=softfloat_tininess_afterRounding) goto done;
    result=0;
done:
    if(!SetEvent(start)) result=1;
    for(unsigned i=0;i<count;++i) {
        DWORD code=1;
        if(WaitForSingleObject(threads[i],5000)!=WAIT_OBJECT_0) ExitProcess(1);
        if(!GetExitCodeThread(threads[i],&code) || code!=0) result=1;
        CloseHandle(threads[i]);
    }
    for(unsigned i=0;i<4;++i) if(tasks[i].ready) CloseHandle(tasks[i].ready);
    CloseHandle(start);
    if(!result) {
        static const char message[]="Wine thread-local SoftFloat state passed\n";
        DWORD written=0;
        if(!WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),message,sizeof(message)-1,&written,NULL) || written!=sizeof(message)-1) result=1;
    }
    return result;
}
