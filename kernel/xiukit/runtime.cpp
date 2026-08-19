/* =============================================================================
 * XIU Operating System — C++ Runtime Stubs for Kernel
 * kernel/xiukit/runtime.cpp
 * ============================================================================= */

#include <kernel/xiu_types.h>
#include <kernel/panic.h>

// minimal operator new/delete for kernel classes

void* operator new(usize size) {
    // todo: use kalloc when wired
    (void)size;
    xiu_panic("C++ operator new called but kalloc not yet wired\n");
    return (void*)0xDEADBEEF;
}

void* operator new[](usize size) {
    (void)size;
    xiu_panic("C++ operator new[] called but kalloc not yet wired\n");
    return (void*)0xDEADBEEF;
}

void operator delete(void* p) {
    (void)p;
}

void operator delete[](void* p) {
    (void)p;
}

void operator delete(void* p, usize size) {
    (void)p; (void)size;
}

void operator delete[](void* p, usize size) {
    (void)p; (void)size;
}

// pure virtual call handler

extern "C" void __cxa_pure_virtual() {
    xiu_panic("Pure virtual function call!\n");
}
