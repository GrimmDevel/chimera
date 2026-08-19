#!/bin/bash
# =============================================================================
# XIU Operating System — QEMU Launch Script
# scripts/run_qemu.sh
# =============================================================================

set -e

ARCH=${1:-x86_64}
DEBUG=${2:-0}
KERNEL="build/${ARCH}/kernel/xiu_kernel.elf"

if [ ! -f "$KERNEL" ]; then
    echo "[XIU] Error: Kernel not found at $KERNEL"
    echo "      Please run 'make build' first."
    exit 1
fi

HOST_ARCH=$(uname -m)

QEMU_FLAGS=(
    "-serial" "stdio"
    "-m" "2G"
    "-vga" "std"
    "-display" "cocoa,zoom-to-fit=on"
    "-smp" "4"
)

# USB: mouse via xHCI (for GUI), keyboard via PS/2 (no Cocoa grab issues)
QEMU_FLAGS+=(
    "-device" "qemu-xhci,id=xhci"
    "-device" "usb-mouse,bus=xhci.0"
)



if [ "$HOST_ARCH" = "arm64" ] && [ "$ARCH" = "x86_64" ]; then
    # Emulation (TCG) is required for x86_64 on Apple Silicon host
    QEMU_FLAGS+=("-cpu" "max")
elif [ "$HOST_ARCH" = "$ARCH" ]; then
    # Hardware acceleration when host and target architectures match
    QEMU_FLAGS+=("-cpu" "host" "-accel" "hvf")
else
    QEMU_FLAGS+=("-cpu" "max")
fi

if [ "$DEBUG" == "1" ]; then
    echo "[XIU] Starting in DEBUG mode (waiting for GDB on :1234)..."
    QEMU_FLAGS+=("-s" "-S")
fi

case "$ARCH" in
    x86_64)
        echo "[XIU] Launching QEMU (x86_64) via ISO & Hard Disk..."
        qemu-system-x86_64 \
            -M q35,vmport=off \
            -boot d \
            -cdrom "build/xiu-${ARCH}.iso" \
            -device piix3-ide,id=ide \
            -drive file="build/disk.img",format=raw,if=none,id=disk,cache=directsync \
            -device ide-hd,drive=disk,bus=ide.0,unit=0 \
            -netdev user,id=net0 \
            -device e1000e,netdev=net0 \
            "${QEMU_FLAGS[@]}"
        ;;
    arm64)
        echo "[XIU] Launching QEMU (ARM64) via ISO..."
        qemu-system-aarch64 \
            -M virt \
            -cpu cortex-a72 \
            -cdrom "build/xiu-${ARCH}.iso" \
            "${QEMU_FLAGS[@]}"
        ;;
    *)
        echo "[XIU] Error: Unsupported architecture $ARCH"
        exit 1
        ;;
esac

