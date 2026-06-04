# duckdb-python/Makefile — DuckDB Python wheel for wasm32-wasip2.
#
# Phase 2 of the DuckDB port (phase 1 = ../duckdb, the core static lib).
# Produces:
#
#   out/duckdb-1.5.3-cp314-cp314-wasi_wasm32.whl
#
# Build model (from upstream duckdb/duckdb-python, scikit-build-core +
# CMake + pybind11):
#
#   * The binding's CMake does `add_subdirectory(${DUCKDB_SOURCE_PATH})`
#     to BUILD DUCKDB CORE FROM SOURCE as part of the binding build —
#     there is no prebuilt-libduckdb_static.a shortcut. So this build
#     reconfigures + recompiles core (this time with a handful of
#     extensions ON), then compiles the pybind11 binding on top.
#     => ../duckdb/out/libduckdb_static.a is NOT reused here; phase 1's
#        value was proving core compiles on wasi + producing the three
#        porting patches, which this build reuses via ../duckdb/patches-duckdb.
#
#   * KEY FINDING that makes this tractable: the binding has NO
#     compile-time dependency on numpy / pandas / pyarrow / Arrow-C++.
#     Every "numpy"/"arrow" include in src/duckdb_py is duckdb's OWN
#     header (duckdb_python/numpy/..., duckdb/common/arrow/... — the
#     latter is DuckDB's Arrow C Data Interface, part of core) plus one
#     header-only <pybind11/numpy.h>. numpy/pandas/pyarrow are touched
#     only at RUNTIME through the Python C API, and are guarded behind
#     .df()/.arrow()/.fetch_numpy() etc. So a standalone wheel works for
#     .fetchall()-style usage; those methods raise ImportError if the
#     optional module isn't installed. This is why we can ship duckdb
#     before pandas/pyarrow wheels exist.
#
# Tagged cp314-cp314 (not abi3): pybind11 uses the full CPython C API.
#
#   make toolchain   host venv (pybind11) + cmake into .toolchain/
#   make configure   CMake configure only (cheap; validates cross-config)
#   make wheel       full build + wheel assembly
#   make all         toolchain → wheel
#   make clean       wipe $(WORKDIR) + out/ (keeps .cache/)
#
# Prerequisite: the wasm CPython 3.14 from ../cpython (libpython3.14.so
# + headers). The `cpython` phony recurses into ../cpython on demand.
#
# NOTE on machine load: this rebuilds DuckDB core + extensions + the
# pybind11 binding. pybind11 TUs are template-heavy (multi-GB each).
# Build parallelism is capped exactly like ../duckdb (explicit JOBS
# derived from cores AND memory) — never a bare `--parallel`.

THIS_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# ---- versions / sources --------------------------------------------------

# duckdb-python and duckdb core are released in lockstep; keep these equal.
DUCKDB_VERSION        := 1.5.3
DUCKDB_PYTHON_VERSION := 1.5.3

DUCKDB_PYTHON_TARBALL_URL := https://github.com/duckdb/duckdb-python/archive/refs/tags/v$(DUCKDB_PYTHON_VERSION).tar.gz
# Core source for external/duckdb. Same tarball the sibling ../duckdb
# subdir builds; we reuse its cached download + its porting patches.
DUCKDB_TARBALL_URL := https://github.com/duckdb/duckdb/archive/refs/tags/v$(DUCKDB_VERSION).tar.gz

PYBIND11_VERSION := 2.13.6
CMAKE_VERSION    := 3.30.5

# DuckDB extensions statically compiled into the wheel (no runtime
# download — they're linked into _duckdb.so and registered at startup).
# core's extension_build_tools.cmake reads BUILD_EXTENSIONS: a name with a
# .github/config/extensions/<name>.cmake is OUT-OF-TREE (fetched at a
# pinned ref, or from a local dir via env DUCKDB_<NAME>_DIRECTORY); else
# it's IN-TREE (built from external/duckdb/extension/<name>).
#   core_functions  date/string/math scalar+aggregate funcs (required)   [in-tree]
#   parquet         read/write parquet                                   [in-tree]
#   json            json scalar funcs + read_json                        [in-tree]
#   icu             timezones, collations, Unicode (bundles ICU data)    [in-tree]
#   inet            INET/IP address types + functions (no network)       [out-of-tree, 0 deps]
#   excel           read/write xlsx                                       [out-of-tree, +expat/zlib/minizip-ng]
#   httpfs          remote read over http(s) via wasi:http (NOT curl)     [out-of-tree, patched: see patches-httpfs/]
BUILD_EXTENSIONS := core_functions;parquet;json;icu;inet;excel;httpfs

# Out-of-tree extension sources. Pinned to the refs in v1.5.3's
# .github/config/extensions/<name>.cmake. We pre-fetch the GitHub archive
# tarball and point DuckDB at it via the DUCKDB_<NAME>_DIRECTORY env
# override, avoiding a git fetch during CMake configure (and the git
# dependency it implies).
INET_REF         := fe7f60bb60245197680fb07ecd1629a1dc3d91c8
INET_TARBALL_URL := https://github.com/duckdb/duckdb-inet/archive/$(INET_REF).tar.gz
EXCEL_REF        := f4c72b5ef04a03b3a78a95b5a2ee94ba93e3178d
EXCEL_TARBALL_URL := https://github.com/duckdb/duckdb-excel/archive/$(EXCEL_REF).tar.gz
HTTPFS_REF       := 52afb4204a3238d6ee132e83340f8d68c40ee91c
HTTPFS_TARBALL_URL := https://github.com/duckdb/duckdb-httpfs/archive/$(HTTPFS_REF).tar.gz

# httpfs needs source patching (curl/openssl → wasi:http transport) + our
# wasi:http client sources, so it gets a prepare step (patch + copy-in)
# beyond the plain tarball→DIRECTORY override the other out-of-tree exts use.
HTTPFS_PATCHES := $(wildcard $(THIS_DIR)patches-httpfs/*.patch)
WASI_HTTP_DIR  := $(THIS_DIR)../duckdb-httpfs-wasi

# excel's CMake does find_package(EXPAT/ZLIB/minizip-ng). We cross-build
# expat + minizip-ng for wasi into a private prefix and reuse cpython's
# zlib (libz.a + zlib.h), then hand excel that prefix via CMAKE_PREFIX_PATH
# + ZLIB_ROOT. minizip-ng is built zlib-only (no bzip2/lzma/zstd/openssl).
EXPAT_VERSION    := 2.6.4
EXPAT_URL        := https://github.com/libexpat/libexpat/releases/download/R_2_6_4/expat-$(EXPAT_VERSION).tar.gz
MINIZIP_VERSION  := 4.0.7
MINIZIP_URL      := https://github.com/zlib-ng/minizip-ng/archive/refs/tags/$(MINIZIP_VERSION).tar.gz

# ---- build parallelism (see ../duckdb/Makefile for the full rationale) ---
# Never a bare `--parallel`: with the Unix Makefiles generator that is
# `make -j` unbounded and OOMs. pybind11 TUs are even more memory-hungry
# than core's, so the memory cap matters more here.
NPROC    := $(shell nproc 2>/dev/null || echo 2)
MEM_GB   := $(shell awk '/MemTotal/ {printf "%d", $$2/1024/1024}' /proc/meminfo 2>/dev/null || echo 2)
MEM_JOBS := $(shell echo $$(( $(MEM_GB) / 2 )) )
MEM_JOBS := $(shell echo $$(( $(MEM_JOBS) < 1 ? 1 : $(MEM_JOBS) )) )
JOBS     ?= $(shell echo $$(( $(NPROC) < $(MEM_JOBS) ? $(NPROC) : $(MEM_JOBS) )) )

# ---- paths ---------------------------------------------------------------

WASI_SDK_PATH       ?= /opt/wasi-sdk
WASI_SYSROOT        := $(WASI_SDK_PATH)/share/wasi-sysroot
WASI_SYSROOT_SHARED := $(WASI_SYSROOT)/lib/wasm32-wasip2

# wasm CPython 3.14 (built by ../cpython). Mirrors the path convention
# in ../pyyaml/Makefile and ../cryptography/Makefile.
CPYTHON_DIR      := $(THIS_DIR)../cpython
CPYTHON_REV      := 0e13686da8bb881b059d35e23c32bcd2e6440099
CPYTHON_WASI_DIR := $(HOME)/cargo-target/cpython-$(CPYTHON_REV)/cpython/builddir/wasi
CPYTHON_INCLUDE  := $(CPYTHON_WASI_DIR)/install/include/python3.14
CPYTHON_LIB      := $(CPYTHON_WASI_DIR)/libpython3.14.so

# Cross-compiling pybind11 against a wasm CPython takes TWO things, for
# two different reasons (both needed):
#
#  1. _PYTHON_SYSCONFIGDATA_NAME + PYTHONPATH (set on the cmake env in
#     the configure recipe). pybind's classic FindPythonLibsNew runs the
#     HOST python to read EXT_SUFFIX + INCLUDEPY from sysconfig; pointing
#     it at the TARGET's cross-built sysconfigdata makes those come back
#     as the wasm values (.cpython-314-wasm32-wasi.so + the target
#     include dir) instead of the host's. (Pyodide uses the same trick.)
#
#  2. -DPYBIND11_USE_CROSSCOMPILING=ON. FindPythonLibsNew ALSO checks
#     pointer size via struct.calcsize('@P') — which is the *running*
#     (host) interpreter's 8, not sysconfig — and FATAL-errors on the
#     8-vs-4 mismatch. The sysconfig trick can't fix that (it's not a
#     config var). This flag sets _PYBIND11_CROSSCOMPILING (CMAKE_CROSS-
#     COMPILING is already true via the wasi toolchain), which skips the
#     size check. The size value is otherwise unused for a MODULE build.
#
#  3. -DPYBIND11_PYTHONLIBS_OVERWRITE=OFF + explicit PYTHON_INCLUDE_DIR /
#     PYTHON_MODULE_EXTENSION. The include dir pybind reads comes from
#     sysconfig.get_path('platinclude') — a SCHEME path tied to the
#     running (host) interpreter's prefix, which the sysconfigdata env
#     trick does NOT redirect (it only overrides get_config_var). With
#     OVERWRITE left ON (the default), pybind clobbers our explicit
#     PYTHON_INCLUDE_DIR with that host path and the binding TUs include
#     the host's pyport.h → "LONG_BIT definition appears wrong for
#     platform". OVERWRITE=OFF makes pybind keep the values we pass.
CPYTHON_SYSCONFIG_DIR  := $(CPYTHON_WASI_DIR)/install/lib/python3.14
CPYTHON_SYSCONFIG_NAME := _sysconfigdata__wasi_wasm32-wasi

# Sibling phase-1 subdir — we reuse its tarball cache + porting patches.
CORE_DIR     := $(THIS_DIR)../duckdb
CORE_PATCHES := $(wildcard $(CORE_DIR)/patches-duckdb/*.patch)

# Heavy trees under $HOME (virtiofs blocks utime during CMake builds);
# .cache + out stay in-tree.
WORKDIR       ?= $(HOME)/.cache/particle/wheels-duckdb-python
TOOLCHAIN_DIR := $(WORKDIR)/toolchain
BUILD_DIR     := $(WORKDIR)/build
CACHE_DIR     := $(THIS_DIR).cache
OUT_DIR       := $(THIS_DIR)out

TOOLCHAIN_BIN := $(TOOLCHAIN_DIR)/bin
CMAKE         := $(TOOLCHAIN_BIN)/cmake
HOST_PYVENV   := $(TOOLCHAIN_DIR)/pyvenv
HOST_PYTHON   := $(HOST_PYVENV)/bin/python

# Source trees.
DPY_SRC      := $(BUILD_DIR)/src/duckdb-python-$(DUCKDB_PYTHON_VERSION)
# external/duckdb inside the duckdb-python tree (loader's default
# DUCKDB_SOURCE_PATH). We extract core here and patch it.
CORE_SRC     := $(DPY_SRC)/external/duckdb
CMAKE_BUILD  := $(BUILD_DIR)/cmake-build

# Out-of-tree extension sources (extracted from pinned-ref tarballs).
INET_TARBALL  := $(CACHE_DIR)/duckdb-inet-$(INET_REF).tar.gz
INET_SRC      := $(BUILD_DIR)/ext-src/duckdb-inet-$(INET_REF)
EXCEL_TARBALL := $(CACHE_DIR)/duckdb-excel-$(EXCEL_REF).tar.gz
EXCEL_SRC     := $(BUILD_DIR)/ext-src/duckdb-excel-$(EXCEL_REF)
HTTPFS_TARBALL := $(CACHE_DIR)/duckdb-httpfs-$(HTTPFS_REF).tar.gz
HTTPFS_SRC     := $(BUILD_DIR)/ext-src/duckdb-httpfs-$(HTTPFS_REF)

# excel native-dep cross-builds (expat + minizip-ng) into a shared prefix.
EXCEL_DEPS_DIR    := $(BUILD_DIR)/excel-deps
EXCEL_DEPS_PREFIX := $(EXCEL_DEPS_DIR)/install
EXPAT_TARBALL     := $(CACHE_DIR)/expat-$(EXPAT_VERSION).tar.gz
EXPAT_SRC         := $(EXCEL_DEPS_DIR)/src/expat-$(EXPAT_VERSION)
MINIZIP_TARBALL   := $(CACHE_DIR)/minizip-ng-$(MINIZIP_VERSION).tar.gz
MINIZIP_SRC       := $(EXCEL_DEPS_DIR)/src/minizip-ng-$(MINIZIP_VERSION)
# cpython's wasi zlib (built by ../cpython); reused for excel + minizip-ng.
ZLIB_ROOT         := $(CPYTHON_WASI_DIR)/deps

# Cached tarballs (in-tree, survive `make clean`).
DPY_TARBALL  := $(CACHE_DIR)/duckdb-python-$(DUCKDB_PYTHON_VERSION).tar.gz
# Reuse the sibling's cached core tarball if present, else download our own.
CORE_TARBALL := $(CORE_DIR)/.cache/duckdb-$(DUCKDB_VERSION).tar.gz
CORE_TARBALL_LOCAL := $(CACHE_DIR)/duckdb-$(DUCKDB_VERSION).tar.gz

EXT_SUFFIX := .cpython-314-wasm32-wasi.so

WHEEL := $(OUT_DIR)/duckdb-$(DUCKDB_PYTHON_VERSION)-cp314-cp314-wasi_wasm32.whl

# Host arch for the CMake tarball.
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

# ---- compile / link flags ------------------------------------------------

WASI_EMULATED_DEFS := -D_WASI_EMULATED_SIGNAL \
                      -D_WASI_EMULATED_GETPID \
                      -D_WASI_EMULATED_PROCESS_CLOCKS \
                      -D_WASI_EMULATED_MMAN

WASI_EMULATED_LIBS := -lwasi-emulated-signal \
                      -lwasi-emulated-getpid \
                      -lwasi-emulated-process-clocks \
                      -lwasi-emulated-mman

# Applied to every TU (core + binding). Mirrors ../duckdb plus the wasm
# CPython include for the binding's Python.h.
DPY_CFLAGS := -fwasm-exceptions \
              -DDUCKDB_WASM=1 \
              -DFSST_MUST_ALIGN \
              $(WASI_EMULATED_DEFS) \
              -fPIC -O2 \
              -isystem $(CPYTHON_INCLUDE)

# Final _duckdb module link: the proven import-dynamic pattern from
# ../pyyaml / ../cffi — leave Python C API + libc symbols as env imports
# resolved at .so-load time by the embedding's dyld, rather than
# hard-linking libpython. Module libraries use CMAKE_MODULE_LINKER_FLAGS.
DPY_MODULE_LDFLAGS := -Wl,--no-entry \
                      -Wl,--unresolved-symbols=import-dynamic \
                      -Wl,--allow-undefined \
                      -L$(WASI_SYSROOT_SHARED) -Wl,-l:libc.so \
                      $(WASI_EMULATED_LIBS)

# ---- phony entry points --------------------------------------------------

.PHONY: all toolchain configure wheel clean cpython check-host-deps

all: wheel

check-host-deps:
	@command -v curl >/dev/null || { echo "ERROR: curl not on PATH"; exit 1; }
	@command -v tar  >/dev/null || { echo "ERROR: tar not on PATH";  exit 1; }
	@command -v patch >/dev/null || { echo "ERROR: patch not on PATH"; exit 1; }
	@command -v go   >/dev/null || { echo "ERROR: go not on PATH (zip-wheel.go)"; exit 1; }
	@command -v uv   >/dev/null || { echo "ERROR: uv not on PATH (host pybind11 venv)"; exit 1; }
	@test -d $(WASI_SDK_PATH) || { echo "ERROR: WASI_SDK_PATH=$(WASI_SDK_PATH) missing"; exit 1; }

# wasm CPython built on demand by ../cpython (its own up-to-date check).
cpython:
	$(MAKE) -C $(CPYTHON_DIR)

# =============================================================================
# Stage 1: toolchain (host pybind11 venv + cmake)
# =============================================================================

toolchain: $(CMAKE) $(HOST_PYTHON)

$(CMAKE_TARBALL): | check-host-deps
	@mkdir -p $(CACHE_DIR)
	curl -fsSL -o $@.tmp $(CMAKE_TARBALL_URL)
	mv $@.tmp $@

$(CMAKE): | $(CMAKE_TARBALL)
	@mkdir -p $(TOOLCHAIN_BIN)
	tar -xzf $(CMAKE_TARBALL) -C $(TOOLCHAIN_DIR) --strip-components=1
	@test -x $@ || (echo "cmake tarball didn't ship bin/cmake at $@"; exit 1)
	@$@ --version | head -1

# Host venv carries pybind11 only — we need its CMake config package
# (find_package(pybind11 CONFIG)) and its headers, NOT a target python.
$(HOST_PYTHON): | check-host-deps
	@mkdir -p $(TOOLCHAIN_DIR)
	uv venv --python 3.14 $(HOST_PYVENV)
	uv pip install --python $(HOST_PYTHON) pybind11==$(PYBIND11_VERSION)

# =============================================================================
# Stage 2: source fetch + extract + patch
# =============================================================================

$(DPY_TARBALL): | check-host-deps
	@mkdir -p $(CACHE_DIR)
	curl -fsSL -o $@.tmp $(DUCKDB_PYTHON_TARBALL_URL)
	mv $@.tmp $@

# Prefer the sibling ../duckdb cached core tarball; fall back to our own.
$(CORE_TARBALL_LOCAL): | check-host-deps
	@mkdir -p $(CACHE_DIR)
	@if [ -f "$(CORE_TARBALL)" ]; then \
	   echo "  reusing sibling core tarball $(CORE_TARBALL)"; \
	   cp "$(CORE_TARBALL)" $@; \
	 else \
	   echo "  downloading core tarball"; \
	   curl -fsSL -o $@.tmp $(DUCKDB_TARBALL_URL); mv $@.tmp $@; \
	 fi

# Extract duckdb-python, extract core into external/duckdb, apply the
# sibling's porting patches to core. Sentinel rebuilds on patch change.
$(BUILD_DIR)/.patched: $(DPY_TARBALL) $(CORE_TARBALL_LOCAL) $(CORE_PATCHES)
	@mkdir -p $(BUILD_DIR)/src
	rm -rf $(DPY_SRC)
	tar -xzf $(DPY_TARBALL) -C $(BUILD_DIR)/src
	@# external/duckdb is an (empty) submodule dir in the tarball; replace
	@# it with the real core source. The parent external/ already exists.
	rm -rf $(CORE_SRC)
	tar -xzf $(CORE_TARBALL_LOCAL) -C $(BUILD_DIR)/src
	@# core tarball unpacks as duckdb-$(DUCKDB_VERSION)/; move into place.
	mv $(BUILD_DIR)/src/duckdb-$(DUCKDB_VERSION) $(CORE_SRC)
	@for p in $(CORE_PATCHES); do \
	    echo "  applying core patch $$p"; \
	    (cd $(CORE_SRC) && patch -p1 --no-backup-if-mismatch < $$p) || exit 1; \
	done
	touch $@

# =============================================================================
# Stage 3: CMake configure
# =============================================================================
#
# Cross-config notes (the fiddly part — pybind11 against a wasm CPython):
#   * pybind11_DIR points find_package(pybind11 CONFIG) at the host
#     venv's installed config package.
#   * We hand pybind11/FindPython the TARGET (wasm) headers via
#     PYTHON_INCLUDE_DIR and force the module extension so the output is
#     named _duckdb.cpython-314-wasm32-wasi.so. PYTHON_EXECUTABLE is the
#     host 3.14 (only used by pybind for detection; nothing target-side
#     runs at build time).
#   * Python symbols are NOT linked — DPY_MODULE_LDFLAGS resolves them as
#     dynamic env imports at load (same model as the other wheels), so we
#     deliberately avoid pointing PYTHON_LIBRARY at a hard link.
#   * Core options mirror ../duckdb: threads off, jemalloc off, httplib
#     off, explicit platform, fake git version. Extension autoload/install
#     OFF (wasi can't fetch). BUILD_EXTENSIONS is our reduced set.

# Out-of-tree extension source fetch (GitHub archive at the pinned ref).
$(INET_TARBALL): | check-host-deps
	@mkdir -p $(CACHE_DIR)
	curl -fsSL -o $@.tmp $(INET_TARBALL_URL)
	mv $@.tmp $@

$(INET_SRC)/CMakeLists.txt: $(INET_TARBALL)
	@mkdir -p $(BUILD_DIR)/ext-src
	rm -rf $(INET_SRC)
	tar -xzf $(INET_TARBALL) -C $(BUILD_DIR)/ext-src
	@test -f $@ || { echo "ERROR: inet tarball missing CMakeLists.txt at $@"; exit 1; }
	touch $@

$(EXCEL_TARBALL): | check-host-deps
	@mkdir -p $(CACHE_DIR)
	curl -fsSL -o $@.tmp $(EXCEL_TARBALL_URL)
	mv $@.tmp $@

$(HTTPFS_TARBALL): | check-host-deps
	@mkdir -p $(CACHE_DIR)
	curl -fsSL -o $@.tmp $(HTTPFS_TARBALL_URL)
	mv $@.tmp $@

# httpfs: extract → apply wasi patches (curl/openssl out, wasi:http in) →
# copy our wasi:http transport + vendored wit-bindgen bindings into src/.
# Re-runs if a patch or the wasi client source changes.
HTTPFS_WASI_SRCS := $(WASI_HTTP_DIR)/wasi_http_client.cpp $(WASI_HTTP_DIR)/http_client.c $(WASI_HTTP_DIR)/http_client.h
$(HTTPFS_SRC)/.prepared: $(HTTPFS_TARBALL) $(HTTPFS_PATCHES) $(HTTPFS_WASI_SRCS)
	@mkdir -p $(BUILD_DIR)/ext-src
	rm -rf $(HTTPFS_SRC)
	tar -xzf $(HTTPFS_TARBALL) -C $(BUILD_DIR)/ext-src
	@for p in $(HTTPFS_PATCHES); do \
	    echo "  applying httpfs patch $$p"; \
	    (cd $(HTTPFS_SRC) && patch -p1 --no-backup-if-mismatch < $$p) || exit 1; \
	done
	cp $(HTTPFS_WASI_SRCS) $(HTTPFS_SRC)/src/
	touch $@

$(EXCEL_SRC)/CMakeLists.txt: $(EXCEL_TARBALL)
	@mkdir -p $(BUILD_DIR)/ext-src
	rm -rf $(EXCEL_SRC)
	tar -xzf $(EXCEL_TARBALL) -C $(BUILD_DIR)/ext-src
	@test -f $@ || { echo "ERROR: excel tarball missing CMakeLists.txt at $@"; exit 1; }
	touch $@

# ---- excel native deps: expat + minizip-ng (wasi static libs) -----------
# expat: XML_POOR_ENTROPY + all real entropy sources off — wasi-libc has
# no arc4random/getrandom and the cross-compile feature checks false-
# positive on arc4random_buf. Entropy quality is irrelevant for xlsx.
EXCEL_DEPS_CFLAGS := -fPIC -D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_GETPID -D_WASI_EMULATED_PROCESS_CLOCKS -D_WASI_EMULATED_MMAN

$(EXPAT_TARBALL): | check-host-deps
	@mkdir -p $(CACHE_DIR)
	curl -fsSL -o $@.tmp $(EXPAT_URL)
	mv $@.tmp $@

$(EXPAT_SRC)/CMakeLists.txt: $(EXPAT_TARBALL)
	@mkdir -p $(EXCEL_DEPS_DIR)/src
	rm -rf $(EXPAT_SRC)
	tar -xzf $(EXPAT_TARBALL) -C $(EXCEL_DEPS_DIR)/src
	touch $@

$(EXCEL_DEPS_PREFIX)/lib/libexpat.a: $(EXPAT_SRC)/CMakeLists.txt | $(CMAKE)
	$(CMAKE) -S $(EXPAT_SRC) -B $(EXPAT_SRC)/build-wasi \
	    -DCMAKE_TOOLCHAIN_FILE=$(WASI_SDK_PATH)/share/cmake/wasi-sdk-p2.cmake \
	    -DCMAKE_INSTALL_PREFIX=$(EXCEL_DEPS_PREFIX) -DCMAKE_BUILD_TYPE=Release \
	    -DEXPAT_BUILD_TESTS=OFF -DEXPAT_BUILD_EXAMPLES=OFF -DEXPAT_BUILD_TOOLS=OFF \
	    -DEXPAT_BUILD_DOCS=OFF -DEXPAT_SHARED_LIBS=OFF \
	    -DEXPAT_WITH_GETRANDOM=OFF -DEXPAT_WITH_SYS_GETRANDOM=OFF \
	    -DEXPAT_DEV_URANDOM=OFF -DEXPAT_WITH_LIBBSD=OFF \
	    -DHAVE_ARC4RANDOM_BUF=OFF -DHAVE_ARC4RANDOM=OFF \
	    -DCMAKE_C_FLAGS='$(EXCEL_DEPS_CFLAGS) -DXML_POOR_ENTROPY'
	$(CMAKE) --build $(EXPAT_SRC)/build-wasi --parallel $(JOBS)
	$(CMAKE) --install $(EXPAT_SRC)/build-wasi
	@printf '✓  expat: '; ls -lh $@ | awk '{print $$5"  "$$NF}'

$(MINIZIP_TARBALL): | check-host-deps
	@mkdir -p $(CACHE_DIR)
	curl -fsSL -o $@.tmp $(MINIZIP_URL)
	mv $@.tmp $@

$(MINIZIP_SRC)/CMakeLists.txt: $(MINIZIP_TARBALL)
	@mkdir -p $(EXCEL_DEPS_DIR)/src
	rm -rf $(MINIZIP_SRC)
	tar -xzf $(MINIZIP_TARBALL) -C $(EXCEL_DEPS_DIR)/src
	touch $@

# minizip-ng needs zlib (cpython's). zlib-only backend.
$(EXCEL_DEPS_PREFIX)/lib/libminizip-ng.a: $(MINIZIP_SRC)/CMakeLists.txt | $(CMAKE) cpython
	$(CMAKE) -S $(MINIZIP_SRC) -B $(MINIZIP_SRC)/build-wasi \
	    -DCMAKE_TOOLCHAIN_FILE=$(WASI_SDK_PATH)/share/cmake/wasi-sdk-p2.cmake \
	    -DCMAKE_INSTALL_PREFIX=$(EXCEL_DEPS_PREFIX) -DCMAKE_BUILD_TYPE=Release \
	    -DBUILD_SHARED_LIBS=OFF -DMZ_COMPAT=OFF \
	    -DMZ_BZIP2=OFF -DMZ_LZMA=OFF -DMZ_ZSTD=OFF -DMZ_OPENSSL=OFF -DMZ_LIBBSD=OFF \
	    -DMZ_PKCRYPT=OFF -DMZ_WZAES=OFF -DMZ_ICONV=OFF \
	    -DMZ_FETCH_LIBS=OFF -DMZ_FORCE_FETCH_LIBS=OFF -DMZ_ZLIB=ON \
	    -DZLIB_ROOT=$(ZLIB_ROOT) -DZLIB_LIBRARY=$(ZLIB_ROOT)/lib/libz.a \
	    -DZLIB_INCLUDE_DIR=$(ZLIB_ROOT)/include \
	    -DCMAKE_C_FLAGS='$(EXCEL_DEPS_CFLAGS)'
	$(CMAKE) --build $(MINIZIP_SRC)/build-wasi --parallel $(JOBS)
	$(CMAKE) --install $(MINIZIP_SRC)/build-wasi
	@printf '✓  minizip-ng: '; ls -lh $@ | awk '{print $$5"  "$$NF}'

EXCEL_DEPS := $(EXCEL_DEPS_PREFIX)/lib/libexpat.a $(EXCEL_DEPS_PREFIX)/lib/libminizip-ng.a

configure: $(CMAKE_BUILD)/CMakeCache.txt

$(CMAKE_BUILD)/CMakeCache.txt: $(BUILD_DIR)/.patched $(INET_SRC)/CMakeLists.txt $(EXCEL_SRC)/CMakeLists.txt $(EXCEL_DEPS) $(HTTPFS_SRC)/.prepared $(CMAKE) $(HOST_PYTHON) | cpython
	@mkdir -p $(CMAKE_BUILD)
	@PYBIND11_DIR=$$($(HOST_PYTHON) -m pybind11 --cmakedir); \
	 echo "  pybind11 cmakedir: $$PYBIND11_DIR"; \
	 cd $(CMAKE_BUILD) && \
	 _PYTHON_SYSCONFIGDATA_NAME=$(CPYTHON_SYSCONFIG_NAME) \
	 PYTHONPATH=$(CPYTHON_SYSCONFIG_DIR) \
	 DUCKDB_INET_DIRECTORY=$(INET_SRC) \
	 DUCKDB_EXCEL_DIRECTORY=$(EXCEL_SRC) \
	 DUCKDB_HTTPFS_DIRECTORY=$(HTTPFS_SRC) \
	 $(CMAKE) $(DPY_SRC) \
	    -DCMAKE_TOOLCHAIN_FILE=$(WASI_SDK_PATH)/share/cmake/wasi-sdk-p2.cmake \
	    -DCMAKE_PROJECT_INCLUDE=$(THIS_DIR)cmake-wasi/wasi-link-features.cmake \
	    -DCMAKE_BUILD_TYPE=Release \
	    -DCMAKE_PREFIX_PATH=$(EXCEL_DEPS_PREFIX) \
	    -DZLIB_ROOT=$(ZLIB_ROOT) \
	    -Dpybind11_DIR=$$PYBIND11_DIR \
	    -DPYTHON_EXECUTABLE=$(HOST_PYTHON) \
	    -DPYTHON_INCLUDE_DIR=$(CPYTHON_INCLUDE) \
	    -DPYTHON_MODULE_EXTENSION=$(EXT_SUFFIX) \
	    -DPYBIND11_FINDPYTHON=OFF \
	    -DPYBIND11_USE_CROSSCOMPILING=ON \
	    -DPYBIND11_PYTHONLIBS_OVERWRITE=OFF \
	    -DDUCKDB_SOURCE_PATH=$(CORE_SRC) \
	    -DBUILD_EXTENSIONS='$(BUILD_EXTENSIONS)' \
	    -DENABLE_EXTENSION_AUTOLOADING=OFF \
	    -DENABLE_EXTENSION_AUTOINSTALL=OFF \
	    -DDISABLE_THREADS=ON \
	    -DENABLE_JEMALLOC=OFF \
	    -DDISABLE_BUILTIN_HTTPLIB=ON \
	    -DDUCKDB_EXPLICIT_PLATFORM=wasi_wasm32 \
	    -DOVERRIDE_GIT_DESCRIBE=v$(DUCKDB_VERSION) \
	    -DCMAKE_C_FLAGS='$(DPY_CFLAGS)' \
	    -DCMAKE_CXX_FLAGS='$(DPY_CFLAGS)' \
	    -DCMAKE_MODULE_LINKER_FLAGS='$(DPY_MODULE_LDFLAGS)' \
	    -DCMAKE_SHARED_LINKER_FLAGS='$(DPY_MODULE_LDFLAGS)'

# =============================================================================
# Stage 4: build the _duckdb module
# =============================================================================

$(BUILD_DIR)/_duckdb$(EXT_SUFFIX): $(CMAKE_BUILD)/CMakeCache.txt
	@echo "  building _duckdb with JOBS=$(JOBS) (nproc=$(NPROC), mem=$(MEM_GB)GB)"
	$(CMAKE) --build $(CMAKE_BUILD) --parallel $(JOBS) --target _duckdb
	@# Locate the built module (pybind names it _duckdb<EXT_SUFFIX>).
	@found=$$(find $(CMAKE_BUILD) -name '_duckdb*.so' | head -1); \
	 test -n "$$found" || { echo "ERROR: _duckdb module not found under $(CMAKE_BUILD)"; exit 1; }; \
	 cp "$$found" $@; \
	 printf '✓  _duckdb module: '; ls -lh $@ | awk '{print $$5"  "$$NF}'

# =============================================================================
# Stage 5: wheel assembly
# =============================================================================
# Layout (matches PyPI duckdb wheels, minus optional adbc/stubs for v0):
#   _duckdb.cpython-314-wasm32-wasi.so   compiled pybind11 module (root)
#   duckdb/                              pure-Python package (imports _duckdb)

wheel: $(WHEEL)

$(WHEEL): $(BUILD_DIR)/_duckdb$(EXT_SUFFIX)
	@rm -rf $(BUILD_DIR)/wheel
	@mkdir -p $(BUILD_DIR)/wheel/duckdb
	@mkdir -p $(BUILD_DIR)/wheel/duckdb-$(DUCKDB_PYTHON_VERSION).dist-info
	cp -r $(DPY_SRC)/duckdb/. $(BUILD_DIR)/wheel/duckdb/
	cp $(BUILD_DIR)/_duckdb$(EXT_SUFFIX) $(BUILD_DIR)/wheel/_duckdb$(EXT_SUFFIX)
	find $(BUILD_DIR)/wheel -name '__pycache__' -type d -exec rm -rf {} +
	printf 'Metadata-Version: 2.1\nName: duckdb\nVersion: %s\nSummary: DuckDB analytical database, cross-built for wasm32-wasip2 (pybind11 binding; numpy/pandas/pyarrow optional at runtime)\nRequires-Python: >=3.10\nLicense: MIT\n' "$(DUCKDB_PYTHON_VERSION)" \
	  > $(BUILD_DIR)/wheel/duckdb-$(DUCKDB_PYTHON_VERSION).dist-info/METADATA
	printf 'Wheel-Version: 1.0\nGenerator: particle-wheels-duckdb 0.1\nRoot-Is-Purelib: false\nTag: cp314-cp314-wasi_wasm32\n' \
	  > $(BUILD_DIR)/wheel/duckdb-$(DUCKDB_PYTHON_VERSION).dist-info/WHEEL
	@mkdir -p $(OUT_DIR)
	cd $(THIS_DIR) && go run zip-wheel.go $(BUILD_DIR)/wheel $(WHEEL)
	@printf '✓  duckdb wheel: '; ls -lh $(WHEEL) | awk '{print $$5"  "$$NF}'

# =============================================================================
# clean
# =============================================================================

clean:
	rm -rf $(WORKDIR) $(OUT_DIR)
	@echo "kept: $(CACHE_DIR) (tarballs, keyed on upstream version)"
