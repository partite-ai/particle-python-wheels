# wheels/cpython/Makefile — wasi cross-build of CPython 3.14.
#
# Produces the artifacts the cffi / cryptography wheel builds need:
#
#   $(CPYTHON_WASI_DIR)/libpython3.14.so          shared libpython
#   $(CPYTHON_WASI_DIR)/install/include/python3.14/  CPython headers
#
# Build inputs are fetched at runtime from upstream:
#   - https://github.com/dicej/cpython/archive/$(CPYTHON_REV).tar.gz
#     (dicej's CPython fork with wasi32 / dynamic-linking patches)
#   - zlib  $(ZLIB_VERSION).tar.gz       — static lib, into deps/
#   - sqlite-autoconf-$(SQLITE_VERSION)  — static lib, into deps/
#
# WARNING — recipe duplication.
# This file currently mirrors the python-lib target in the repo
# root's Makefile (everything from `CPYTHON_REV :=` down through
# `$(PYTHON_LIB_SO):`). Both write to the same shared output tree
# ($(HOME)/cargo-target/cpython-$(CPYTHON_REV)/), so in a monorepo
# checkout whichever runs first wins and the other detects
# "already built" and skips. The duplication is intentional: when
# wheels/ moves to its own repo, this Makefile is what travels with
# it — the main repo keeps its own copy. Keep the two in sync until
# the split happens.
#
# Outputs (consumed by ../cffi/Makefile and ../cryptography/Makefile):
#   $(PYTHON_LIB_SO)              libpython3.14.so
#   $(CPYTHON_INCLUDE)            include/python3.14/
#
# Usage:
#   make          # build everything (libpython3.14.so + headers)
#   make clean    # wipe build trees; keep downloaded tarballs

THIS_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

WASI_SDK_PATH       ?= /opt/wasi-sdk
WASI_SYSROOT        := $(WASI_SDK_PATH)/share/wasi-sysroot
WASI_SYSROOT_SHARED := $(WASI_SYSROOT)/lib/wasm32-wasip2

# Versions / source URLs.
CPYTHON_REV    := 0e13686da8bb881b059d35e23c32bcd2e6440099
ZLIB_VERSION   := 1.3.1
SQLITE_VERSION := 3510200
SQLITE_YEAR    := 2026

# Cache + build trees live under $HOME (not the workspace) because the
# CPython / autotools builds write through utime() during install,
# which virtiofs in the dev container blocks. Cache holds downloaded
# tarballs; PYBUILD_DIR holds extracted sources + builddirs.
CACHE_DIR         := $(HOME)/.cache/particle/cpython-build
CARGO_TARGET_BASE := $(HOME)/cargo-target
PYBUILD_DIR       := $(CARGO_TARGET_BASE)/cpython-$(CPYTHON_REV)

CPYTHON_TARBALL := $(CACHE_DIR)/cpython-$(CPYTHON_REV).tar.gz
ZLIB_TARBALL    := $(CACHE_DIR)/zlib-$(ZLIB_VERSION).tar.gz
SQLITE_TARBALL  := $(CACHE_DIR)/sqlite-autoconf-$(SQLITE_VERSION).tar.gz

CPYTHON_SRC        := $(PYBUILD_DIR)/cpython
ZLIB_SRC           := $(PYBUILD_DIR)/zlib-$(ZLIB_VERSION)
SQLITE_SRC         := $(PYBUILD_DIR)/sqlite-autoconf-$(SQLITE_VERSION)
CPYTHON_NATIVE_DIR := $(CPYTHON_SRC)/builddir/build
CPYTHON_WASI_DIR   := $(CPYTHON_SRC)/builddir/wasi
CPYTHON_DEPS_DIR   := $(CPYTHON_WASI_DIR)/deps

PYTHON_LIB_SO   := $(CPYTHON_WASI_DIR)/libpython3.14.so
CPYTHON_INCLUDE := $(CPYTHON_WASI_DIR)/install/include/python3.14

# Cross-build flags for the static deps (zlib, sqlite) we link into
# libpython3.14.so. wasi-emulated-signal is needed because zlib's
# build assumes raise() / sigaction(); wasi-emulated-process-clocks
# similarly papers over sqlite's gettimeofday paths.
DEPS_CFLAGS    := --target=wasm32-wasi --sysroot=$(WASI_SYSROOT) -I$(WASI_SYSROOT)/include/wasm32-wasip1 -D_WASI_EMULATED_SIGNAL -fPIC
DEPS_LDFLAGS   := --target=wasm32-wasip2 --sysroot=$(WASI_SYSROOT) -L$(WASI_SYSROOT)/lib -lwasi-emulated-signal
SQLITE_CFLAGS  := --target=wasm32-wasi --sysroot=$(WASI_SYSROOT) -I$(WASI_SYSROOT)/include/wasm32-wasip1 -D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_PROCESS_CLOCKS -fPIC -O2 -DSQLITE_OMIT_WAL -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_OMIT_LOCALTIME -DSQLITE_OMIT_RANDOMNESS -DSQLITE_OMIT_SHARED_CACHE
SQLITE_LDFLAGS := --target=wasm32-wasip2 --sysroot=$(WASI_SYSROOT) -L$(WASI_SYSROOT)/lib

.PHONY: all clean
all: $(PYTHON_LIB_SO)
	@printf '✓  '; ls -lh $(PYTHON_LIB_SO) | awk '{print $$5"  "$$NF}'

# ---- source downloads (cached) ----

$(CPYTHON_TARBALL):
	@mkdir -p $(CACHE_DIR)
	curl -fsSL -o $@.tmp \
	  https://github.com/dicej/cpython/archive/$(CPYTHON_REV).tar.gz
	mv $@.tmp $@

$(ZLIB_TARBALL):
	@mkdir -p $(CACHE_DIR)
	curl -fsSL -o $@.tmp \
	  https://github.com/madler/zlib/releases/download/v$(ZLIB_VERSION)/zlib-$(ZLIB_VERSION).tar.gz
	mv $@.tmp $@

$(SQLITE_TARBALL):
	@mkdir -p $(CACHE_DIR)
	curl -fsSL -o $@.tmp \
	  https://sqlite.org/$(SQLITE_YEAR)/sqlite-autoconf-$(SQLITE_VERSION).tar.gz
	mv $@.tmp $@

# ---- extract ----

$(CPYTHON_SRC)/configure: $(CPYTHON_TARBALL)
	@mkdir -p $(PYBUILD_DIR)
	rm -rf $(CPYTHON_SRC) $(PYBUILD_DIR)/cpython-$(CPYTHON_REV)
	tar -xzf $(CPYTHON_TARBALL) -C $(PYBUILD_DIR)
	mv $(PYBUILD_DIR)/cpython-$(CPYTHON_REV) $(CPYTHON_SRC)
	touch $@

$(ZLIB_SRC)/configure: $(ZLIB_TARBALL)
	@mkdir -p $(PYBUILD_DIR)
	rm -rf $(ZLIB_SRC)
	tar -xzf $(ZLIB_TARBALL) -C $(PYBUILD_DIR)
	touch $@

$(SQLITE_SRC)/configure: $(SQLITE_TARBALL)
	@mkdir -p $(PYBUILD_DIR)
	rm -rf $(SQLITE_SRC)
	tar -xzf $(SQLITE_TARBALL) -C $(PYBUILD_DIR)
	touch $@

# ---- native host Python (build interpreter for the cross-build) ----

$(CPYTHON_NATIVE_DIR)/python: $(CPYTHON_SRC)/configure
	@mkdir -p $(CPYTHON_NATIVE_DIR)
	cd $(CPYTHON_NATIVE_DIR) && ../../configure --prefix=$(CPYTHON_NATIVE_DIR)/install
	$(MAKE) -C $(CPYTHON_NATIVE_DIR)

# ---- zlib (static, into wasi deps dir) ----

$(CPYTHON_DEPS_DIR)/lib/libz.a: $(ZLIB_SRC)/configure $(CPYTHON_SRC)/configure
	@mkdir -p $(CPYTHON_DEPS_DIR)
	cd $(ZLIB_SRC) && \
	  AR=$(WASI_SDK_PATH)/bin/ar \
	  CC=$(WASI_SDK_PATH)/bin/clang \
	  RANLIB=$(WASI_SDK_PATH)/bin/ranlib \
	  CFLAGS='$(DEPS_CFLAGS)' \
	  LDFLAGS='$(DEPS_LDFLAGS)' \
	  ./configure --static --prefix=$(CPYTHON_DEPS_DIR)
	$(MAKE) -C $(ZLIB_SRC) \
	  AR=$(WASI_SDK_PATH)/bin/ar \
	  ARFLAGS=rcs \
	  CC=$(WASI_SDK_PATH)/bin/clang \
	  RANLIB=$(WASI_SDK_PATH)/bin/ranlib \
	  CFLAGS='$(DEPS_CFLAGS)' \
	  LDFLAGS='$(DEPS_LDFLAGS)' \
	  static install

# ---- sqlite (static, into wasi deps dir) ----

$(CPYTHON_DEPS_DIR)/lib/libsqlite3.a: $(SQLITE_SRC)/configure $(CPYTHON_SRC)/configure
	@mkdir -p $(CPYTHON_DEPS_DIR)/lib $(CPYTHON_DEPS_DIR)/include
	cd $(SQLITE_SRC) && \
	  AR=$(WASI_SDK_PATH)/bin/ar \
	  CC=$(WASI_SDK_PATH)/bin/clang \
	  RANLIB=$(WASI_SDK_PATH)/bin/ranlib \
	  CFLAGS='$(SQLITE_CFLAGS)' \
	  LDFLAGS='$(SQLITE_LDFLAGS)' \
	  ./configure --host=wasm32-wasi --prefix=$(CPYTHON_DEPS_DIR) \
	    --disable-shared --enable-static \
	    --disable-readline --disable-threadsafe --disable-load-extension
	$(MAKE) -C $(SQLITE_SRC) \
	  AR=$(WASI_SDK_PATH)/bin/ar \
	  ARFLAGS=rcs \
	  CC=$(WASI_SDK_PATH)/bin/clang \
	  RANLIB=$(WASI_SDK_PATH)/bin/ranlib \
	  CFLAGS='$(SQLITE_CFLAGS)' \
	  libsqlite3.a
	cp $(SQLITE_SRC)/libsqlite3.a $@
	cp $(SQLITE_SRC)/sqlite3.h $(CPYTHON_DEPS_DIR)/include/sqlite3.h
	@if [ -f $(SQLITE_SRC)/sqlite3ext.h ]; then \
	   cp $(SQLITE_SRC)/sqlite3ext.h $(CPYTHON_DEPS_DIR)/include/sqlite3ext.h; \
	 fi

# ---- CPython wasi cross-build ----
#
# Setup.local is written AFTER configure but BEFORE make, matching
# build.rs ordering. The `_sqlite3` line lists the extension's source
# files because configure won't auto-wire them when cross-compiling.

$(CPYTHON_WASI_DIR)/libpython3.14.a: \
    $(CPYTHON_SRC)/configure \
    $(CPYTHON_NATIVE_DIR)/python \
    $(CPYTHON_DEPS_DIR)/lib/libz.a \
    $(CPYTHON_DEPS_DIR)/lib/libsqlite3.a
	@mkdir -p $(CPYTHON_WASI_DIR)
	cd $(CPYTHON_WASI_DIR) && \
	  CONFIG_SITE=../../Tools/wasm/wasi/config.site-wasm32-wasi \
	  WASI_SDK_PATH=$(WASI_SDK_PATH) \
	  CFLAGS='--target=wasm32-wasip2 -fPIC -I$(CPYTHON_DEPS_DIR)/include' \
	  LDFLAGS='--target=wasm32-wasip2 -L$(CPYTHON_DEPS_DIR)/lib' \
	  ../../Tools/wasm/wasi-env ../../configure -C \
	    --host=wasm32-unknown-wasip2 \
	    --build=$$(../../config.guess) \
	    --with-build-python=$(CPYTHON_NATIVE_DIR)/python \
	    --prefix=$(CPYTHON_WASI_DIR)/install \
	    --disable-test-modules \
	    --enable-ipv6
	@mkdir -p $(CPYTHON_WASI_DIR)/Modules
	@printf '%s\n' '_sqlite3 _sqlite/blob.c _sqlite/connection.c _sqlite/cursor.c _sqlite/microprotocols.c _sqlite/module.c _sqlite/prepare_protocol.c _sqlite/row.c _sqlite/statement.c _sqlite/util.c -I$(CPYTHON_DEPS_DIR)/include -L$(CPYTHON_DEPS_DIR)/lib -lsqlite3' \
	  > $(CPYTHON_WASI_DIR)/Modules/Setup.local
	$(MAKE) -C $(CPYTHON_WASI_DIR) build_all install

# ---- libpython3.14.so (hand-linked, --whole-archive) ----

$(PYTHON_LIB_SO): $(CPYTHON_WASI_DIR)/libpython3.14.a
	$(WASI_SDK_PATH)/bin/clang \
	  --target=wasm32-wasip2 -shared -o $@ \
	  -Wl,--whole-archive $(CPYTHON_WASI_DIR)/libpython3.14.a -Wl,--no-whole-archive \
	  $(CPYTHON_WASI_DIR)/Modules/_hacl/libHacl_HMAC.a \
	  $(CPYTHON_WASI_DIR)/Modules/_hacl/libHacl_Hash_BLAKE2.a \
	  $(CPYTHON_WASI_DIR)/Modules/_hacl/libHacl_Hash_MD5.a \
	  $(CPYTHON_WASI_DIR)/Modules/_hacl/libHacl_Hash_SHA1.a \
	  $(CPYTHON_WASI_DIR)/Modules/_hacl/libHacl_Hash_SHA2.a \
	  $(CPYTHON_WASI_DIR)/Modules/_hacl/libHacl_Hash_SHA3.a \
	  $(CPYTHON_WASI_DIR)/Modules/_decimal/libmpdec/libmpdec.a \
	  $(CPYTHON_WASI_DIR)/Modules/expat/libexpat.a \
	  $(CPYTHON_DEPS_DIR)/lib/libz.a \
	  $(CPYTHON_DEPS_DIR)/lib/libsqlite3.a \
	  -lwasi-emulated-signal -lwasi-emulated-getpid -lwasi-emulated-process-clocks -ldl

clean:
	rm -rf $(PYBUILD_DIR)
	@echo "kept: $(CACHE_DIR) (upstream tarballs)"
