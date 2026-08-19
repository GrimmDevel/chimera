# =============================================================================
# XIU Operating System — Root Makefile (CMake Wrapper)
# =============================================================================

# Ensure Homebrew LLVM is first in PATH for cross-compilation tools
export PATH := /opt/homebrew/opt/llvm/bin:/usr/local/opt/llvm/bin:$(PATH)

# Default architecture (x86_64 or arm64)
ARCH ?= x86_64
BUILD_TYPE ?= RelWithDebInfo
BUILD_DIR = build/$(ARCH)
VERBOSE ?= 0

ifeq ($(VERBOSE),1)
CMAKE_VERBOSE_FLAG = -DXIU_VERBOSE=ON
else
CMAKE_VERBOSE_FLAG = -DXIU_VERBOSE=OFF
endif

# CMake toolchain file mapping
TOOLCHAIN = cmake/toolchain-$(ARCH).cmake

.PHONY: all build clean qemu help debug debug-iso iso disk recreate-disk

all: build

# ── Build Target ─────────────────────────────────────────────────────────────
# Configures and builds the kernel using CMake
build:
	@echo "[XIU] Building for $(ARCH) ($(BUILD_TYPE), VERBOSE=$(VERBOSE))..."
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && cmake -DCMAKE_TOOLCHAIN_FILE=../../$(TOOLCHAIN) \
		-DCMAKE_BUILD_TYPE=$(BUILD_TYPE) $(CMAKE_VERBOSE_FLAG) ../..
	@cmake --build $(BUILD_DIR) --parallel $$(sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 1)
	@echo "[XIU] Build complete: $(BUILD_DIR)/kernel/xiu_kernel.elf"

# ── Clean Target ─────────────────────────────────────────────────────────────
clean:
	@echo "[XIU] Cleaning $(BUILD_DIR)..."
	@rm -rf $(BUILD_DIR)

# ── Disk Target ─────────────────────────────────────────────────────────────
# Generates or preserves the FAT32 hard disk image
disk: build
	@python3 scripts/make_disk.py

# Recreate fresh FAT32 disk image from scratch (wipes user data)
recreate-disk: build
	@python3 scripts/make_disk.py --force

# ── QEMU Target ──────────────────────────────────────────────────────────────
# Boots the kernel in QEMU
qemu: iso disk
	@./scripts/run_qemu.sh $(ARCH)

run: qemu

debug:
	@$(MAKE) build BUILD_TYPE=Debug VERBOSE=1
	@$(MAKE) disk
	@./scripts/run_qemu.sh $(ARCH) 1

debug-iso:
	@$(MAKE) build BUILD_TYPE=Debug VERBOSE=1
	@$(MAKE) iso

# ── ISO Target ──────────────────────────────────────────────────────────────
# Packages the kernel into a bootable ISO using Limine
iso: build
	@./scripts/make_iso.sh $(ARCH)

# ── Help Target ──────────────────────────────────────────────────────────────
help:
	@echo "XIU OS Build System"
	@echo "Usage:"
	@echo "  make build          Build the kernel (default ARCH=x86_64)"
	@echo "  make ARCH=arm64     Build for ARM64"
	@echo "  make qemu           Build and run in QEMU"
	@echo "  make clean          Remove build artifacts"
