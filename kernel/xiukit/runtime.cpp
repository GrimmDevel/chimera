/* =============================================================================
 * XIU Operating System — C++ Runtime Stubs for Kernel
 * kernel/xiukit/runtime.cpp
 * ============================================================================= */

#include <kernel/xiu_types.h>
#include <kernel/panic.h>

// minimal operator new/delete for kernel classes

extern "C" void *kalloc(usize size);
extern "C" void kfree(void *ptr);

void* operator new(usize size) {
    void *ptr = kalloc(size);
    if (!ptr) {
        xiu_panic("C++ operator new: Out of memory (size=%zu)\n", size);
    }
    return ptr;
}

void* operator new[](usize size) {
    void *ptr = kalloc(size);
    if (!ptr) {
        xiu_panic("C++ operator new[]: Out of memory (size=%zu)\n", size);
    }
    return ptr;
}

void operator delete(void* p) noexcept {
    kfree(p);
}

void operator delete[](void* p) noexcept {
    kfree(p);
}

void operator delete(void* p, usize size) noexcept {
    (void)size;
    kfree(p);
}

void operator delete[](void* p, usize size) noexcept {
    (void)size;
    kfree(p);
}

// pure virtual call handler

extern "C" void __cxa_pure_virtual() {
    xiu_panic("Pure virtual function call!\n");
}
