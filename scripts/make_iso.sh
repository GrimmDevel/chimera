#!/bin/bash
# =============================================================================
# Chimera Operating System — ISO Creation Script (Limine)
# scripts/make_iso.sh
# =============================================================================

set -e

ARCH=${1:-x86_64}
CMDLINE_ARG=${2:-""}
KERNEL="build/${ARCH}/kernel/chimera_kernel.elf"
ISO="build/chimera-${ARCH}.iso"
ISO_ROOT="build/iso_root"

if [ ! -f "$KERNEL" ]; then
    echo "[CHIMERA] Error: Kernel not found at $KERNEL"
    exit 1
fi

echo "[CHIMERA] Preparing ISO root..."
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
            sh|dash|zsh|ls|cat|cp|mv|rm|mkdir|pwd|date|sleep|kill|chmod|df|echo|clear|true|false)
                cp "$bin_path" "$ISO_ROOT/bin/"
                cp "$bin_path" "$ISO_ROOT/usr/bin/"
                ;;
            ifconfig|ping)
                cp "$bin_path" "$ISO_ROOT/sbin/"
                ;;
            wserver)
                cp "$bin_path" "$ISO_ROOT/usr/sbin/"
                ;;
            WindowServer)
                mkdir -p "$ISO_ROOT/System/Library/CoreServices"
                cp "$bin_path" "$ISO_ROOT/System/Library/CoreServices/"
                cp "$bin_path" "$ISO_ROOT/usr/sbin/"
                cp "$bin_path" "$ISO_ROOT/usr/bin/"
                ;;
            *)
                cp "$bin_path" "$ISO_ROOT/usr/bin/"
                ;;
        esac
    fi
done

if [ -d "etc" ]; then
    cp -r etc/* "$ISO_ROOT/private/etc/" 2>/dev/null || true
fi

mkdir -p "$ISO_ROOT/Users/root" "$ISO_ROOT/Users/fvr" "$ISO_ROOT/Users/user" "$ISO_ROOT/Users/Shared"
for u in root fvr user; do
    mkdir -p "$ISO_ROOT/Users/$u/Desktop" "$ISO_ROOT/Users/$u/Documents" "$ISO_ROOT/Users/$u/Downloads" "$ISO_ROOT/Users/$u/Library" "$ISO_ROOT/Users/$u/Pictures" "$ISO_ROOT/Users/$u/Public"
    touch "$ISO_ROOT/Users/$u/Desktop/.localized" "$ISO_ROOT/Users/$u/Documents/.localized" "$ISO_ROOT/Users/$u/Downloads/.localized" "$ISO_ROOT/Users/$u/Library/.localized" "$ISO_ROOT/Users/$u/Pictures/.localized" "$ISO_ROOT/Users/$u/Public/.localized"
    printf 'export PATH="/bin:/usr/bin:/sbin:/usr/sbin:/usr/local/bin"\nexport TERM="xterm-256color"\nexport PROMPT="%%n@%%m %%~ %%# "\nexport PS1="%%n@%%m %%~ %%# "\nunsetopt zle\nunsetopt promptcr\nunsetopt promptsp\n' > "$ISO_ROOT/Users/$u/.zshrc"
    printf '#include <stdio.h>\n\nint main() {\n    printf("Hello from self-hosted Chimera C compiler!\\n");\n    return 0;\n}\n' > "$ISO_ROOT/Users/$u/hello.c"
done


printf "Welcome to Chimera Operating System!\nApple Darwin / Mach-BSD Hybrid Architecture.\n" > "$ISO_ROOT/private/etc/motd"
printf "Chimera OS v0.1.0 (Darwin 24.0.0 %s)\n" "$ARCH" > "$ISO_ROOT/private/etc/version"
printf "127.0.0.1\tlocalhost\n10.0.2.15\tchimera-mac\n" > "$ISO_ROOT/private/etc/hosts"

# Generate complete limine.conf with all kernel modules
cat << EOF > "$ISO_ROOT/limine.conf"
# =============================================================================
# Chimera Operating System — Limine Configuration (v8.x Format)
# limine.conf
# =============================================================================

timeout: 0

/Chimera Operating System
    protocol: limine
    kernel_path: boot():/kernel.elf
EOF

if [ -n "$CMDLINE_ARG" ]; then
    echo "    cmdline: $CMDLINE_ARG" >> "$ISO_ROOT/limine.conf"
fi

for f in $(find "$ISO_ROOT/bin" "$ISO_ROOT/sbin" "$ISO_ROOT/usr/bin" "$ISO_ROOT/usr/sbin" "$ISO_ROOT/private/etc" "$ISO_ROOT/Users" -type f 2>/dev/null | sort); do
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
    echo "[CHIMERA] Error: Limine directory or limine-bios.sys not found."
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

echo "[CHIMERA] Creating ISO with xorriso..."
xorriso -as mkisofs -b limine-bios-cd.bin \
        -no-emul-boot -boot-load-size 4 -boot-info-table \
        --efi-boot limine-uefi-cd.bin \
        -efi-boot-part --efi-boot-image --protective-msdos-label \
        "$ISO_ROOT" -o "$ISO"

echo "[CHIMERA] Installing Limine to ISO..."
if [ -f "./build/limine/limine" ]; then
    ./build/limine/limine bios-install "$ISO"
elif command -v limine &> /dev/null; then
    limine bios-install "$ISO"
else
    echo "[CHIMERA] Warning: 'limine' tool not found. BIOS boot may not work."
    echo "      Install it with 'brew install limine' for full BIOS support."
fi

echo "[CHIMERA] ISO created: $ISO"
