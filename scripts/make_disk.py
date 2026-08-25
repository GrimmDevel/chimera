#!/usr/bin/env python3
"""
XIU Operating System — FAT32 Disk Image Generator & In-Place Binary Updater
scripts/make_disk.py
Generates a 64MB FAT32 disk image containing system directories,
configurations, and userland ELF binaries matching macOS/Darwin hierarchy.
"""

import os
import sys
import struct

DISK_SIZE = 512 * 1024 * 1024  # 512 MB
SECTOR_SIZE = 512
SECTORS_PER_CLUSTER = 8        # 4 KB cluster
RESERVED_SECTORS = 32
NUM_FATS = 2
SECTORS_PER_FAT = 2048
ROOT_CLUSTER = 2

TOTAL_SECTORS = DISK_SIZE // SECTOR_SIZE
DATA_START_SECTOR = RESERVED_SECTORS + NUM_FATS * SECTORS_PER_FAT
TOTAL_CLUSTERS = (TOTAL_SECTORS - DATA_START_SECTOR) // SECTORS_PER_CLUSTER


def make_lfn_entries(long_name: str, name_83: bytes, ext_83: bytes) -> list:
    short_11 = name_83 + ext_83
    chk = 0
    for b in short_11:
        chk = (((chk & 1) << 7) + (chk >> 1) + b) & 0xFF

    utf16_chars = [ord(c) for c in long_name] + [0]
    while len(utf16_chars) % 13 != 0:
        utf16_chars.append(0xFFFF)

    num_entries = len(utf16_chars) // 13
    entries = []
    for i in range(num_entries):
        seq = (num_entries - i)
        if i == 0:
            seq |= 0x40  # last logical entry
        chunk = utf16_chars[(num_entries - 1 - i) * 13 : (num_entries - i) * 13]

        entry = bytearray(32)
        entry[0] = seq
        for j in range(5):
            struct.pack_into("<H", entry, 1 + j * 2, chunk[j])
        entry[11] = 0x0F  # LFN attribute
        entry[12] = 0x00
        entry[13] = chk
        for j in range(6):
            struct.pack_into("<H", entry, 14 + j * 2, chunk[5 + j])
        struct.pack_into("<H", entry, 26, 0)
        for j in range(2):
            struct.pack_into("<H", entry, 28 + j * 2, chunk[11 + j])
        entries.append(bytes(entry))
    return entries


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
                c_off = self.cluster_offset(c)
                self.image[c_off : c_off + SECTORS_PER_CLUSTER * SECTOR_SIZE] = b"\x00" * (SECTORS_PER_CLUSTER * SECTOR_SIZE)
                return c
        for c in range(ROOT_CLUSTER + 1, self.next_free_cluster):
            if self.fat[c] == 0:
                self.fat[c] = 0x0FFFFFFF
                self.next_free_cluster = c + 1
                c_off = self.cluster_offset(c)
                self.image[c_off : c_off + SECTORS_PER_CLUSTER * SECTOR_SIZE] = b"\x00" * (SECTORS_PER_CLUSTER * SECTOR_SIZE)
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
        return name.encode("ascii", "replace"), ext.encode("ascii", "replace")

    def make_dir_entry(self, name_83: bytes, ext_83: bytes, attr: int, start_cluster: int, size: int) -> bytes:
        entry = bytearray(32)
        entry[0:8] = name_83
        entry[8:11] = ext_83
        entry[11] = attr
        struct.pack_into("<H", entry, 20, (start_cluster >> 16) & 0xFFFF)
        struct.pack_into("<H", entry, 26, start_cluster & 0xFFFF)
        struct.pack_into("<I", entry, 28, size)
        return bytes(entry)

    def _append_to_dir(self, dir_cluster: int, entry: bytes):
        cluster_size = SECTORS_PER_CLUSTER * SECTOR_SIZE
        cur = dir_cluster
        while True:
            offset = self.cluster_offset(cur)
            for i in range(0, cluster_size, 32):
                if self.image[offset + i] == 0x00 or self.image[offset + i] == 0xE5:
                    self.image[offset + i : offset + i + 32] = entry
                    return
            nxt = self.fat[cur]
            if nxt >= 0x0FFFFFF8 or nxt < 2:
                new_c = self.alloc_cluster()
                self.fat[cur] = new_c
                cur = new_c
            else:
                cur = nxt

    def add_directory(self, parent_cluster: int, dirname: str) -> int:
        dir_cluster = self.alloc_cluster()
        dir_offset = self.cluster_offset(dir_cluster)

        dot_entry = self.make_dir_entry(b".       ", b"   ", 0x10, dir_cluster, 0)
        dotdot_cluster = parent_cluster if parent_cluster != ROOT_CLUSTER else 0
        dotdot_entry = self.make_dir_entry(b"..      ", b"   ", 0x10, dotdot_cluster, 0)
        self.image[dir_offset : dir_offset + 32] = dot_entry
        self.image[dir_offset + 32 : dir_offset + 64] = dotdot_entry

        name_83, ext_83 = self.make_83_name(dirname)
        lfn_entries = make_lfn_entries(dirname, name_83, ext_83)
        for lfn in lfn_entries:
            self._append_to_dir(parent_cluster, lfn)
        entry = self.make_dir_entry(name_83, ext_83, 0x10, dir_cluster, 0)
        self._append_to_dir(parent_cluster, entry)
        return dir_cluster

    def add_file(self, parent_cluster: int, filename: str, data: bytes):
        start_cluster, size = self.write_file_data(data)
        name_83, ext_83 = self.make_83_name(filename)
        lfn_entries = make_lfn_entries(filename, name_83, ext_83)
        for lfn in lfn_entries:
            self._append_to_dir(parent_cluster, lfn)
        entry = self.make_dir_entry(name_83, ext_83, 0x20, start_cluster, size)
        self._append_to_dir(parent_cluster, entry)

    def load_existing(self, path: str):
        with open(path, "rb") as f:
            self.image = bytearray(f.read())
        fat_offset = RESERVED_SECTORS * SECTOR_SIZE
        for c in range(TOTAL_CLUSTERS + 2):
            if fat_offset + c * 4 + 4 <= len(self.image):
                val = struct.unpack_from("<I", self.image, fat_offset + c * 4)[0] & 0x0FFFFFFF
                self.fat[c] = val

    def update_binaries(self, bin_dir: str, binaries: list):
        dir_files = {}
        for name in binaries:
            bin_path = os.path.join(bin_dir, name)
            if not os.path.isfile(bin_path):
                continue
            with open(bin_path, "rb") as f:
                data = f.read()
            target_dir = get_target_dir_for_binary(name)
            if target_dir not in dir_files:
                dir_files[target_dir] = {}
            dir_files[target_dir][name] = data

        for target_dir, files in dir_files.items():
            self.update_dir_files(target_dir, files)

    def find_or_create_dir(self, path: str):
        parts = [p.strip() for p in path.strip("/").split("/") if p.strip()]
        cur_cluster = ROOT_CLUSTER
        for part in parts:
            cur_offset = self.cluster_offset(cur_cluster)
            name_83, ext_83 = self.make_83_name(part)
            target = name_83 + ext_83
            found = None
            for i in range(0, SECTORS_PER_CLUSTER * SECTOR_SIZE, 32):
                entry = self.image[cur_offset + i : cur_offset + i + 32]
                if entry[0] == 0x00: break
                if entry[0] == 0xE5: continue
                if entry[0:11] == target and (entry[11] & 0x10) and entry[11] != 0x0F:
                    ch = struct.unpack_from("<H", entry, 20)[0]
                    cl = struct.unpack_from("<H", entry, 26)[0]
                    found = (ch << 16) | cl
                    break
            if not found:
                found = self.add_directory(cur_cluster, part)
            cur_cluster = found
        return cur_cluster

    def update_dir_files(self, dir_path: str, files_dict: dict):
        dir_cluster = self.find_or_create_dir(dir_path)
        if not dir_cluster:
            return

        dir_offset = self.cluster_offset(dir_cluster)
        updated = 0
        for name, data in files_dict.items():
            name_83, ext_83 = self.make_83_name(name)
            target_key = name_83 + ext_83

            found_slot = None
            for i in range(0, SECTORS_PER_CLUSTER * SECTOR_SIZE, 32):
                entry = self.image[dir_offset + i : dir_offset + i + 32]
                if entry[0] == 0x00:
                    if found_slot is None: found_slot = dir_offset + i
                    break
                if entry[0] == 0xE5:
                    if found_slot is None: found_slot = dir_offset + i
                    continue
                if entry[0:11] == target_key and entry[11] != 0x0F:
                    ch = struct.unpack_from("<H", entry, 20)[0]
                    cl = struct.unpack_from("<H", entry, 26)[0]
                    old_cluster = (ch << 16) | cl
                    cur = old_cluster
                    while cur >= 2 and cur < 0x0FFFFFF8:
                        nxt = self.fat[cur]
                        self.fat[cur] = 0
                        cur = nxt
                    found_slot = dir_offset + i
                    break

            start_cluster, size = self.write_file_data(data)
            entry = self.make_dir_entry(name_83, ext_83, 0x20, start_cluster, size)
            if found_slot is not None:
                self.image[found_slot : found_slot + 32] = entry
                updated += 1
            else:
                self.add_file(dir_cluster, name, data)
                updated += 1

        self.write_fat_tables()
        if updated > 0:
            print(f"[make_disk] Updated {updated} files in /{dir_path} on persistent disk.")


def copy_include_tree(disk, base_path, src_dir):
    for root, dirs, files in os.walk(src_dir):
        rel = os.path.relpath(root, src_dir)
        if rel == ".":
            target_dir = base_path
        else:
            target_dir = f"{base_path}/{rel}"
        files_dict = {}
        for f in files:
            if f.endswith(".h") or f.endswith(".def") or f.endswith(".inc"):
                fpath = os.path.join(root, f)
                with open(fpath, "rb") as fp:
                    files_dict[f] = fp.read()
        if files_dict:
            disk.update_dir_files(target_dir, files_dict)


BIN_MAP = {
    # /bin (Root & POSIX essentials)
    "zsh": "bin",
    "sh": "bin",
    "stty": "bin",
    "dash": "bin",
    "ls": "bin",
    "cat": "bin",
    "cp": "bin",
    "mv": "bin",
    "rm": "bin",
    "mkdir": "bin",
    "pwd": "bin",
    "date": "bin",
    "sleep": "bin",
    "kill": "bin",
    "chmod": "bin",
    "df": "bin",
    "echo": "bin",
    "clear": "bin",
    "true": "bin",
    "false": "bin",

    # /sbin (Admin & Network essentials)
    "ifconfig": "sbin",
    "ping": "sbin",

    # /usr/sbin (Daemons / System servers)
    "wserver": "usr/sbin",
    "WindowServer": "System/Library/CoreServices",
    "SystemUIServer": "System/Library/CoreServices",
    "Dock": "System/Library/CoreServices",
    "Filer": "System/Library/CoreServices",

    # /usr/bin (Default for all other tools, compilers, apps)
    "login": "usr/bin",
    "passwd": "usr/bin",
    "su": "usr/bin",
    "id": "usr/bin",
    "whoami": "usr/bin",
    "fastfetch": "usr/bin",
    "flashfetch": "usr/bin",
}


def get_target_dir_for_binary(name: str) -> str:
    return BIN_MAP.get(name, "usr/bin")


def discover_binaries(bin_dir: str) -> list:
    discovered = []
    if not os.path.isdir(bin_dir):
        return discovered
    for item in os.listdir(bin_dir):
        full_path = os.path.join(bin_dir, item)
        if os.path.isfile(full_path) and not item.endswith((".a", ".o", ".obj", ".txt", ".cmake", ".json", ".ninja")):
            try:
                with open(full_path, "rb") as f:
                    magic = f.read(4)
                if magic in (b"\xcf\xfa\xed\xfe", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xfe\xed\xfa\xce", b"\x7fELF"):
                    discovered.append(item)
            except Exception:
                pass
    return sorted(discovered)


def install_system_files(disk):
    def read_etc_file(name, default):
        p = os.path.join("etc", name)
        if os.path.isfile(p):
            with open(p, "rb") as f:
                return f.read()
        return default

    etc_files = {
        "motd": b"Welcome to XIU Operating System!\nHybrid Mach/BSD Architecture (Darwin/XNU compatible).\n",
        "version": b"XIU OS v1.0.0 (x86_64)\n",
        "hosts": b"127.0.0.1\tlocalhost\n10.0.2.15\tMac\n",
        "resolv.conf": b"nameserver 10.0.2.3\n",
        "passwd": read_etc_file("passwd", b"root:*:0:0:System Administrator:/Users/root:/bin/zsh\nfvr:*:501:20:fvr:/Users/fvr:/bin/zsh\nuser:*:502:20:Default User:/Users/user:/bin/zsh\n"),
        "master.passwd": read_etc_file("master.passwd", b"root::0:0::0:0:System Administrator:/Users/root:/bin/zsh\nfvr::501:20::0:0:fvr:/Users/fvr:/bin/zsh\nuser::502:20::0:0:Default User:/Users/user:/bin/zsh\n"),
        "group": read_etc_file("group", b"wheel:*:0:root\nadmin:*:80:root,fvr,user\nstaff:*:20:fvr,user\n"),
        "shells": read_etc_file("shells", b"/bin/zsh\n/bin/sh\n"),
        "adduser.conf": read_etc_file("adduser.conf", b""),
        "sudoers": b"root\tALL=(ALL) ALL\n%admin\tALL=(ALL) ALL\n%wheel\tALL=(ALL) ALL\n",
        "sudo.conf": b"# sudo.conf\n",
        "zshenv": b'export PATH="/bin:/usr/bin:/sbin:/usr/sbin:/usr/local/bin"\n',
        "zprofile": b'export PATH="/bin:/usr/bin:/sbin:/usr/sbin:/usr/local/bin"\nexport TERM="xterm-256color"\nexport PROMPT="%n@%m %~ %# "\nexport PS1="%n@%m %~ %# "\n',
        "zshrc": b'export PATH="/bin:/usr/bin:/sbin:/usr/sbin:/usr/local/bin"\nexport TERM="xterm-256color"\nexport PROMPT="%n@%m %~ %# "\nexport PS1="%n@%m %~ %# "\nunsetopt zle\nunsetopt promptcr\nunsetopt promptsp\n',
        "zlogin": b'',

        "termcap": b"xterm-256color|xterm with 256 colors:\\\n\t:am:xn:hs:co#80:li#25:colors#256:\\\n\t:cl=\\E[2J\\E[H:cd=\\E[J:ce=\\E[K:cm=\\E[%i%d;%dH:\\\n\t:up=\\E[A:do=\\E[B:le=\\E[D:nd=\\E[C:\\\n\t:so=\\E[7m:se=\\E[27m:us=\\E[4m:ue=\\E[24m:\\\n\t:md=\\E[1m:me=\\E[0m:AF=\\E[3%dm:AB=\\E[4%dm:\\\n\t:kb=\\177:kd=\\E[B:kl=\\E[D:kr=\\E[C:ku=\\E[A:\n",
    }
    disk.update_dir_files("private/etc", etc_files)

    sysversion_content = b'''<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>BuildID</key>
	<string>7B930DF0-XIU-2026</string>
	<key>ProductBuildVersion</key>
	<string>24A348</string>
	<key>ProductCopyright</key>
	<string>2026 XIU Project</string>
	<key>ProductName</key>
	<string>XIU OS</string>
	<key>ProductUserVisibleVersion</key>
	<string>1.0</string>
	<key>ProductVersion</key>
	<string>1.0.0</string>
</dict>
</plist>
'''
    serverversion_content = b'''<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>ProductName</key>
	<string>XIU Server</string>
	<key>ProductVersion</key>
	<string>1.0.0</string>
	<key>ProductBuildVersion</key>
	<string>24A348</string>
</dict>
</plist>
'''
    coreservices_files = {
        "SystemVersion.plist": sysversion_content,
        "ServerVersion.plist": serverversion_content,
    }
    disk.update_dir_files("System/Library/CoreServices", coreservices_files)

    pref_files = {
        "com.xiu.system.plist": b'''<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>Languages</key>
	<array>
		<string>en-US</string>
		<string>ru-RU</string>
	</array>
	<key>Locale</key>
	<string>en_US</string>
	<key>Country</key>
	<string>US</string>
</dict>
</plist>
'''
    }
    disk.update_dir_files("Library/Preferences", pref_files)

    calc_app_files = {
        "Info.plist": b'''<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleExecutable</key>
	<string>calc</string>
	<key>CFBundleIdentifier</key>
	<string>org.xiu.calculator</string>
	<key>CFBundleName</key>
	<string>Calculator</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleShortVersionString</key>
	<string>1.0</string>
</dict>
</plist>
'''
    }
    disk.update_dir_files("Applications/Calculator.app/Contents", calc_app_files)

    term_app_files = {
        "Info.plist": b'''<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleExecutable</key>
	<string>zsh</string>
	<key>CFBundleIdentifier</key>
	<string>org.xiu.terminal</string>
	<key>CFBundleName</key>
	<string>Terminal</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleShortVersionString</key>
	<string>1.0</string>
</dict>
</plist>
'''
    }
    disk.update_dir_files("Applications/Terminal.app/Contents", term_app_files)

    root_files = {
        ".zshenv": b"",
        ".zprofile": b"",
        ".zlogin": b"",
        ".zshrc": b'''# ~/.zshrc for root
export HOME=/Users/root
export USER=root
export LOGNAME=root
export PATH=/bin:/usr/bin:/sbin:/usr/sbin:/usr/local/bin
export TERM=xterm-256color
export PROMPT='%n@%m %~ %# '
unset RPROMPT
unsetopt zle
unsetopt promptcr
unsetopt promptsp
alias ls='ls'
alias ll='ls -la'
alias la='ls -a'
alias clear='clear'
alias zsh='/bin/zsh'
alias sh='/bin/sh'
''',
        ".profile": b'export HOME=/Users/root\nexport USER=root\nexport LOGNAME=root\nexport PATH=/bin:/usr/bin:/sbin:/usr/sbin:/usr/local/bin\nexport TERM=xterm-256color\nexport PROMPT="%n@%m %~ %# "\n',
        "hello.c": b'#include <stdio.h>\n\nint main() {\n    printf("Hello from self-hosted XIU C compiler!\\n");\n    return 0;\n}\n'
    }
    disk.update_dir_files("Users/root", root_files)

    fvr_files = {
        ".zshenv": b"",
        ".zprofile": b"",
        ".zlogin": b"",
        ".zshrc": b'''# ~/.zshrc for fvr
export HOME=/Users/fvr
export USER=fvr
export LOGNAME=fvr
export PATH=/bin:/usr/bin:/sbin:/usr/sbin:/usr/local/bin
export TERM=xterm-256color
export PROMPT='%n@%m %~ %# '
unset RPROMPT
unsetopt zle
unsetopt promptcr
unsetopt promptsp
alias ls='ls'
alias ll='ls -la'
alias la='ls -a'
alias clear='clear'
alias zsh='/bin/zsh'
alias sh='/bin/sh'
''',
        ".profile": b'export HOME=/Users/fvr\nexport USER=fvr\nexport LOGNAME=fvr\nexport PATH=/bin:/usr/bin:/sbin:/usr/sbin:/usr/local/bin\nexport TERM=xterm-256color\nexport PROMPT="%n@%m %~ %# "\n',
        "hello.c": b'#include <stdio.h>\n\nint main() {\n    printf("Hello from self-hosted XIU C compiler!\\n");\n    return 0;\n}\n'
    }
    disk.update_dir_files("Users/fvr", fvr_files)

    user_files = {
        ".zshenv": b"",
        ".zprofile": b"",
        ".zlogin": b"",
        ".zshrc": b'''# ~/.zshrc for user
export HOME=/Users/user
export USER=user
export LOGNAME=user
export PATH=/bin:/usr/bin:/sbin:/usr/sbin:/usr/local/bin
export TERM=xterm-256color
export PROMPT='%n@%m %~ %# '
unset RPROMPT
unsetopt zle
unsetopt promptcr
unsetopt promptsp
alias ls='ls'
alias ll='ls -la'
alias la='ls -a'
alias clear='clear'
alias zsh='/bin/zsh'
alias sh='/bin/sh'
''',
        ".profile": b'export HOME=/Users/user\nexport USER=user\nexport LOGNAME=user\nexport PATH=/bin:/usr/bin:/sbin:/usr/sbin:/usr/local/bin\nexport TERM=xterm-256color\nexport PROMPT="%n@%m %~ %# "\n',
    }
    disk.update_dir_files("Users/user", user_files)
    disk.update_dir_files("Users/Shared", {"welcome.txt": b"Hello from persistent user storage on disk0!\n"})


def copy_ravynos_assets(disk, workspace):
    ravynos_dir = os.path.join(workspace, "ravynos")
    if not os.path.isdir(ravynos_dir):
        return

    # 1. SystemLibrary/LaunchAgents
    agents_dir = os.path.join(ravynos_dir, "SystemLibrary", "LaunchAgents")
    if os.path.isdir(agents_dir):
        files_dict = {}
        for f in os.listdir(agents_dir):
            if f.endswith(".json") or f.endswith(".plist"):
                with open(os.path.join(agents_dir, f), "rb") as fp:
                    files_dict[f] = fp.read()
        if files_dict:
            disk.update_dir_files("System/Library/LaunchAgents", files_dict)

    # 2. SystemLibrary/LaunchDaemons
    daemons_dir = os.path.join(ravynos_dir, "SystemLibrary", "LaunchDaemons")
    if os.path.isdir(daemons_dir):
        files_dict = {}
        for f in os.listdir(daemons_dir):
            if f.endswith(".json") or f.endswith(".plist"):
                with open(os.path.join(daemons_dir, f), "rb") as fp:
                    files_dict[f] = fp.read()
        if files_dict:
            disk.update_dir_files("System/Library/LaunchDaemons", files_dict)

    # 3. Desktop Pictures
    pics_dir = os.path.join(ravynos_dir, "SystemLibrary", "Desktop_Pictures")
    if os.path.isdir(pics_dir):
        files_dict = {}
        for f in os.listdir(pics_dir):
            p = os.path.join(pics_dir, f)
            if os.path.isfile(p) and f.endswith((".png", ".jpg", ".JPG", ".jpeg", ".json")) and os.path.getsize(p) < 24 * 1024 * 1024:
                with open(p, "rb") as fp:
                    files_dict[f] = fp.read()
        if files_dict:
            disk.update_dir_files("System/Library/Desktop Pictures", files_dict)

    # 4. Fonts
    fonts_dir = os.path.join(ravynos_dir, "SystemLibrary", "Fonts", "TTF")
    if os.path.isdir(fonts_dir):
        files_dict = {}
        for f in os.listdir(fonts_dir):
            p = os.path.join(fonts_dir, f)
            if os.path.isfile(p) and f.endswith((".ttf", ".otf")) and os.path.getsize(p) < 500 * 1024:
                with open(p, "rb") as fp:
                    files_dict[f] = fp.read()
        if files_dict:
            disk.update_dir_files("System/Library/Fonts", files_dict)
            disk.update_dir_files("Library/Fonts", files_dict)

    # 5. WindowServer & CoreServices Resources
    ws_dir = os.path.join(ravynos_dir, "CoreServices", "WindowServer")
    if os.path.isdir(ws_dir):
        files_dict = {}
        for f in ["Icon.png", "Info.plist"]:
            p = os.path.join(ws_dir, f)
            if os.path.isfile(p):
                with open(p, "rb") as fp:
                    files_dict[f] = fp.read()
        if files_dict:
            disk.update_dir_files("System/Library/CoreServices/WindowServer.app/Contents/Resources", files_dict)

        c_dir = os.path.join(ws_dir, "Cursors")
        if os.path.isdir(c_dir):
            c_dict = {}
            for f in os.listdir(c_dir):
                if f.endswith(".png"):
                    with open(os.path.join(c_dir, f), "rb") as fp:
                        c_dict[f] = fp.read()
            if c_dict:
                disk.update_dir_files("System/Library/CoreServices/WindowServer.app/Contents/Resources/Cursors", c_dict)

        d_dir = os.path.join(ws_dir, "Decorations")
        if os.path.isdir(d_dir):
            d_dict = {}
            for f in os.listdir(d_dir):
                if f.endswith(".png"):
                    with open(os.path.join(d_dir, f), "rb") as fp:
                        d_dict[f] = fp.read()
            if d_dict:
                disk.update_dir_files("System/Library/CoreServices/WindowServer.app/Contents/Resources/Decorations", d_dict)

    # 6. SystemUIServer Resources
    sysui_dir = os.path.join(ravynos_dir, "CoreServices", "WindowServer", "SystemUIServer")
    if os.path.isdir(sysui_dir):
        files_dict = {}
        for f in ["Info.plist", "ravynos-mark-64.png", "NSMenuArrow.tiff", "NSMenuViewDoubleRightArrow.tiff"]:
            p = os.path.join(sysui_dir, f)
            if os.path.isfile(p):
                with open(p, "rb") as fp:
                    files_dict[f] = fp.read()
        if files_dict:
            disk.update_dir_files("System/Library/CoreServices/SystemUIServer.app/Contents/Resources", files_dict)

    # 7. Dock Resources
    dock_dir = os.path.join(ravynos_dir, "CoreServices", "Dock")
    if os.path.isdir(dock_dir):
        files_dict = {}
        for f in ["Info.plist", "Dock.png", "running.png", "window.png"]:
            p = os.path.join(dock_dir, f)
            if os.path.isfile(p):
                with open(p, "rb") as fp:
                    files_dict[f] = fp.read()
        if files_dict:
            disk.update_dir_files("System/Library/CoreServices/Dock.app/Contents/Resources", files_dict)

    # 8. Filer Resources
    filer_dir = os.path.join(ravynos_dir, "CoreServices", "Filer")
    if os.path.isdir(filer_dir):
        files_dict = {}
        for f in ["filer.png"]:
            p = os.path.join(filer_dir, f)
            if os.path.isfile(p):
                with open(p, "rb") as fp:
                    files_dict[f] = fp.read()
        if files_dict:
            disk.update_dir_files("System/Library/CoreServices/Filer.app/Contents/Resources", files_dict)


def main():
    workspace = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    out_dir = os.path.join(workspace, "build")
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, "disk.img")
    bin_dir = os.path.join(workspace, "build", "x86_64", "usr")
    binaries = discover_binaries(bin_dir)

    print(f"[make_disk] Generating 512MB FAT32 disk image: {out_path}")
    disk = FAT32Disk()
    disk.write_bpb()


    # macOS / Darwin Canonical Hierarchy
    disk.find_or_create_dir("Applications")

    disk.find_or_create_dir("Library")
    disk.find_or_create_dir("Library/Preferences")
    disk.find_or_create_dir("Library/Application Support")
    disk.find_or_create_dir("Library/Frameworks")
    disk.find_or_create_dir("Library/Fonts")
    disk.find_or_create_dir("Library/Audio")
    disk.find_or_create_dir("Library/Logs")

    disk.find_or_create_dir("System")
    disk.find_or_create_dir("System/Library")
    coreservices_cluster = disk.find_or_create_dir("System/Library/CoreServices")
    disk.find_or_create_dir("System/Library/Frameworks")
    disk.find_or_create_dir("System/Library/Fonts")
    disk.find_or_create_dir("System/DriverKit")
    disk.find_or_create_dir("System/Volumes")

    disk.find_or_create_dir("Users")
    shared_cluster = disk.find_or_create_dir("Users/Shared")
    root_home_cluster = disk.find_or_create_dir("Users/root")

    for u in ["root", "fvr", "user"]:
        disk.find_or_create_dir(f"Users/{u}")
        disk.find_or_create_dir(f"Users/{u}/Desktop")
        disk.find_or_create_dir(f"Users/{u}/Documents")
        disk.find_or_create_dir(f"Users/{u}/Downloads")
        disk.find_or_create_dir(f"Users/{u}/Library")
        disk.find_or_create_dir(f"Users/{u}/Library/Preferences")
        disk.find_or_create_dir(f"Users/{u}/Pictures")
        disk.find_or_create_dir(f"Users/{u}/Public")

    disk.find_or_create_dir("Volumes")
    disk.find_or_create_dir("Volumes/Macintosh HD")

    disk.find_or_create_dir("Network")
    bin_cluster = disk.find_or_create_dir("bin")
    disk.find_or_create_dir("sbin")

    disk.find_or_create_dir("usr")
    usr_bin_cluster = disk.find_or_create_dir("usr/bin")
    disk.find_or_create_dir("usr/sbin")
    usr_lib_cluster = disk.find_or_create_dir("usr/lib")
    usr_include_cluster = disk.find_or_create_dir("usr/include")
    disk.find_or_create_dir("usr/share")
    disk.find_or_create_dir("usr/local")
    disk.find_or_create_dir("usr/local/bin")
    disk.find_or_create_dir("usr/local/lib")
    disk.find_or_create_dir("usr/local/include")

    disk.find_or_create_dir("private")
    etc_cluster = disk.find_or_create_dir("private/etc")
    disk.find_or_create_dir("private/var")
    disk.find_or_create_dir("private/var/log")
    disk.find_or_create_dir("private/var/run")
    disk.find_or_create_dir("private/var/tmp")
    disk.find_or_create_dir("private/var/db")
    disk.find_or_create_dir("private/var/root")
    disk.find_or_create_dir("private/tmp")
    disk.find_or_create_dir("tmp")
    disk.find_or_create_dir("var")
    disk.find_or_create_dir("var/tmp")

    disk.find_or_create_dir("cores")
    disk.find_or_create_dir("opt")

    motd_content = b"Welcome to XIU Operating System!\nApple Darwin / Mach-BSD Hybrid Architecture.\n"
    disk.add_file(etc_cluster, "motd", motd_content)
    version_content = b"XIU OS v0.1.0 (Darwin 24.0.0 x86_64)\n"
    disk.add_file(etc_cluster, "version", version_content)
    hosts_content = b"127.0.0.1\tlocalhost\n10.0.2.15\txiu-mac\n"
    disk.add_file(etc_cluster, "hosts", hosts_content)

    sysversion_content = b'<?xml version="1.0" encoding="UTF-8"?>\n<dict>\n  <key>ProductBuildVersion</key>\n  <string>24A348</string>\n  <key>ProductName</key>\n  <string>XIU OS</string>\n  <key>ProductVersion</key>\n  <string>15.0</string>\n</dict>\n'
    disk.add_file(coreservices_cluster, "SysVer.plist", sysversion_content)
    disk.add_file(coreservices_cluster, "SystemVersion.plist", sysversion_content)

    welcome_content = b"Hello from persistent user storage on disk0!\n"
    disk.add_file(shared_cluster, "welcome.txt", welcome_content)
    hello_content = b'#include <stdio.h>\n\nint main() {\n    printf("Hello from self-hosted XIU C compiler!\\n");\n    return 0;\n}\n'
    disk.add_file(shared_cluster, "hello.c", hello_content)
    disk.add_file(root_home_cluster, "hello.c", hello_content)

    # Copy Mach-O runtime objects and libraries into /usr/lib
    crt0_path = os.path.join(bin_dir, "CMakeFiles", "xiu_crt0.dir", "libsystem", "crt0.S.obj")
    libsystem_path = os.path.join(bin_dir, "libsystem_xiu.a")
    if os.path.isfile(crt0_path):
        with open(crt0_path, "rb") as f:
            crt0_data = f.read()
        disk.add_file(usr_lib_cluster, "crt0.o", crt0_data)
    if os.path.isfile(libsystem_path):
        with open(libsystem_path, "rb") as f:
            libc_data = f.read()
        disk.add_file(usr_lib_cluster, "libc.a", libc_data)
        disk.add_file(usr_lib_cluster, "libsystem_xiu.a", libc_data)

    # Copy C header trees into /usr/include
    copy_include_tree(disk, "usr/include", os.path.join(workspace, "usr", "libsystem", "include"))
    copy_include_tree(disk, "usr/include", os.path.join(workspace, "tinycc", "include"))
    copy_include_tree(disk, "usr/include/kernel", os.path.join(workspace, "kernel", "include", "kernel"))
    copy_include_tree(disk, "usr/include/net", os.path.join(workspace, "kernel", "include", "net"))

    copied_count = 0
    for name in binaries:
        bin_path = os.path.join(bin_dir, name)
        if os.path.isfile(bin_path):
            with open(bin_path, "rb") as f:
                data = f.read()
            target_dir = get_target_dir_for_binary(name)
            target_cluster = disk.find_or_create_dir(target_dir)
            disk.add_file(target_cluster, name, data)
            copied_count += 1

    # Install system plists and configs
    install_system_files(disk)

    # Copy missing desktop assets, fonts, wallpapers, and service configs from /ravynos
    copy_ravynos_assets(disk, workspace)

    disk.write_fat_tables()
    with open(out_path, "wb") as f:
        f.write(disk.image)


    print(f"[make_disk] Successfully created {out_path} ({copied_count} binaries installed in Darwin hierarchy).")


if __name__ == "__main__":
    main()
