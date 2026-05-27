# pydantic-core/Makefile — pydantic_core → wasm32-wasip2 wheel.
#
# Lightest of the cross-builds: pure Rust + PyO3, no C deps. The recipe
# is essentially cryptography's flow with AWS-LC and the host cffi
# codegen stripped out. Maturin produces a wheel against the wasm
# CPython at $(CPYTHON_INCLUDE); a post-build `wheel tags` retag yields
# the cp314-abi3-wasi_wasm32 filename the index expects.
#
#   make toolchain   bootstrap maturin + a host venv with `wheel`
#   make wheel       cross-build the pydantic_core wheel
#   make all         toolchain → wheel
#   make clean       wipe .build/ and .toolchain/ (keeps .cache/)
#
# CPython prerequisite: this build reads $(CPYTHON_INCLUDE). Sibling
# ../cpython builds it; the wheel target depends on a `cpython` phony
# that recursively invokes `make -C ../cpython`.

THIS_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# ---- versions / sources --------------------------------------------------

PYDANTIC_CORE_VERSION   := 2.47.0
PYDANTIC_CORE_SDIST_URL := https://files.pythonhosted.org/packages/source/p/pydantic_core/pydantic_core-$(PYDANTIC_CORE_VERSION).tar.gz

MATURIN_VERSION := 1.9.4

# ---- paths ---------------------------------------------------------------

WASI_SDK_PATH       ?= /opt/wasi-sdk
WASI_SYSROOT        := $(WASI_SDK_PATH)/share/wasi-sysroot
WASI_SYSROOT_SHARED := $(WASI_SYSROOT)/lib/wasm32-wasip2

# Wasm CPython we cross-build against. See cryptography/Makefile for
# the full explanation — same convention applies here.
CPYTHON_DIR      := $(THIS_DIR)../cpython
CPYTHON_REV      := 0e13686da8bb881b059d35e23c32bcd2e6440099
CPYTHON_WASI_DIR := $(HOME)/cargo-target/cpython-$(CPYTHON_REV)/cpython/builddir/wasi
CPYTHON_INCLUDE  := $(CPYTHON_WASI_DIR)/install/include/python3.14

# Off-workspace WORKDIR for the same virtiofs/utime reason as the
# other component builds.
WORKDIR       ?= $(HOME)/.cache/particle/wheels-pydantic-core
TOOLCHAIN_DIR := $(WORKDIR)/toolchain
BUILD_DIR     := $(WORKDIR)/build
CACHE_DIR     := $(THIS_DIR).cache
OUT_DIR       := $(THIS_DIR)out

TOOLCHAIN_BIN := $(TOOLCHAIN_DIR)/bin
MATURIN       := $(TOOLCHAIN_BIN)/maturin

# Host CPython 3.14 venv — only needed for `python -m wheel tags`
# (the post-build retag pass). Different from the wasm CPython we
# cross-compile against.
HOST_PYVENV := $(TOOLCHAIN_DIR)/pyvenv
HOST_PYTHON := $(HOST_PYVENV)/bin/python

# Source tree (extracted from the sdist).
PYDANTIC_CORE_SRC     := $(BUILD_DIR)/src/pydantic_core-$(PYDANTIC_CORE_VERSION)
PYDANTIC_CORE_TARBALL := $(CACHE_DIR)/pydantic_core-$(PYDANTIC_CORE_VERSION).tar.gz

# Final output. Maturin's filename depends on pydantic-core's pyo3 abi3
# settings; the retag pass at the bottom forces the deterministic name.
WHEEL_OUT := $(OUT_DIR)/pydantic_core-$(PYDANTIC_CORE_VERSION)-cp314-abi3-wasi_wasm32.whl

# PyO3 cross-config. Identical to cryptography/pyo3-wasm-cpython314.txt
# by design; duplicated rather than symlinked so this subdir is
# self-contained per the same "lifts into its own repo" convention.
PYO3_CONFIG_FILE_ABS := $(THIS_DIR)pyo3-wasm-cpython314.txt

# ---- phony entry points --------------------------------------------------

.PHONY: all toolchain wheel clean check-host-deps cpython

all: wheel

check-host-deps:
	@command -v cargo >/dev/null || { echo "ERROR: cargo not on PATH"; exit 1; }
	@command -v curl  >/dev/null || { echo "ERROR: curl not on PATH";  exit 1; }
	@command -v tar   >/dev/null || { echo "ERROR: tar not on PATH";   exit 1; }
	@command -v uv    >/dev/null || { echo "ERROR: uv not on PATH (needed to provision the host 3.14 venv for the wheel-tags retag pass)"; exit 1; }
	@test -d $(WASI_SDK_PATH)    || { echo "ERROR: WASI_SDK_PATH=$(WASI_SDK_PATH) missing"; exit 1; }

cpython:
	$(MAKE) -C $(CPYTHON_DIR)

# ---- toolchain bootstrap -------------------------------------------------

toolchain: $(MATURIN) $(HOST_PYTHON)

$(HOST_PYTHON): | check-host-deps
	@mkdir -p $(TOOLCHAIN_DIR)
	uv venv --python 3.14 $(HOST_PYVENV)
	uv pip install --python $(HOST_PYTHON) wheel

$(MATURIN): | check-host-deps
	@mkdir -p $(TOOLCHAIN_DIR)
	cargo install --root $(TOOLCHAIN_DIR) --locked maturin@$(MATURIN_VERSION)

# ---- source fetch + extract ----------------------------------------------

$(PYDANTIC_CORE_TARBALL): | check-host-deps
	@mkdir -p $(CACHE_DIR)
	curl -fsSL -o $@.tmp $(PYDANTIC_CORE_SDIST_URL)
	mv $@.tmp $@

$(PYDANTIC_CORE_SRC)/pyproject.toml: $(PYDANTIC_CORE_TARBALL)
	@mkdir -p $(BUILD_DIR)/src
	rm -rf $(PYDANTIC_CORE_SRC)
	tar -xzf $(PYDANTIC_CORE_TARBALL) -C $(BUILD_DIR)/src
	touch $@

# ---- wheel build ---------------------------------------------------------

wheel: $(WHEEL_OUT)

$(WHEEL_OUT): $(PYDANTIC_CORE_SRC)/pyproject.toml $(MATURIN) $(HOST_PYTHON) $(PYO3_CONFIG_FILE_ABS) | cpython
	@mkdir -p $(OUT_DIR)
	cd $(PYDANTIC_CORE_SRC) && \
	  PATH=$(TOOLCHAIN_BIN):$$PATH \
	  PYO3_CONFIG_FILE=$(PYO3_CONFIG_FILE_ABS) \
	  PYO3_CROSS=1 \
	  PYO3_PYTHON=$(HOST_PYTHON) \
	  WASI_SYSROOT=$(WASI_SYSROOT) \
	  RUSTFLAGS='-C relocation-model=pic -C link-self-contained=no -C link-arg=-Wl,--no-entry -C link-arg=-Wl,--unresolved-symbols=import-dynamic -C link-arg=-Wl,--allow-undefined -C link-arg=-L$(WASI_SYSROOT_SHARED) -C link-arg=-Wl,-l:libc.so' \
	  CARGO_INCREMENTAL=0 \
	  CARGO_BUILD_TARGET=wasm32-wasip2 \
	  CC=$(WASI_SDK_PATH)/bin/clang \
	  AR=$(WASI_SDK_PATH)/bin/ar \
	  CC_wasm32_wasip2=$(WASI_SDK_PATH)/bin/clang \
	  AR_wasm32_wasip2=$(WASI_SDK_PATH)/bin/ar \
	  CFLAGS_wasm32_wasip2='--target=wasm32-wasip2 --sysroot=$(WASI_SYSROOT) -isystem $(CPYTHON_INCLUDE) -fPIC' \
	  CARGO_TARGET_WASM32_WASIP2_LINKER=$(WASI_SDK_PATH)/bin/clang \
	  $(MATURIN) build --release --target wasm32-wasip2 \
	    --out $(OUT_DIR)
	@# Maturin's emitted filename depends on pydantic-core's pyo3 abi3
	@# settings (could be cp37-abi3-any or cp314-abi3-any). Force the
	@# canonical cp314-abi3-wasi_wasm32 via `wheel tags`; --remove drops
	@# the maturin original so $(OUT_DIR) is left with a single wheel.
	$(HOST_PYTHON) -m wheel tags --remove \
	  --python-tag cp314 \
	  --abi-tag abi3 \
	  --platform-tag wasi_wasm32 \
	  $(OUT_DIR)/pydantic_core-$(PYDANTIC_CORE_VERSION)-*.whl
	@printf '✓  wheel: '; ls -lh $@ | awk '{print $$5"  "$$NF}'

# ---- clean ---------------------------------------------------------------

clean:
	rm -rf $(WORKDIR) $(OUT_DIR)
	@echo "kept: $(CACHE_DIR) (tarballs, keyed on upstream version)"
