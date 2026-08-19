// xhci usb 3.0 driver + hid keyboard/mouse
// fucking ai cant fix nothing in this file so dont even try
#include <kernel/input.h>
#include <kernel/panic.h>
#include <kernel/spinlock.h>
#include <kernel/xiu_types.h>

extern "C" void kprintf(const char *fmt, ...);
extern "C" xiu_paddr_t pmm_alloc_page(void);
extern "C" xiu_paddr_t pmm_alloc_pages(usize count);
extern "C" void console_in_push(char c);
extern "C" void console_scroll_viewport(int delta);
extern "C" void console_scroll_to_bottom(void);
extern "C" void xiukit_hid_push_key_event(u32 scancode, u32 unicode, u32 mods,
                                          bool release);
extern "C" void xiukit_hid_push_mouse_event(i32 dx, i32 dy, i32 dz,
                                            u32 buttons);

// cap regs
#define XHCI_CAP_CAPLENGTH 0x00
#define XHCI_CAP_HCIVERSION 0x02
#define XHCI_CAP_HCSPARAMS1 0x04
#define XHCI_CAP_HCSPARAMS2 0x08
#define XHCI_CAP_HCSPARAMS3 0x0C
#define XHCI_CAP_HCCPARAMS1 0x10
#define XHCI_CAP_DBOFF 0x14
#define XHCI_CAP_RTSOFF 0x18

// op regs
#define XHCI_OP_USBCMD 0x00
#define XHCI_OP_USBSTS 0x04
#define XHCI_OP_PAGESIZE 0x08
#define XHCI_OP_DNCTRL 0x14
#define XHCI_OP_CRCR 0x18
#define XHCI_OP_DCBAAP 0x30
#define XHCI_OP_CONFIG 0x38
#define XHCI_OP_PORTSC_BASE 0x400

#define XHCI_CMD_RS (1U << 0)
#define XHCI_CMD_HCRST (1U << 1)
#define XHCI_CMD_INTE (1U << 2)

#define XHCI_STS_HCH (1U << 0)
#define XHCI_STS_CNR (1U << 11)

#define XHCI_PORTSC_CCS (1U << 0)
#define XHCI_PORTSC_PED (1U << 1)
#define XHCI_PORTSC_PR (1U << 4)
#define XHCI_PORTSC_PLS_MASK (0xFU << 5)
#define XHCI_PORTSC_PP (1U << 9)
#define XHCI_PORTSC_SPEED_MASK (0xFU << 10)
#define XHCI_PORTSC_LWS (1U << 16)
#define XHCI_PORTSC_CSC (1U << 17)
#define XHCI_PORTSC_PEC (1U << 18)
#define XHCI_PORTSC_WRC (1U << 19)
#define XHCI_PORTSC_OCC (1U << 20)
#define XHCI_PORTSC_PRC (1U << 21)
#define XHCI_PORTSC_PLC (1U << 22)
#define XHCI_PORTSC_CEC (1U << 23)
#define XHCI_PORTSC_CAS (1U << 24)
#define XHCI_PORTSC_WPR (1U << 31)

#define XHCI_PORT_RO ((1U << 0) | (1U << 3) | (0xFU << 10) | (1U << 30))
#define XHCI_PORT_RWS ((0xFU << 5) | (1U << 9) | (0x3U << 14) | (0x7U << 25))

// mask off pls so we dont accidentally suspend port and lose 5v vbus
static inline u32 xhci_port_state_to_neutral(u32 portsc) {
  return (portsc & XHCI_PORT_RO) |
         ((portsc & XHCI_PORT_RWS) & ~XHCI_PORTSC_PLS_MASK);
}

#define TRB_TYPE_NORMAL 1
#define TRB_TYPE_SETUP_STAGE 2
#define TRB_TYPE_DATA_STAGE 3
#define TRB_TYPE_STATUS_STAGE 4
#define TRB_TYPE_LINK 6
#define TRB_TYPE_ENABLE_SLOT 9
#define TRB_TYPE_DISABLE_SLOT 10
#define TRB_TYPE_ADDRESS_DEVICE 11
#define TRB_TYPE_CONFIG_EP 12
#define TRB_TYPE_EVALUATE_CTX 13
#define TRB_TYPE_TRANSFER_EVENT 32
#define TRB_TYPE_CMD_COMPLETION 33
#define TRB_TYPE_PORT_STATUS_CHANGE 34

typedef struct XIU_PACKED {
  u64 param;
  u32 status;
  u32 control;
} xhci_trb_t;

typedef struct XIU_PACKED {
  u64 ring_base;
  u32 ring_size;
  u32 reserved;
} xhci_erst_entry_t;

#define XHCI_MAX_DEVICES 8

typedef struct {
  u8 slot_id;
  u8 port_id;
  u8 ep_type;
  u16 max_packet_size;
  bool active;

  xhci_trb_t *transfer_ring;
  xiu_paddr_t transfer_ring_phys;
  u32 transfer_cycle;
  u32 transfer_enqueue;

  xhci_trb_t *ep1_ring;
  xiu_paddr_t ep1_ring_phys;
  u32 ep1_cycle;
  u32 ep1_enqueue;

  u8 report_buffer[64];
  xiu_paddr_t report_buffer_phys;
  u8 prev_keys[6];
  u8 prev_modifiers;
  u8 prev_mouse_buttons;
  u8 led_state;
  u8 ep1_dci; // actual DCI of the interrupt IN endpoint (default 3 = EP1 IN)
} xhci_device_t;

namespace XIUKit {

class XHCIDriver {
private:
  u64 m_mmio_base = 0;
  u64 m_op_base = 0;
  u64 m_rt_base = 0;
  u64 m_db_base = 0;
  u8 m_caplength = 0;
  u8 m_max_slots = 0;
  u8 m_max_ports = 0;
  u32 m_context_size = 32;

  u64 *m_dcbaa = nullptr;
  xiu_paddr_t m_dcbaa_phys = 0;

  xhci_trb_t *m_cmd_ring = nullptr;
  xiu_paddr_t m_cmd_ring_phys = 0;
  u32 m_cmd_enqueue = 0;
  u32 m_cmd_cycle = 1;

  xhci_trb_t *m_ev_ring = nullptr;
  xiu_paddr_t m_ev_ring_phys = 0;
  xhci_erst_entry_t *m_erst = nullptr;
  xiu_paddr_t m_erst_phys = 0;
  u32 m_ev_dequeue = 0;
  u32 m_ev_cycle = 1;
  u64 m_interrupter0 = 0;

  xhci_device_t m_devices[XHCI_MAX_DEVICES];
  u32 m_num_devices = 0;

  spinlock_t m_lock = {};
  bool m_initialized = false;

  static inline void outb(u16 port, u8 val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
  }

  static inline u8 inb(u16 port) {
    u8 ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
  }

  // pit ch2 delay calibration so we don't depend on lapic or tsc
  static inline void delay_us(u32 us) {
    while (us > 0) {
      u32 chunk_us = (us > 50000) ? 50000 : us;
      u32 ticks = (chunk_us * 1193182ULL) / 1000000ULL;
      if (ticks > 0xFFFF)
        ticks = 0xFFFF;
      if (ticks == 0)
        ticks = 1;

      u8 val = inb(0x61) & 0xFD;
      outb(0x61, val & ~1);
      outb(0x43, 0xB0);
      outb(0x42, (u8)(ticks & 0xFF));
      outb(0x42, (u8)((ticks >> 8) & 0xFF));
      outb(0x61, val | 1);

      while (!(inb(0x61) & 0x20)) {
        __asm__ volatile("pause");
      }
      outb(0x61, val & ~1);
      us -= chunk_us;
    }
  }

  static inline void delay_ms(u32 ms) { delay_us(ms * 1000); }

  static inline u32 read_mmio32(u64 addr) { return *(volatile u32 *)addr; }
  static inline void write_mmio32(u64 addr, u32 val) {
    *(volatile u32 *)addr = val;
  }
  static inline u64 read_mmio64(u64 addr) { return *(volatile u64 *)addr; }
  static inline void write_mmio64(u64 addr, u64 val) {
    *(volatile u64 *)addr = val;
  }

  void ring_doorbell(u8 target, u8 stream) {
    write_mmio32(m_db_base + (target * 4), stream);
  }

  // take over controller from bios
  void handle_bios_handoff() {
    u32 hccparams1 = read_mmio32(m_mmio_base + XHCI_CAP_HCCPARAMS1);
    u16 xecp = (hccparams1 >> 16) & 0xFFFF;
    if (!xecp)
      return;

    u64 cap_ptr = m_mmio_base + (xecp << 2);
    while (cap_ptr) {
      u32 cap_hdr = read_mmio32(cap_ptr);
      u8 cap_id = cap_hdr & 0xFF;
      u8 next_cap = (cap_hdr >> 8) & 0xFF;

      if (cap_id == 1) {
        if (cap_hdr & (1U << 16)) {
          kprintf("[xHCI] Requesting OS ownership from BIOS (USBLEGSUP)...\n");
          write_mmio32(cap_ptr, (cap_hdr & ~(1U << 16)) | (1U << 24));

          for (int ms = 0; ms < 1000; ms++) {
            u32 val = read_mmio32(cap_ptr);
            if (!(val & (1U << 16))) {
              kprintf("  [  OK  ]  BIOS handed off xHCI ownership\n");
              break;
            }
            delay_ms(1);
          }
        }
        // clear smi flags
        u32 legctl = read_mmio32(cap_ptr + 4);
        legctl &= ~(1U << 0 | 1U << 4 | 1U << 13 | 1U << 14 | 1U << 15);
        legctl |= (1U << 29 | 1U << 30 | 1U << 31);
        write_mmio32(cap_ptr + 4, legctl);
        break;
      }
      if (!next_cap)
        break;
      cap_ptr += (next_cap << 2);
    }
  }

  bool send_command(u64 param, u32 status, u32 control,
                    u8 *out_slot_id = nullptr) {
    irq_flags_t irq = spinlock_lock_irqsave(&m_lock);

    u32 idx = m_cmd_enqueue;
    m_cmd_ring[idx].param = param;
    m_cmd_ring[idx].status = status;
    m_cmd_ring[idx].control = control | (m_cmd_cycle ? 1 : 0);

    m_cmd_enqueue++;
    if (m_cmd_enqueue >= 255) {
      m_cmd_ring[m_cmd_enqueue].param = m_cmd_ring_phys;
      m_cmd_ring[m_cmd_enqueue].status = 0;
      m_cmd_ring[m_cmd_enqueue].control =
          (TRB_TYPE_LINK << 10) | (1U << 1) | (m_cmd_cycle ? 1 : 0);
      m_cmd_enqueue = 0;
      m_cmd_cycle ^= 1;
    }

    __asm__ volatile("mfence" ::: "memory");
    ring_doorbell(0, 0);

    for (int ms = 0; ms < 1000; ms++) {
      while (true) {
        xhci_trb_t *ev = &m_ev_ring[m_ev_dequeue];
        u32 c = ev->control & 1;
        if (c != m_ev_cycle)
          break;

        u32 trb_type = (ev->control >> 10) & 0x3F;
        u8 slot = (ev->control >> 24) & 0xFF;
        u8 completion_code = (ev->status >> 24) & 0xFF;

        m_ev_dequeue++;
        if (m_ev_dequeue >= 256) {
          m_ev_dequeue = 0;
          m_ev_cycle ^= 1;
        }
        write_mmio64(m_interrupter0 + 0x18,
                     (m_ev_ring_phys + m_ev_dequeue * sizeof(xhci_trb_t)) |
                         (1ULL << 3));

        if (trb_type == TRB_TYPE_CMD_COMPLETION) {
          if (out_slot_id)
            *out_slot_id = slot;
          spinlock_unlock_irqrestore(&m_lock, irq);
          if (completion_code != 1) {
            kprintf("[xHCI] CMD Type=%u Completion Code=%u (FAILED)\n",
                    (control >> 10) & 0x3F, completion_code);
          }
          return (completion_code == 1);
        }
      }
      spinlock_unlock_irqrestore(&m_lock, irq);
      delay_ms(1);
      irq = spinlock_lock_irqsave(&m_lock);
    }
    spinlock_unlock_irqrestore(&m_lock, irq);
    kprintf("[xHCI] CMD Type=%u TIMED OUT waiting for completion event\n",
            (control >> 10) & 0x3F);
    return false;
  }

  bool send_control_transfer_no_data(xhci_device_t *dev, u8 req_type, u8 req,
                                     u16 value, u16 index) {
    irq_flags_t irq = spinlock_lock_irqsave(&m_lock);

    u32 idx = dev->transfer_enqueue;

    u64 setup_data = (u64)req_type | ((u64)req << 8) | ((u64)value << 16) |
                     ((u64)index << 32);
    dev->transfer_ring[idx].param = setup_data;
    dev->transfer_ring[idx].status = 8;
    dev->transfer_ring[idx].control = (TRB_TYPE_SETUP_STAGE << 10) | (1U << 6) |
                                      (0U << 16) |
                                      (dev->transfer_cycle ? 1 : 0);
    idx++;
    if (idx >= 255) {
      dev->transfer_ring[idx].param = dev->transfer_ring_phys;
      dev->transfer_ring[idx].status = 0;
      dev->transfer_ring[idx].control =
          (TRB_TYPE_LINK << 10) | (1U << 1) | (dev->transfer_cycle ? 1 : 0);
      idx = 0;
      dev->transfer_cycle ^= 1;
    }

    dev->transfer_ring[idx].param = 0;
    dev->transfer_ring[idx].status = 0;
    dev->transfer_ring[idx].control = (TRB_TYPE_STATUS_STAGE << 10) |
                                      (1U << 16) | (1U << 5) |
                                      (dev->transfer_cycle ? 1 : 0);
    idx++;
    if (idx >= 255) {
      dev->transfer_ring[idx].param = dev->transfer_ring_phys;
      dev->transfer_ring[idx].status = 0;
      dev->transfer_ring[idx].control =
          (TRB_TYPE_LINK << 10) | (1U << 1) | (dev->transfer_cycle ? 1 : 0);
      idx = 0;
      dev->transfer_cycle ^= 1;
    }

    dev->transfer_enqueue = idx;
    __asm__ volatile("mfence" ::: "memory");
    ring_doorbell(dev->slot_id, 1);

    for (int ms = 0; ms < 500; ms++) {
      while (true) {
        xhci_trb_t *ev = &m_ev_ring[m_ev_dequeue];
        u32 c = ev->control & 1;
        if (c != m_ev_cycle)
          break;

        u32 trb_type = (ev->control >> 10) & 0x3F;
        u8 slot = (ev->control >> 24) & 0xFF;
        u8 cc = (ev->status >> 24) & 0xFF;

        m_ev_dequeue++;
        if (m_ev_dequeue >= 256) {
          m_ev_dequeue = 0;
          m_ev_cycle ^= 1;
        }
        write_mmio64(m_interrupter0 + 0x18,
                     (m_ev_ring_phys + m_ev_dequeue * sizeof(xhci_trb_t)) |
                         (1ULL << 3));

        if (trb_type == TRB_TYPE_TRANSFER_EVENT && slot == dev->slot_id) {
          spinlock_unlock_irqrestore(&m_lock, irq);
          return (cc == 1 || cc == 0);
        }
      }
      spinlock_unlock_irqrestore(&m_lock, irq);
      delay_ms(1);
      irq = spinlock_lock_irqsave(&m_lock);
    }
    spinlock_unlock_irqrestore(&m_lock, irq);
    return false;
  }

  bool send_control_transfer_data_out(xhci_device_t *dev, u8 req_type, u8 req,
                                      u16 value, u16 index, const void *data,
                                      u16 length) {
    irq_flags_t irq = spinlock_lock_irqsave(&m_lock);

    u32 idx = dev->transfer_enqueue;

    if (data && length > 0) {
      u8 *buf = (u8 *)(dev->report_buffer_phys + g_hhdm_base + 256);
      __builtin_memcpy(buf, data, length);
    }

    u64 setup_data = (u64)req_type | ((u64)req << 8) | ((u64)value << 16) |
                     ((u64)index << 32);
    dev->transfer_ring[idx].param = setup_data;
    dev->transfer_ring[idx].status = 8;
    dev->transfer_ring[idx].control = (TRB_TYPE_SETUP_STAGE << 10) | (1U << 6) |
                                      (2U << 16) |
                                      (dev->transfer_cycle ? 1 : 0);
    idx++;
    if (idx >= 255) {
      dev->transfer_ring[idx].param = dev->transfer_ring_phys;
      dev->transfer_ring[idx].status = 0;
      dev->transfer_ring[idx].control =
          (TRB_TYPE_LINK << 10) | (1U << 1) | (dev->transfer_cycle ? 1 : 0);
      idx = 0;
      dev->transfer_cycle ^= 1;
    }

    dev->transfer_ring[idx].param = dev->report_buffer_phys + 256;
    dev->transfer_ring[idx].status = length;
    dev->transfer_ring[idx].control = (TRB_TYPE_DATA_STAGE << 10) | (0U << 16) |
                                      (dev->transfer_cycle ? 1 : 0);
    idx++;
    if (idx >= 255) {
      dev->transfer_ring[idx].param = dev->transfer_ring_phys;
      dev->transfer_ring[idx].status = 0;
      dev->transfer_ring[idx].control =
          (TRB_TYPE_LINK << 10) | (1U << 1) | (dev->transfer_cycle ? 1 : 0);
      idx = 0;
      dev->transfer_cycle ^= 1;
    }

    dev->transfer_ring[idx].param = 0;
    dev->transfer_ring[idx].status = 0;
    dev->transfer_ring[idx].control = (TRB_TYPE_STATUS_STAGE << 10) |
                                      (1U << 16) | (1U << 5) |
                                      (dev->transfer_cycle ? 1 : 0);
    idx++;
    if (idx >= 255) {
      dev->transfer_ring[idx].param = dev->transfer_ring_phys;
      dev->transfer_ring[idx].status = 0;
      dev->transfer_ring[idx].control =
          (TRB_TYPE_LINK << 10) | (1U << 1) | (dev->transfer_cycle ? 1 : 0);
      idx = 0;
      dev->transfer_cycle ^= 1;
    }

    dev->transfer_enqueue = idx;
    __asm__ volatile("mfence" ::: "memory");
    ring_doorbell(dev->slot_id, 1);

    for (int ms = 0; ms < 500; ms++) {
      while (true) {
        xhci_trb_t *ev = &m_ev_ring[m_ev_dequeue];
        u32 c = ev->control & 1;
        if (c != m_ev_cycle)
          break;

        u32 trb_type = (ev->control >> 10) & 0x3F;
        u8 slot = (ev->control >> 24) & 0xFF;
        u8 cc = (ev->status >> 24) & 0xFF;

        m_ev_dequeue++;
        if (m_ev_dequeue >= 256) {
          m_ev_dequeue = 0;
          m_ev_cycle ^= 1;
        }
        write_mmio64(m_interrupter0 + 0x18,
                     (m_ev_ring_phys + m_ev_dequeue * sizeof(xhci_trb_t)) |
                         (1ULL << 3));

        if (trb_type == TRB_TYPE_TRANSFER_EVENT && slot == dev->slot_id) {
          spinlock_unlock_irqrestore(&m_lock, irq);
          return (cc == 1 || cc == 0);
        }
      }
      spinlock_unlock_irqrestore(&m_lock, irq);
      delay_ms(1);
      irq = spinlock_lock_irqsave(&m_lock);
    }
    spinlock_unlock_irqrestore(&m_lock, irq);
    return false;
  }

  bool send_control_transfer_data_in(xhci_device_t *dev, u8 req_type, u8 req,
                                     u16 value, u16 index, void *out_data,
                                     u16 length) {
    irq_flags_t irq = spinlock_lock_irqsave(&m_lock);

    u32 idx = dev->transfer_enqueue;

    u64 setup_data = (u64)req_type | ((u64)req << 8) | ((u64)value << 16) |
                     ((u64)index << 32);
    dev->transfer_ring[idx].param = setup_data;
    dev->transfer_ring[idx].status = 8;
    dev->transfer_ring[idx].control = (TRB_TYPE_SETUP_STAGE << 10) | (1U << 6) |
                                      (3U << 16) |
                                      (dev->transfer_cycle ? 1 : 0);
    idx++;
    if (idx >= 255) {
      dev->transfer_ring[idx].param = dev->transfer_ring_phys;
      dev->transfer_ring[idx].status = 0;
      dev->transfer_ring[idx].control =
          (TRB_TYPE_LINK << 10) | (1U << 1) | (dev->transfer_cycle ? 1 : 0);
      idx = 0;
      dev->transfer_cycle ^= 1;
    }

    dev->transfer_ring[idx].param = dev->report_buffer_phys + 512;
    dev->transfer_ring[idx].status = length;
    dev->transfer_ring[idx].control = (TRB_TYPE_DATA_STAGE << 10) | (1U << 16) |
                                      (dev->transfer_cycle ? 1 : 0);
    idx++;
    if (idx >= 255) {
      dev->transfer_ring[idx].param = dev->transfer_ring_phys;
      dev->transfer_ring[idx].status = 0;
      dev->transfer_ring[idx].control =
          (TRB_TYPE_LINK << 10) | (1U << 1) | (dev->transfer_cycle ? 1 : 0);
      idx = 0;
      dev->transfer_cycle ^= 1;
    }

    dev->transfer_ring[idx].param = 0;
    dev->transfer_ring[idx].status = 0;
    dev->transfer_ring[idx].control = (TRB_TYPE_STATUS_STAGE << 10) |
                                      (0U << 16) | (1U << 5) |
                                      (dev->transfer_cycle ? 1 : 0);
    idx++;
    if (idx >= 255) {
      dev->transfer_ring[idx].param = dev->transfer_ring_phys;
      dev->transfer_ring[idx].status = 0;
      dev->transfer_ring[idx].control =
          (TRB_TYPE_LINK << 10) | (1U << 1) | (dev->transfer_cycle ? 1 : 0);
      idx = 0;
      dev->transfer_cycle ^= 1;
    }

    dev->transfer_enqueue = idx;
    __asm__ volatile("mfence" ::: "memory");
    ring_doorbell(dev->slot_id, 1);

    for (int ms = 0; ms < 500; ms++) {
      while (true) {
        xhci_trb_t *ev = &m_ev_ring[m_ev_dequeue];
        u32 c = ev->control & 1;
        if (c != m_ev_cycle)
          break;

        u32 trb_type = (ev->control >> 10) & 0x3F;
        u8 slot = (ev->control >> 24) & 0xFF;
        u8 cc = (ev->status >> 24) & 0xFF;

        m_ev_dequeue++;
        if (m_ev_dequeue >= 256) {
          m_ev_dequeue = 0;
          m_ev_cycle ^= 1;
        }
        write_mmio64(m_interrupter0 + 0x18,
                     (m_ev_ring_phys + m_ev_dequeue * sizeof(xhci_trb_t)) |
                         (1ULL << 3));

        if (trb_type == TRB_TYPE_TRANSFER_EVENT && slot == dev->slot_id) {
          if (out_data && length > 0) {
            const void *src =
                (const void *)(dev->report_buffer_phys + g_hhdm_base + 512);
            __builtin_memcpy(out_data, src, length);
          }
          spinlock_unlock_irqrestore(&m_lock, irq);
          return (cc == 1 || cc == 0);
        }
      }
      spinlock_unlock_irqrestore(&m_lock, irq);
      delay_ms(1);
      irq = spinlock_lock_irqsave(&m_lock);
    }
    spinlock_unlock_irqrestore(&m_lock, irq);
    return false;
  }

  void set_keyboard_leds(xhci_device_t *dev, u8 led_mask) {
    u8 report[1] = {led_mask};
    dev->led_state = led_mask;
    send_control_transfer_data_out(dev, 0x21, 0x09, 0x0200, 0, report, 1);
  }

  void set_port_power(u8 port_id, bool enable) {
    if (port_id < 1 || port_id > m_max_ports)
      return;
    u64 portsc_addr = m_op_base + XHCI_OP_PORTSC_BASE + (port_id - 1) * 0x10;
    u32 portsc = read_mmio32(portsc_addr);
    u32 neutral = xhci_port_state_to_neutral(portsc);
    if (enable) {
      write_mmio32(portsc_addr, neutral | XHCI_PORTSC_PP);
    } else {
      write_mmio32(portsc_addr, neutral & ~XHCI_PORTSC_PP);
    }
  }

  void power_cycle_port(u8 port_id) {
    set_port_power(port_id, false);
    delay_ms(50);
    set_port_power(port_id, true);
    delay_ms(150);
  }

  void decode_usb_keyboard(xhci_device_t *dev, const u8 *report) {
    u8 modifiers = report[0];
    u32 mods = 0;
    if (modifiers & 0x01)
      mods |= XIU_MOD_LCTRL | XIU_MOD_CTRL;
    if (modifiers & 0x02)
      mods |= XIU_MOD_LSHIFT | XIU_MOD_SHIFT;
    if (modifiers & 0x04)
      mods |= XIU_MOD_LALT | XIU_MOD_ALT;
    if (modifiers & 0x08)
      mods |= XIU_MOD_LCMD | XIU_MOD_CMD;
    if (modifiers & 0x10)
      mods |= XIU_MOD_RCTRL | XIU_MOD_CTRL;
    if (modifiers & 0x20)
      mods |= XIU_MOD_RSHIFT | XIU_MOD_SHIFT;
    if (modifiers & 0x40)
      mods |= XIU_MOD_RALT | XIU_MOD_ALT;
    if (modifiers & 0x80)
      mods |= XIU_MOD_RCMD | XIU_MOD_CMD;

    for (int i = 2; i < 8; i++) {
      u8 key = report[i];
      if (key >= 0x04 && key <= 0x65) {
        bool was_pressed = false;
        for (int j = 0; j < 6; j++) {
          if (dev->prev_keys[j] == key) {
            was_pressed = true;
            break;
          }
        }
        if (!was_pressed) {
          u32 unicode = 0;
          bool shift = (mods & XIU_MOD_SHIFT) != 0;

          if (key >= 0x04 && key <= 0x1D) {
            unicode = shift ? ('A' + (key - 0x04)) : ('a' + (key - 0x04));
          } else if (key >= 0x1E && key <= 0x27) {
            const char *num = "1234567890";
            const char *sym = "!@#$%^&*()";
            unicode = shift ? sym[key - 0x1E] : num[key - 0x1E];
          } else {
            switch (key) {
            case 0x28:
              unicode = '\n';
              break;
            case 0x29:
              unicode = 0x1B;
              break;
            case 0x2A:
              unicode = '\b';
              break;
            case 0x2B:
              unicode = '\t';
              break;
            case 0x2C:
              unicode = ' ';
              break;
            case 0x2D:
              unicode = shift ? '_' : '-';
              break;
            case 0x2E:
              unicode = shift ? '+' : '=';
              break;
            case 0x2F:
              unicode = shift ? '{' : '[';
              break;
            case 0x30:
              unicode = shift ? '}' : ']';
              break;
            case 0x31:
              unicode = shift ? '|' : '\\';
              break;
            case 0x33:
              unicode = shift ? ':' : ';';
              break;
            case 0x34:
              unicode = shift ? '"' : '\'';
              break;
            case 0x35:
              unicode = shift ? '~' : '`';
              break;
            case 0x36:
              unicode = shift ? '<' : ',';
              break;
            case 0x37:
              unicode = shift ? '>' : '.';
              break;
            case 0x38:
              unicode = shift ? '?' : '/';
              break;
            case 0x4F:
              unicode = 0x1004;
              break;
            case 0x50:
              unicode = 0x1003;
              break;
            case 0x51:
              unicode = 0x1002;
              break;
            case 0x52:
              unicode = 0x1001;
              break;
            default:
              break;
            }
          }

          if (key == 0x39) {
            dev->led_state ^= 0x02;
            set_keyboard_leds(dev, dev->led_state);
          } else if (key == 0x53) {
            dev->led_state ^= 0x01;
            set_keyboard_leds(dev, dev->led_state);
          }

          if (unicode != 0) {
            xiukit_hid_push_key_event(key, unicode, mods, false);
          }
        }
      }
    }

    for (int j = 0; j < 6; j++) {
      u8 old_key = dev->prev_keys[j];
      if (old_key >= 0x04) {
        bool still_pressed = false;
        for (int i = 2; i < 8; i++) {
          if (report[i] == old_key) {
            still_pressed = true;
            break;
          }
        }
        if (!still_pressed) {
          xiukit_hid_push_key_event(old_key, 0, mods, true);
        }
      }
    }

    for (int i = 0; i < 6; i++)
      dev->prev_keys[i] = report[2 + i];
    dev->prev_modifiers = modifiers;
  }

  // mouse packet parser - handles report id or direct boot packets
  void decode_usb_mouse(xhci_device_t *dev, const u8 *report) {
    (void)dev;
    u8 buttons = 0;
    i8 dx = 0;
    i8 dy = 0;
    i8 dz = 0;

    if (report[0] == 1 || report[0] == 2) {
      buttons = report[1] & 0x07;
      dx = (i8)report[2];
      dy = (i8)report[3];
      dz = (i8)report[4];
    } else if (report[1] == 0 && (report[2] != 0 || report[3] != 0)) {
      buttons = report[0] & 0x07;
      dx = (i8)report[2];
      dy = (i8)report[3];
      dz = (i8)report[4];
    } else {
      buttons = report[0] & 0x07;
      dx = (i8)report[1];
      dy = (i8)report[2];
      dz = (i8)report[3];
    }

    xiukit_hid_push_mouse_event((i32)dx, (i32)dy, (i32)dz, (u32)buttons);
  }

public:
  void init(u64 pci_bar0) {
    if (!pci_bar0 || m_initialized)
      return;

    m_mmio_base = pci_bar0 + g_hhdm_base;
    m_caplength = *(volatile u8 *)(m_mmio_base + XHCI_CAP_CAPLENGTH);
    m_op_base = m_mmio_base + m_caplength;

    u32 hcsparams1 = read_mmio32(m_mmio_base + XHCI_CAP_HCSPARAMS1);
    m_max_slots = hcsparams1 & 0xFF;
    m_max_ports = (hcsparams1 >> 24) & 0xFF;

    u32 hccparams1 = read_mmio32(m_mmio_base + XHCI_CAP_HCCPARAMS1);
    m_context_size = (hccparams1 & (1 << 2)) ? 64 : 32;

    u32 dboff = read_mmio32(m_mmio_base + XHCI_CAP_DBOFF);
    m_db_base = m_mmio_base + dboff;

    u32 rtsoff = read_mmio32(m_mmio_base + XHCI_CAP_RTSOFF);
    m_rt_base = m_mmio_base + rtsoff;
    m_interrupter0 = m_rt_base + 0x20;

    kprintf("[xHCI] Initializing USB 3.0 Controller (Slots=%u, Ports=%u, "
            "CSZ=%u)...\n",
            m_max_slots, m_max_ports, m_context_size);

    handle_bios_handoff();

    // stop hc
    u32 usbcmd = read_mmio32(m_op_base + XHCI_OP_USBCMD);
    write_mmio32(m_op_base + XHCI_OP_USBCMD, usbcmd & ~XHCI_CMD_RS);
    for (int i = 0; i < 50; i++) {
      if (read_mmio32(m_op_base + XHCI_OP_USBSTS) & XHCI_STS_HCH)
        break;
      delay_ms(1);
    }

    // reset hc
    write_mmio32(m_op_base + XHCI_OP_USBCMD, XHCI_CMD_HCRST);
    for (int i = 0; i < 100; i++) {
      if (!(read_mmio32(m_op_base + XHCI_OP_USBCMD) & XHCI_CMD_HCRST) &&
          !(read_mmio32(m_op_base + XHCI_OP_USBSTS) & XHCI_STS_CNR))
        break;
      delay_ms(1);
    }

    write_mmio32(m_op_base + XHCI_OP_CONFIG, m_max_slots);

    m_dcbaa_phys = pmm_alloc_pages(1);
    m_dcbaa = (u64 *)(m_dcbaa_phys + g_hhdm_base);
    __builtin_memset(m_dcbaa, 0, 4096);

    // coffee lake dies with HSE if scratchpads are missing
    u32 hcsparams2 = read_mmio32(m_mmio_base + XHCI_CAP_HCSPARAMS2);
    u32 max_scratch_hi = (hcsparams2 >> 21) & 0x1F;
    u32 max_scratch_lo = (hcsparams2 >> 27) & 0x1F;
    u32 max_scratch = (max_scratch_hi << 5) | max_scratch_lo;

    if (max_scratch > 0) {
      usize array_pages = (max_scratch * sizeof(u64) + 4095) / 4096;
      xiu_paddr_t sp_array_phys = pmm_alloc_pages(array_pages);
      u64 *sp_array = (u64 *)(sp_array_phys + g_hhdm_base);
      __builtin_memset(sp_array, 0, array_pages * 4096);

      for (u32 i = 0; i < max_scratch; i++) {
        xiu_paddr_t sp_page = pmm_alloc_pages(1);
        __builtin_memset((void *)(sp_page + g_hhdm_base), 0, 4096);
        sp_array[i] = sp_page;
      }

      m_dcbaa[0] = sp_array_phys;
      kprintf("  [  OK  ]  xHCI Scratchpad: %u buffer(s) allocated at phys "
              "0x%016llx\n",
              max_scratch, sp_array_phys);
    }

    write_mmio64(m_op_base + XHCI_OP_DCBAAP, m_dcbaa_phys);

    m_cmd_ring_phys = pmm_alloc_pages(1);
    m_cmd_ring = (xhci_trb_t *)(m_cmd_ring_phys + g_hhdm_base);
    __builtin_memset(m_cmd_ring, 0, 4096);
    m_cmd_enqueue = 0;
    m_cmd_cycle = 1;
    write_mmio64(m_op_base + XHCI_OP_CRCR, m_cmd_ring_phys | 1);

    m_ev_ring_phys = pmm_alloc_pages(1);
    m_ev_ring = (xhci_trb_t *)(m_ev_ring_phys + g_hhdm_base);
    __builtin_memset(m_ev_ring, 0, 4096);

    m_erst_phys = pmm_alloc_pages(1);
    m_erst = (xhci_erst_entry_t *)(m_erst_phys + g_hhdm_base);
    __builtin_memset(m_erst, 0, 4096);
    m_erst[0].ring_base = m_ev_ring_phys;
    m_erst[0].ring_size = 256;

    write_mmio32(m_interrupter0 + 0x08, 1);
    write_mmio64(m_interrupter0 + 0x10, m_erst_phys);
    write_mmio64(m_interrupter0 + 0x18, m_ev_ring_phys | (1ULL << 3));
    write_mmio32(m_interrupter0 + 0x00, read_mmio32(m_interrupter0 + 0x00) | 2);

    // must run controller before writing PP
    write_mmio32(m_op_base + XHCI_OP_USBCMD, XHCI_CMD_RS | XHCI_CMD_INTE);
    for (int i = 0; i < 50; i++) {
      if (!(read_mmio32(m_op_base + XHCI_OP_USBSTS) & XHCI_STS_HCH))
        break;
      delay_ms(1);
    }

    m_initialized = true;
    kprintf("  [  OK  ]  xHCI Controller Active (Ports=%u)\n", m_max_ports);

    probe_ports();
  }

  void probe_ports() {
    for (u8 p = 1; p <= m_max_ports; p++) {
      set_port_power(p, true);
    }

    delay_ms(150);

    for (u8 p = 1; p <= m_max_ports && m_num_devices < XHCI_MAX_DEVICES; p++) {
      u64 portsc_addr = m_op_base + XHCI_OP_PORTSC_BASE + (p - 1) * 0x10;
      u32 portsc = read_mmio32(portsc_addr);

      if (!(portsc & XHCI_PORTSC_CCS)) {
        for (int spin = 0; spin < 50; spin++) {
          delay_ms(2);
          portsc = read_mmio32(portsc_addr);
          if (portsc & XHCI_PORTSC_CCS)
            break;
        }
      }

      if (portsc & XHCI_PORTSC_CCS) {
        u8 speed = (portsc >> 10) & 0x0F;
        bool is_enabled = (portsc & XHCI_PORTSC_PED) != 0;

        if (!is_enabled && speed != 4) {
          kprintf("[xHCI] Port %u: Connected (USB2/1.1). Resetting...\n", p);

          write_mmio32(portsc_addr, xhci_port_state_to_neutral(portsc) |
                                        XHCI_PORTSC_PR | XHCI_PORTSC_PP);

          bool reset_ok = false;
          for (int ms = 0; ms < 250; ms++) {
            u32 s = read_mmio32(portsc_addr);
            if ((s & XHCI_PORTSC_PED) && !(s & XHCI_PORTSC_PR)) {
              reset_ok = true;
              break;
            }
            delay_ms(1);
          }

          if (!reset_ok) {
            kprintf("[xHCI] Port %u: Reset timed out (PORTSC=0x%08x), power "
                    "cycling port...\n",
                    p, read_mmio32(portsc_addr));
            power_cycle_port(p);
            continue;
          }

          u32 s = read_mmio32(portsc_addr);
          u32 ack = s & (XHCI_PORTSC_PRC | XHCI_PORTSC_CSC | XHCI_PORTSC_PEC |
                         XHCI_PORTSC_WRC | XHCI_PORTSC_OCC | XHCI_PORTSC_PLC |
                         XHCI_PORTSC_CEC);
          write_mmio32(portsc_addr,
                       xhci_port_state_to_neutral(s) | ack | XHCI_PORTSC_PP);

          delay_ms(20);
          set_port_power(p, true);

          portsc = read_mmio32(portsc_addr);
          speed = (portsc >> 10) & 0x0F;
        } else {
          kprintf("[xHCI] Port %u: Connected & Ready (Speed=%u, "
                  "SuperSpeed/Pre-enabled)\n",
                  p, speed);
        }

        kprintf("[xHCI] Port %u: Enabled, Final Speed=%u\n", p, speed);

        u8 slot_id = 0;
        bool ok = send_command(0, 0, (TRB_TYPE_ENABLE_SLOT << 10), &slot_id);
        if (ok && slot_id > 0) {
          kprintf("[xHCI] Enabled Slot %u on Port %u\n", slot_id, p);
          init_hid_device(slot_id, p, speed);
        } else {
          kprintf("[xHCI] Port %u: ENABLE_SLOT failed (ok=%d, slot_id=%u)\n", p,
                  (int)ok, slot_id);
        }
      }
    }
  }

  void init_hid_device(u8 slot_id, u8 port_id, u8 speed) {
    if (m_num_devices >= XHCI_MAX_DEVICES)
      return;
    if (speed == 0)
      speed = 1;

    xhci_device_t *dev = &m_devices[m_num_devices];
    __builtin_memset(dev, 0, sizeof(xhci_device_t));
    dev->slot_id = slot_id;
    dev->port_id = port_id;

    u16 ep0_max_packet = 8;
    if (speed == 4)
      ep0_max_packet = 512;
    else if (speed == 3)
      ep0_max_packet = 64;
    else if (speed == 1)
      ep0_max_packet = 8;
    else
      ep0_max_packet = 8;

    dev->max_packet_size = 8;
    dev->ep_type = (m_num_devices == 0) ? 1 : 2;

    dev->transfer_ring_phys = pmm_alloc_pages(1);
    dev->transfer_ring = (xhci_trb_t *)(dev->transfer_ring_phys + g_hhdm_base);
    __builtin_memset(dev->transfer_ring, 0, 4096);
    dev->transfer_cycle = 1;

    dev->ep1_ring_phys = pmm_alloc_pages(1);
    dev->ep1_ring = (xhci_trb_t *)(dev->ep1_ring_phys + g_hhdm_base);
    __builtin_memset(dev->ep1_ring, 0, 4096);
    dev->ep1_cycle = 1;

    dev->report_buffer_phys = pmm_alloc_pages(1);
    __builtin_memset((void *)(dev->report_buffer_phys + g_hhdm_base), 0, 4096);

    xiu_paddr_t in_ctx_phys = pmm_alloc_pages(1);
    u8 *in_ctx = (u8 *)(in_ctx_phys + g_hhdm_base);
    __builtin_memset(in_ctx, 0, 4096);

    u32 *icc = (u32 *)in_ctx;
    icc[1] = 0x03;

    u8 *slot_ctx = in_ctx + m_context_size;
    u32 *sc = (u32 *)slot_ctx;
    sc[0] = (1U << 27) | (speed << 20);
    sc[1] = (port_id << 16);

    u8 *ep0_ctx = in_ctx + (m_context_size * 2);
    u32 *ep0 = (u32 *)ep0_ctx;
    ep0[1] = (ep0_max_packet << 16) | (4U << 3) | (3U << 1);
    u64 ep0_tr_ptr = dev->transfer_ring_phys | 1;
    ep0[2] = (u32)(ep0_tr_ptr & 0xFFFFFFFF);
    ep0[3] = (u32)(ep0_tr_ptr >> 32);
    ep0[4] = 8;

    xiu_paddr_t out_ctx_phys = pmm_alloc_pages(1);
    __builtin_memset((void *)(out_ctx_phys + g_hhdm_base), 0, 4096);
    m_dcbaa[slot_id] = out_ctx_phys;

    if (!send_command(in_ctx_phys, 0,
                      (TRB_TYPE_ADDRESS_DEVICE << 10) | (slot_id << 24))) {
      kprintf("[xHCI] Address Device failed on Slot %u\n", slot_id);
      return;
    }
    kprintf("  [  OK  ]  xHCI Slot %u Addressed (Dev %u)\n", slot_id,
            m_num_devices);

    bool cfg_ok = send_control_transfer_no_data(dev, 0x00, 0x09, 1, 0);
    kprintf("[xHCI] Slot %u: SET_CONFIGURATION(1) -> %s\n", slot_id,
            cfg_ok ? "OK" : "FAILED");

    // Parse config descriptor: find HID interface + interrupt IN endpoint.
    // Walk by descriptor length (proper USB descriptor traversal).
    u8 cfg_desc[64];
    __builtin_memset(cfg_desc, 0, sizeof(cfg_desc));
    u8 ep_dci = 3;      // default: EP1 IN
    u8 ep_interval = 4; // default: 2^(4-1)=8 microframes ≈ 1 ms
    u16 ep1_max_pkt = (speed == 3 || speed == 4) ? 64 : 8;
    if (send_control_transfer_data_in(dev, 0x80, 0x06, (0x02 << 8), 0, cfg_desc,
                                      sizeof(cfg_desc))) {
      int i = 0;
      while (i + 1 < 64) {
        u8 dlen = cfg_desc[i];
        u8 dtype = cfg_desc[i + 1];
        if (dlen < 2 || i + dlen > 64)
          break;
        if (dtype == 4 && dlen >= 9) { // Interface descriptor
          u8 if_class = cfg_desc[i + 5];
          u8 if_proto = cfg_desc[i + 7];
          if (if_class == 3) {
            if (if_proto == 1)
              dev->ep_type = 1;
            else if (if_proto == 2)
              dev->ep_type = 2;
            kprintf("  [  OK  ]  xHCI Slot %u HID Protocol=%u (%s)\n", slot_id,
                    if_proto, dev->ep_type == 1 ? "KEYBOARD" : "MOUSE");
          }
        } else if (dtype == 5 && dlen >= 7) { // Endpoint descriptor
          u8 ep_addr = cfg_desc[i + 2];
          u8 ep_attr = cfg_desc[i + 3];
          u16 pkt = cfg_desc[i + 4] | ((u16)cfg_desc[i + 5] << 8);
          u8 binterval = cfg_desc[i + 6];
          if ((ep_addr & 0x80) && (ep_attr & 0x03) == 0x03) { // IN + interrupt
            u8 ep_num = ep_addr & 0x0F;
            ep_dci = (ep_num << 1) | 1; // DCI = ep_num*2 + 1 (IN)
            if (pkt & 0x7FF)
              ep1_max_pkt = pkt & 0x7FF;
            // xHCI Interval field (DW0[23:16]): for HS/SS it is bInterval-1;
            // for FS/LS convert ms→microframes then take floor(log2).
            if (speed == 3 || speed == 4) {
              ep_interval = binterval > 1 ? binterval - 1 : 3;
            } else {
              u32 mf = (u32)(binterval ? binterval : 10) * 8;
              u8 iv = 0;
              while ((1U << iv) < mf && iv < 15)
                iv++;
              ep_interval = iv;
            }
            kprintf("[xHCI] Slot %u: EP%u IN → DCI=%u MaxPkt=%u Interval=%u\n",
                    slot_id, ep_num, ep_dci, ep1_max_pkt, ep_interval);
          }
        }
        i += dlen;
      }
    }
    dev->ep1_dci = ep_dci;

    __builtin_memset(in_ctx, 0, 4096);
    icc[0] = 0;
    icc[1] = (1U << 0) | (1U << ep_dci); // slot + actual endpoint

    u8 *dev_ctx = (u8 *)(m_dcbaa[slot_id] + g_hhdm_base);
    __builtin_memcpy(slot_ctx, dev_ctx, m_context_size);
    sc[0] = (sc[0] & ~(0x1FU << 27)) | (3U << 27);

    // EP context lives at ICC + (DCI+1) * ctx_size
    u8 *ep1_ctx = in_ctx + (m_context_size * (ep_dci + 1));
    u32 *ep1 = (u32 *)ep1_ctx;
    ep1[0] = (u32)ep_interval << 16; // FIXED: Interval at DW0[23:16]
    ep1[1] = (ep1_max_pkt << 16) | (7U << 3) | (3U << 1);
    u64 ep1_tr_ptr = dev->ep1_ring_phys | 1;
    ep1[2] = (u32)(ep1_tr_ptr & 0xFFFFFFFF);
    ep1[3] = (u32)(ep1_tr_ptr >> 32);
    ep1[4] = (ep1_max_pkt << 16) | 8;

    bool ep_ok = send_command(in_ctx_phys, 0,
                              (TRB_TYPE_CONFIG_EP << 10) | (slot_id << 24));
    kprintf("[xHCI] Slot %u: CONFIG_EP -> %s\n", slot_id,
            ep_ok ? "OK" : "FAILED");

    // switch to boot protocol
    send_control_transfer_no_data(dev, 0x21, 0x0B, 0, 0);
    send_control_transfer_no_data(dev, 0x21, 0x0A, 0, 0);

    if (dev->ep_type == 1) {
      set_keyboard_leds(dev, 0x01);
    }

    dev->ep1_cycle = 1;
    dev->ep1_enqueue = 0;
    __builtin_memset(dev->ep1_ring, 0, 16 * sizeof(xhci_trb_t));
    dev->ep1_ring[15].param = dev->ep1_ring_phys;
    dev->ep1_ring[15].status = 0;
    dev->ep1_ring[15].control = (TRB_TYPE_LINK << 10) | (1U << 1);

    dev->active = true;
    m_num_devices++;

    queue_interrupt_transfer(dev);
  }

  void queue_interrupt_transfer(xhci_device_t *dev) {
    if (!dev->active)
      return;

    u32 idx = dev->ep1_enqueue;

    // Write Normal TRB at current enqueue position
    dev->ep1_ring[idx].param = dev->report_buffer_phys;
    dev->ep1_ring[idx].status = 8;
    dev->ep1_ring[idx].control =
        (TRB_TYPE_NORMAL << 10) | (1U << 5) | (1U << 2) | (dev->ep1_cycle & 1);

    idx++;
    if (idx == 15) {
      // Reached Link TRB slot — arm it with current cycle, Toggle Cycle set
      dev->ep1_ring[15].control =
          (TRB_TYPE_LINK << 10) | (1U << 1) | (dev->ep1_cycle & 1);
      dev->ep1_cycle ^= 1;
      idx = 0;
    }
    dev->ep1_enqueue = idx;

    __asm__ volatile("mfence" ::: "memory");
    ring_doorbell(dev->slot_id, dev->ep1_dci);
  }

  void poll_events() {
    if (!m_initialized || !m_ev_ring || !m_interrupter0)
      return;

    irq_flags_t irq = spinlock_lock_irqsave(&m_lock);

    while (true) {
      xhci_trb_t *ev = &m_ev_ring[m_ev_dequeue];
      u32 c = ev->control & 1;
      if (c != m_ev_cycle)
        break;

      u32 trb_type = (ev->control >> 10) & 0x3F;
      if (trb_type == TRB_TYPE_TRANSFER_EVENT) {
        u8 slot = (ev->control >> 24) & 0xFF;
        u8 dci = (ev->control >> 16) & 0xFF;
        u8 cc = (ev->status >> 24) & 0xFF;
        for (u32 i = 0; i < m_num_devices; i++) {
          if (m_devices[i].slot_id == slot && m_devices[i].ep1_dci == dci &&
              m_devices[i].active) {
            const u8 *report =
                (const u8 *)(m_devices[i].report_buffer_phys + g_hhdm_base);
            // cc 1=Success, 13=Short Packet — both valid for HID interrupt IN
            if (cc == 1 || cc == 13) {
              if (m_devices[i].ep_type == 1) {
                decode_usb_keyboard(&m_devices[i], report);
              } else {
                decode_usb_mouse(&m_devices[i], report);
              }
            }
            // cycle is managed inside queue_interrupt_transfer
            queue_interrupt_transfer(&m_devices[i]);
            break;
          }
        }
      }

      m_ev_dequeue++;
      if (m_ev_dequeue >= 256) {
        m_ev_dequeue = 0;
        m_ev_cycle ^= 1;
      }
      write_mmio64(m_interrupter0 + 0x18,
                   (m_ev_ring_phys + m_ev_dequeue * sizeof(xhci_trb_t)) |
                       (1ULL << 3));
    }

    spinlock_unlock_irqrestore(&m_lock, irq);
  }

  void dump_status() {
    if (!m_initialized) {
      kprintf("\n[XIU USB STATUS] xHCI Controller: NOT INITIALIZED\n\n");
      return;
    }

    u32 usbcmd = read_mmio32(m_op_base + XHCI_OP_USBCMD);
    u32 usbsts = read_mmio32(m_op_base + XHCI_OP_USBSTS);
    u32 config = read_mmio32(m_op_base + XHCI_OP_CONFIG);
    u32 iman = read_mmio32(m_interrupter0 + 0x00);
    u64 erdp = read_mmio64(m_interrupter0 + 0x18);

    kprintf("\n=======================================================\n");
    kprintf(
        "[XIU USB STATUS] xHCI Controller Diagnostics (Ports=%u, Devices=%u)\n",
        m_max_ports, m_num_devices);
    kprintf("  Registers: USBCMD=0x%08x (RS=%u) | USBSTS=0x%08x (HCH=%u, "
            "CNR=%u) | CONFIG=0x%08x\n",
            usbcmd, usbcmd & 1, usbsts, usbsts & 1, (usbsts >> 11) & 1, config);
    kprintf("  Interrupter 0: IMAN=0x%08x (IE=%u) | ERDP=0x%016llx | "
            "EvDequeue=%u (Cycle=%u)\n",
            iman, (iman >> 1) & 1, erdp, m_ev_dequeue, m_ev_cycle);

    kprintf("  Root Hub Ports Status:\n");
    u32 connected_count = 0;
    for (u8 p = 1; p <= m_max_ports; p++) {
      u64 portsc_addr = m_op_base + XHCI_OP_PORTSC_BASE + (p - 1) * 0x10;
      u32 portsc = read_mmio32(portsc_addr);
      if (portsc & XHCI_PORTSC_CCS) {
        connected_count++;
        u8 speed = (portsc >> 10) & 0x0F;
        const char *spd_str = (speed == 4)   ? "SuperSpeed"
                              : (speed == 3) ? "HighSpeed"
                              : (speed == 1) ? "FullSpeed"
                              : (speed == 2) ? "LowSpeed"
                                             : "Unknown";
        kprintf("    -> Port %2u: PORTSC=0x%08x [CCS=1 PED=%u PR=%u PLS=%u "
                "PP=%u Speed=%u (%s)]\n",
                p, portsc, (portsc >> 1) & 1, (portsc >> 4) & 1,
                (portsc >> 5) & 0x0F, (portsc >> 9) & 1, speed, spd_str);
      }
    }
    if (connected_count == 0) {
      kprintf("    -> No devices physically connected to root hub ports.\n");
    }

    kprintf("  Enumerated HID Devices (%u):\n", m_num_devices);
    for (u32 i = 0; i < m_num_devices; i++) {
      u64 portsc_addr =
          m_op_base + XHCI_OP_PORTSC_BASE + (m_devices[i].port_id - 1) * 0x10;
      u32 portsc = read_mmio32(portsc_addr);
      kprintf("    -> Dev %u: Slot=%u, Port=%u (PORTSC=0x%08x), Type=%s, "
              "MaxPkt=%u, Active=%s, LED=0x%02x\n",
              i + 1, m_devices[i].slot_id, m_devices[i].port_id, portsc,
              m_devices[i].ep_type == 1 ? "KEYBOARD" : "MOUSE",
              m_devices[i].max_packet_size, m_devices[i].active ? "YES" : "NO",
              m_devices[i].led_state);
    }
    if (m_num_devices == 0) {
      kprintf("    -> No USB HID devices active.\n");
    }
    kprintf("=======================================================\n\n");
  }
};

static XHCIDriver s_xhci;

} // namespace XIUKit

extern "C" void xiukit_xhci_init(u64 pci_bar0) {
  XIUKit::s_xhci.init(pci_bar0);
}

extern "C" void xiukit_xhci_poll(void) { XIUKit::s_xhci.poll_events(); }

extern "C" void xiukit_xhci_dump_status(void) { XIUKit::s_xhci.dump_status(); }
