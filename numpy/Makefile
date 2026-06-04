# numpy/Makefile — NumPy 2.4.6 wheel for wasm32-wasip2.
#
# Produces:
#   out/numpy-2.4.6-cp314-cp314-wasi_wasm32.whl
#
# NumPy 2.4 builds with meson-python (NOT the old setup.py/numpy.distutils
# path). So this drives a meson cross-compile, carrying over the three
# tricks that make a wasi NumPy tractable (dicej/wasi-wheels pioneered the
# first two for the setup.py era; they transfer cleanly to meson):
#
#   1. -D__EMSCRIPTEN__=1 — NumPy has emscripten-specific code paths
#      (single-threaded, no SIMD asm, longdouble handling) guarded on
#      __EMSCRIPTEN__. wasi has no such guards upstream, so we masquerade
#      as emscripten at the preprocessor (and set the cross-file
#      host_machine.system to 'emscripten' to match).
#   2. _PYTHON_SYSCONFIGDATA_NAME + PYTHONPATH — point the host python at
#      the wasm CPython's cross-built sysconfigdata so meson's python
#      introspection reports the target EXT_SUFFIX / SOABI / include dir.
#      (Same trick used in ../duckdb-python; verified against our cpython.)
#   3. BLAS-less: NumPy 2.4's `allow-noblas` already defaults true, so the
#      reference fallbacks are used; we also pass it explicitly and turn
#      off threading + all SIMD/optimization (no wasm SIMD on wasip2).
#
# meson-python tags the wheel with the *host* platform (get_platform() is
# not a sysconfig var the trick can redirect — it reads os.uname()). So we
# retag the platform to wasi_wasm32 after the build, like ../cryptography.
#
# Tagged cp314-cp314 (NumPy's C extensions use CPython internals; per-minor).
#
#   make toolchain   host venv (meson/ninja/cython/build/meson-python)
#   make configure   meson setup only (cheap; validates the cross-file)
#   make wheel       full build + retag
#   make all         toolchain → wheel
#   make clean       wipe $(WORKDIR) + out/ (keeps .cache/)
#
# Prerequisite: wasm CPython 3.14 from ../cpython (headers + sysconfigdata
# + libpython3.14.so). The `cpython` phony recurses on demand.

THIS_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# ---- versions / sources --------------------------------------------------

NUMPY_VERSION := 2.4.6
NUMPY_SDIST_URL := https://files.pythonhosted.org/packages/d0/ad/fed0499ce6a338d2a03ebae59cd15093910c8875328855781952abf6c2fe/numpy-$(NUMPY_VERSION).tar.gz

# Host build tooling (installed into the venv). meson-python pulls meson;
# pin ninja + cython to known-good. Cython >=3.0.6 per numpy pyproject.
MESON_PYTHON_VERSION := 0.18.0
CYTHON_VERSION       := 3.1.1
NINJA_VERSION        := 1.11.1.1
BUILD_VERSION        := 1.2.2

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
CPYTHON_LIB      := $(CPYTHON_WASI_DIR)/libpython3.14.so
CPYTHON_SYSCONFIG_DIR  := $(CPYTHON_WASI_DIR)/install/lib/python3.14
CPYTHON_SYSCONFIG_NAME := _sysconfigdata__wasi_wasm32-wasi

WORKDIR       ?= $(HOME)/.cache/particle/wheels-numpy
TOOLCHAIN_DIR := $(WORKDIR)/toolchain
BUILD_DIR     := $(WORKDIR)/build
CACHE_DIR     := $(THIS_DIR).cache
OUT_DIR       := $(THIS_DIR)out

HOST_PYVENV := $(TOOLCHAIN_DIR)/pyvenv
HOST_PYTHON := $(HOST_PYVENV)/bin/python

NUMPY_TARBALL := $(CACHE_DIR)/numpy-$(NUMPY_VERSION).tar.gz
NUMPY_SRC     := $(BUILD_DIR)/src/numpy-$(NUMPY_VERSION)
MESON_BUILD   := $(BUILD_DIR)/meson-build
MP_BUILD      := $(BUILD_DIR)/mp-build
CROSSFILE     := $(BUILD_DIR)/wasm32-wasip2-cross.ini
NUMPY_PATCHES := $(wildcard $(THIS_DIR)patches-numpy/*.patch)
# Thin clang wrappers that strip GNU-ld --start-group/--end-group (wasm-ld
# rejects them as unknown args). meson only uses its group-free WASM linker
# class for emcc; with raw wasi-sdk clang it picks generic LLD and inserts
# --start-group around libraries. wasm-ld resolves archives iteratively, so
# dropping the group markers is a no-op semantically.
CC_WRAP  := $(BUILD_DIR)/wasi-cc
CXX_WRAP := $(BUILD_DIR)/wasi-cxx

WHEEL := $(OUT_DIR)/numpy-$(NUMPY_VERSION)-cp314-cp314-wasi_wasm32.whl

# ---- compile / link flags (embedded into the cross-file) -----------------
# c_args: target + the emscripten masquerade + wasi-libc emulation defs +
# the target python include. NPY_NO_SIGNAL: no signal-based FPE handling.
#
# -msimd128: enable the finalized WebAssembly fixed-width 128-bit SIMD.
# numpy's OWN universal-intrinsics kernels have no wasm backend (only
# sse/avx/neon/vec/lsx), so disable-optimization stays on and those stay
# scalar — but -msimd128 lets LLVM AUTO-VECTORIZE those scalar loops into
# v128/f32x4/etc instructions (verified: a scalar add loop lowers to
# f32x4.add). Modest speedup, no numpy changes. Baseline SIMD only — NOT
# -mrelaxed-simd (newer, less universally supported). TRADEOFF: the module
# now REQUIRES a SIMD128-capable runtime to instantiate; wasm has no
# in-module runtime dispatch, so it's baked in at compile time. Fine for
# modern wasip2 engines (wasmtime/wasmer/browsers/Node all support it).
NUMPY_C_ARGS := '--target=wasm32-wasip2', '-msimd128', '-D__EMSCRIPTEN__=1', '-DNPY_NO_SIGNAL', '-D_WASI_EMULATED_SIGNAL', '-D_WASI_EMULATED_MMAN', '-D_WASI_EMULATED_PROCESS_CLOCKS', '-D_WASI_EMULATED_GETPID', '-fPIC', '-I$(CPYTHON_INCLUDE)'

# c_link_args: import-dynamic pattern (Python C API + libc + the wasi
# emulated-* symbols all resolved at .so-load by the embedding's dyld —
# libpython3.14.so provides them). We deliberately DON'T link -l:libc.so
# or the emulated libs here: that shared libc can't link into meson's
# feature-CHECK executables ("attempted static link of dynamic object"),
# and import-dynamic already turns those undefined symbols into load-time
# imports for the final modules.
#
# -fuse-ld=lld forces plain wasm-ld. wasi-sdk's DEFAULT wasip2 linker is
# wasm-component-ld (builds components); meson runs `clang -Wl,--version`,
# gets "wasm-component-ld 0.5.20", and can't classify it. wasm-ld reports
# as LLD (meson knows it) AND emits core wasm shared modules — what the
# embedding dlopen's (like cffi/duckdb), not wasip2 components.
NUMPY_LINK_ARGS := '--target=wasm32-wasip2', '-fuse-ld=lld', '-Wl,--unresolved-symbols=import-dynamic', '-Wl,--allow-undefined'

# Meson project options for a single-threaded, BLAS-less, SIMD-free build.
MESON_OPTS := -Dallow-noblas=true \
              -Ddisable-threading=true \
              -Ddisable-optimization=true \
              -Ddisable-svml=true \
              -Ddisable-highway=true \
              -Ddisable-intel-sort=true

# ---- phony entry points --------------------------------------------------

.PHONY: all toolchain configure wheel clean cpython check-host-deps

all: wheel

check-host-deps:
	@command -v curl >/dev/null || { echo "ERROR: curl not on PATH"; exit 1; }
	@command -v tar  >/dev/null || { echo "ERROR: tar not on PATH";  exit 1; }
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
	  meson-python==$(MESON_PYTHON_VERSION) \
	  cython==$(CYTHON_VERSION) \
	  ninja==$(NINJA_VERSION) \
	  build==$(BUILD_VERSION) \
	  patchelf \
	  wheel
	@# patchelf is a meson-python build dep on Linux (RPATH fixups). It's
	@# never actually run on our cross-built .so — those are wasm, not ELF,
	@# so meson-python skips them — but its presence is required by the
	@# build-dependency check under --no-isolation.

# =============================================================================
# Stage 2: source fetch + extract + cross-file generation
# =============================================================================

$(NUMPY_TARBALL): | check-host-deps
	@mkdir -p $(CACHE_DIR)
	curl -fsSL -o $@.tmp $(NUMPY_SDIST_URL)
	mv $@.tmp $@

$(NUMPY_SRC)/pyproject.toml: $(NUMPY_TARBALL)
	@mkdir -p $(BUILD_DIR)/src
	rm -rf $(NUMPY_SRC)
	tar -xzf $(NUMPY_TARBALL) -C $(BUILD_DIR)/src
	touch $@

# Apply wasi porting patches. Sentinel re-applies on a fresh extract or
# when a patch changes.
#   0001-temp-elide-disable-on-wasi.patch — numpy/_core temp_elide.c uses
#     dladdr/Dl_info (from <dlfcn.h>); wasi-libc's dlfcn.h declares
#     dlopen/dlsym but NOT dladdr/Dl_info. meson's has_function reports
#     HAVE_BACKTRACE/HAVE_DLFCN_H true (import-dynamic makes every probe
#     link), so numpy compiles the elision path and the wasi header gap
#     errors. Guard it off on __wasi__ → numpy takes its no-elision
#     fallback (a perf optimization, not a correctness feature).
$(BUILD_DIR)/.patched: $(NUMPY_SRC)/pyproject.toml $(NUMPY_PATCHES)
	@for p in $(NUMPY_PATCHES); do \
	    echo "  applying $$p"; \
	    (cd $(NUMPY_SRC) && patch -p1 --no-backup-if-mismatch < $$p) || exit 1; \
	done
	touch $@

# Meson cross-file. Generated here so the wasi-sdk + wasm-cpython absolute
# paths are baked correctly. host_machine.system=emscripten pairs with the
# -D__EMSCRIPTEN__=1 masquerade so numpy's meson.build takes its emscripten
# branches. needs_exe_wrapper=true: meson must not try to RUN wasm binaries
# during configure (it falls back to safe defaults for run-checks).
$(CROSSFILE): $(HOST_PYTHON) | $(BUILD_DIR)/.patched
	@mkdir -p $(BUILD_DIR)
	@# Generate the --start-group/--end-group-stripping clang wrappers.
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
	  "c_args = [$(NUMPY_C_ARGS)]" \
	  "cpp_args = [$(NUMPY_C_ARGS)]" \
	  "c_link_args = [$(NUMPY_LINK_ARGS)]" \
	  "cpp_link_args = [$(NUMPY_LINK_ARGS)]" \
	  > $(CROSSFILE)
	@echo "✓  cross-file: $(CROSSFILE)"

# =============================================================================
# Stage 3: meson configure (cheap validation gate)
# =============================================================================
# Cheap "does it configure" gate before the heavy compile. MUST use numpy's
# VENDORED meson (vendored-meson/meson/meson.py) — numpy's meson_cpu/
# framework imports a custom 'features' module that stock PyPI meson lacks
# ("Module features does not exist"). meson-python uses the vendored meson
# automatically (per pyproject [tool.meson-python] meson=...); this gate
# mirrors that so it actually validates the real build path.

configure: $(MESON_BUILD)/build.ninja

$(MESON_BUILD)/build.ninja: $(CROSSFILE) | cpython
	rm -rf $(MESON_BUILD)
	cd $(NUMPY_SRC) && \
	  PATH=$(HOST_PYVENV)/bin:$$PATH \
	  _PYTHON_SYSCONFIGDATA_NAME=$(CPYTHON_SYSCONFIG_NAME) \
	  PYTHONPATH=$(CPYTHON_SYSCONFIG_DIR) \
	  $(HOST_PYTHON) vendored-meson/meson/meson.py setup $(MESON_BUILD) \
	    --cross-file=$(CROSSFILE) \
	    $(MESON_OPTS)
	@echo "✓  meson configured: $(MESON_BUILD)"

# =============================================================================
# Stage 4: full wheel build (meson-python) + retag
# =============================================================================

wheel: $(WHEEL)

$(WHEEL): $(CROSSFILE) | cpython
	@mkdir -p $(BUILD_DIR)/dist $(OUT_DIR)
	@# meson reads cross-file [built-in options] (c_args etc.) ONLY on the
	@# first `meson setup`; a reconfigure of an existing build dir keeps the
	@# cached values. So if the cross-file changed (e.g. a compile flag like
	@# -msimd128 was added), wipe the persistent build dir to force a clean
	@# setup. Otherwise keep it for incremental rebuilds.
	@if [ -f $(MP_BUILD)/build.ninja ] && [ $(CROSSFILE) -nt $(MP_BUILD)/build.ninja ]; then \
	   echo "  cross-file changed → clean meson setup (wiping $(MP_BUILD))"; \
	   rm -rf $(MP_BUILD); \
	 fi
	@echo "  building numpy with JOBS=$(JOBS) (nproc=$(NPROC), mem=$(MEM_GB)GB)"
	cd $(NUMPY_SRC) && \
	  PATH=$(HOST_PYVENV)/bin:$$PATH \
	  _PYTHON_SYSCONFIGDATA_NAME=$(CPYTHON_SYSCONFIG_NAME) \
	  PYTHONPATH=$(CPYTHON_SYSCONFIG_DIR) \
	  $(HOST_PYTHON) -m build --wheel --no-isolation \
	    --outdir $(BUILD_DIR)/dist \
	    -C build-dir=$(MP_BUILD) \
	    -C setup-args=--cross-file=$(CROSSFILE) \
	    $(foreach o,$(MESON_OPTS),-C setup-args=$(o)) \
	    -C compile-args=-j$(JOBS) \
	    .
	@# meson-python tags with the host platform; retag to wasi_wasm32 and
	@# drop the original (--remove), mirroring ../cryptography.
	$(HOST_PYTHON) -m wheel tags --platform-tag wasi_wasm32 --remove \
	  $(BUILD_DIR)/dist/numpy-$(NUMPY_VERSION)-cp314-cp314-*.whl
	cp $(BUILD_DIR)/dist/numpy-$(NUMPY_VERSION)-cp314-cp314-wasi_wasm32.whl $(WHEEL)
	@printf '✓  numpy wheel: '; ls -lh $(WHEEL) | awk '{print $$5"  "$$NF}'

# =============================================================================
# clean
# =============================================================================

clean:
	rm -rf $(WORKDIR) $(OUT_DIR)
	@echo "kept: $(CACHE_DIR) (tarballs, keyed on upstream version)"
