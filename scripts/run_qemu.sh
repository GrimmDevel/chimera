#!/bin/bash
# =============================================================================
# Chimera Operating System — QEMU Launch Script
# scripts/run_qemu.sh
# =============================================================================

set -e

ARCH="x86_64"
DEBUG=0
CMDLINE_ARG=""

for arg in "$@"; do
    case "$arg" in
        x86_64|arm64)
            ARCH="$arg"
            ;;
        1)
            DEBUG=1
            ;;
        -wserver|wserver|gui|-gui|--wserver)
            CMDLINE_ARG="-wserver"
            ;;
    esac
done

if [ "$WSERVER" = "1" ] || [ "$GUI" = "1" ]; then
    CMDLINE_ARG="-wserver"
fi

KERNEL="build/${ARCH}/kernel/chimera_kernel.elf"

if [ ! -f "$KERNEL" ]; then
    echo "[CHIMERA] Error: Kernel not found at $KERNEL"
    echo "      Please run 'make build' first."
    exit 1
fi

./scripts/make_iso.sh "$ARCH" "$CMDLINE_ARG"

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
    # Multi-threaded TCG with 512MB JIT Translation Block cache for Apple Silicon
    QEMU_FLAGS+=("-accel" "tcg,tb-size=512,thread=multi" "-cpu" "Nehalem")
elif [ "$HOST_ARCH" = "$ARCH" ]; then
    # Hardware acceleration when host and target architectures match
    QEMU_FLAGS+=("-cpu" "host" "-accel" "hvf")
else
    QEMU_FLAGS+=("-accel" "tcg,tb-size=512,thread=multi" "-cpu" "max")
fi

if [ "$DEBUG" == "1" ]; then
    echo "[CHIMERA] Starting in DEBUG mode (waiting for GDB on :1234)..."
    QEMU_FLAGS+=("-s" "-S")
fi

case "$ARCH" in
    x86_64)
        echo "[CHIMERA] Launching QEMU (x86_64) via ISO & Hard Disk..."
        qemu-system-x86_64 \
            -M q35,vmport=off \
            -boot d \
            -cdrom "build/chimera-${ARCH}.iso" \
            -device piix3-ide,id=ide \
            -drive file="build/disk.img",format=raw,if=none,id=disk,cache=writeback \
            -device ide-hd,drive=disk,bus=ide.0,unit=0 \
            -netdev user,id=net0 \
            -device e1000e,netdev=net0 \
            "${QEMU_FLAGS[@]}"
        ;;
    arm64)
        echo "[CHIMERA] Launching QEMU (ARM64) via ISO..."
        qemu-system-aarch64 \
            -M virt \
            -cpu cortex-a72 \
            -cdrom "build/chimera-${ARCH}.iso" \
            "${QEMU_FLAGS[@]}"
        ;;
    *)
        echo "[CHIMERA] Error: Unsupported architecture $ARCH"
        exit 1
        ;;
esac

