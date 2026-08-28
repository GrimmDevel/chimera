#!/bin/bash
# scripts/build_darwin.sh
# Build Darwin/Mach-O kernel, standalone EFI bootloader, and boot it.

set -e

echo "[BUILD] Compiling kernel + userspace (CMake)..."
cmake -B build -DCMAKE_TOOLCHAIN_FILE=$(pwd)/cmake/toolchain-x86_64.cmake . 2>/dev/null
cmake --build build -j$(sysctl -n hw.ncpu 2>/dev/null || nproc)
cp build/kernel/chimera_kernel.elf mach_kernel

echo "[BUILD] Compiling standalone efiloader to BOOTX64.EFI..."
clang -target x86_64-unknown-windows \
      -ffreestanding -fno-stack-protector -fshort-wchar -mno-red-zone \
      -nostdlibinc -c boot/efi/efiloader.c -o efiloader.o

lld-link -subsystem:efi_application -entry:efi_main efiloader.o -out:bootx64.efi

echo "[BUILD] Generating FAT32 EFI Disk Image..."
mkdir -p build
./scripts/make_efi_img.sh

echo "[QEMU] Booting via UEFI (OVMF)..."

# Locate OVMF
OVMF_PATH=""
for p in "/opt/homebrew/share/qemu/edk2-x86_64-code.fd" "/usr/share/OVMF/OVMF_CODE.fd" "/usr/local/share/qemu/edk2-x86_64-code.fd"; do
    if [ -f "$p" ]; then
        OVMF_PATH="$p"
        break
    fi
done

if [ -z "$OVMF_PATH" ]; then
    echo "Warning: OVMF not found! QEMU might fail to boot UEFI."
    # Fallback to qemu's default if it has one built-in
    QEMU_BIOS=""
else
    QEMU_BIOS="-drive if=pflash,format=raw,readonly=on,file=$OVMF_PATH"
fi

qemu-system-x86_64 \
    -m 2G -smp 4 \
    -M q35 -cpu max \
    $QEMU_BIOS \
    -drive format=raw,file=build/disk.img \
    -serial stdio \
    -display cocoa,zoom-to-fit=on \
    -device virtio-vga
