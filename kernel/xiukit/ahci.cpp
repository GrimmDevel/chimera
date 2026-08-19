/* =============================================================================
 * XIU Operating System — AHCI/SATA Storage Driver
 * kernel/xiukit/ahci.cpp
 * ============================================================================= */

#include <kernel/xiu_types.h>
#include <kernel/panic.h>

namespace XIUKit {

class AHCIDriver {
public:
    AHCIDriver(u64 base_addr) : m_base_addr(base_addr) {}

    void init() {
        kprintf("[XIU-Kit] AHCI: Initializing controller at %p\n", (void*)m_base_addr);
        /* todo: Global Host Control reset */ , port enumeration    }

    xiu_error_t read_blocks(u32 port, u64 lba, u32 count, void *buffer) {
        (void)port; (void)lba; (void)count; (void)buffer;
        return XIU_SUCCESS;
    }

private:
    u64 m_base_addr;
};

} // namespace XIUKit

extern "C" void xiukit_ahci_init(u64 base_addr) {
    static XIUKit::AHCIDriver s_ahci(base_addr);
    s_ahci.init();
}
