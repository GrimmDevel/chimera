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
# Copy core userspace binaries into ISO according to Darwin hierarchy
mkdir -p "$ISO_ROOT/bin" "$ISO_ROOT/sbin" "$ISO_ROOT/usr/bin" "$ISO_ROOT/usr/sbin"
mkdir -p "$ISO_ROOT/private/etc" "$ISO_ROOT/private/var" "$ISO_ROOT/private/tmp"
mkdir -p "$ISO_ROOT/Applications" "$ISO_ROOT/Users" "$ISO_ROOT/Library" "$ISO_ROOT/System"

for bin_path in build/${ARCH}/usr/*; do
    if [ -f "$bin_path" ]; then
        name="$(basename "$bin_path")"
        case "$name" in
            *.a|*.o|*.obj|*.txt|*.cmake|*.ninja|*.json|Makefile|CMakeFiles|cmake_install.cmake|elf) ;;
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

printf "Welcome to XIU Operating System!\nApple Darwin / Mach-BSD Hybrid Architecture.\n" > "$ISO_ROOT/private/etc/motd"
printf "XIU OS v0.1.0 (Darwin 24.0.0 %s)\n" "$ARCH" > "$ISO_ROOT/private/etc/version"
printf "127.0.0.1\tlocalhost\n10.0.2.15\txiu-mac\n" > "$ISO_ROOT/private/etc/hosts"

# Generate complete limine.conf with all kernel modules
cat << 'EOF' > "$ISO_ROOT/limine.conf"
# =============================================================================
# XIU Operating System — Limine Configuration (v8.x Format)
# limine.conf
# =============================================================================

timeout: 0

/XIU Operating System
    protocol: limine
    kernel_path: boot():/kernel.elf
EOF

for f in $(find "$ISO_ROOT/bin" "$ISO_ROOT/sbin" "$ISO_ROOT/usr/bin" "$ISO_ROOT/usr/sbin" "$ISO_ROOT/private/etc" -type f 2>/dev/null | sort); do
    rel_path="${f#$ISO_ROOT}"
    echo "    module_path: boot():$rel_path" >> "$ISO_ROOT/limine.conf"
done

mkdir -p "$ISO_ROOT/boot/limine" "$ISO_ROOT/EFI/BOOT"
cp "$ISO_ROOT/limine.conf" "$ISO_ROOT/limine.cfg"
cp "$ISO_ROOT/limine.conf" "$ISO_ROOT/boot/limine/"
cp "$ISO_ROOT/limine.conf" "$ISO_ROOT/boot/limine/limine.cfg"
cp "$ISO_ROOT/limine.conf" "$ISO_ROOT/EFI/BOOT/"
cp "$ISO_ROOT/limine.conf" "$ISO_ROOT/EFI/BOOT/limine.cfg"

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
