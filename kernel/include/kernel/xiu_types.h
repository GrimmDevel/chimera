// fundamental types
#pragma once
#ifndef XIU_TYPES_H
#define XIU_TYPES_H

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

#define XIU_PACKED          __attribute__((packed))
#define XIU_ALIGNED(n)      __attribute__((aligned(n)))
#define XIU_NORETURN        __attribute__((noreturn))
#define XIU_NOINLINE        __attribute__((noinline))
#define XIU_ALWAYS_INLINE   __attribute__((always_inline)) static inline
#define XIU_COLD            __attribute__((cold))
#define XIU_HOT             __attribute__((hot))
#define XIU_USED            __attribute__((used))
#define XIU_UNUSED          __attribute__((unused))
#define XIU_SECTION(s)      __attribute__((section(s)))
#define XIU_VISIBILITY(v)   __attribute__((visibility(v)))
#define XIU_WARN_UNUSED     __attribute__((warn_unused_result))
#define XIU_NODISCARD       [[nodiscard]]
#define XIU_LIKELY(x)       __builtin_expect(!!(x), 1)
#define XIU_UNLIKELY(x)     __builtin_expect(!!(x), 0)
#define XIU_UNREACHABLE()   __builtin_unreachable()
#define XIU_BARRIER()       __asm__ volatile("" ::: "memory")

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

typedef uptr        xiu_vaddr_t;
typedef u64         xiu_paddr_t;
typedef u64         xiu_psize_t;
typedef u64         xiu_offset_t;
typedef u64         xiu_size_t;

typedef u32         xiu_pid_t;
typedef u32         xiu_tid_t;
typedef u32         xiu_uid_t;
typedef u32         xiu_gid_t;
typedef u32         xiu_port_name_t;
typedef u64         xiu_port_gen_t;
typedef u64         xiu_task_id_t;
typedef u64         xiu_obj_id_t;

typedef s32         xiu_error_t;

#define XIU_SUCCESS          ((xiu_error_t)  0)
#define XIU_ERR_GENERIC      ((xiu_error_t) -1)
#define XIU_ERR_NOMEM        ((xiu_error_t) -2)
#define XIU_ERR_INVALID      ((xiu_error_t) -3)
#define XIU_ERR_NORESOURCE   ((xiu_error_t) -4)
#define XIU_ERR_NOPERM       ((xiu_error_t) -5)
#define XIU_ERR_NOTFOUND     ((xiu_error_t) -6)
#define XIU_ERR_BUSY         ((xiu_error_t) -7)
#define XIU_ERR_TIMEOUT      ((xiu_error_t) -8)
#define XIU_ERR_OVERFLOW     ((xiu_error_t) -9)
#define XIU_ERR_ALIGN        ((xiu_error_t)-10)
#define XIU_ERR_RANGE        ((xiu_error_t)-11)
#define XIU_ERR_IPC          ((xiu_error_t)-12)
#define XIU_ERR_PORT_DEAD    ((xiu_error_t)-13)
#define XIU_ERR_PORT_FULL    ((xiu_error_t)-14)
#define XIU_ERR_PORT_EMPTY   ((xiu_error_t)-16)
#define XIU_ERR_NOTSUP       ((xiu_error_t)-15)
#define XIU_ERR_WOULDBLOCK   ((xiu_error_t)-17)
#define XIU_ERR_NOTCONN      ((xiu_error_t)-18)
#define XIU_ERR_NOT_FOUND    XIU_ERR_NOTFOUND
#define XIU_ERR_NOT_SUPPORTED XIU_ERR_NOTSUP
#define XIU_ERR_UNSUPPORTED  XIU_ERR_NOTSUP
#define XIU_ERR_NOT_CONNECTED XIU_ERR_NOTCONN

#define XIU_SUCCEEDED(e)     ((e) == XIU_SUCCESS)
#define XIU_FAILED(e)        ((e) != XIU_SUCCESS)

typedef u64         xiu_abstime_t;
typedef s64         xiu_reltime_t;

#define XIU_TRUE    true
#define XIU_FALSE   false

#ifndef XIU_PAGE_SIZE
#  define XIU_PAGE_SIZE 4096
#endif
#define XIU_PAGE_MASK       ((xiu_size_t)(XIU_PAGE_SIZE - 1))
#define XIU_PAGE_ALIGN(a)   (((xiu_vaddr_t)(a) + XIU_PAGE_MASK) & ~XIU_PAGE_MASK)
#define XIU_PAGE_TRUNC(a)   ((xiu_vaddr_t)(a) & ~XIU_PAGE_MASK)
#define XIU_PAGE_OFFSET(a)  ((xiu_vaddr_t)(a) &  XIU_PAGE_MASK)
#define XIU_PAGES(bytes)    (((bytes) + XIU_PAGE_MASK) / XIU_PAGE_SIZE)

#define XIU_ALIGN(x, a)     (((x) + (typeof(x))(a) - 1) & ~((typeof(x))(a) - 1))
#define XIU_IS_ALIGNED(x,a) (!((x) & ((typeof(x))(a) - 1)))
#define XIU_MIN(a, b)       ((a) < (b) ? (a) : (b))
#define XIU_MAX(a, b)       ((a) > (b) ? (a) : (b))
#define XIU_CLAMP(x,lo,hi)  (XIU_MAX((lo), XIU_MIN((x), (hi))))
#define XIU_ARRAY_LEN(a)    (sizeof(a) / sizeof((a)[0]))
#define XIU_MEMBER_SIZE(t,m) (sizeof(((t*)0)->m))
#define XIU_OFFSET_OF(t, m) __builtin_offsetof(t, m)
#define XIU_CONTAINER_OF(ptr, type, member) \
    ((type *)((u8 *)(ptr) - XIU_OFFSET_OF(type, member)))

#define XIU_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)

XIU_STATIC_ASSERT(sizeof(u8)   == 1, "u8 size mismatch");
XIU_STATIC_ASSERT(sizeof(u16)  == 2, "u16 size mismatch");
XIU_STATIC_ASSERT(sizeof(u32)  == 4, "u32 size mismatch");
XIU_STATIC_ASSERT(sizeof(u64)  == 8, "u64 size mismatch");
XIU_STATIC_ASSERT(sizeof(uptr) == 8, "uptr must be 64-bit");

#define XIU_VADDR_NULL  ((xiu_vaddr_t)0)
#define XIU_PADDR_NULL  ((xiu_paddr_t)0)
#define XIU_PORT_NULL   ((xiu_port_name_t)0)
#define XIU_PORT_DEAD   ((xiu_port_name_t)(~0ULL))

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
