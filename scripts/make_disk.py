#!/usr/bin/env python3
"""
XIU Operating System — FAT32 Disk Image Generator & In-Place Binary Updater
scripts/make_disk.py
Generates a 64MB FAT32 disk image containing system directories,
configurations, and userland ELF binaries.
"""

import os
import sys
import struct

DISK_SIZE = 64 * 1024 * 1024  # 64 MB
SECTOR_SIZE = 512
SECTORS_PER_CLUSTER = 8        # 4 KB cluster
RESERVED_SECTORS = 32
NUM_FATS = 2
SECTORS_PER_FAT = 256
ROOT_CLUSTER = 2

TOTAL_SECTORS = DISK_SIZE // SECTOR_SIZE
DATA_START_SECTOR = RESERVED_SECTORS + NUM_FATS * SECTORS_PER_FAT
TOTAL_CLUSTERS = (TOTAL_SECTORS - DATA_START_SECTOR) // SECTORS_PER_CLUSTER

class FAT32Disk:
    def __init__(self):
        self.image = bytearray(DISK_SIZE)
        self.fat = [0] * (TOTAL_CLUSTERS + 2)
        self.fat[0] = 0x0FFFFFF8
        self.fat[1] = 0x0FFFFFFF
        self.fat[ROOT_CLUSTER] = 0x0FFFFFFF
        self.next_free_cluster = ROOT_CLUSTER + 1

    def cluster_to_lba(self, cluster):
        return DATA_START_SECTOR + (cluster - 2) * SECTORS_PER_CLUSTER

    def cluster_offset(self, cluster):
        return self.cluster_to_lba(cluster) * SECTOR_SIZE

    def write_bpb(self):
        self.image[0:3] = b"\xEB\x58\x90"
        self.image[3:11] = b"XIU_OS  "
        struct.pack_into("<H", self.image, 11, SECTOR_SIZE)
        self.image[13] = SECTORS_PER_CLUSTER
        struct.pack_into("<H", self.image, 14, RESERVED_SECTORS)
        self.image[16] = NUM_FATS
        struct.pack_into("<H", self.image, 17, 0)
        struct.pack_into("<H", self.image, 19, 0)
        self.image[21] = 0xF8
        struct.pack_into("<H", self.image, 22, 0)
        struct.pack_into("<H", self.image, 24, 63)
        struct.pack_into("<H", self.image, 26, 255)
        struct.pack_into("<I", self.image, 28, 0)
        struct.pack_into("<I", self.image, 32, TOTAL_SECTORS)
        struct.pack_into("<I", self.image, 36, SECTORS_PER_FAT)
        struct.pack_into("<H", self.image, 40, 0)
        struct.pack_into("<H", self.image, 42, 0)
        struct.pack_into("<I", self.image, 44, ROOT_CLUSTER)
        struct.pack_into("<H", self.image, 48, 1)
        struct.pack_into("<H", self.image, 50, 6)
        self.image[64] = 0x80
        self.image[66] = 0x29
        struct.pack_into("<I", self.image, 67, 0x12345678)
        self.image[71:82] = b"XIU SYSTEM "
        self.image[82:90] = b"FAT32   "
        self.image[510:512] = b"\x55\xAA"

        fsinfo_offset = 1 * SECTOR_SIZE
        self.image[fsinfo_offset : fsinfo_offset + 4] = b"RRaA"
        self.image[fsinfo_offset + 484 : fsinfo_offset + 488] = b"rrAa"
        struct.pack_into("<I", self.image, fsinfo_offset + 488, TOTAL_CLUSTERS - 20)
        struct.pack_into("<I", self.image, fsinfo_offset + 492, ROOT_CLUSTER + 1)
        self.image[fsinfo_offset + 510 : fsinfo_offset + 512] = b"\x55\xAA"

    def alloc_cluster(self) -> int:
        for c in range(self.next_free_cluster, TOTAL_CLUSTERS + 2):
            if self.fat[c] == 0:
                self.fat[c] = 0x0FFFFFFF
                self.next_free_cluster = c + 1
                return c
        for c in range(ROOT_CLUSTER + 1, self.next_free_cluster):
            if self.fat[c] == 0:
                self.fat[c] = 0x0FFFFFFF
                self.next_free_cluster = c + 1
                return c
        raise RuntimeError("FAT32 disk full")

    def write_fat_tables(self):
        fat_bytes = bytearray(SECTORS_PER_FAT * SECTOR_SIZE)
        for i, val in enumerate(self.fat):
            if i * 4 + 4 <= len(fat_bytes):
                struct.pack_into("<I", fat_bytes, i * 4, val & 0x0FFFFFFF)

        fat1_offset = RESERVED_SECTORS * SECTOR_SIZE
        fat2_offset = (RESERVED_SECTORS + SECTORS_PER_FAT) * SECTOR_SIZE
        self.image[fat1_offset : fat1_offset + len(fat_bytes)] = fat_bytes
        self.image[fat2_offset : fat2_offset + len(fat_bytes)] = fat_bytes

    def write_file_data(self, data: bytes):
        if not data:
            return 0, 0

        cluster_size = SECTORS_PER_CLUSTER * SECTOR_SIZE
        total_clusters_needed = (len(data) + cluster_size - 1) // cluster_size

        clusters = [self.alloc_cluster() for _ in range(total_clusters_needed)]

        for i in range(len(clusters) - 1):
            self.fat[clusters[i]] = clusters[i + 1]
        self.fat[clusters[-1]] = 0x0FFFFFFF

        offset = 0
        for cluster in clusters:
            chunk = data[offset : offset + cluster_size]
            c_off = self.cluster_offset(cluster)
            self.image[c_off : c_off + len(chunk)] = chunk
            offset += len(chunk)

        return clusters[0], len(data)

    def make_83_name(self, filename: str):
        parts = filename.split(".")
        name = parts[0][:8].upper().ljust(8)
        ext = parts[1][:3].upper().ljust(3) if len(parts) > 1 else "   "
        return name.encode("ascii"), ext.encode("ascii")

    def make_dir_entry(self, name_83: bytes, ext_83: bytes, attr: int, start_cluster: int, size: int) -> bytes:
        entry = bytearray(32)
        entry[0:8] = name_83
        entry[8:11] = ext_83
        entry[11] = attr
        struct.pack_into("<H", entry, 20, (start_cluster >> 16) & 0xFFFF)
        struct.pack_into("<H", entry, 26, start_cluster & 0xFFFF)
        struct.pack_into("<I", entry, 28, size)
        return bytes(entry)

    def add_directory(self, parent_cluster: int, dirname: str) -> int:
        dir_cluster = self.alloc_cluster()
        dir_offset = self.cluster_offset(dir_cluster)

        dot_entry = self.make_dir_entry(b".       ", b"   ", 0x10, dir_cluster, 0)
        dotdot_cluster = parent_cluster if parent_cluster != ROOT_CLUSTER else 0
        dotdot_entry = self.make_dir_entry(b"..      ", b"   ", 0x10, dotdot_cluster, 0)
        self.image[dir_offset : dir_offset + 32] = dot_entry
        self.image[dir_offset + 32 : dir_offset + 64] = dotdot_entry

        name_83, ext_83 = self.make_83_name(dirname)
        entry = self.make_dir_entry(name_83, ext_83, 0x10, dir_cluster, 0)
        self._append_to_dir(parent_cluster, entry)
        return dir_cluster

    def add_file(self, parent_cluster: int, filename: str, data: bytes):
        start_cluster, size = self.write_file_data(data)
        name_83, ext_83 = self.make_83_name(filename)
        entry = self.make_dir_entry(name_83, ext_83, 0x20, start_cluster, size)
        self._append_to_dir(parent_cluster, entry)

    def _append_to_dir(self, dir_cluster: int, entry: bytes):
        cluster_size = SECTORS_PER_CLUSTER * SECTOR_SIZE
        offset = self.cluster_offset(dir_cluster)
        for i in range(0, cluster_size, 32):
            if self.image[offset + i] == 0x00 or self.image[offset + i] == 0xE5:
                self.image[offset + i : offset + i + 32] = entry
                return
        print(f"[make_disk] Warning: Directory cluster {dir_cluster} is full")

    def load_existing(self, path: str):
        with open(path, "rb") as f:
            self.image = bytearray(f.read())
        fat_offset = RESERVED_SECTORS * SECTOR_SIZE
        for c in range(TOTAL_CLUSTERS + 2):
            if fat_offset + c * 4 + 4 <= len(self.image):
                val = struct.unpack_from("<I", self.image, fat_offset + c * 4)[0] & 0x0FFFFFFF
                self.fat[c] = val

    def update_binaries(self, bin_dir: str, binaries: list):
        root_offset = self.cluster_offset(ROOT_CLUSTER)
        bin_cluster = None
        for i in range(0, SECTORS_PER_CLUSTER * SECTOR_SIZE, 32):
            entry = self.image[root_offset + i : root_offset + i + 32]
            if entry[0] == 0x00: break
            if entry[0] == 0xE5: continue
            if entry[0:11] == b"BIN        " and (entry[11] & 0x10):
                ch = struct.unpack_from("<H", entry, 20)[0]
                cl = struct.unpack_from("<H", entry, 26)[0]
                bin_cluster = (ch << 16) | cl
                break
        if not bin_cluster:
            print("[make_disk] /bin not found on existing disk.")
            return

        bin_offset = self.cluster_offset(bin_cluster)
        updated = 0
        for name in binaries:
            bin_path = os.path.join(bin_dir, name)
            if not os.path.isfile(bin_path):
                continue
            with open(bin_path, "rb") as f:
                data = f.read()

            name_83, ext_83 = self.make_83_name(name)
            target_key = name_83 + ext_83

            found_slot = None
            for i in range(0, SECTORS_PER_CLUSTER * SECTOR_SIZE, 32):
                entry = self.image[bin_offset + i : bin_offset + i + 32]
                if entry[0] == 0x00:
                    if found_slot is None: found_slot = bin_offset + i
                    break
                if entry[0] == 0xE5:
                    if found_slot is None: found_slot = bin_offset + i
                    continue
                if entry[0:11] == target_key:
                    ch = struct.unpack_from("<H", entry, 20)[0]
                    cl = struct.unpack_from("<H", entry, 26)[0]
                    old_cluster = (ch << 16) | cl
                    cur = old_cluster
                    while cur >= 2 and cur < 0x0FFFFFF8:
                        nxt = self.fat[cur]
                        self.fat[cur] = 0
                        cur = nxt
                    found_slot = bin_offset + i
                    break

            start_cluster, size = self.write_file_data(data)
            entry = self.make_dir_entry(name_83, ext_83, 0x20, start_cluster, size)
            if found_slot is not None:
                self.image[found_slot : found_slot + 32] = entry
                updated += 1

        self.write_fat_tables()
        print(f"[make_disk] Updated {updated} binaries in /bin on persistent disk.")

def main():
    workspace = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    out_dir = os.path.join(workspace, "build")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "disk.img")
    bin_dir = os.path.join(workspace, "build", "x86_64", "usr")
    binaries = ["sh", "dash", "ls", "cat", "echo", "mkdir", "rm", "pwd", "neofetch", "proclist", "kilo", "touch", "tcc", "ifconfig", "ping", "nc", "curl", "machdemo", "smpdemo", "wserver", "guiapp", "calc"]

    force = "--force" in sys.argv or "-f" in sys.argv
    if os.path.isfile(out_path) and not force:
        print(f"[make_disk] Disk image {out_path} exists. Updating /bin binaries in-place...")
        disk = FAT32Disk()
        disk.load_existing(out_path)
        disk.update_binaries(bin_dir, binaries)
        with open(out_path, "wb") as f:
            f.write(disk.image)
        return

    print(f"[make_disk] Generating 64MB FAT32 disk image: {out_path}")
    disk = FAT32Disk()
    disk.write_bpb()

    bin_cluster = disk.add_directory(ROOT_CLUSTER, "bin")
    etc_cluster = disk.add_directory(ROOT_CLUSTER, "etc")
    users_cluster = disk.add_directory(ROOT_CLUSTER, "Users")
    sys_cluster = disk.add_directory(ROOT_CLUSTER, "System")
    tmp_cluster = disk.add_directory(ROOT_CLUSTER, "tmp")
    test_cluster = disk.add_directory(ROOT_CLUSTER, "test")
    lib_cluster = disk.add_directory(ROOT_CLUSTER, "lib")
    usr_cluster = disk.add_directory(ROOT_CLUSTER, "usr")
    usr_include_cluster = disk.add_directory(usr_cluster, "include")
    usr_lib_cluster = disk.add_directory(usr_cluster, "lib")

    motd_content = b"Welcome to XIU Operating System!\nHybrid Mach/BSD Kernel with persistent FAT32 storage.\n"
    disk.add_file(etc_cluster, "motd", motd_content)
    version_content = b"XIU OS v0.1.0 (x86_64-console)\n"
    disk.add_file(etc_cluster, "version", version_content)
    welcome_content = b"Hello from persistent user storage on disk0!\n"
    disk.add_file(users_cluster, "welcome.txt", welcome_content)
    hello_content = b'#include <stdio.h>\n\nint main() {\n    printf("Hello from self-hosted XIU C compiler!\\n");\n    return 0;\n}\n'
    disk.add_file(test_cluster, "hello.c", hello_content)

    # Copy runtime objects and libraries
    crt0_path = os.path.join(bin_dir, "CMakeFiles", "xiu_crt0.dir", "libsystem", "crt0.S.obj")
    libsystem_path = os.path.join(bin_dir, "libsystem_xiu.a")
    if os.path.isfile(crt0_path):
        with open(crt0_path, "rb") as f:
            crt0_data = f.read()
        disk.add_file(lib_cluster, "crt0.o", crt0_data)
        disk.add_file(usr_lib_cluster, "crt0.o", crt0_data)
    if os.path.isfile(libsystem_path):
        with open(libsystem_path, "rb") as f:
            libc_data = f.read()
        disk.add_file(lib_cluster, "libc.a", libc_data)
        disk.add_file(lib_cluster, "libsystem_xiu.a", libc_data)
        disk.add_file(usr_lib_cluster, "libc.a", libc_data)
        disk.add_file(usr_lib_cluster, "libsystem_xiu.a", libc_data)

    # Copy C headers into /usr/include
    inc_dir = os.path.join(workspace, "usr", "libsystem", "include")
    if os.path.isdir(inc_dir):
        for h in os.listdir(inc_dir):
            h_path = os.path.join(inc_dir, h)
            if os.path.isfile(h_path):
                with open(h_path, "rb") as f:
                    disk.add_file(usr_include_cluster, h, f.read())

    copied_count = 0
    for name in binaries:
        bin_path = os.path.join(bin_dir, name)
        if os.path.isfile(bin_path):
            with open(bin_path, "rb") as f:
                data = f.read()
            disk.add_file(bin_cluster, name, data)
            copied_count += 1

    disk.write_fat_tables()
    with open(out_path, "wb") as f:
        f.write(disk.image)

    print(f"[make_disk] Successfully created {out_path} ({copied_count} binaries installed in /bin).")

if __name__ == "__main__":
    main()
