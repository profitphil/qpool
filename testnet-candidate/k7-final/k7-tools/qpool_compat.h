// qpool_compat.h -- force-included (-include) into every translation unit of the QPOOL compile harness.
// Portability shims so the Qubic core headers (written for MSVC / Linux-clang with LLP64 assumptions)
// parse and compile with Apple clang targeting x86_64 macOS (LP64). None of this is contract code and
// none of it ever appears in an error report. It has NO effect on what the contract must obey.
#pragma once

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cpuid.h>
#undef __cpuid
#include <immintrin.h>

// --- 128-bit multiply / shift intrinsics that only MSVC (and not clang) provides -----------------
static inline unsigned long long _umul128(unsigned long long a, unsigned long long b, unsigned long long* hi)
{
    unsigned __int128 p = (unsigned __int128)a * b;
    *hi = (unsigned long long)(p >> 64);
    return (unsigned long long)p;
}
static inline long long _mul128(long long a, long long b, long long* hi)
{
    __int128 p = (__int128)a * b;
    *hi = (long long)(p >> 64);
    return (long long)p;
}
static inline unsigned long long __shiftleft128(unsigned long long lo, unsigned long long hi, unsigned char s)
{
    s = (unsigned char)(s & 63);
    return s ? ((hi << s) | (lo >> (64 - s))) : hi;
}
static inline unsigned long long __shiftright128(unsigned long long lo, unsigned long long hi, unsigned char s)
{
    s = (unsigned char)(s & 63);
    return s ? ((lo >> s) | (hi << (64 - s))) : lo;
}

// --- MSVC _Interlocked* family on top of the GCC/clang __sync builtins ---------------------------
// 'long' is 8 bytes on LP64, so the 'long' overloads are 64-bit operations on a real 'long' object.
static inline char _InterlockedCompareExchange8(volatile char* p, char x, char c) { return __sync_val_compare_and_swap(p, c, x); }
static inline char _InterlockedExchange8(volatile char* p, char x) { return __sync_lock_test_and_set(p, x); }
static inline long _InterlockedCompareExchange(volatile long* p, long x, long c) { return __sync_val_compare_and_swap(p, c, x); }
static inline long _InterlockedExchange(volatile long* p, long x) { return __sync_lock_test_and_set(p, x); }
static inline int  _InterlockedExchange(volatile int* p, int x) { return __sync_lock_test_and_set(p, x); }
static inline long _InterlockedIncrement(volatile long* p) { return __sync_add_and_fetch(p, 1); }
static inline long _InterlockedDecrement(volatile long* p) { return __sync_sub_and_fetch(p, 1); }
static inline long long _InterlockedCompareExchange64(volatile long long* p, long long x, long long c) { return __sync_val_compare_and_swap(p, c, x); }
static inline long long _InterlockedExchange64(volatile long long* p, long long x) { return __sync_lock_test_and_set(p, x); }
static inline long long _InterlockedIncrement64(volatile long long* p) { return __sync_add_and_fetch(p, 1); }
static inline long long _InterlockedAnd64(volatile long long* p, long long x) { return __sync_fetch_and_and(p, x); }
static inline long long _InterlockedExchangeAdd64(volatile long long* p, long long x) { return __sync_fetch_and_add(p, x); }
static inline long long _interlockedadd64(volatile long long* p, long long x) { return __sync_add_and_fetch(p, x); }

// --- MSVC __cpuid(int[4], leaf) -------------------------------------------------------------------
static inline void __cpuid(int out[4], int leaf)
{
    unsigned int a = 0, b = 0, c = 0, d = 0;
    __get_cpuid((unsigned int)leaf, &a, &b, &c, &d);
    out[0] = (int)a; out[1] = (int)b; out[2] = (int)c; out[3] = (int)d;
}

// --- MSVC calling-convention keyword and CRT functions used by the core / its tests ---------------
#define _cdecl
static inline int _wfopen_s(FILE** f, const wchar_t* name, const wchar_t* mode)
{
    char n[1024], m[16]; size_t i = 0;
    for (; name[i] && i < sizeof(n) - 1; ++i) n[i] = (char)name[i];
    n[i] = 0; i = 0;
    for (; mode[i] && i < sizeof(m) - 1; ++i) m[i] = (char)mode[i];
    m[i] = 0;
    *f = fopen(n, m);
    return *f ? 0 : 1;
}
static inline int wcstombs_s(size_t* conv, char* dst, const wchar_t* src, size_t max)
{
    size_t i = 0;
    for (; src[i] && i + 1 < max; ++i) dst[i] = (char)src[i];
    dst[i] = 0;
    if (conv) *conv = i + 1;
    return 0;
}
