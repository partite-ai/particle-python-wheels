# duckdb/Makefile — cross-build DuckDB as a static lib for wasm32-wasip2.
#
# This is *not* a wheel build — phase 1 produces only:
#
#   out/libduckdb_static.a            single static archive
#   out/include/duckdb.h              public C API header
#   out/include/duckdb.hpp            public C++ API header
#   out/include/duckdb/...            transitive C++ headers
#
# Phase 2 (separate subdir, future work) will compile DuckDB's
# tools/pythonpkg against this archive + the wasm CPython to produce
# the duckdb-*.whl that pip can install.
#
# Build configuration mirrors duckdb-wasm's MVP bundle, retargeted off
# Emscripten onto wasi-sdk:
#
#   -fwasm-exceptions      DuckDB's error model is exceptions; EH-bundle
#                          equivalent (wasi-sdk libc++ supports this since
#                          wasi-sdk 25+).
#   -DDUCKDB_NO_THREADS    excludes src/parallel/ threadpool code at
#                          compile time; the runtime falls back to
#                          single-threaded TaskScheduler. (Set via
#                          CMake's DISABLE_THREADS option.)
#   -DFSST_MUST_ALIGN      vendored fsst string-compression code path
#                          that assumes unaligned access is not safe;
#                          matches the duckdb-wasm define.
#   -D_WASI_EMULATED_*     wasi-libc emulation knobs DuckDB needs:
#                          mman (block_allocator does anonymous mmap),
#                          signal/getpid/process_clocks (incidental).
#
# Patches in patches-duckdb/ are applied with `patch -p1` after extract:
#
#   0001-printer-wasi-terminal-width.patch — Printer::TerminalWidth()
#     uses ioctl(TIOCGWINSZ); wasi-libc's <sys/ioctl.h> has neither
#     struct winsize nor TIOCGWINSZ. Return a fixed 80 cols on __wasi__.
#
#   0002-local-file-system-wasi-no-fcntl-locks.patch — the open() path's
#     advisory file-locking block uses fcntl(F_SETLK/F_GETLK) with
#     F_RDLCK/F_WRLCK, none of which wasi-libc defines. A wasi embedding
#     is single-process, so the lock is a no-op; guard the block out
#     under __wasi__.
#
# NOTE for future version bumps: v1.5.3's storage layer only has the
# pread/pwrite path (LocalFileSystem). The MMAP-backed storage path
# (DatabaseHandle::OpenMemoryMap) landed on main *after* v1.5.3. When
# bumping to a release that ships it, add a patch that early-throws on
# __wasi__ — wasi-libc's mmap is a malloc+pread stub with no MAP_SHARED
# writeback, so the MMAP io_mode would silently lose writes.
#
# Usage:
#   make toolchain     bootstrap cmake into .toolchain/
#   make libduckdb     cross-build libduckdb_static.a
#   make all           toolchain → libduckdb
#   make clean         wipe $(WORKDIR) + out/ (keeps .cache/)

THIS_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# ---- versions / sources --------------------------------------------------

# Pin to v1.5.3 — same DuckDB release duckdb-wasm currently bundles, so
# we inherit their de-facto "this compiles to wasm" validation.
DUCKDB_VERSION     := 1.5.3
DUCKDB_TARBALL_URL := https://github.com/duckdb/duckdb/archive/refs/tags/v$(DUCKDB_VERSION).tar.gz

CMAKE_VERSION := 3.30.5

# ---- build parallelism ---------------------------------------------------
#
# CRITICAL: never pass `--parallel` with no argument. With the Unix
# Makefiles generator (the wasi-sdk toolchain file picks no generator,
# so CMake defaults to it) a bare `--parallel` becomes `make -j` with
# NO job limit — unbounded concurrent compiles. DuckDB's unity-build
# translation units are 1–2 GB of RAM *each* to compile, so an
# unbounded -j OOMs and wedges the machine regardless of core count.
#
# So: always an explicit number. Default to the smaller of nproc and a
# memory-derived cap (≈1 job per 2 GB of RAM, since unity TUs are the
# memory bound, not the core bound). Override with `make JOBS=N`.
#
# On very low-memory machines also consider DISABLE_UNITY=ON in the
# CMake invocation below — that trades many-small-TUs (lower peak
# memory each) for a longer wall-clock build.
NPROC    := $(shell nproc 2>/dev/null || echo 2)
MEM_GB   := $(shell awk '/MemTotal/ {printf "%d", $$2/1024/1024}' /proc/meminfo 2>/dev/null || echo 2)
MEM_JOBS := $(shell echo $$(( $(MEM_GB) / 2 )) )
# clamp MEM_JOBS to at least 1
MEM_JOBS := $(shell echo $$(( $(MEM_JOBS) < 1 ? 1 : $(MEM_JOBS) )) )
JOBS     ?= $(shell echo $$(( $(NPROC) < $(MEM_JOBS) ? $(NPROC) : $(MEM_JOBS) )) )

# ---- paths ---------------------------------------------------------------

WASI_SDK_PATH ?= /opt/wasi-sdk
WASI_SYSROOT  := $(WASI_SDK_PATH)/share/wasi-sysroot

# Heavy build trees live under $HOME (not the workspace) — virtiofs in
# the dev container blocks utime(), and DuckDB's CMake-driven build
# touches a lot of timestamps. .cache/ stays in-tree (just the tarball)
# so a `make clean` doesn't re-download.
WORKDIR       ?= $(HOME)/.cache/particle/wheels-duckdb
TOOLCHAIN_DIR := $(WORKDIR)/toolchain
BUILD_DIR     := $(WORKDIR)/build
CACHE_DIR     := $(THIS_DIR).cache
OUT_DIR       := $(THIS_DIR)out

TOOLCHAIN_BIN := $(TOOLCHAIN_DIR)/bin
CMAKE         := $(TOOLCHAIN_BIN)/cmake

DUCKDB_TARBALL := $(CACHE_DIR)/duckdb-$(DUCKDB_VERSION).tar.gz
DUCKDB_SRC     := $(BUILD_DIR)/src/duckdb-$(DUCKDB_VERSION)
DUCKDB_BUILD   := $(BUILD_DIR)/duckdb-build
DUCKDB_PREFIX  := $(BUILD_DIR)/duckdb-install

# Final outputs.
LIBDUCKDB_STATIC := $(OUT_DIR)/libduckdb_static.a
DUCKDB_H_OUT     := $(OUT_DIR)/include/duckdb.h

# Host arch detection — Kitware ships per-arch CMake tarballs.
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

# ---- compile flags -------------------------------------------------------
#
# These are appended to whatever DuckDB's CMakeLists picks. The base
# wasi cross-compile bits (--target, --sysroot, CC, AR, RANLIB) come
# from the wasi-sdk-p2.cmake toolchain file.
#
# DUCKDB_WASM=1 mirrors duckdb-wasm's CMake — DuckDB has a handful of
# `#ifdef DUCKDB_WASM` guards (single-threaded scheduler fallback paths,
# extension-loading shortcuts) that we want active.

WASI_EMULATED_DEFS := -D_WASI_EMULATED_SIGNAL \
                      -D_WASI_EMULATED_GETPID \
                      -D_WASI_EMULATED_PROCESS_CLOCKS \
                      -D_WASI_EMULATED_MMAN

WASI_EMULATED_LIBS := -lwasi-emulated-signal \
                      -lwasi-emulated-getpid \
                      -lwasi-emulated-process-clocks \
                      -lwasi-emulated-mman

# -mllvm -wasm-use-legacy-eh=false: emit the standardized exception-handling
# proposal (try_table/throw_ref) instead of LLVM's default legacy EH. Same
# C++ runtime ABI, pure codegen change. (See ../duckdb-python/Makefile.)
DUCKDB_EXTRA_CFLAGS := -fwasm-exceptions -mllvm -wasm-use-legacy-eh=false \
                       -DDUCKDB_WASM=1 \
                       -DFSST_MUST_ALIGN \
                       $(WASI_EMULATED_DEFS) \
                       -fPIC -O2

# ---- phony entry points --------------------------------------------------

.PHONY: all toolchain libduckdb clean check-host-deps

all: libduckdb

check-host-deps:
	@command -v curl >/dev/null || { echo "ERROR: curl not on PATH"; exit 1; }
	@command -v tar  >/dev/null || { echo "ERROR: tar not on PATH";  exit 1; }
	@command -v patch >/dev/null || { echo "ERROR: patch not on PATH"; exit 1; }
	@test -d $(WASI_SDK_PATH)    || { echo "ERROR: WASI_SDK_PATH=$(WASI_SDK_PATH) missing"; exit 1; }
	@test -f $(WASI_SDK_PATH)/share/cmake/wasi-sdk-p2.cmake \
	  || { echo "ERROR: wasi-sdk-p2.cmake missing under $(WASI_SDK_PATH)/share/cmake/"; exit 1; }

# =============================================================================
# Stage 1: toolchain bootstrap (CMake)
# =============================================================================
#
# Kitware ships a portable self-contained tarball. We extract it into
# $(TOOLCHAIN_DIR) so $(CMAKE) lands at $(TOOLCHAIN_BIN)/cmake without
# polluting the host PATH.

toolchain: $(CMAKE)

$(CMAKE_TARBALL): | check-host-deps
	@mkdir -p $(CACHE_DIR)
	curl -fsSL -o $@.tmp $(CMAKE_TARBALL_URL)
	mv $@.tmp $@

# Order-only on the tarball: when $(CMAKE) survives in TOOLCHAIN_DIR we
# don't want a missing .cache/ tarball to re-download + re-extract.
$(CMAKE): | $(CMAKE_TARBALL)
	@mkdir -p $(TOOLCHAIN_BIN)
	tar -xzf $(CMAKE_TARBALL) -C $(TOOLCHAIN_DIR) --strip-components=1
	@test -x $@ || (echo "cmake tarball didn't ship bin/cmake at $@"; exit 1)
	@$@ --version | head -1

# =============================================================================
# Stage 2: DuckDB source fetch + extract + patches
# =============================================================================

$(DUCKDB_TARBALL): | check-host-deps
	@mkdir -p $(CACHE_DIR)
	curl -fsSL -o $@.tmp $(DUCKDB_TARBALL_URL)
	mv $@.tmp $@

# Sentinel: src extracted + patches applied. Re-extract whenever the
# tarball changes (version bump) OR any patch under patches-duckdb/
# changes — patches are applied with `patch -p1` so editing one means
# re-applying against a fresh tree.
$(BUILD_DIR)/.patched: $(DUCKDB_TARBALL) $(wildcard $(THIS_DIR)patches-duckdb/*.patch)
	@mkdir -p $(BUILD_DIR)/src
	rm -rf $(DUCKDB_SRC)
	tar -xzf $(DUCKDB_TARBALL) -C $(BUILD_DIR)/src
	@for p in $(wildcard $(THIS_DIR)patches-duckdb/*.patch); do \
	    echo "  applying $$p"; \
	    (cd $(DUCKDB_SRC) && patch -p1 --no-backup-if-mismatch < $$p) || exit 1; \
	done
	touch $@

# =============================================================================
# Stage 3: CMake configure + build + install
# =============================================================================
#
# Knobs explained:
#
#   wasi-sdk-p2.cmake          sets CC/AR/RANLIB, sysroot, CMAKE_SYSTEM_*
#                              for wasm32-wasip2 cross-compile.
#   DUCKDB_EXPLICIT_PLATFORM   skip the duckdb_platform_binary step —
#                              that target would be a wasm binary we
#                              can't execute on the host. Value matches
#                              the wheel platform tag style.
#   OVERRIDE_GIT_DESCRIBE      tarball has no .git, so the version
#                              regex fallback would fail; supply a fake.
#   DISABLE_THREADS=ON         excludes parallel/ threadpool — wasip2
#                              has no std::thread, and TaskScheduler
#                              degrades to single-threaded execution.
#   DISABLE_BUILTIN_EXTENSIONS, DISABLE_EXTENSION_LOAD
#                              v0 ships core DuckDB only. Loadable
#                              extensions (httpfs, etc.) live in a
#                              separate repo and would each be their
#                              own port; defer.
#   DISABLE_BUILTIN_HTTPLIB    drops vendored httplib + mbedtls TLS path.
#                              No HTTP in v0 — wasi:http story is TBD.
#   ENABLE_JEMALLOC=OFF        jemalloc's build assumes pthreads + a
#                              real malloc replacement path neither of
#                              which is available on wasi.
#   ENABLE_SANITIZER=OFF       DuckDB's option default is ON (it expects
#                              callers to know to turn it off for release
#                              builds); we hard-off.
#   BUILD_SHELL=OFF, BUILD_UNITTESTS=OFF, BUILD_BENCHMARKS=OFF
#                              we want only libduckdb_static.a.

libduckdb: $(LIBDUCKDB_STATIC) $(DUCKDB_H_OUT)
	@printf '✓  libduckdb_static.a: '
	@ls -lh $(LIBDUCKDB_STATIC) | awk '{print $$5"  "$$NF}'

$(LIBDUCKDB_STATIC) $(DUCKDB_H_OUT) &: $(BUILD_DIR)/.patched | $(CMAKE)
	@mkdir -p $(DUCKDB_BUILD) $(DUCKDB_PREFIX) $(OUT_DIR)/include
	cd $(DUCKDB_BUILD) && $(CMAKE) $(DUCKDB_SRC) \
	    -DCMAKE_TOOLCHAIN_FILE=$(WASI_SDK_PATH)/share/cmake/wasi-sdk-p2.cmake \
	    -DCMAKE_INSTALL_PREFIX=$(DUCKDB_PREFIX) \
	    -DCMAKE_BUILD_TYPE=Release \
	    -DBUILD_SHARED_LIBS=OFF \
	    -DBUILD_SHELL=OFF \
	    -DBUILD_UNITTESTS=OFF \
	    -DBUILD_BENCHMARKS=OFF \
	    -DENABLE_SANITIZER=OFF \
	    -DENABLE_UBSAN=OFF \
	    -DENABLE_JEMALLOC=OFF \
	    -DDISABLE_THREADS=ON \
	    -DDISABLE_BUILTIN_EXTENSIONS=ON \
	    -DDISABLE_EXTENSION_LOAD=ON \
	    -DDISABLE_BUILTIN_HTTPLIB=ON \
	    -DDUCKDB_EXPLICIT_PLATFORM=wasi_wasm32 \
	    -DOVERRIDE_GIT_DESCRIBE=v$(DUCKDB_VERSION) \
	    -DCMAKE_C_FLAGS='$(DUCKDB_EXTRA_CFLAGS)' \
	    -DCMAKE_CXX_FLAGS='$(DUCKDB_EXTRA_CFLAGS)' \
	    -DCMAKE_EXE_LINKER_FLAGS='$(WASI_EMULATED_LIBS)' \
	    -DCMAKE_SHARED_LINKER_FLAGS='$(WASI_EMULATED_LIBS)'
	@echo "  building with JOBS=$(JOBS) (nproc=$(NPROC), mem=$(MEM_GB)GB → mem-cap=$(MEM_JOBS))"
	$(CMAKE) --build $(DUCKDB_BUILD) --parallel $(JOBS) --target duckdb_static
	@# DuckDB's CMake produces libduckdb_static.a in src/ under the build
	@# dir. We copy directly instead of `cmake --install` because the
	@# install target also tries to install the (disabled) shell + test
	@# binaries.
	cp $(DUCKDB_BUILD)/src/libduckdb_static.a $(LIBDUCKDB_STATIC)
	@# Public headers: the C API single-header (duckdb.h) plus the C++
	@# header tree under src/include/duckdb/ and the top-level duckdb.hpp.
	cp $(DUCKDB_SRC)/src/include/duckdb.h   $(OUT_DIR)/include/duckdb.h
	cp $(DUCKDB_SRC)/src/include/duckdb.hpp $(OUT_DIR)/include/duckdb.hpp
	cp -r $(DUCKDB_SRC)/src/include/duckdb  $(OUT_DIR)/include/duckdb

# =============================================================================
# clean
# =============================================================================

clean:
	rm -rf $(WORKDIR) $(OUT_DIR)
	@echo "kept: $(CACHE_DIR) (tarballs, keyed on upstream version)"
