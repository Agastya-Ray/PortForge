#ifndef PF_WINE_IMPORTS_H
#define PF_WINE_IMPORTS_H

/* Include after the generated PE translation unit. */
#include <windows.h>

typedef LONG (WINAPI *pf_wine_compare_fn)(const FILETIME *, const FILETIME *);
typedef BOOL (WINAPI *pf_wine_convert_fn)(const FILETIME *, SYSTEMTIME *);

struct pf_wine_api {
    HMODULE kernel32;
    pf_wine_compare_fn compare;
    pf_wine_convert_fn convert;
    struct pf_pe_export exports[2];
};

static inline FILETIME pf_wine_read_filetime(uint64_t address) {
    const uint8_t *bytes = (const uint8_t *)(uintptr_t)address;
    return (FILETIME){pf_read_le32(bytes), pf_read_le32(bytes + 4)};
}

static inline uint64_t pf_wine_compare(void *context, uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    (void)c;
    (void)d;
    const struct pf_wine_api *api = context;
    FILETIME left = pf_wine_read_filetime(a), right = pf_wine_read_filetime(b);
    return (uint32_t)api->compare(&left, &right);
}

static inline uint64_t pf_wine_convert(void *context, uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    (void)c;
    (void)d;
    const struct pf_wine_api *api = context;
    FILETIME input = pf_wine_read_filetime(a);
    SYSTEMTIME output;
    BOOL ok = api->convert(&input, &output);
    if (ok) {
        const WORD fields[] = {
            output.wYear, output.wMonth, output.wDayOfWeek, output.wDay,
            output.wHour, output.wMinute, output.wSecond, output.wMilliseconds,
        };
        uint8_t *bytes = (uint8_t *)(uintptr_t)b;
        for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i) {
            bytes[2 * i] = (uint8_t)fields[i];
            bytes[2 * i + 1] = (uint8_t)(fields[i] >> 8);
        }
    }
    return (uint32_t)ok;
}

/* The caller owns a fresh API object and keeps it alive until the bindings are retired. */
static inline DWORD pf_wine_open(struct pf_wine_api *api) {
    HMODULE module = LoadLibraryExA("kernel32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) return GetLastError();
    FARPROC compare = GetProcAddress(module, "CompareFileTime");
    FARPROC convert = GetProcAddress(module, "FileTimeToSystemTime");
    if (!compare || !convert) {
        FreeLibrary(module);
        return ERROR_PROC_NOT_FOUND;
    }
    *api = (struct pf_wine_api){
        .kernel32 = module,
        .compare = (pf_wine_compare_fn)(void (*)(void))compare,
        .convert = (pf_wine_convert_fn)(void (*)(void))convert,
        .exports = {
            {
                .dll = "kernel32.dll", .name = "CompareFileTime",
                .invoke = pf_wine_compare, .context = api, .args = {{8, false}, {8, false}},
            },
            {
                .dll = "kernel32.dll", .name = "FileTimeToSystemTime",
                .invoke = pf_wine_convert, .context = api, .args = {{8, false}, {16, true}},
            },
        },
    };
    return ERROR_SUCCESS;
}

static inline void pf_wine_close(struct pf_wine_api *api) {
    if (api->kernel32) FreeLibrary(api->kernel32);
    *api = (struct pf_wine_api){0};
}

#endif
