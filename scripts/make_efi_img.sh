#!/bin/bash
# scripts/make_efi_img.sh
# Generate a FAT32 UEFI bootable disk image containing BOOTX64.EFI and mach_kernel

set -e

EFI_IMG="build/disk.img"
BOOTX_EFI="bootx64.efi"
MACH_KERNEL="mach_kernel"

echo "[EFI] Creating FAT32 image..."
rm -f "$EFI_IMG"
# Create a 64MB sparse file
dd if=/dev/zero of="$EFI_IMG" bs=1m count=256

# Format as FAT32 using mformat (mtools) — newfs_msdos doesn't size the FS to the file
mformat -i "$EFI_IMG" -F -T $((256 * 2048)) ::

# Use mtools to copy files into the FAT32 image without sudo mounting
echo "[EFI] Populating EFI partition..."
mmd -i "$EFI_IMG" ::/EFI
mmd -i "$EFI_IMG" ::/EFI/BOOT
mcopy -i "$EFI_IMG" "$BOOTX_EFI" ::/EFI/BOOT/BOOTX64.EFI

mmd -i "$EFI_IMG" ::/System
mmd -i "$EFI_IMG" ::/System/Library
mmd -i "$EFI_IMG" ::/System/Library/Kernels
mcopy -i "$EFI_IMG" "$MACH_KERNEL" ::/System/Library/Kernels/mach_kernel

echo "[EFI] Success: $EFI_IMG created."

# copy userspace binaries into /bin on the disk image
echo "[EFI] Populating /bin with userspace binaries..."
mmd -i "$EFI_IMG" ::/bin || true
for f in build/usr/*; do
    [ -x "$f" ] && [ -f "$f" ] && ! [[ "$f" == *.a ]] && mcopy -i "$EFI_IMG" "$f" ::/bin/$(basename "$f")
done
echo "[EFI] Userspace binaries copied."

echo "[EFI] Populating /private/etc..."
mmd -i "$EFI_IMG" ::/private || true
mmd -i "$EFI_IMG" ::/private/etc || true
# We can't use wildcards directly with mcopy without shell expansion of the host dir
mcopy -s -i "$EFI_IMG" etc/* ::/private/etc/
echo "[EFI] /private/etc populated."
