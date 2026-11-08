#ifndef PF_WINE_IMPORTS_H
#define PF_WINE_IMPORTS_H

/* Include after the generated PE translation unit. */
#include <windows.h>

typedef LONG (WINAPI *pf_wine_compare_fn)(const FILETIME *, const FILETIME *);
typedef BOOL (WINAPI *pf_wine_convert_fn)(const FILETIME *, SYSTEMTIME *);
typedef HANDLE (WINAPI *pf_wine_std_handle_fn)(DWORD);
typedef BOOL (WINAPI *pf_wine_write_fn)(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED);

enum { PF_WINE_EXPORT_COUNT = 5, PF_WINE_STDOUT = 1, PF_WINE_STDERR = 2 };

struct pf_wine_api {
    HMODULE kernel32;
    pf_wine_compare_fn compare;
    pf_wine_convert_fn convert;
    pf_wine_std_handle_fn std_handle;
    pf_wine_write_fn write;
    HANDLE output, error;
    struct pf_pe_export exports[PF_WINE_EXPORT_COUNT];
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

static inline uint64_t pf_wine_std_handle(void *context, uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    (void)b;
    (void)c;
    (void)d;
    struct pf_wine_api *api = context;
    DWORD stream = (DWORD)a;
    if (stream != STD_OUTPUT_HANDLE && stream != STD_ERROR_HANDLE) return UINT64_MAX;
    HANDLE handle = api->std_handle(stream);
    if (!handle) return 0;
    if (handle == INVALID_HANDLE_VALUE) return UINT64_MAX;
    if (stream == STD_OUTPUT_HANDLE) {
        api->output = handle;
        return PF_WINE_STDOUT;
    }
    api->error = handle;
    return PF_WINE_STDERR;
}

static inline uint64_t pf_wine_write(void *context, uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    const struct pf_wine_api *api = context;
    HANDLE handle = a == PF_WINE_STDOUT ? api->output : a == PF_WINE_STDERR ? api->error : NULL;
    uint8_t *out = (uint8_t *)(uintptr_t)d;
    pf_write_le32(out, 0);
    DWORD written = 0;
    const uint8_t empty = 0;
    const void *input = (DWORD)c ? (const void *)(uintptr_t)b : &empty;
    BOOL ok = handle ? api->write(handle, input, (DWORD)c, &written, NULL) : FALSE;
    pf_write_le32(out, written);
    return (uint32_t)ok;
}

static inline uint64_t pf_wine_exit(void *context, uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
    (void)context;
    (void)b;
    (void)c;
    (void)d;
    return (uint32_t)a;
}

/* The caller owns a fresh API object and keeps it alive until the bindings are retired. */
static inline DWORD pf_wine_open(struct pf_wine_api *api) {
    HMODULE module = LoadLibraryExA("kernel32.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) return GetLastError();
    FARPROC compare = GetProcAddress(module, "CompareFileTime");
    FARPROC convert = GetProcAddress(module, "FileTimeToSystemTime");
    FARPROC std_handle = GetProcAddress(module, "GetStdHandle");
    FARPROC write = GetProcAddress(module, "WriteFile");
    if (!compare || !convert || !std_handle || !write) {
        FreeLibrary(module);
        return ERROR_PROC_NOT_FOUND;
    }
    *api = (struct pf_wine_api){
        .kernel32 = module,
        .compare = (pf_wine_compare_fn)(void (*)(void))compare,
        .convert = (pf_wine_convert_fn)(void (*)(void))convert,
        .std_handle = (pf_wine_std_handle_fn)(void (*)(void))std_handle,
        .write = (pf_wine_write_fn)(void (*)(void))write,
        .exports = {
            {
                .dll = "kernel32.dll", .name = "CompareFileTime",
                .invoke = pf_wine_compare, .context = api, .args = {{.size = 8}, {.size = 8}},
            },
            {
                .dll = "kernel32.dll", .name = "FileTimeToSystemTime",
                .invoke = pf_wine_convert, .context = api, .args = {{.size = 8}, {.size = 16, .writable = true}},
            },
            {
                .dll = "kernel32.dll", .name = "GetStdHandle", .invoke = pf_wine_std_handle, .context = api,
            },
            {
                .dll = "kernel32.dll", .name = "WriteFile", .invoke = pf_wine_write, .context = api,
                .args = {{0}, {.size_arg = 3}, {0}, {.size = 4, .writable = true}}, .null_fifth = true,
            },
            {
                .dll = "kernel32.dll", .name = "ExitProcess", .invoke = pf_wine_exit, .exits = true,
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
