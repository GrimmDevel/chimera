# ChimeraKit Driver Framework

Modular C++ driver subsystem for bus enumeration, USB 3.0, input devices, and graphics controllers.

## PCI Bus Enumeration
`chimerakit_pci` scans 256 buses, 32 devices, and 8 functions:
- Reads Vendor ID, Device ID, Class Code, Subclass, and Header Type.
- Configures PCI command register (`0x04`) enabling Bus Mastering, Memory Space, and I/O Space.
- Discovers and maps BAR0 MMIO base addresses for Intel e1000 NIC and xHCI USB controller.

## xHCI USB 3.0 Host Controller
Implements the xHCI 1.1 specification for low-speed, full-speed, high-speed, and SuperSpeed devices:
1. **BIOS Handoff**: Acquires OS-owned semaphore via Extended Capability registers (`XECP`).
2. **Rings & Data Structures**:
   - `m_cmd_ring`: Command ring for Enable Slot, Address Device, Configure Endpoint commands.
   - `m_ev_ring`: Primary event ring pointing to `ERSTBA` with dequeue pointer updated via `ERDP`.
   - `transfer_ring`: Endpoint 0 control transfer ring (Setup, Data, Status stages).
   - `ep1_ring`: Interrupt IN transfer ring with Link TRBs and toggle cycle bits.
3. **Device Enumeration**:
   - Reads device and configuration descriptors, parses interface and endpoint descriptors.
   - Issues `SET_CONFIGURATION(1)`, `SET_PROTOCOL(0)` (boot protocol), and `SET_IDLE(0)`.
   - Configures endpoint contexts with hardware polling interval and maximum packet size.
4. **Report Decoding**:
   - Keyboard: 8-byte HID boot reports parsed for modifier keys, keycode array, press/release state transitions.
   - Mouse: 3/4/8-byte reports decoded for button masks and relative DX/DY/DZ coordinate updates.

## AppleHIDDriver & Input Layer
`kernel/chimerakit/hid.cpp` provides unified input abstraction:
- Ring buffers for keyboard events and relative/absolute mouse motion.
- Scancode to ASCII/Unicode translation with Shift, Caps Lock, Ctrl, Alt, and Command modifiers.
- Direct integration into virtual console line discipline (`console_in_push()`) and `/dev/mouse`.

## Framebuffer & Display Driver
`kernel/console.c` renders graphics and text onto the linear Limine framebuffer:
- 8x16 VGA bitmap glyph rasterizer with ANSI color parsing.
- Double-buffered shadow RAM (`s_fb_shadow`) avoids un-cached VRAM reads during scrolling.
- 1000-line scrollback buffer with Shift+PageUp / Shift+PageDown viewport navigation.
