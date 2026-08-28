/* =============================================================================
 * Chimera Operating System — C++ Runtime Stubs for Kernel
 * kernel/chimerakit/runtime.cpp
 * ============================================================================= */

#include <kernel/chimera_types.h>
#include <kernel/panic.h>

// minimal operator new/delete for kernel classes

extern "C" void *kalloc(usize size);
extern "C" void kfree(void *ptr);

void* operator new(usize size) {
    void *ptr = kalloc(size);
    if (!ptr) {
        chimera_panic("C++ operator new: Out of memory (size=%zu)\n", size);
    }
    return ptr;
}

void* operator new[](usize size) {
    void *ptr = kalloc(size);
    if (!ptr) {
        chimera_panic("C++ operator new[]: Out of memory (size=%zu)\n", size);
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
    chimera_panic("Pure virtual function call!\n");
}
