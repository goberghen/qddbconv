/*
 * qddbconv - Qt6 Audio Converter
 * Copyright (C) 2026 goberghen & Arena.ai Agent
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Native 32-bit PE loader and renderer helper for M5_EmuHw.dll + M5_EmuSmw5.dll
 * (Variant 1: runs natively on x86_64 Linux without Wine and on x64 Windows
 * without requiring registry entries).
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <asm/ldt.h>
#include <dirent.h>
#endif

#ifndef WINAPI
#define WINAPI __attribute__((stdcall))
#endif
#ifndef CDECL
#define CDECL  __attribute__((cdecl))
#endif
typedef struct {
    char name[64];
    uint8_t *base;
    uint32_t size_of_image;
    uint32_t entry_rva;
    uint32_t export_rva;
    uint32_t export_size;
} LoadedPe;

static LoadedPe g_modules[8];
static int g_num_modules = 0;
static char g_dll_dir[512] = ".";
static uint32_t g_last_error = 0;
static void *g_tls_slots[64];
static int g_tls_next = 1;
static LoadedPe *load_pe_dll(const char *path_or_name);
static void *resolve_pe_export_by_name(LoadedPe *pe, const char *name);
static void *resolve_pe_export_by_ordinal(LoadedPe *pe, uint16_t ordinal);

/* Win32 KERNEL32 / WINMM / USER32 shims */
static void WINAPI win32_SetLastError(uint32_t err) { g_last_error = err; }
static uint32_t WINAPI win32_GetLastError(void) { return g_last_error; }
static uint32_t WINAPI win32_GetVersion(void) { return 0x0A280105; /* WinXP 5.1.2600 */ }
static int WINAPI win32_GetVersionExA(uint32_t *osvi) {
    if (osvi) {
        osvi[1] = 5; osvi[2] = 1; osvi[3] = 2600; osvi[4] = 2;
    }
    return 1;
}
static void * WINAPI win32_HeapCreate(uint32_t flOptions, uint32_t dwInitialSize, uint32_t dwMaximumSize) {
    (void)flOptions; (void)dwInitialSize; (void)dwMaximumSize;
    return (void *)0xCAFE0001;
}
static int WINAPI win32_HeapDestroy(void *hHeap) { (void)hHeap; return 1; }
static void * WINAPI win32_HeapAlloc(void *hHeap, uint32_t dwFlags, uint32_t dwBytes) {
    (void)hHeap;
    size_t sz = dwBytes ? dwBytes : 1;
    uint32_t *raw = (uint32_t *)((dwFlags & 0x08) ? calloc(1, sz + 8) : malloc(sz + 8));
    if (!raw) return NULL;
    raw[0] = (uint32_t)sz;
    raw[1] = 0x48454150;
    return (void *)(raw + 2);
}
static void * WINAPI win32_HeapReAlloc(void *hHeap, uint32_t dwFlags, void *lpMem, uint32_t dwBytes) {
    (void)hHeap;
    if (!lpMem) return win32_HeapAlloc(hHeap, dwFlags, dwBytes);
    uint32_t *raw = ((uint32_t *)lpMem) - 2;
    uint32_t old_sz = raw[0];
    size_t sz = dwBytes ? dwBytes : 1;
    uint32_t *nraw = (uint32_t *)realloc(raw, sz + 8);
    if (!nraw) return NULL;
    if ((dwFlags & 0x08) && sz > old_sz) {
        memset(((uint8_t *)(nraw + 2)) + old_sz, 0, sz - old_sz);
    }
    nraw[0] = (uint32_t)sz;
    nraw[1] = 0x48454150;
    return (void *)(nraw + 2);
}
static int WINAPI win32_HeapFree(void *hHeap, uint32_t dwFlags, void *lpMem) {
    (void)hHeap; (void)dwFlags;
    if (!lpMem) return 1;
    uint32_t *raw = ((uint32_t *)lpMem) - 2;
    if (raw[1] == 0x48454150) free(raw);
    else free(lpMem);
    return 1;
}
static uint32_t WINAPI win32_HeapSize(void *hHeap, uint32_t dwFlags, const void *lpMem) {
    (void)hHeap; (void)dwFlags;
    if (!lpMem) return 0;
    const uint32_t *raw = ((const uint32_t *)lpMem) - 2;
    return (raw[1] == 0x48454150) ? raw[0] : 0;
}
static void * WINAPI win32_GetProcessHeap(void) { return (void *)0xCAFE0001; }
static void * WINAPI win32_VirtualAlloc(void *lpAddress, uint32_t dwSize, uint32_t flAllocationType, uint32_t flProtect) {
    (void)lpAddress; (void)flAllocationType; (void)flProtect;
    return calloc(1, dwSize ? dwSize : 4096);
}
static int WINAPI win32_VirtualFree(void *lpAddress, uint32_t dwSize, uint32_t dwFreeType) {
    (void)dwSize;
    if (dwFreeType == 0x8000 && lpAddress) free(lpAddress);
    return 1;
}
static void * WINAPI win32_LocalAlloc(uint32_t uFlags, uint32_t uBytes) {
    return win32_HeapAlloc(NULL, (uFlags & 0x40) ? 0x08 : 0, uBytes);
}
static void * WINAPI win32_LocalReAlloc(void *hMem, uint32_t uBytes, uint32_t uFlags) {
    return win32_HeapReAlloc(NULL, (uFlags & 0x40) ? 0x08 : 0, hMem, uBytes);
}
static void * WINAPI win32_LocalFree(void *hMem) {
    win32_HeapFree(NULL, 0, hMem);
    return NULL;
}
static void * WINAPI win32_GlobalAlloc(uint32_t uFlags, uint32_t dwBytes) {
    return win32_HeapAlloc(NULL, (uFlags & 0x40) ? 0x08 : 0, dwBytes);
}
static void * WINAPI win32_GlobalLock(void *hMem) { return hMem; }
static int WINAPI win32_GlobalUnlock(void *hMem) { (void)hMem; return 1; }
static void * WINAPI win32_GlobalFree(void *hMem) {
    win32_HeapFree(NULL, 0, hMem);
    return NULL;
}

static void WINAPI win32_InitializeCriticalSection(void *cs) { (void)cs; }
static void WINAPI win32_EnterCriticalSection(void *cs) { (void)cs; }
static void WINAPI win32_LeaveCriticalSection(void *cs) { (void)cs; }
static void WINAPI win32_DeleteCriticalSection(void *cs) { (void)cs; }

static uint32_t WINAPI win32_TlsAlloc(void) {
    if (g_tls_next >= 64) return 0xFFFFFFFF;
    return (uint32_t)(g_tls_next++);
}
static void * WINAPI win32_TlsGetValue(uint32_t idx) {
    return (idx < 64) ? g_tls_slots[idx] : NULL;
}
static int WINAPI win32_TlsSetValue(uint32_t idx, void *val) {
    if (idx >= 64) return 0;
    g_tls_slots[idx] = val;
    return 1;
}
static int WINAPI win32_TlsFree(uint32_t idx) { (void)idx; return 1; }

static void * WINAPI win32_CreateEventA(void *sec, int manual, int initial, const char *name) {
    (void)sec; (void)manual; (void)name;
    int *ev = (int *)calloc(1, sizeof(int));
    *ev = initial;
    return ev;
}
static int WINAPI win32_SetEvent(void *hEvent) {
    if (hEvent) *(int *)hEvent = 1;
    return 1;
}
static int WINAPI win32_ResetEvent(void *hEvent) {
    if (hEvent) *(int *)hEvent = 0;
    return 1;
}
static uint32_t WINAPI win32_WaitForSingleObject(void *hHandle, uint32_t dwMilliseconds) {
    (void)hHandle; (void)dwMilliseconds;
    return 0;
}
static int WINAPI win32_CloseHandle(void *hObject) {
    (void)hObject;
    return 1;
}

static uint32_t WINAPI win32_InterlockedIncrement(int32_t *lpAddend) { return ++(*lpAddend); }
static uint32_t WINAPI win32_InterlockedDecrement(int32_t *lpAddend) { return --(*lpAddend); }
static uint32_t WINAPI win32_InterlockedExchange(int32_t *Target, int32_t Value) {
    int32_t old = *Target;
    *Target = Value;
    return old;
}

static char * WINAPI win32_GetCommandLineA(void) { return "qddbconv-smaf32.exe"; }
static char * WINAPI win32_GetEnvironmentStrings(void) {
    char *env = (char *)calloc(1, 4);
    return env;
}
static void * WINAPI win32_GetEnvironmentStringsW(void) {
    return calloc(1, 4);
}
static int WINAPI win32_FreeEnvironmentStringsA(char *penv) { free(penv); return 1; }
static int WINAPI win32_FreeEnvironmentStringsW(void *penv) { free(penv); return 1; }
static void WINAPI win32_GetSystemTime(uint16_t *st) {
    if (st) { memset(st, 0, 16); st[0] = 2026; st[1] = 10; st[3] = 18; }
}
static void WINAPI win32_GetLocalTime(uint16_t *st) {
    if (st) { memset(st, 0, 16); st[0] = 2026; st[1] = 10; st[3] = 18; }
}
static uint32_t WINAPI win32_GetTimeZoneInformation(void *tzi) {
    if (tzi) memset(tzi, 0, 172);
    return 0;
}
static uint32_t WINAPI win32_GetOEMCP(void) { return 437; }
static void WINAPI win32_Sleep(uint32_t ms) { (void)ms; }
static uint32_t WINAPI win32_GetTickCount(void) { return 1000; }
static void WINAPI win32_ExitProcess(uint32_t code) { exit((int)code); }
static uint32_t WINAPI win32_GetACP(void) { return 1252; }
static int WINAPI win32_GetCPInfo(uint32_t cp, uint8_t *lpCPInfo) {
    (void)cp;
    if (lpCPInfo) {
        memset(lpCPInfo, 0, 20);
        *(uint32_t *)lpCPInfo = 1;
        lpCPInfo[4] = '?';
    }
    return 1;
}
static void * WINAPI win32_GetStdHandle(uint32_t nStdHandle) { (void)nStdHandle; return (void *)1; }
static uint32_t WINAPI win32_GetFileType(void *hFile) { (void)hFile; return 2; /* FILE_TYPE_CHAR */ }
static uint32_t WINAPI win32_SetHandleCount(uint32_t uNumber) { return uNumber; }
static void WINAPI win32_GetStartupInfoA(uint8_t *lpStartupInfo) {
    if (lpStartupInfo) memset(lpStartupInfo, 0, 68);
}
static uint32_t WINAPI win32_GetCurrentThreadId(void) { return 1001; }
static void * WINAPI win32_GetCurrentThread(void) { return (void *)0xFFFFFFFE; }
static uint32_t WINAPI win32_GetCurrentProcessId(void) { return 1000; }
static void * WINAPI win32_GetCurrentProcess(void) { return (void *)0xFFFFFFFF; }

static uint32_t WINAPI win32_GetModuleFileNameA(void *hModule, char *lpFilename, uint32_t nSize) {
    (void)hModule;
    if (lpFilename && nSize > 0) {
        snprintf(lpFilename, nSize, "C:\\qddbconv\\M5_EmuSmw5.dll");
        return (uint32_t)strlen(lpFilename);
    }
    return 0;
}
static void * WINAPI win32_GetModuleHandleA(const char *lpModuleName) {
    if (!lpModuleName) return g_num_modules > 0 ? g_modules[0].base : (void *)0x10000000;
    for (int i = 0; i < g_num_modules; ++i) {
        if (strcasecmp(g_modules[i].name, lpModuleName) == 0) return g_modules[i].base;
    }
    return (void *)0x70000000;
}
static void * WINAPI win32_LoadLibraryA(const char *lpLibFileName) {
    
    if (!lpLibFileName) return NULL;
    const char *slash = strrchr(lpLibFileName, '\\');
    const char *base = slash ? (slash + 1) : lpLibFileName;
    LoadedPe *pe = load_pe_dll(base);
    return pe ? pe->base : NULL;
}
static int WINAPI win32_FreeLibrary(void *hLibModule) {
    (void)hLibModule;
    return 1;
}
static void * WINAPI win32_GetProcAddress(void *hModule, const char *lpProcName);

static int WINAPI win32_MultiByteToWideChar(uint32_t cp, uint32_t flags, const char *src, int srclen, uint16_t *dst, int dstlen) {
    (void)cp; (void)flags;
    if (!src) return 0;
    int len = (srclen < 0) ? ((int)strlen(src) + 1) : srclen;
    if (!dst || dstlen == 0) return len;
    int n = (len < dstlen) ? len : dstlen;
    for (int i = 0; i < n; ++i) dst[i] = (uint8_t)src[i];
    return n;
}
static int WINAPI win32_WideCharToMultiByte(uint32_t cp, uint32_t flags, const uint16_t *src, int srclen, char *dst, int dstlen, const char *def, int *used) {
    (void)cp; (void)flags; (void)def;
    if (used) *used = 0;
    if (!src) return 0;
    int len = 0;
    if (srclen < 0) { while (src[len]) len++; len++; } else len = srclen;
    if (!dst || dstlen == 0) return len;
    int n = (len < dstlen) ? len : dstlen;
    for (int i = 0; i < n; ++i) dst[i] = (char)(src[i] & 0xFF);
    return n;
}
static int WINAPI win32_GetStringTypeA(uint32_t locale, uint32_t infoType, const char *src, int cchSrc, uint16_t *lpCharType) {
    (void)locale; (void)infoType;
    int len = (cchSrc < 0) ? ((int)strlen(src) + 1) : cchSrc;
    if (lpCharType) memset(lpCharType, 0, len * sizeof(uint16_t));
    return 1;
}
static int WINAPI win32_GetStringTypeW(uint32_t infoType, const uint16_t *src, int cchSrc, uint16_t *lpCharType) {
    (void)infoType; (void)src;
    int len = (cchSrc < 0) ? 1 : cchSrc;
    if (lpCharType) memset(lpCharType, 0, len * sizeof(uint16_t));
    return 1;
}
static int WINAPI win32_LCMapStringA(uint32_t locale, uint32_t flags, const char *src, int srclen, char *dst, int dstlen) {
    (void)locale; (void)flags;
    int len = (srclen < 0) ? ((int)strlen(src) + 1) : srclen;
    if (!dst || dstlen == 0) return len;
    int n = (len < dstlen) ? len : dstlen;
    memcpy(dst, src, n);
    return n;
}
static int WINAPI win32_LCMapStringW(uint32_t locale, uint32_t flags, const uint16_t *src, int srclen, uint16_t *dst, int dstlen) {
    (void)locale; (void)flags;
    int len = 0;
    if (srclen < 0) { while (src[len]) len++; len++; } else len = srclen;
    if (!dst || dstlen == 0) return len;
    int n = (len < dstlen) ? len : dstlen;
    memcpy(dst, src, n * sizeof(uint16_t));
    return n;
}
static int WINAPI win32_lstrlenA(const char *s) { return s ? (int)strlen(s) : 0; }
static char * WINAPI win32_lstrcpyA(char *dst, const char *src) { return strcpy(dst, src ? src : ""); }
static char * WINAPI win32_lstrcpynA(char *dst, const char *src, int n) {
    if (n <= 0) return dst;
    strncpy(dst, src ? src : "", n - 1);
    dst[n - 1] = '\0';
    return dst;
}
static char * WINAPI win32_lstrcatA(char *dst, const char *src) { return strcat(dst, src ? src : ""); }
static int WINAPI win32_lstrcmpA(const char *a, const char *b) { return strcmp(a ? a : "", b ? b : ""); }
static int WINAPI win32_lstrcmpiA(const char *a, const char *b) { return strcasecmp(a ? a : "", b ? b : ""); }

/* WINMM shims */
static uint32_t WINAPI win32_timeGetDevCaps(uint32_t *ptc, uint32_t cbtc) {
    if (ptc && cbtc >= 8) {
        ptc[0] = 1;
        ptc[1] = 1000;
    }
    return 0;
}
static uint32_t WINAPI win32_timeBeginPeriod(uint32_t uPeriod) { (void)uPeriod; return 0; }
static uint32_t WINAPI win32_timeEndPeriod(uint32_t uPeriod) { (void)uPeriod; return 0; }
static uint32_t WINAPI win32_timeSetEvent(uint32_t delay, uint32_t res, void *cb, uint32_t user, uint32_t event) {
    (void)delay; (void)res; (void)cb; (void)user; (void)event;
    return 1;
}
static uint32_t WINAPI win32_timeKillEvent(uint32_t id) { (void)id; return 0; }

typedef struct {
    const char *name;
    void *fn;
} Win32ApiEntry;

static const Win32ApiEntry g_win32_apis[] = {
    {"SetLastError", win32_SetLastError},
    {"GetLastError", win32_GetLastError},
    {"GetVersion", win32_GetVersion},
    {"GetVersionExA", win32_GetVersionExA},
    {"HeapCreate", win32_HeapCreate},
    {"HeapDestroy", win32_HeapDestroy},
    {"HeapAlloc", win32_HeapAlloc},
    {"HeapReAlloc", win32_HeapReAlloc},
    {"HeapFree", win32_HeapFree},
    {"HeapSize", win32_HeapSize},
    {"GetProcessHeap", win32_GetProcessHeap},
    {"VirtualAlloc", win32_VirtualAlloc},
    {"VirtualFree", win32_VirtualFree},
    {"LocalAlloc", win32_LocalAlloc},
    {"LocalReAlloc", win32_LocalReAlloc},
    {"LocalFree", win32_LocalFree},
    {"GlobalAlloc", win32_GlobalAlloc},
    {"GlobalLock", win32_GlobalLock},
    {"GlobalUnlock", win32_GlobalUnlock},
    {"GlobalFree", win32_GlobalFree},
    {"InitializeCriticalSection", win32_InitializeCriticalSection},
    {"EnterCriticalSection", win32_EnterCriticalSection},
    {"LeaveCriticalSection", win32_LeaveCriticalSection},
    {"DeleteCriticalSection", win32_DeleteCriticalSection},
    {"TlsAlloc", win32_TlsAlloc},
    {"TlsGetValue", win32_TlsGetValue},
    {"TlsSetValue", win32_TlsSetValue},
    {"TlsFree", win32_TlsFree},
    {"CreateEventA", win32_CreateEventA},
    {"SetEvent", win32_SetEvent},
    {"ResetEvent", win32_ResetEvent},
    {"WaitForSingleObject", win32_WaitForSingleObject},
    {"CloseHandle", win32_CloseHandle},
    {"InterlockedIncrement", win32_InterlockedIncrement},
    {"InterlockedDecrement", win32_InterlockedDecrement},
    {"InterlockedExchange", win32_InterlockedExchange},
    {"GetCommandLineA", win32_GetCommandLineA},
    {"GetEnvironmentStrings", win32_GetEnvironmentStrings},
    {"GetEnvironmentStringsA", win32_GetEnvironmentStrings},
    {"GetEnvironmentStringsW", win32_GetEnvironmentStringsW},
    {"FreeEnvironmentStringsA", win32_FreeEnvironmentStringsA},
    {"FreeEnvironmentStringsW", win32_FreeEnvironmentStringsW},
    {"GetACP", win32_GetACP},
    {"GetOEMCP", win32_GetOEMCP},
    {"GetSystemTime", win32_GetSystemTime},
    {"GetLocalTime", win32_GetLocalTime},
    {"GetTimeZoneInformation", win32_GetTimeZoneInformation},
    {"GetTickCount", win32_GetTickCount},
    {"Sleep", win32_Sleep},
    {"ExitProcess", win32_ExitProcess},
    {"GetCPInfo", win32_GetCPInfo},
    {"GetStdHandle", win32_GetStdHandle},
    {"GetFileType", win32_GetFileType},
    {"SetHandleCount", win32_SetHandleCount},
    {"GetStartupInfoA", win32_GetStartupInfoA},
    {"GetCurrentThreadId", win32_GetCurrentThreadId},
    {"GetCurrentThread", win32_GetCurrentThread},
    {"GetCurrentProcessId", win32_GetCurrentProcessId},
    {"GetCurrentProcess", win32_GetCurrentProcess},
    {"GetModuleFileNameA", win32_GetModuleFileNameA},
    {"GetModuleHandleA", win32_GetModuleHandleA},
    {"LoadLibraryA", win32_LoadLibraryA},
    {"FreeLibrary", win32_FreeLibrary},
    {"GetProcAddress", win32_GetProcAddress},
    {"MultiByteToWideChar", win32_MultiByteToWideChar},
    {"WideCharToMultiByte", win32_WideCharToMultiByte},
    {"GetStringTypeA", win32_GetStringTypeA},
    {"GetStringTypeW", win32_GetStringTypeW},
    {"LCMapStringA", win32_LCMapStringA},
    {"LCMapStringW", win32_LCMapStringW},
    {"lstrlenA", win32_lstrlenA},
    {"lstrcpyA", win32_lstrcpyA},
    {"lstrcpynA", win32_lstrcpynA},
    {"lstrcatA", win32_lstrcatA},
    {"lstrcmpA", win32_lstrcmpA},
    {"lstrcmpiA", win32_lstrcmpiA},
    {"timeGetDevCaps", win32_timeGetDevCaps},
    {"timeBeginPeriod", win32_timeBeginPeriod},
    {"timeEndPeriod", win32_timeEndPeriod},
    {"timeSetEvent", win32_timeSetEvent},
    {"timeKillEvent", win32_timeKillEvent},
    {NULL, NULL}
};

static int WINAPI unimplemented_win32_stub(void) {
    void *ret = __builtin_return_address(0);
    fprintf(stderr, "ERROR: Unimplemented Win32 API stub called from %p (smw5 base=%p, hw base=%p)!\n",
            ret, g_modules[0].base, g_num_modules > 1 ? g_modules[1].base : NULL);
    exit(2);
}

static void *lookup_win32_api(const char *name) {
    for (int i = 0; g_win32_apis[i].name; ++i) {
        if (strcmp(g_win32_apis[i].name, name) == 0) return g_win32_apis[i].fn;
    }
    return (void *)unimplemented_win32_stub;
}

static void * WINAPI win32_GetProcAddress(void *hModule, const char *lpProcName) {
    for (int i = 0; i < g_num_modules; ++i) {
        if (g_modules[i].base == (uint8_t *)hModule) {
            if (((uintptr_t)lpProcName >> 16) == 0) {
                return resolve_pe_export_by_ordinal(&g_modules[i], (uint16_t)(uintptr_t)lpProcName);
            } else {
                return resolve_pe_export_by_name(&g_modules[i], lpProcName);
            }
        }
    }
    if (((uintptr_t)lpProcName >> 16) != 0) {
        for (int i = 0; g_win32_apis[i].name; ++i) {
            if (strcmp(g_win32_apis[i].name, lpProcName) == 0) return g_win32_apis[i].fn;
        }
    }
    return NULL;
}

static void *resolve_pe_export_by_ordinal(LoadedPe *pe, uint16_t ordinal) {
    if (!pe || !pe->export_rva) return NULL;
    uint8_t *exp = pe->base + pe->export_rva;
    uint32_t base_ord = *(uint32_t *)(exp + 16);
    uint32_t num_funcs = *(uint32_t *)(exp + 20);
    uint32_t funcs_rva = *(uint32_t *)(exp + 28);
    if (ordinal < base_ord || (ordinal - base_ord) >= num_funcs) return NULL;
    uint32_t *funcs = (uint32_t *)(pe->base + funcs_rva);
    uint32_t rva = funcs[ordinal - base_ord];
    return rva ? (pe->base + rva) : NULL;
}

static void *resolve_pe_export_by_name(LoadedPe *pe, const char *name) {
    if (!pe || !pe->export_rva || !name) return NULL;
    uint8_t *exp = pe->base + pe->export_rva;
    uint32_t num_funcs = *(uint32_t *)(exp + 20);
    uint32_t num_names = *(uint32_t *)(exp + 24);
    uint32_t funcs_rva = *(uint32_t *)(exp + 28);
    uint32_t names_rva = *(uint32_t *)(exp + 32);
    uint32_t ords_rva  = *(uint32_t *)(exp + 36);
    uint32_t *funcs = (uint32_t *)(pe->base + funcs_rva);
    uint32_t *names = (uint32_t *)(pe->base + names_rva);
    uint16_t *ords  = (uint16_t *)(pe->base + ords_rva);
    for (uint32_t i = 0; i < num_names; ++i) {
        const char *ename = (const char *)(pe->base + names[i]);
        if (strcmp(ename, name) == 0) {
            uint16_t ord_idx = ords[i];
            if (ord_idx < num_funcs) {
                return pe->base + funcs[ord_idx];
            }
        }
    }
    return NULL;
}

static LoadedPe *load_pe_dll(const char *path_or_name) {
    for (int i = 0; i < g_num_modules; ++i) {
        if (strcasecmp(g_modules[i].name, path_or_name) == 0) return &g_modules[i];
    }
    char fullpath[1024];
    if (strchr(path_or_name, '/') || strchr(path_or_name, '\\')) {
        snprintf(fullpath, sizeof(fullpath), "%s", path_or_name);
    } else {
        snprintf(fullpath, sizeof(fullpath), "%s/%s", g_dll_dir, path_or_name);
    }
    FILE *f = fopen(fullpath, "rb");
#ifndef _WIN32
    if (!f && !strchr(path_or_name, '/') && !strchr(path_or_name, '\\')) {
        DIR *d = opendir(g_dll_dir);
        if (d) {
            struct dirent *de;
            while ((de = readdir(d)) != NULL) {
                if (strcasecmp(de->d_name, path_or_name) == 0) {
                    snprintf(fullpath, sizeof(fullpath), "%s/%s", g_dll_dir, de->d_name);
                    f = fopen(fullpath, "rb");
                    break;
                }
            }
            closedir(d);
        }
    }
#endif
    if (!f) {
        fprintf(stderr, "Failed to open DLL: %s\n", fullpath);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size_t fsize = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *raw = (uint8_t *)malloc(fsize);
    if (fread(raw, 1, fsize, f) != fsize) {
        fclose(f); free(raw); return NULL;
    }
    fclose(f);

    uint32_t pe_off = *(uint32_t *)(raw + 0x3C);
    uint8_t *pe_hdr = raw + pe_off;
    uint16_t num_sections = *(uint16_t *)(pe_hdr + 6);
    uint16_t opt_hdr_size = *(uint16_t *)(pe_hdr + 20);
    uint8_t *opt_hdr = pe_hdr + 24;
    uint32_t entry_rva = *(uint32_t *)(opt_hdr + 16);
    uint32_t preferred_base = *(uint32_t *)(opt_hdr + 28);
    uint32_t size_of_image = *(uint32_t *)(opt_hdr + 56);
    uint32_t size_of_headers = *(uint32_t *)(opt_hdr + 60);

#ifdef _WIN32
    uint8_t *map_base = (uint8_t *)VirtualAlloc(NULL, size_of_image,
                                                MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!map_base) {
        free(raw); return NULL;
    }
#else
    uint8_t *map_base = (uint8_t *)mmap(
        NULL, size_of_image,
        PROT_READ | PROT_WRITE | PROT_EXEC,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0
    );
    if (map_base == MAP_FAILED) {
        free(raw); return NULL;
    }
#endif
    memset(map_base, 0, size_of_image);
    memcpy(map_base, raw, size_of_headers);

    uint8_t *sec_table = opt_hdr + opt_hdr_size;
    for (uint16_t i = 0; i < num_sections; ++i) {
        uint8_t *sec = sec_table + i * 40;
        uint32_t vma_rva = *(uint32_t *)(sec + 12);
        uint32_t raw_sz  = *(uint32_t *)(sec + 16);
        uint32_t raw_off = *(uint32_t *)(sec + 20);
        if (raw_sz > 0 && raw_off + raw_sz <= fsize && vma_rva + raw_sz <= size_of_image) {
            memcpy(map_base + vma_rva, raw + raw_off, raw_sz);
        }
    }

    /* Apply Base Relocations */
    int32_t delta = (int32_t)((uintptr_t)map_base - (uintptr_t)preferred_base);
    uint32_t reloc_rva  = *(uint32_t *)(opt_hdr + 96 + 5 * 8);
    uint32_t reloc_size = *(uint32_t *)(opt_hdr + 96 + 5 * 8 + 4);
    if (delta != 0 && reloc_rva && reloc_size) {
        uint32_t pos = 0;
        while (pos + 8 <= reloc_size) {
            uint32_t page_rva = *(uint32_t *)(map_base + reloc_rva + pos);
            uint32_t blk_size = *(uint32_t *)(map_base + reloc_rva + pos + 4);
            if (blk_size < 8) break;
            uint32_t count = (blk_size - 8) / 2;
            uint16_t *entries = (uint16_t *)(map_base + reloc_rva + pos + 8);
            for (uint32_t j = 0; j < count; ++j) {
                uint16_t type = entries[j] >> 12;
                uint16_t off  = entries[j] & 0x0FFF;
                if (type == 3) { /* IMAGE_REL_BASED_HIGHLOW */
                    *(uint32_t *)(map_base + page_rva + off) += delta;
                }
            }
            pos += blk_size;
        }
    }

    /* Resolve Imports */
    uint32_t import_rva = *(uint32_t *)(opt_hdr + 96 + 1 * 8);
    if (import_rva) {
        uint8_t *imp = map_base + import_rva;
        while (1) {
            uint32_t orig_thunk = *(uint32_t *)(imp + 0);
            uint32_t name_rva   = *(uint32_t *)(imp + 12);
            uint32_t iat_rva    = *(uint32_t *)(imp + 16);
            if (!orig_thunk && !name_rva && !iat_rva) break;
            uint32_t *thunk = (uint32_t *)(map_base + (orig_thunk ? orig_thunk : iat_rva));
            uint32_t *iat   = (uint32_t *)(map_base + iat_rva);
            while (*thunk) {
                uint32_t val = *thunk;
                if (val & 0x80000000) {
                    *iat = (uint32_t)(uintptr_t)unimplemented_win32_stub;
                } else {
                    const char *fn_name = (const char *)(map_base + val + 2);
                    *iat = (uint32_t)(uintptr_t)lookup_win32_api(fn_name);
                }
                thunk++;
                iat++;
            }
            imp += 20;
        }
    }

    LoadedPe *mod = &g_modules[g_num_modules++];
    const char *slash = strrchr(path_or_name, '/');
    snprintf(mod->name, sizeof(mod->name), "%s", slash ? (slash + 1) : path_or_name);
    mod->base = map_base;
    mod->size_of_image = size_of_image;
    mod->entry_rva = entry_rva;
    mod->export_rva  = *(uint32_t *)(opt_hdr + 96 + 0 * 8);
    mod->export_size = *(uint32_t *)(opt_hdr + 96 + 0 * 8 + 4);

    free(raw);

    /* Initialize CRT heap & emulator singleton */
    if (strcasecmp(mod->name, "M5_EmuHw.dll") == 0) {
        typedef int (CDECL *fn_init_t)(void);
        typedef void (CDECL *fn_void_t)(void);
        ((fn_init_t)(mod->base + 0x17a1c))(); /* _heap_init */
        ((fn_init_t)(mod->base + 0x186d6))(); /* _mtinit */
        ((fn_void_t)(mod->base + 0x181c0))(); /* _ioinit */
        ((fn_void_t)(mod->base + 0x14f9a))(); /* _onexitinit */
        *(uint32_t *)(mod->base + 0xf1310) = (uint32_t)(uintptr_t)(mod->base + 0x29084);
        {
            void *this_ptr = mod->base + 0xf1310;
            void *fn = mod->base + 0x01910;
            __asm__ volatile("movl %0, %%ecx; call *%1" : : "r"(this_ptr), "r"(fn) : "eax", "edx", "ecx", "memory");
        }
    } else if (strcasecmp(mod->name, "M5_EmuSmw5.dll") == 0) {
        typedef int (CDECL *fn_init_t)(void);
        typedef void (CDECL *fn_void_t)(void);
        ((fn_init_t)(mod->base + 0xe6cbc))(); /* _heap_init */
        ((fn_init_t)(mod->base + 0xe899e))(); /* _mtinit */
        ((fn_void_t)(mod->base + 0xe8488))(); /* _ioinit */
        ((fn_void_t)(mod->base + 0xe3364))(); /* _onexitinit */
        *(uint32_t *)(mod->base + 0x23b6e0) = (uint32_t)(uintptr_t)(mod->base + 0x118378);
        {
            void *this_ptr = mod->base + 0x23b6e0;
            void *fn = mod->base + 0xa9b20;
            __asm__ volatile("movl %0, %%ecx; call *%1" : : "r"(this_ptr), "r"(fn) : "eax", "edx", "ecx", "memory");
        }
        strcpy((char *)(mod->base + 0x23b6e0 + 0x128), (const char *)(mod->base + 0x240568));
        /* Stub CStdioFile::Open (0xecfaf) to return 0 (no M5_EmuSmw5.ini needed) */
        mod->base[0xecfaf + 0] = 0x33;
        mod->base[0xecfaf + 1] = 0xC0;
        mod->base[0xecfaf + 2] = 0xC2;
        mod->base[0xecfaf + 3] = 0x0C;
        mod->base[0xecfaf + 4] = 0x00;
        /* Stub AfxBeginThread (0xec264) to return 0 (we pump 0x1330 synchronously) */
        mod->base[0xec264 + 0] = 0x33;
        mod->base[0xec264 + 1] = 0xC0;
        mod->base[0xec264 + 2] = 0xC2;
        mod->base[0xec264 + 3] = 0x18;
        mod->base[0xec264 + 4] = 0x00;
    }

    return mod;
}

static volatile int g_eof_reached = 0;
static int CDECL smaf_callback(int32_t event) {
    if ((event & 0xFF) == 0x7F || (event & 0xFF) == 0x7E || event == -1) {
        g_eof_reached = 1;
    }
    return 0;
}


static void setup_win32_teb(void) {
#ifndef _WIN32
    void *teb = calloc(1, 4096);
    ((uint32_t *)teb)[0] = 0xFFFFFFFFu;
    ((uint32_t *)teb)[1] = 0x7FFFFFFFu;
    ((uint32_t *)teb)[2] = 0x00001000u;
    ((uint32_t *)teb)[6] = (uint32_t)(uintptr_t)teb;
    struct user_desc desc;
    memset(&desc, 0, sizeof(desc));
    desc.entry_number = -1;
    desc.base_addr = (uint32_t)(uintptr_t)teb;
    desc.limit = 4095;
    desc.seg_32bit = 1;
    desc.useable = 1;
    if (syscall(SYS_set_thread_area, &desc) == 0) {
        unsigned int sel = (desc.entry_number << 3) | 3;
        __asm__ volatile("movw %w0, %%fs" : : "r"(sel));
    }
#endif
}

static void write_wav_header(FILE *f, uint32_t sample_rate, uint32_t num_frames) {
    uint32_t data_bytes = num_frames * 4;
    uint32_t riff_size = 36 + data_bytes;
    uint16_t fmt = 1, ch = 2, block_align = 4, bps = 16;
    uint32_t byte_rate = sample_rate * 4;
    uint32_t fmt_len = 16;

    fwrite("RIFF", 1, 4, f);
    fwrite(&riff_size, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmt_len, 4, 1, f);
    fwrite(&fmt, 2, 1, f);
    fwrite(&ch, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bps, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&data_bytes, 4, 1, f);
}

typedef int (CDECL *MaSound_EmuInitialize_t)(uint32_t sr, uint32_t emuType, uint32_t mode);
typedef int (CDECL *MaSound_Initialize_t)(void);
typedef int (CDECL *MaSound_DeviceControl_t)(uint32_t cmd, uint32_t p1, uint32_t p2, uint32_t p3);
typedef int (CDECL *MaSound_Create_t)(uint32_t type);
typedef int (CDECL *MaSound_Load_t)(int funcId, uint8_t *buf, uint32_t size, uint32_t flag, void *cb, uint32_t ext);
typedef int (CDECL *MaSound_Open_t)(int funcId, int fileId, uint32_t p1, uint32_t p2);
typedef int (CDECL *MaSound_Standby_t)(int funcId, int fileId, uint32_t ext);
typedef int (CDECL *MaSound_Control_t)(int funcId, int fileId, uint32_t cmd, void *param, uint32_t ext);
typedef int (CDECL *MaSound_Seek_t)(int funcId, int fileId, uint32_t pos, uint32_t flag, uint32_t ext);
typedef int (CDECL *MaSound_Start_t)(int funcId, int fileId, uint32_t playMode, uint32_t ext);
typedef int (CDECL *MaSound_Generate_t)(int16_t *bufL, int16_t *bufR, uint32_t samples);

int main(int argc, char **argv) {
    setup_win32_teb();

    if (argc >= 3 && strcmp(argv[1], "--probe") == 0) {
        snprintf(g_dll_dir, sizeof(g_dll_dir), "%s", argv[2]);
        LoadedPe *smw5 = load_pe_dll("M5_EmuSmw5.dll");
        if (!smw5) {
            fprintf(stderr, "ERROR: Failed to load M5_EmuSmw5.dll / M5_EmuHw.dll from %s\n", g_dll_dir);
            return 1;
        }
        MaSound_EmuInitialize_t EmuInit = (MaSound_EmuInitialize_t)resolve_pe_export_by_ordinal(smw5, 2);
        MaSound_Initialize_t Init = (MaSound_Initialize_t)resolve_pe_export_by_ordinal(smw5, 5);
        if (!EmuInit || !Init || EmuInit(44100, 0, 0) != 0 || Init() != 0) {
            fprintf(stderr, "ERROR: MaSound_EmuInitialize / MaSound_Initialize failed\n");
            return 1;
        }
        printf("OK: M5_EmuSmw5.dll + M5_EmuHw.dll initialized\n");
        return 0;
    }

    if (argc >= 4 && (strcmp(argv[1], "--duration") == 0 || (strcmp(argv[1], "--render") == 0 && argc >= 5))) {
        int is_render = (strcmp(argv[1], "--render") == 0);
        snprintf(g_dll_dir, sizeof(g_dll_dir), "%s", argv[2]);
        const char *mmf_path = argv[3];
        const char *wav_path = is_render ? argv[4] : NULL;

        LoadedPe *smw5 = load_pe_dll("M5_EmuSmw5.dll");
        if (!smw5) {
            fprintf(stderr, "ERROR: Failed to load M5_EmuSmw5.dll / M5_EmuHw.dll from %s\n", g_dll_dir);
            return 1;
        }

        MaSound_EmuInitialize_t EmuInit = (MaSound_EmuInitialize_t)resolve_pe_export_by_ordinal(smw5, 2);
        MaSound_Generate_t Generate     = (MaSound_Generate_t)resolve_pe_export_by_ordinal(smw5, 3);
        MaSound_Initialize_t Init       = (MaSound_Initialize_t)resolve_pe_export_by_ordinal(smw5, 5);
        MaSound_DeviceControl_t DevCtrl = (MaSound_DeviceControl_t)resolve_pe_export_by_ordinal(smw5, 7);
        MaSound_Create_t Create         = (MaSound_Create_t)resolve_pe_export_by_ordinal(smw5, 8);
        MaSound_Load_t Load             = (MaSound_Load_t)resolve_pe_export_by_ordinal(smw5, 9);
        MaSound_Open_t Open             = (MaSound_Open_t)resolve_pe_export_by_ordinal(smw5, 10);
        MaSound_Control_t Control       = (MaSound_Control_t)resolve_pe_export_by_ordinal(smw5, 11);
        MaSound_Standby_t Standby       = (MaSound_Standby_t)resolve_pe_export_by_ordinal(smw5, 12);
        MaSound_Seek_t Seek             = (MaSound_Seek_t)resolve_pe_export_by_ordinal(smw5, 13);
        MaSound_Start_t Start           = (MaSound_Start_t)resolve_pe_export_by_ordinal(smw5, 14);

        if (!EmuInit || !Init || EmuInit(44100, 0, 0) != 0 || Init() != 0) {
            fprintf(stderr, "ERROR: MaSound_EmuInitialize / MaSound_Initialize failed\n");
            return 1;
        }

        DevCtrl(0x0d, 0, 0, 0);
        DevCtrl(0x02, 0x1f, 0, 0);
        DevCtrl(0x03, 0, 0x1f, 0x1f);
        DevCtrl(0x04, 0x1f, 0, 0);
        DevCtrl(0x05, 0x02, 0, 0);
        DevCtrl(0x06, 0, 0, 0);
        DevCtrl(0x08, 0x02, 0, 0);
        DevCtrl(0x09, 0, 0, 0);

        FILE *mf = fopen(mmf_path, "rb");
        if (!mf) {
            fprintf(stderr, "ERROR: Cannot open %s\n", mmf_path);
            return 1;
        }
        fseek(mf, 0, SEEK_END);
        long msize_l = ftell(mf);
        fseek(mf, 0, SEEK_SET);
        if (msize_l <= 0) {
            fclose(mf);
            return 1;
        }
        uint32_t msize = (uint32_t)msize_l;
        uint8_t *mbuf = (uint8_t *)malloc(msize);
        if (!mbuf || fread(mbuf, 1, msize, mf) != msize) {
            free(mbuf);
            fclose(mf);
            return 1;
        }
        fclose(mf);

        int funcId = Create(1);
        if (funcId < 0) return 1;
        int fileId = Load(funcId, mbuf, msize, 1, (void *)smaf_callback, 0);
        if (fileId < 0) return 1;
        if (Open(funcId, fileId, 0, 0) != 0) return 1;
        if (Standby(funcId, fileId, 0) != 0) return 1;

        int durationMs = Control(funcId, fileId, 5, 0, 0);
        if (!is_render) {
            if (durationMs <= 0) return 1;
            printf("%d\n", durationMs);
            return 0;
        }

        if (durationMs <= 0 || durationMs > 600000) {
            durationMs = 60000;
        }

        /* Use vol = 91 (-4.4 dB master headroom) and unity DAC output mode (0x61b7 = 0xb4)
         * to prevent hard clipping on dense multi-track FM + ADPCM hybrid .mmf files. */
        uint8_t tempo = 100, key = 0, vol = 91;
        Control(funcId, fileId, 1, &tempo, 0);
        Control(funcId, fileId, 2, &key, 0);
        Control(funcId, fileId, 0, &vol, 0);
        Seek(funcId, fileId, 0, 0, 0);
        g_eof_reached = 0;
        if (Start(funcId, fileId, 1, 0) != 0) {
            fprintf(stderr, "ERROR: MaSound_Start failed\n");
            return 1;
        }
        LoadedPe *hw = load_pe_dll("M5_EmuHw.dll");
        if (hw && hw->base) {
            hw->base[0xf1310 + 0x61a7] = 0xf0;
            hw->base[0xf1310 + 0x61a8] = 0xf0;
            hw->base[0xf1310 + 0x61b7] = 0xb4;
        }

        FILE *wf = fopen(wav_path, "wb");
        if (!wf) {
            fprintf(stderr, "ERROR: Cannot open %s for writing\n", wav_path);
            return 1;
        }
        write_wav_header(wf, 44100, 0);

        int16_t bufL[441], bufR[441], stereo[882];
        int total_blocks = (durationMs / 10) + 50;
        uint32_t total_frames = 0;
        for (int blk = 0; blk < total_blocks; ++blk) {
            memset(bufL, 0, sizeof(bufL));
            memset(bufR, 0, sizeof(bufR));
            Generate(bufL, bufR, 441);
            ((void (CDECL *)(void))(smw5->base + 0x1330))();
            for (int i = 0; i < 441; ++i) {
                stereo[i * 2]     = bufL[i];
                stereo[i * 2 + 1] = bufR[i];
            }
            fwrite(stereo, sizeof(int16_t), 882, wf);
            total_frames += 441;
            if (g_eof_reached) {
                for (int t = 0; t < 15; ++t) {
                    memset(bufL, 0, sizeof(bufL));
                    memset(bufR, 0, sizeof(bufR));
                    Generate(bufL, bufR, 441);
                    for (int i = 0; i < 441; ++i) {
                        stereo[i * 2]     = bufL[i];
                        stereo[i * 2 + 1] = bufR[i];
                    }
                    fwrite(stereo, sizeof(int16_t), 882, wf);
                    total_frames += 441;
                }
                break;
            }
        }

        fseek(wf, 0, SEEK_SET);
        write_wav_header(wf, 44100, total_frames);
        fclose(wf);
        return 0;
    }

    fprintf(stderr, "Usage:\n"
                    "  %s --probe <dll_dir>\n"
                    "  %s --duration <dll_dir> <in.mmf>\n"
                    "  %s --render <dll_dir> <in.mmf> <out.wav>\n",
            argv[0], argv[0], argv[0]);
    return 1;
}
