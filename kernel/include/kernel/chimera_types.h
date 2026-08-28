// fundamental types
#pragma once
#ifndef CHIMERA_TYPES_H
#define CHIMERA_TYPES_H

#ifndef KERNEL
#define KERNEL 1
#endif
#ifndef KERNEL_PRIVATE
#define KERNEL_PRIVATE 1
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdatomic.h>

#define CHIMERA_PACKED          __attribute__((packed))
#define CHIMERA_ALIGNED(n)      __attribute__((aligned(n)))
#define CHIMERA_NORETURN        __attribute__((noreturn))
#define CHIMERA_NOINLINE        __attribute__((noinline))
#define CHIMERA_ALWAYS_INLINE   __attribute__((always_inline)) static inline
#define CHIMERA_COLD            __attribute__((cold))
#define CHIMERA_HOT             __attribute__((hot))
#define CHIMERA_USED            __attribute__((used))
#define CHIMERA_UNUSED          __attribute__((unused))
#define CHIMERA_SECTION(s)      __attribute__((section(s)))
#define CHIMERA_VISIBILITY(v)   __attribute__((visibility(v)))
#define CHIMERA_WARN_UNUSED     __attribute__((warn_unused_result))
#define CHIMERA_NODISCARD       [[nodiscard]]
#define CHIMERA_LIKELY(x)       __builtin_expect(!!(x), 1)
#define CHIMERA_UNLIKELY(x)     __builtin_expect(!!(x), 0)
#define CHIMERA_UNREACHABLE()   __builtin_unreachable()
#define CHIMERA_BARRIER()       __asm__ volatile("" ::: "memory")

typedef uint8_t     u8;
typedef uint16_t    u16;
typedef uint32_t    u32;
typedef uint64_t    u64;
typedef __uint128_t u128;

typedef int8_t      s8;
typedef int16_t     s16;
typedef int32_t     s32;
typedef int64_t     s64;
typedef __int128_t  s128;

typedef s8          i8;
typedef s16         i16;
typedef s32         i32;
typedef s64         i64;
typedef s128        i128;

typedef uintptr_t   uptr;
typedef intptr_t    sptr;
typedef ptrdiff_t   pdiff;
typedef size_t      usize;
typedef sptr        ssize;

#if !defined(__cplusplus) && __STDC_VERSION__ < 202311L
#  define nullptr ((void*)0)
#endif

typedef uptr        chimera_vaddr_t;
typedef u64         chimera_paddr_t;
typedef u64         chimera_psize_t;
typedef u64         chimera_offset_t;
typedef u64         chimera_size_t;

typedef u32         chimera_pid_t;
typedef u32         chimera_tid_t;
typedef u32         chimera_uid_t;
typedef u32         chimera_gid_t;
typedef u32         chimera_port_name_t;
typedef u64         chimera_port_gen_t;
typedef u64         chimera_task_id_t;
typedef u64         chimera_obj_id_t;

typedef s32         chimera_error_t;

#define CHIMERA_SUCCESS          ((chimera_error_t)  0)
#define CHIMERA_ERR_GENERIC      ((chimera_error_t) -1)
#define CHIMERA_ERR_NOMEM        ((chimera_error_t) -2)
#define CHIMERA_ERR_INVALID      ((chimera_error_t) -3)
#define CHIMERA_ERR_NORESOURCE   ((chimera_error_t) -4)
#define CHIMERA_ERR_NOPERM       ((chimera_error_t) -5)
#define CHIMERA_ERR_NOTFOUND     ((chimera_error_t) -6)
#define CHIMERA_ERR_BUSY         ((chimera_error_t) -7)
#define CHIMERA_ERR_TIMEOUT      ((chimera_error_t) -8)
#define CHIMERA_ERR_OVERFLOW     ((chimera_error_t) -9)
#define CHIMERA_ERR_ALIGN        ((chimera_error_t)-10)
#define CHIMERA_ERR_RANGE        ((chimera_error_t)-11)
#define CHIMERA_ERR_IPC          ((chimera_error_t)-12)
#define CHIMERA_ERR_PORT_DEAD    ((chimera_error_t)-13)
#define CHIMERA_ERR_PORT_FULL    ((chimera_error_t)-14)
#define CHIMERA_ERR_PORT_EMPTY   ((chimera_error_t)-16)
#define CHIMERA_ERR_NOTSUP       ((chimera_error_t)-15)
#define CHIMERA_ERR_WOULDBLOCK   ((chimera_error_t)-17)
#define CHIMERA_ERR_NOTCONN      ((chimera_error_t)-18)
#define CHIMERA_ERR_NOT_FOUND    CHIMERA_ERR_NOTFOUND
#define CHIMERA_ERR_NOT_SUPPORTED CHIMERA_ERR_NOTSUP
#define CHIMERA_ERR_UNSUPPORTED  CHIMERA_ERR_NOTSUP
#define CHIMERA_ERR_NOT_CONNECTED CHIMERA_ERR_NOTCONN

#define CHIMERA_SUCCEEDED(e)     ((e) == CHIMERA_SUCCESS)
#define CHIMERA_FAILED(e)        ((e) != CHIMERA_SUCCESS)

typedef u64         chimera_abstime_t;
typedef s64         chimera_reltime_t;

#define CHIMERA_TRUE    true
#define CHIMERA_FALSE   false

#ifndef CHIMERA_PAGE_SIZE
#  define CHIMERA_PAGE_SIZE 4096
#endif
#define CHIMERA_PAGE_MASK       ((chimera_size_t)(CHIMERA_PAGE_SIZE - 1))
#define CHIMERA_PAGE_ALIGN(a)   (((chimera_vaddr_t)(a) + CHIMERA_PAGE_MASK) & ~CHIMERA_PAGE_MASK)
#define CHIMERA_PAGE_TRUNC(a)   ((chimera_vaddr_t)(a) & ~CHIMERA_PAGE_MASK)
#define CHIMERA_PAGE_OFFSET(a)  ((chimera_vaddr_t)(a) &  CHIMERA_PAGE_MASK)
#define CHIMERA_PAGES(bytes)    (((bytes) + CHIMERA_PAGE_MASK) / CHIMERA_PAGE_SIZE)

#define CHIMERA_ALIGN(x, a)     (((x) + (typeof(x))(a) - 1) & ~((typeof(x))(a) - 1))
#define CHIMERA_IS_ALIGNED(x,a) (!((x) & ((typeof(x))(a) - 1)))
#define CHIMERA_MIN(a, b)       ((a) < (b) ? (a) : (b))
#define CHIMERA_MAX(a, b)       ((a) > (b) ? (a) : (b))
#define CHIMERA_CLAMP(x,lo,hi)  (CHIMERA_MAX((lo), CHIMERA_MIN((x), (hi))))
#define CHIMERA_ARRAY_LEN(a)    (sizeof(a) / sizeof((a)[0]))
#define CHIMERA_MEMBER_SIZE(t,m) (sizeof(((t*)0)->m))
#define CHIMERA_OFFSET_OF(t, m) __builtin_offsetof(t, m)
#define CHIMERA_CONTAINER_OF(ptr, type, member) \
    ((type *)((u8 *)(ptr) - CHIMERA_OFFSET_OF(type, member)))

#define CHIMERA_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)

CHIMERA_STATIC_ASSERT(sizeof(u8)   == 1, "u8 size mismatch");
CHIMERA_STATIC_ASSERT(sizeof(u16)  == 2, "u16 size mismatch");
CHIMERA_STATIC_ASSERT(sizeof(u32)  == 4, "u32 size mismatch");
CHIMERA_STATIC_ASSERT(sizeof(u64)  == 8, "u64 size mismatch");
CHIMERA_STATIC_ASSERT(sizeof(uptr) == 8, "uptr must be 64-bit");

#define CHIMERA_VADDR_NULL  ((chimera_vaddr_t)0)
#define CHIMERA_PADDR_NULL  ((chimera_paddr_t)0)
#define CHIMERA_PORT_NULL   ((chimera_port_name_t)0)
#define CHIMERA_PORT_DEAD   ((chimera_port_name_t)(~0ULL))

#ifdef __cplusplus
extern "C" {
#endif
extern u64 g_hhdm_base;
#ifdef __cplusplus
}
#endif
#define HHDM_BASE g_hhdm_base
#define HHDM_PTR(phys) ((void *)((u64)(phys) + g_hhdm_base))

#endif
