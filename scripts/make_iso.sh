#!/bin/bash
# =============================================================================
# XIU Operating System — ISO Creation Script (Limine)
# scripts/make_iso.sh
# =============================================================================

set -e

ARCH=${1:-x86_64}
KERNEL="build/${ARCH}/kernel/xiu_kernel.elf"
ISO="build/xiu-${ARCH}.iso"
ISO_ROOT="build/iso_root"

if [ ! -f "$KERNEL" ]; then
    echo "[XIU] Error: Kernel not found at $KERNEL"
    exit 1
fi

echo "[XIU] Preparing ISO root..."
rm -rf "$ISO_ROOT"
mkdir -p "$ISO_ROOT"

cp "$KERNEL" "$ISO_ROOT/kernel.elf"
cp limine.conf "$ISO_ROOT/"
cp limine.cfg "$ISO_ROOT/" 2>/dev/null || true

mkdir -p "$ISO_ROOT/boot/limine"
cp limine.conf "$ISO_ROOT/boot/limine/"
cp limine.cfg "$ISO_ROOT/boot/limine/" 2>/dev/null || true

mkdir -p "$ISO_ROOT/EFI/BOOT"
cp limine.conf "$ISO_ROOT/EFI/BOOT/"
cp limine.cfg "$ISO_ROOT/EFI/BOOT/" 2>/dev/null || true

# Copy core userspace binaries into ISO according to Darwin hierarchy
mkdir -p "$ISO_ROOT/bin" "$ISO_ROOT/sbin" "$ISO_ROOT/usr/bin" "$ISO_ROOT/usr/sbin"
for bin_path in build/${ARCH}/usr/*; do
    if [ -f "$bin_path" ]; then
        name="$(basename "$bin_path")"
        case "$name" in
            *.a|*.o|*.obj|*.txt|*.cmake|*.ninja|*.json) ;;
            sh|dash|ls|cat|cp|mv|rm|mkdir|pwd|date|sleep|kill|chmod|df|echo|clear|true|false)
                cp "$bin_path" "$ISO_ROOT/bin/"
                ;;
            ifconfig|ping)
                cp "$bin_path" "$ISO_ROOT/sbin/"
                ;;
            wserver)
                cp "$bin_path" "$ISO_ROOT/usr/sbin/"
                ;;
            *)
                cp "$bin_path" "$ISO_ROOT/usr/bin/"
                ;;
        esac
    fi
done

LIMINE_DIR="build/limine"
if [ ! -d "$LIMINE_DIR" ]; then
    for path in "/opt/homebrew/share/limine" "/usr/local/share/limine" "/usr/share/limine"; do
        if [ -d "$path" ] && [ -f "$path/limine-bios.sys" ]; then
            LIMINE_DIR="$path"
            break
        fi
    done
fi

if [ ! -d "$LIMINE_DIR" ] || [ ! -f "$LIMINE_DIR/limine-bios.sys" ]; then
    echo "[XIU] Error: Limine directory or limine-bios.sys not found."
    echo "      Please install limine (e.g. 'brew install limine') or place it in build/limine"
    exit 1
fi

cp "$LIMINE_DIR/limine-bios.sys" "$ISO_ROOT/"
cp "$LIMINE_DIR/limine-bios-cd.bin" "$ISO_ROOT/"
cp "$LIMINE_DIR/limine-uefi-cd.bin" "$ISO_ROOT/"

# Copy UEFI executables for direct USB / UEFI boot
[ -f "$LIMINE_DIR/BOOTX64.EFI" ] && cp "$LIMINE_DIR/BOOTX64.EFI" "$ISO_ROOT/EFI/BOOT/"
[ -f "$LIMINE_DIR/BOOTIA32.EFI" ] && cp "$LIMINE_DIR/BOOTIA32.EFI" "$ISO_ROOT/EFI/BOOT/"
[ -f "$LIMINE_DIR/BOOTAA64.EFI" ] && cp "$LIMINE_DIR/BOOTAA64.EFI" "$ISO_ROOT/EFI/BOOT/"

echo "[XIU] Creating ISO with xorriso..."
xorriso -as mkisofs -b limine-bios-cd.bin \
        -no-emul-boot -boot-load-size 4 -boot-info-table \
        --efi-boot limine-uefi-cd.bin \
        -efi-boot-part --efi-boot-image --protective-msdos-label \
        "$ISO_ROOT" -o "$ISO"

echo "[XIU] Installing Limine to ISO..."
if [ -f "./build/limine/limine" ]; then
    ./build/limine/limine bios-install "$ISO"
elif command -v limine &> /dev/null; then
    limine bios-install "$ISO"
else
    echo "[XIU] Warning: 'limine' tool not found. BIOS boot may not work."
    echo "      Install it with 'brew install limine' for full BIOS support."
fi

echo "[XIU] ISO created: $ISO"
