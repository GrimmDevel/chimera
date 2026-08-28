# chimerakit driver framework

modular c++ driver subsystem for bus enumeration, usb 3.0, input devices, and graphics.

## pci bus enumeration
`chimerakit_pci` scans 256 buses, 32 devices, 8 functions:
- reads vendor id / device id / class code
- enables bus mastering, memory space, and io space in command register (`0x04`)
- extracts BAR0 mmio base addresses for e1000 nic and xhci controller

## xhci usb 3.0 host controller
implements xhci 1.1 specification for full-speed, high-speed, and superspeed devices:
1. **bios handoff**: acquires os-owned semaphore via extended capability registers (`XECP`).
2. **rings & buffers**:
   - `m_cmd_ring`: command ring for enable slot, address device, configure endpoint.
   - `m_ev_ring`: event ring pointing to `ERSTBA` with dequeue pointer written to `ERDP`.
   - `transfer_ring`: endpoint 0 control transfer ring (setup, data, status stages).
   - `ep1_ring`: 16-trb ring for interrupt IN endpoints linked with toggle cycle link trb.
3. **hid enumeration**:
   - queries configuration descriptors, parses interface and endpoint descriptors.
   - sends `SET_CONFIGURATION(1)`, `SET_PROTOCOL(0)` (boot protocol), `SET_IDLE(0)`.
   - configures endpoint context with actual endpoint interval and max packet size.
4. **packet decoding**:
   - keyboard: 8-byte boot report parsed for modifiers, scan codes, key press/release transitions.
   - mouse: 3/4/8-byte reports parsed for button masks and relative dx/dy/dz movement.

## hid input layer
`kernel/chimerakit/hid.cpp` provides unified input abstraction:
- maintains circular ringbuffers for keyboard events and mouse motion.
- decodes scancodes to unicode with shift, caps lock, ctrl, alt, and cmd modifiers.
- feeds canonical line buffer in `console_in_push()`.

## framebuffer console
`kernel/console.c` renders text onto linear limine framebuffer:
- 8x16 vga bitmap glyph rasterization.
- double-buffered shadow ram (`s_fb_shadow`) in cached memory avoids slow un-cached vram reads during scrolling.
- ansi escape parser handles cursor repositioning, screen clear (`\033[2J`), and colors.
- 1000-line scrollback buffer with shift+pageup/pagedown viewport navigation.
