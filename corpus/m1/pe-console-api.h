#ifndef PF_CONSOLE_API_H
#define PF_CONSOLE_API_H

#include <stdint.h>

/* The guest is linked against kernel32 with no C startup library. */
__declspec(dllimport) void *GetStdHandle(uint32_t stream);
__declspec(dllimport) int WriteFile(void *handle, const void *bytes, uint32_t size,
                                   uint32_t *written, void *overlapped);
__declspec(dllimport) __declspec(noreturn) void ExitProcess(uint32_t code);

#define STD_OUTPUT_HANDLE ((uint32_t)-11)
#define STD_ERROR_HANDLE ((uint32_t)-12)

#endif
