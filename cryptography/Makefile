# wheels/cryptography — pyca/cryptography 48.0.0 → wasm32-wasip2 wheel.
#
# Self-contained: every input lives under THIS_DIR (tarballs, source
# trees, toolchain binaries, build artifacts, the final wheel). The
# only outside input is the wasm32-wasip2 CPython 3.14 the repo root
# builds at $(CPYTHON_WASI_DIR) below; replace that with a downloadable
# tarball when moving this folder to its own repo.
#
#   make toolchain   bootstrap maturin + cmake into .toolchain/
#   make aws-lc      cross-build AWS-LC static libs for wasm32-wasip2
#   make wheel       build cryptography-48.0.0-*.whl
#   make all         toolchain → aws-lc → wheel
#   make clean       wipe .build/ and .toolchain/ (keeps .cache/)
#
# CPython prerequisite: this build links against
# ../cpython/builddir/wasi/libpython3.14.so and reads its headers.
# Sibling `../cpython` builds them; the wheel target depends on a
# `cpython` phony that recursively invokes `make -C ../cpython`.

THIS_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# ---- versions / sources --------------------------------------------------

CRYPTOGRAPHY_VERSION := 48.0.0
CRYPTOGRAPHY_SDIST_URL := https://files.pythonhosted.org/packages/9f/a9/db8f313fdcd85d767d4973515e1db101f9c71f95fced83233de224673757/cryptography-$(CRYPTOGRAPHY_VERSION).tar.gz

# AWS-LC release with wasm32-wasi support. Track CI's pinned ref so
# we're using something they themselves test against.
AWS_LC_REV := v1.73.0
AWS_LC_TARBALL_URL := https://github.com/aws/aws-lc/archive/refs/tags/$(AWS_LC_REV).tar.gz

# maturin and cmake versions we install into .toolchain/.
MATURIN_VERSION := 1.9.4
CMAKE_VERSION   := 3.30.5
# bindgen-cli is invoked by openssl-sys's build.rs to generate Rust FFI
# bindings from AWS-LC's headers (the AWS-LC code path needs it; the
# stock OpenSSL path doesn't).
BINDGEN_VERSION := 0.71.1

# ---- paths ---------------------------------------------------------------

WASI_SDK_PATH   ?= /opt/wasi-sdk
WASI_SYSROOT    := $(WASI_SDK_PATH)/share/wasi-sysroot
# Shared-libc sysroot — libc.so + crt1.o etc. live here. We point the
# wheel link at this instead of the static libc.a so cargo's cdylib
# imports malloc/free/realloc from env (resolved to libc.so at
# composition time) rather than statically linking wasi-libc-static's
# dlmalloc, which would give the wheel its own private heap and
# corrupt libpython's pymalloc-allocated structs.
WASI_SYSROOT_SHARED := $(WASI_SYSROOT)/lib/wasm32-wasip2

# The wasm CPython we cross-build against. Produced by sibling
# ../cpython — see the `cpython` phony below for the recursive
# `make -C ../cpython` that builds it on demand. The output path is
# the shared $(HOME)/cargo-target/cpython-$REV/... convention; if
# the repo root's `make python-lib` has already populated it,
# nothing extra runs.
CPYTHON_DIR       := $(THIS_DIR)../cpython
CPYTHON_REV       := 0e13686da8bb881b059d35e23c32bcd2e6440099
CPYTHON_WASI_DIR  := $(HOME)/cargo-target/cpython-$(CPYTHON_REV)/cpython/builddir/wasi
CPYTHON_INCLUDE   := $(CPYTHON_WASI_DIR)/install/include/python3.14
CPYTHON_LIB       := $(CPYTHON_WASI_DIR)/libpython3.14.so

# The build + toolchain trees include tarball extracts and symlinks
# that `tar` populates with utime() calls. virtiofs (this dev
# container's workspace mount) blocks utime, so those dirs live under
# $HOME. Cache (curl downloads) and out (the final wheel) stay
# in-workspace where the user can see them.
WORKDIR       ?= $(HOME)/.cache/particle/wheels-cryptography
TOOLCHAIN_DIR := $(WORKDIR)/toolchain
BUILD_DIR     := $(WORKDIR)/build
CACHE_DIR     := $(THIS_DIR).cache
OUT_DIR       := $(THIS_DIR)out

# Tooling we install locally. PATH-injection happens once in the
# recipes — having explicit absolute paths avoids surprises from
# shell PATH inheritance.
TOOLCHAIN_BIN := $(TOOLCHAIN_DIR)/bin
MATURIN       := $(TOOLCHAIN_BIN)/maturin
CMAKE         := $(TOOLCHAIN_BIN)/cmake
BINDGEN       := $(TOOLCHAIN_BIN)/bindgen

# Host CPython 3.14 (uv-managed) plus cffi + setuptools — needed to run
# cryptography-cffi's build.rs (it invokes _cffi_src/build_openssl.py
# to generate the cffi shim's C source). Different from the wasm
# CPython we cross-compile *against* — that lives at CPYTHON_WASI_DIR.
HOST_PYVENV     := $(TOOLCHAIN_DIR)/pyvenv
HOST_PYTHON     := $(HOST_PYVENV)/bin/python

# Source trees (extracted from tarballs into .build/src).
CRYPTOGRAPHY_SRC := $(BUILD_DIR)/src/cryptography-$(CRYPTOGRAPHY_VERSION)
AWS_LC_SRC       := $(BUILD_DIR)/src/aws-lc-$(AWS_LC_REV)

# Cached tarballs (survive `make clean`; keyed by upstream version).
CRYPTOGRAPHY_TARBALL := $(CACHE_DIR)/cryptography-$(CRYPTOGRAPHY_VERSION).tar.gz
AWS_LC_TARBALL       := $(CACHE_DIR)/aws-lc-$(AWS_LC_REV).tar.gz

# AWS-LC build dir + artifacts the cryptography crate will link against.
AWS_LC_BUILD     := $(BUILD_DIR)/aws-lc-build
AWS_LC_PREFIX    := $(BUILD_DIR)/aws-lc-install
AWS_LC_LIBCRYPTO := $(AWS_LC_PREFIX)/lib/libcrypto.a
AWS_LC_LIBSSL    := $(AWS_LC_PREFIX)/lib/libssl.a

# Final output. Maturin's own filename is cp314-abi3-any; we retag it
# to wasi_wasm32 after the build (see the recipe for $(WHEEL_OUT)) so
# pip on a non-wasi host correctly ignores this wheel as incompatible.
WHEEL_OUT := $(OUT_DIR)/cryptography-$(CRYPTOGRAPHY_VERSION)-cp314-abi3-wasi_wasm32.whl

# Detect host arch for the cmake tarball — kitware ships per-arch.
HOST_ARCH := $(shell uname -m)
ifeq ($(HOST_ARCH),x86_64)
  CMAKE_TARBALL_ARCH := linux-x86_64
else ifeq ($(HOST_ARCH),aarch64)
  CMAKE_TARBALL_ARCH := linux-aarch64
else ifeq ($(HOST_ARCH),arm64)
  CMAKE_TARBALL_ARCH := linux-aarch64
else
  $(error unsupported host arch: $(HOST_ARCH))
endif
CMAKE_TARBALL_URL := https://github.com/Kitware/CMake/releases/download/v$(CMAKE_VERSION)/cmake-$(CMAKE_VERSION)-$(CMAKE_TARBALL_ARCH).tar.gz
CMAKE_TARBALL     := $(CACHE_DIR)/cmake-$(CMAKE_VERSION)-$(CMAKE_TARBALL_ARCH).tar.gz

# ---- phony entry points --------------------------------------------------

.PHONY: all toolchain aws-lc wheel clean check-host-deps

all: wheel

check-host-deps:
	@command -v cargo >/dev/null || { echo "ERROR: cargo not on PATH"; exit 1; }
	@command -v curl  >/dev/null || { echo "ERROR: curl not on PATH";  exit 1; }
	@command -v tar   >/dev/null || { echo "ERROR: tar not on PATH";   exit 1; }
	@command -v uv    >/dev/null || { echo "ERROR: uv not on PATH (needed for the host CPython 3.14 that drives cryptography's cffi codegen)"; exit 1; }
	@test -d $(WASI_SDK_PATH)    || { echo "ERROR: WASI_SDK_PATH=$(WASI_SDK_PATH) missing"; exit 1; }

# CPython is built by ../cpython on demand. Phony so each invocation
# re-enters that Makefile (which has its own up-to-date check); the
# first call from a fresh checkout populates $(CPYTHON_LIB) and
# $(CPYTHON_INCLUDE), subsequent ones are near-instant.
.PHONY: cpython
cpython:
	$(MAKE) -C $(CPYTHON_DIR)

# =============================================================================
# Stage 1: toolchain bootstrap
# =============================================================================

toolchain: $(MATURIN) $(CMAKE) $(BINDGEN) $(HOST_PYTHON)

# Host CPython venv: cryptography-cffi's build.rs runs
# _cffi_src/build_openssl.py via `python3`, which needs cffi +
# setuptools. uv ships a managed Python on this box; we put a venv on
# top with just those two packages.
$(HOST_PYTHON): | check-host-deps
	@mkdir -p $(TOOLCHAIN_DIR)
	uv venv --python 3.14 $(HOST_PYVENV)
	uv pip install --python $(HOST_PYTHON) cffi setuptools wheel

# Pin maturin via `cargo install --root` — installs into our own
# toolchain dir, doesn't pollute $CARGO_HOME/bin.
$(MATURIN): | check-host-deps
	@mkdir -p $(TOOLCHAIN_DIR)
	cargo install --root $(TOOLCHAIN_DIR) --locked maturin@$(MATURIN_VERSION)

# Same pattern for bindgen-cli; openssl-sys's AWS-LC binding-gen path
# shells out to a `bindgen` binary at build time. After install we
# rename the real binary to `bindgen-real` and drop a shell shim in
# its place that appends --sysroot=$(WASI_SYSROOT) to every call. The
# shim is what makes libclang resolve <stdint.h> against wasi-libc
# instead of falling back to host /usr/include — see
# bindgen-wasm-wrapper.sh's header for the why.
$(BINDGEN): bindgen-wasm-wrapper.sh | check-host-deps
	@mkdir -p $(TOOLCHAIN_DIR)
	cargo install --root $(TOOLCHAIN_DIR) --locked bindgen-cli@$(BINDGEN_VERSION)
	mv $@ $@-real
	install -m 0755 bindgen-wasm-wrapper.sh $@

# CMake binary release from Kitware. The tarball ships a portable
# self-contained tree; we extract directly into .toolchain/ so the
# `cmake` symlink lands in $(TOOLCHAIN_BIN).
$(CMAKE_TARBALL): | check-host-deps
	@mkdir -p $(CACHE_DIR)
	curl -fsSL -o $@.tmp $(CMAKE_TARBALL_URL)
	mv $@.tmp $@

$(CMAKE): $(CMAKE_TARBALL)
	@mkdir -p $(TOOLCHAIN_BIN)
	@# Strip leading cmake-X.Y.Z-arch/ so the contents land in TOOLCHAIN_DIR.
	tar -xzf $(CMAKE_TARBALL) -C $(TOOLCHAIN_DIR) --strip-components=1
	@test -x $@ || (echo "cmake tarball didn't ship bin/cmake at $@"; exit 1)
	@$@ --version | head -1

# =============================================================================
# Stage 2: AWS-LC cross-build (wasm32-wasip2 static libs)
# =============================================================================

aws-lc: $(AWS_LC_LIBCRYPTO)

$(AWS_LC_TARBALL): | check-host-deps
	@mkdir -p $(CACHE_DIR)
	curl -fsSL -o $@.tmp $(AWS_LC_TARBALL_URL)
	mv $@.tmp $@

$(AWS_LC_SRC)/CMakeLists.txt: $(AWS_LC_TARBALL) $(wildcard $(THIS_DIR)patches-aws-lc/*.patch)
	@mkdir -p $(BUILD_DIR)/src
	rm -rf $(AWS_LC_SRC)
	tar -xzf $(AWS_LC_TARBALL) -C $(BUILD_DIR)/src
	@# Tarball directory name is `aws-lc-<rev-without-leading-v>`;
	@# rename to our consistent path.
	mv $(BUILD_DIR)/src/aws-lc-$(patsubst v%,%,$(AWS_LC_REV)) $(AWS_LC_SRC)
	@for p in $(wildcard $(THIS_DIR)patches-aws-lc/*.patch); do \
	    echo "  applying $$p"; \
	    (cd $(AWS_LC_SRC) && patch -p1 --no-backup-if-mismatch < $$p) || exit 1; \
	done
	touch $@

# Configure + build. WASI SDK ships wasi-sdk-p2.cmake which sets CC,
# AR, RANLIB, sysroot, and the right cmake CMAKE_SYSTEM_NAME so
# find_package / try_compile work cross-compiled. The DISABLE_* flags
# below skip subprojects that need a host Perl/Go (which we have
# neither in the sandbox nor want as dependencies).
$(AWS_LC_LIBCRYPTO): $(AWS_LC_SRC)/CMakeLists.txt $(CMAKE)
	@mkdir -p $(AWS_LC_BUILD) $(AWS_LC_PREFIX)
	cd $(AWS_LC_BUILD) && $(CMAKE) $(AWS_LC_SRC) \
	    -DCMAKE_TOOLCHAIN_FILE=$(WASI_SDK_PATH)/share/cmake/wasi-sdk-p2.cmake \
	    -DCMAKE_INSTALL_PREFIX=$(AWS_LC_PREFIX) \
	    -DCMAKE_BUILD_TYPE=Release \
	    -DBUILD_SHARED_LIBS=OFF \
	    -DBUILD_TESTING=OFF \
	    -DBUILD_TOOL=OFF \
	    -DDISABLE_PERL=ON \
	    -DDISABLE_GO=ON \
	    -DOPENSSL_NO_ASM=ON \
	    -DCMAKE_C_FLAGS='-fPIC -D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_GETPID -D_WASI_EMULATED_PROCESS_CLOCKS -DOPENSSL_NO_TTY'
	$(CMAKE) --build $(AWS_LC_BUILD) --parallel
	$(CMAKE) --install $(AWS_LC_BUILD)
	@printf '✓  aws-lc: '; ls -lh $@ | awk '{print $$5"  "$$NF}'

# =============================================================================
# Stage 3: cryptography wheel
# =============================================================================

PYO3_CONFIG_FILE_ABS := $(THIS_DIR)pyo3-wasm-cpython314.txt

wheel: $(WHEEL_OUT)

$(CRYPTOGRAPHY_TARBALL): | check-host-deps
	@mkdir -p $(CACHE_DIR)
	curl -fsSL -o $@.tmp $(CRYPTOGRAPHY_SDIST_URL)
	mv $@.tmp $@

$(CRYPTOGRAPHY_SRC)/pyproject.toml: $(CRYPTOGRAPHY_TARBALL)
	@mkdir -p $(BUILD_DIR)/src
	rm -rf $(CRYPTOGRAPHY_SRC)
	tar -xzf $(CRYPTOGRAPHY_TARBALL) -C $(BUILD_DIR)/src
	touch $@

# Patches against the cryptography sdist tree live in
# patches-cryptography/. Each is `patch -p1` applied. Currently:
#   0001-cffi-build-rs-honor-WASM_PYTHON_INCLUDE.patch — lets us
#     override cryptography-cffi/build.rs's host-include resolution
#     with the wasm CPython's include dir (host has LONG_BIT=64,
#     target is 32-bit, pyport.h trips on the mismatch).
$(BUILD_DIR)/.patched: $(CRYPTOGRAPHY_SRC)/pyproject.toml $(wildcard $(THIS_DIR)patches-cryptography/*.patch)
	@for p in $(wildcard $(THIS_DIR)patches-cryptography/*.patch); do \
	    echo "  applying $$p"; \
	    (cd $(CRYPTOGRAPHY_SRC) && patch -p1 --no-backup-if-mismatch < $$p) || exit 1; \
	done
	touch $@

$(WHEEL_OUT): $(BUILD_DIR)/.patched $(AWS_LC_LIBCRYPTO) $(MATURIN) $(BINDGEN) $(HOST_PYTHON) $(PYO3_CONFIG_FILE_ABS) | cpython
	@mkdir -p $(OUT_DIR)
	cd $(CRYPTOGRAPHY_SRC) && \
	  PATH=$(TOOLCHAIN_BIN):$$PATH \
	  PYO3_CONFIG_FILE=$(PYO3_CONFIG_FILE_ABS) \
	  PYO3_CROSS=1 \
	  PYO3_PYTHON=$(HOST_PYTHON) \
	  WASM_PYTHON_INCLUDE=$(CPYTHON_INCLUDE) \
	  AWS_LC_INSTALL_DIR=$(AWS_LC_PREFIX) \
	  OPENSSL_DIR=$(AWS_LC_PREFIX) \
	  OPENSSL_LIB_DIR=$(AWS_LC_PREFIX)/lib \
	  OPENSSL_INCLUDE_DIR=$(AWS_LC_PREFIX)/include \
	  OPENSSL_STATIC=1 \
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
	@# Maturin tags its output cp314-abi3-any; the wasi-cross build is
	@# only loadable inside a wasm32-wasip2 CPython, so retag to
	@# cp314-abi3-wasi_wasm32 — pip then correctly ignores it on
	@# non-wasi hosts. `wheel tags --remove` drops the original.
	$(HOST_PYTHON) -m wheel tags --remove --platform-tag wasi_wasm32 \
	  $(OUT_DIR)/cryptography-$(CRYPTOGRAPHY_VERSION)-cp314-abi3-any.whl
	@printf '✓  wheel: '; ls -lh $@ | awk '{print $$5"  "$$NF}'

# =============================================================================
# clean
# =============================================================================

clean:
	rm -rf $(WORKDIR) $(OUT_DIR)
	@echo "kept: $(CACHE_DIR) (tarballs, keyed on upstream version)"
