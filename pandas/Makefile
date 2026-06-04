# pandas/Makefile — pandas 3.0.3 wheel for wasm32-wasip2.
#
# Produces:
#   out/pandas-3.0.3-cp314-cp314-wasi_wasm32.whl
#
# pandas 3.0 is meson-python (like NumPy 2.x), so this reuses the same
# wasm cross machinery as ../numpy almost verbatim: a meson cross-file,
# the --start-group-stripping clang wrapper, -fuse-ld=lld, import-dynamic
# linking, the _PYTHON_SYSCONFIGDATA_NAME trick, the -D__EMSCRIPTEN__=1
# masquerade, and -msimd128. See ../numpy/Makefile for the full rationale
# behind each.
#
# THE key difference from duckdb-python: pandas compiles against NumPy's
# C API (Cython `cimport numpy` + numpy/arrayobject.h). So unlike duckdb
# (where numpy is runtime-optional), pandas NEEDS numpy headers at BUILD
# time. pandas' meson runs `import numpy; numpy.get_include()` on the
# build python, so we install a HOST numpy==2.4.6 into the toolchain venv
# purely to provide its (arch-independent, wasm-aware) headers + .pxd
# files. NumPy 2.4's npy_cpu.h already recognizes __EMSCRIPTEN__/__wasm__
# (NPY_CPU_WASM), so no numpy-header patching is needed (dicej's old
# numpy<2 recipe had to patch npy_cpu.h / npy_interrupt.h — obsolete here).
#
# Tagged cp314-cp314 (Cython extensions use CPython internals; per-minor).
#
#   make toolchain   host venv (meson/cython/numpy/versioneer/...)
#   make configure   meson setup only (cheap; validates the cross-file)
#   make wheel       full build + retag
#   make all         toolchain → wheel
#   make clean       wipe $(WORKDIR) + out/ (keeps .cache/)
#
# Prerequisites: the wasm CPython 3.14 from ../cpython, and the wasi NumPy
# wheel is the conceptual prerequisite (pandas is useless without a numpy
# to import at runtime) though the BUILD only needs host numpy's headers.

THIS_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# ---- versions / sources --------------------------------------------------

PANDAS_VERSION := 3.0.3
PANDAS_SDIST_URL := https://files.pythonhosted.org/packages/f8/87/4341c6252d1c47b08768c3d25ac487362bf403f0313ddae4a2a26c9b1b4c/pandas-$(PANDAS_VERSION).tar.gz

# Host build tooling. pandas pyproject: meson-python>=0.17.1,<1,
# meson>=1.2.1,<2, Cython>3.1.0,<4, numpy>=2.0.0, versioneer[toml].
# NumPy pinned to 2.4.6 so the build-time headers match the wasi numpy
# wheel we ship from ../numpy.
MESON_PYTHON_VERSION := 0.18.0
MESON_VERSION        := 1.9.0
CYTHON_VERSION       := 3.1.4
NINJA_VERSION        := 1.11.1.1
BUILD_VERSION        := 1.2.2
NUMPY_HOST_VERSION   := 2.4.6

# ---- build parallelism (see ../duckdb/Makefile for the full rationale) ---
NPROC    := $(shell nproc 2>/dev/null || echo 2)
MEM_GB   := $(shell awk '/MemTotal/ {printf "%d", $$2/1024/1024}' /proc/meminfo 2>/dev/null || echo 2)
MEM_JOBS := $(shell echo $$(( $(MEM_GB) / 2 )) )
MEM_JOBS := $(shell echo $$(( $(MEM_JOBS) < 1 ? 1 : $(MEM_JOBS) )) )
JOBS     ?= $(shell echo $$(( $(NPROC) < $(MEM_JOBS) ? $(NPROC) : $(MEM_JOBS) )) )

# ---- paths ---------------------------------------------------------------

WASI_SDK_PATH       ?= /opt/wasi-sdk
WASI_SYSROOT        := $(WASI_SDK_PATH)/share/wasi-sysroot
WASI_SYSROOT_SHARED := $(WASI_SYSROOT)/lib/wasm32-wasip2

CPYTHON_DIR      := $(THIS_DIR)../cpython
CPYTHON_REV      := 0e13686da8bb881b059d35e23c32bcd2e6440099
CPYTHON_WASI_DIR := $(HOME)/cargo-target/cpython-$(CPYTHON_REV)/cpython/builddir/wasi
CPYTHON_INCLUDE  := $(CPYTHON_WASI_DIR)/install/include/python3.14
CPYTHON_SYSCONFIG_DIR  := $(CPYTHON_WASI_DIR)/install/lib/python3.14
CPYTHON_SYSCONFIG_NAME := _sysconfigdata__wasi_wasm32-wasi

WORKDIR       ?= $(HOME)/.cache/particle/wheels-pandas
TOOLCHAIN_DIR := $(WORKDIR)/toolchain
BUILD_DIR     := $(WORKDIR)/build
CACHE_DIR     := $(THIS_DIR).cache
OUT_DIR       := $(THIS_DIR)out

HOST_PYVENV := $(TOOLCHAIN_DIR)/pyvenv
HOST_PYTHON := $(HOST_PYVENV)/bin/python

PANDAS_TARBALL := $(CACHE_DIR)/pandas-$(PANDAS_VERSION).tar.gz
PANDAS_SRC     := $(BUILD_DIR)/src/pandas-$(PANDAS_VERSION)
MESON_BUILD    := $(BUILD_DIR)/meson-build
MP_BUILD       := $(BUILD_DIR)/mp-build
CROSSFILE      := $(BUILD_DIR)/wasm32-wasip2-cross.ini
PANDAS_PATCHES := $(wildcard $(THIS_DIR)patches-pandas/*.patch)
# clang wrappers stripping GNU-ld --start-group/--end-group (wasm-ld
# rejects them); see ../numpy/Makefile.
CC_WRAP  := $(BUILD_DIR)/wasi-cc
CXX_WRAP := $(BUILD_DIR)/wasi-cxx

WHEEL := $(OUT_DIR)/pandas-$(PANDAS_VERSION)-cp314-cp314-wasi_wasm32.whl

# ---- compile / link flags (embedded into the cross-file) -----------------
# Mirrors ../numpy: target + -msimd128 (baseline wasm SIMD, no relaxed) +
# emscripten masquerade + wasi-libc emulation defs + target python include.
PANDAS_C_ARGS := '--target=wasm32-wasip2', '-msimd128', '-D__EMSCRIPTEN__=1', '-DNPY_NO_SIGNAL', '-D_WASI_EMULATED_SIGNAL', '-D_WASI_EMULATED_MMAN', '-D_WASI_EMULATED_PROCESS_CLOCKS', '-D_WASI_EMULATED_GETPID', '-fPIC', '-I$(CPYTHON_INCLUDE)'

# import-dynamic: Python C API + libc + emulated-* symbols resolved at
# .so-load by the embedding. Don't link shared libc.so (breaks meson check
# executables). -fuse-ld=lld → wasm-ld (core modules; meson can classify it).
PANDAS_LINK_ARGS := '--target=wasm32-wasip2', '-fuse-ld=lld', '-Wl,--unresolved-symbols=import-dynamic', '-Wl,--allow-undefined'

# ---- phony entry points --------------------------------------------------

.PHONY: all toolchain configure wheel clean cpython check-host-deps

all: wheel

check-host-deps:
	@command -v curl >/dev/null || { echo "ERROR: curl not on PATH"; exit 1; }
	@command -v tar  >/dev/null || { echo "ERROR: tar not on PATH";  exit 1; }
	@command -v patch >/dev/null || { echo "ERROR: patch not on PATH"; exit 1; }
	@command -v uv   >/dev/null || { echo "ERROR: uv not on PATH (host venv)"; exit 1; }
	@test -d $(WASI_SDK_PATH) || { echo "ERROR: WASI_SDK_PATH=$(WASI_SDK_PATH) missing"; exit 1; }

cpython:
	$(MAKE) -C $(CPYTHON_DIR)

# =============================================================================
# Stage 1: host toolchain venv
# =============================================================================

toolchain: $(HOST_PYTHON)

$(HOST_PYTHON): | check-host-deps
	@mkdir -p $(TOOLCHAIN_DIR)
	uv venv --python 3.14 $(HOST_PYVENV)
	uv pip install --python $(HOST_PYTHON) \
	  'meson-python==$(MESON_PYTHON_VERSION)' \
	  'meson==$(MESON_VERSION)' \
	  'cython==$(CYTHON_VERSION)' \
	  'ninja==$(NINJA_VERSION)' \
	  'build==$(BUILD_VERSION)' \
	  'numpy==$(NUMPY_HOST_VERSION)' \
	  'versioneer[toml]' \
	  patchelf \
	  wheel
	@# numpy is installed for its BUILD-TIME headers + .pxd files only
	@# (pandas runs `import numpy; numpy.get_include()`); it is never
	@# linked into the wasm output. patchelf: meson-python Linux build dep,
	@# never run on the wasm .so (not ELF → skipped).

# =============================================================================
# Stage 2: source fetch + extract + patch + cross-file
# =============================================================================

$(PANDAS_TARBALL): | check-host-deps
	@mkdir -p $(CACHE_DIR)
	curl -fsSL -o $@.tmp $(PANDAS_SDIST_URL)
	mv $@.tmp $@

$(PANDAS_SRC)/pyproject.toml: $(PANDAS_TARBALL)
	@mkdir -p $(BUILD_DIR)/src
	rm -rf $(PANDAS_SRC)
	tar -xzf $(PANDAS_TARBALL) -C $(BUILD_DIR)/src
	touch $@

# patches-pandas/ is empty for now (none needed at 3.0.3). Sentinel keeps
# the mechanism ready for any wasi porting patch a future bump needs.
$(BUILD_DIR)/.patched: $(PANDAS_SRC)/pyproject.toml $(PANDAS_PATCHES)
	@for p in $(PANDAS_PATCHES); do \
	    echo "  applying $$p"; \
	    (cd $(PANDAS_SRC) && patch -p1 --no-backup-if-mismatch < $$p) || exit 1; \
	done
	touch $@

# Meson cross-file (+ clang wrappers). Identical shape to ../numpy.
$(CROSSFILE): $(HOST_PYTHON) | $(BUILD_DIR)/.patched
	@mkdir -p $(BUILD_DIR)
	@printf '%s\n' \
	  '#!/usr/bin/env bash' \
	  'out=()' \
	  'for a in "$$@"; do' \
	  '  case "$$a" in -Wl,--start-group|-Wl,--end-group|--start-group|--end-group) continue;; esac' \
	  '  out+=("$$a")' \
	  'done' \
	  'exec $(WASI_SDK_PATH)/bin/clang "$${out[@]}"' \
	  > $(CC_WRAP)
	@sed 's#bin/clang "#bin/clang++ "#' $(CC_WRAP) > $(CXX_WRAP)
	@chmod +x $(CC_WRAP) $(CXX_WRAP)
	@printf '%s\n' \
	  '[binaries]' \
	  "c = '$(CC_WRAP)'" \
	  "cpp = '$(CXX_WRAP)'" \
	  "ar = '$(WASI_SDK_PATH)/bin/llvm-ar'" \
	  "ranlib = '$(WASI_SDK_PATH)/bin/llvm-ranlib'" \
	  "strip = '$(WASI_SDK_PATH)/bin/llvm-strip'" \
	  "python = '$(HOST_PYTHON)'" \
	  '' \
	  '[host_machine]' \
	  "system = 'emscripten'" \
	  "cpu_family = 'wasm32'" \
	  "cpu = 'wasm'" \
	  "endian = 'little'" \
	  '' \
	  '[properties]' \
	  'needs_exe_wrapper = true' \
	  'skip_sanity_check = true' \
	  "longdouble_format = 'IEEE_QUAD_LE'" \
	  '' \
	  '[built-in options]' \
	  "c_args = [$(PANDAS_C_ARGS)]" \
	  "cpp_args = [$(PANDAS_C_ARGS)]" \
	  "c_link_args = [$(PANDAS_LINK_ARGS)]" \
	  "cpp_link_args = [$(PANDAS_LINK_ARGS)]" \
	  > $(CROSSFILE)
	@echo "✓  cross-file: $(CROSSFILE)"

# =============================================================================
# Stage 3: meson configure (cheap validation gate)
# =============================================================================
# pandas uses the venv's stock meson (its build-system requires meson<2);
# unlike numpy it does NOT vendor meson, so no 'features' module concern.

configure: $(MESON_BUILD)/build.ninja

$(MESON_BUILD)/build.ninja: $(CROSSFILE) | cpython
	rm -rf $(MESON_BUILD)
	cd $(PANDAS_SRC) && \
	  PATH=$(HOST_PYVENV)/bin:$$PATH \
	  _PYTHON_SYSCONFIGDATA_NAME=$(CPYTHON_SYSCONFIG_NAME) \
	  PYTHONPATH=$(CPYTHON_SYSCONFIG_DIR) \
	  $(HOST_PYVENV)/bin/meson setup $(MESON_BUILD) \
	    --cross-file=$(CROSSFILE)
	@echo "✓  meson configured: $(MESON_BUILD)"

# =============================================================================
# Stage 4: full wheel build (meson-python) + retag
# =============================================================================

wheel: $(WHEEL)

$(WHEEL): $(CROSSFILE) | cpython
	@mkdir -p $(BUILD_DIR)/dist $(OUT_DIR)
	@# Wipe the persistent build dir if the cross-file changed — meson reads
	@# cross-file [built-in options] only at first setup (see ../numpy).
	@if [ -f $(MP_BUILD)/build.ninja ] && [ $(CROSSFILE) -nt $(MP_BUILD)/build.ninja ]; then \
	   echo "  cross-file changed → clean meson setup (wiping $(MP_BUILD))"; \
	   rm -rf $(MP_BUILD); \
	 fi
	@echo "  building pandas with JOBS=$(JOBS) (nproc=$(NPROC), mem=$(MEM_GB)GB)"
	cd $(PANDAS_SRC) && \
	  PATH=$(HOST_PYVENV)/bin:$$PATH \
	  _PYTHON_SYSCONFIGDATA_NAME=$(CPYTHON_SYSCONFIG_NAME) \
	  PYTHONPATH=$(CPYTHON_SYSCONFIG_DIR) \
	  $(HOST_PYTHON) -m build --wheel --no-isolation \
	    --outdir $(BUILD_DIR)/dist \
	    -C build-dir=$(MP_BUILD) \
	    -C setup-args=--cross-file=$(CROSSFILE) \
	    -C compile-args=-j$(JOBS) \
	    .
	@# meson-python tags with the host platform; retag to wasi_wasm32.
	$(HOST_PYTHON) -m wheel tags --platform-tag wasi_wasm32 --remove \
	  $(BUILD_DIR)/dist/pandas-$(PANDAS_VERSION)-cp314-cp314-*.whl
	cp $(BUILD_DIR)/dist/pandas-$(PANDAS_VERSION)-cp314-cp314-wasi_wasm32.whl $(WHEEL)
	@printf '✓  pandas wheel: '; ls -lh $(WHEEL) | awk '{print $$5"  "$$NF}'

# =============================================================================
# clean
# =============================================================================

clean:
	rm -rf $(WORKDIR) $(OUT_DIR)
	@echo "kept: $(CACHE_DIR) (tarballs, keyed on upstream version)"
