/* =============================================================================
 * XIU Operating System — XIU-Kit PCI Driver Foundation
 * kernel/include/xiukit/xiukit_pci.hpp
 * ============================================================================= */

#pragma once
#ifndef XIUKIT_PCI_HPP
#define XIUKIT_PCI_HPP

#include <kernel/xiu_types.h>

#ifdef __cplusplus
namespace XIUKit {

/**
 * PCIDevice — Represents a physical PCI device discovered on the bus.
 */
class PCIDevice {
public:
    PCIDevice(u8 bus, u8 dev, u8 func);
    virtual ~PCIDevice() = default;

    u16 getVendorID() const { return m_vendorID; }
    u16 getDeviceID() const { return m_deviceID; }
    u8  getClassCode() const { return m_classCode; }
    u8  getSubClass() const { return m_subClass; }

    // configuration Space Access
    u32 configRead32(u8 offset);
    void configWrite32(u8 offset, u32 value);

    // resource Management
    xiu_paddr_t getBAR(u8 index);
    usize       getBARSize(u8 index);

protected:
    u8 m_bus, m_dev, m_func;
    u16 m_vendorID, m_deviceID;
    u8 m_classCode, m_subClass, m_progIF;
};

/**
 * PCIManager — Singleton service for PCI bus enumeration and matching.
 */
class PCIManager {
public:
    static PCIManager& getInstance();

    void probeAll();
    void registerDriver(u16 vendor, u16 device, void* (*factory)(PCIDevice*));

private:
    PCIManager() = default;
};

} // namespace XIUKit
#endif

// c interface for kernel main
#ifdef __cplusplus
extern "C" {
#endif

void xiukit_pci_init(void);

#ifdef __cplusplus
}
#endif

#endif /* XIUKIT_PCI_HPP */
