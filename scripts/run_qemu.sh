#!/bin/bash
# =============================================================================
# Chimera Operating System — QEMU Launch Script
# scripts/run_qemu.sh
# =============================================================================

set -e

# Stage-0 multicore baseline: use one vCPU unless an explicit controlled SMP
# test supplies CHIMERA_VCPUS.
CHIMERA_VCPUS="${CHIMERA_VCPUS:-1}"
case "$CHIMERA_VCPUS" in
    ''|*[!0-9]*|0)
        echo "[CHIMERA] Error: CHIMERA_VCPUS must be an integer from 1 to 16."
        exit 1
        ;;
esac
if [ "$CHIMERA_VCPUS" -gt 16 ]; then
    echo "[CHIMERA] Error: CHIMERA_VCPUS must not exceed 16."
    exit 1
fi

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

KERNEL="build/${ARCH}/kernel/mach_kernel"

if [ ! -f "$KERNEL" ]; then
    echo "[CHIMERA] Error: Kernel not found at $KERNEL"
    echo "      Please run 'make build' first."
    exit 1
fi

HOST_ARCH=$(uname -m)

QEMU_FLAGS=(
    "-serial" "stdio"
    "-m" "2G"
    "-vga" "std"
    "-display" "cocoa,zoom-to-fit=on"
    # Keep the default runner aligned with make run: SMP scheduling is not
    # stable yet, so boot a single vCPU for reliable console input.
    "-smp" "$CHIMERA_VCPUS"
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
        if [ ! -f "bootx64.efi" ]; then
            echo "[CHIMERA] Error: bootx64.efi not found. Run 'make run' once to build the UEFI loader."
            exit 1
        fi
        echo "[CHIMERA] Preparing Mach-O UEFI disk image..."
        MACH_KERNEL="$KERNEL" USR_BIN_DIR="build/${ARCH}/usr" ./scripts/make_efi_img.sh
        echo "[CHIMERA] Launching QEMU (x86_64) via UEFI disk, vCPUs: $CHIMERA_VCPUS..."
        OVMF_PATH=""
        for p in "/opt/homebrew/share/qemu/edk2-x86_64-code.fd" "/usr/share/OVMF/OVMF_CODE.fd" "/usr/local/share/qemu/edk2-x86_64-code.fd"; do
            if [ -f "$p" ]; then OVMF_PATH="$p"; break; fi
        done
        if [ -z "$OVMF_PATH" ]; then
            echo "[CHIMERA] Error: OVMF firmware not found"
            exit 1
        fi
        qemu-system-x86_64 \
    -no-reboot \
            -M q35,vmport=off \
            -drive if=pflash,format=raw,readonly=on,file="$OVMF_PATH" \
            -drive file="build/disk.img",format=raw,if=ide,cache=writeback \
            -netdev user,id=net0 \
            -device e1000e,netdev=net0 \
            "${QEMU_FLAGS[@]}"
        ;;
    arm64)
        echo "[CHIMERA] Error: the Mach-O UEFI loader currently supports x86_64 only."
        exit 1
        ;;
    *)
        echo "[CHIMERA] Error: Unsupported architecture $ARCH"
        exit 1
        ;;
esac
