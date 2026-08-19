/* =============================================================================
 * XIU Operating System — XIU-Kit PCI Manager Implementation
 * kernel/xiukit/xiukit_pci.cpp
 * ============================================================================= */

#include <xiukit/xiukit_pci.hpp>
#include <kernel/panic.h>

namespace XIUKit {

// port i/o helpers

static inline void outl(u16 port, u32 val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline u32 inl(u16 port) {
    u32 ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

// pcidevice implementation

PCIDevice::PCIDevice(u8 bus, u8 dev, u8 func) 
    : m_bus(bus), m_dev(dev), m_func(func) 
{
    u32 id = configRead32(0);
    m_vendorID = (u16)(id & 0xFFFF);
    m_deviceID = (u16)(id >> 16);

    u32 info = configRead32(0x08);
    m_classCode = (u8)(info >> 24);
    m_subClass  = (u8)(info >> 16);
    m_progIF    = (u8)(info >> 8);
}

u32 PCIDevice::configRead32(u8 offset) {
    u32 address = (u32)((u32)m_bus << 16) | ((u32)m_dev << 11) |
                  ((u32)m_func << 8) | (offset & 0xFC) | ((u32)0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

void PCIDevice::configWrite32(u8 offset, u32 value) {
    u32 address = (u32)((u32)m_bus << 16) | ((u32)m_dev << 11) |
                  ((u32)m_func << 8) | (offset & 0xFC) | ((u32)0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

xiu_paddr_t PCIDevice::getBAR(u8 index) {
    if (index >= 6) return 0;
    u32 bar = configRead32(0x10 + (index * 4));
    if (bar & 1) return bar & ~0x3; // i/O
    if ((bar & 0x06) == 0x04 && index < 5) {
        u32 bar_high = configRead32(0x10 + ((index + 1) * 4));
        return ((u64)bar_high << 32) | (bar & ~0xFULL);
    }
    return bar & ~0xFULL; // 32-bit Memory
}

// pcimanager implementation

PCIManager& PCIManager::getInstance() {
    static PCIManager instance;
    return instance;
}

extern "C" u64 g_e1000_pci_bar0 = 0;
extern "C" void xiukit_xhci_init(u64 pci_bar0);

void PCIManager::probeAll() {
    kprintf("[XIU-Kit] Enumerating PCI Bus...\n");

    for (u16 bus = 0; bus < 256; bus++) {
        for (u8 dev = 0; dev < 32; dev++) {
            for (u8 func = 0; func < 8; func++) {
                u32 id = 0x80000000 | (bus << 16) | (dev << 11) | (func << 8);
                outl(PCI_CONFIG_ADDRESS, id);
                if (inl(PCI_CONFIG_DATA) != 0xFFFFFFFF) {
                    PCIDevice device(bus, dev, func);
                    kprintf("        pci: %02x:%02x.%d  id=%04x:%04x  class=%02x.%02x\n",
                            bus, dev, func,
                            device.getVendorID(), device.getDeviceID(),
                            device.getClassCode(), device.getSubClass());
                    
                    // intel 82540EM / 82574L Ethernet Controller
                    if (device.getVendorID() == 0x8086 && 
                        (device.getDeviceID() == 0x10d3 || device.getDeviceID() == 0x100e || 
                         device.getDeviceID() == 0x100f || device.getDeviceID() == 0x153a ||
                         device.getClassCode() == 0x02)) {
                        u32 cmd = device.configRead32(0x04);
                        device.configWrite32(0x04, cmd | 0x07);
                        g_e1000_pci_bar0 = device.getBAR(0);
                    }

                    // usb 3.0 / xhci host controller
                    if (device.getClassCode() == 0x0C && device.getSubClass() == 0x03) {
                        u32 cmd = device.configRead32(0x04);
                        device.configWrite32(0x04, (cmd & ~0x0400) | 0x0007);

                        // intel xhci port routing
                        if (device.getVendorID() == 0x8086) {
                            u16 dev_id = device.getDeviceID();
                            if (dev_id == 0x1e31 || dev_id == 0x8c31 || dev_id == 0x9c31 ||
                                dev_id == 0x8cb1 || dev_id == 0x9cb1 || dev_id == 0x0f35 || dev_id == 0x22b5) {
                                u32 xusb2prm = device.configRead32(0xD4); // xusb2prm
                                u32 usb3prm  = device.configRead32(0xDC); // usb3prm
                                if (usb3prm != 0 && usb3prm != 0xFFFFFFFF) {
                                    device.configWrite32(0xD8, usb3prm);  // usb3_pssen
                                }
                                if (xusb2prm != 0 && xusb2prm != 0xFFFFFFFF) {
                                    device.configWrite32(0xD0, xusb2prm); // xusb2pr
                                }
                            }
                        }

                        // ensure PCI Power Management State is D0
                        u8 cap_ptr = (u8)(device.configRead32(0x34) & 0xFC);
                        while (cap_ptr >= 0x40 && cap_ptr <= 0xFC) {
                            u32 cap_hdr = device.configRead32(cap_ptr);
                            u8 cap_id = (u8)(cap_hdr & 0xFF);
                            u8 next_ptr = (u8)((cap_hdr >> 8) & 0xFC);
                            if (cap_id == 0x01) { // pci Power Management
                                u32 pmcsr = device.configRead32(cap_ptr + 4);
                                if ((pmcsr & 0x03) != 0) {
                                    device.configWrite32(cap_ptr + 4, pmcsr & ~0x03); // force D0
                                }
                                break;
                            }
                            if (!next_ptr) break;
                            cap_ptr = next_ptr;
                        }

                        u64 bar0 = device.getBAR(0);
                        xiukit_xhci_init(bar0);
                    }

                    // multi-function check
                    if (func == 0) {
                        u32 header = device.configRead32(0x0C);
                        if (!(header & 0x00800000)) break;
                    }
                }
            }
        }
    }
}

} // namespace XIUKit

extern "C" void xiukit_pci_init(void) {
    XIUKit::PCIManager::getInstance().probeAll();
}
