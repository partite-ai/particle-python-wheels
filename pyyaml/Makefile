# pyyaml/Makefile — cross-build PyYAML 6.0.3 for wasm32-wasip2.
#
# Heavier than regex/cffi: PyYAML 6.0.3's sdist ships only
# yaml/_yaml.pyx (Cython source) — they dropped the pre-generated .c.
# So this Makefile owns three steps the other wheel builds don't:
#
#   1. cross-build libyaml 0.2.5 as a static .a (autotools; same shape
#      as the zlib/sqlite path in ../cpython/Makefile).
#   2. provision a host CPython 3.14 venv with Cython 3, and run
#      `cython` on _yaml.pyx to generate _yaml.c.
#   3. compile _yaml.c against libyaml.a + the wasm CPython headers,
#      link as a shared .so with the same import-dynamic pattern used
#      by cffi and regex.
#
# Outputs (consumed by the root Makefile):
#   out/pyyaml-$(PYYAML_VERSION)-cp314-cp314-wasi_wasm32.whl
#
# Tagged cp314-cp314 (not abi3) because the Cython-generated extension
# pokes at CPython internals; per-minor recompiles are the norm.
#
#   make            build the wheel
#   make clean      wipe build trees + output (keeps .cache/ tarballs)

THIS_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# ---- versions / sources --------------------------------------------------

PYYAML_VERSION  := 6.0.3
LIBYAML_VERSION := 0.2.5
CYTHON_VERSION  := 3.0.11

PYYAML_TARBALL_URL  := https://files.pythonhosted.org/packages/source/p/pyyaml/pyyaml-$(PYYAML_VERSION).tar.gz
LIBYAML_TARBALL_URL := https://github.com/yaml/libyaml/releases/download/$(LIBYAML_VERSION)/yaml-$(LIBYAML_VERSION).tar.gz

# ---- paths ---------------------------------------------------------------

WASI_SDK_PATH ?= /opt/wasi-sdk
WASI_SYSROOT  := $(WASI_SDK_PATH)/share/wasi-sysroot

CPYTHON_DIR     := $(THIS_DIR)../cpython
CPYTHON_REV     := 0e13686da8bb881b059d35e23c32bcd2e6440099
CPYTHON_INCLUDE := $(HOME)/cargo-target/cpython-$(CPYTHON_REV)/cpython/builddir/wasi/install/include/python3.14

WORKDIR       ?= $(HOME)/.cache/particle/wheels-pyyaml
PYYAML_SRC    := $(WORKDIR)/pyyaml-$(PYYAML_VERSION)
LIBYAML_SRC   := $(WORKDIR)/yaml-$(LIBYAML_VERSION)
BUILD_DIR     := $(WORKDIR)/build
CACHE_DIR     := $(THIS_DIR).cache
OUT_DIR       := $(THIS_DIR)out
TOOLCHAIN_DIR := $(WORKDIR)/toolchain
HOST_PYVENV   := $(TOOLCHAIN_DIR)/pyvenv
HOST_PYTHON   := $(HOST_PYVENV)/bin/python
CYTHON        := $(HOST_PYVENV)/bin/cython

LIBYAML_INSTALL := $(BUILD_DIR)/libyaml-install
LIBYAML_LIB     := $(LIBYAML_INSTALL)/lib/libyaml.a
LIBYAML_INCLUDE := $(LIBYAML_INSTALL)/include

PYYAML_TARBALL  := $(CACHE_DIR)/pyyaml-$(PYYAML_VERSION).tar.gz
LIBYAML_TARBALL := $(CACHE_DIR)/yaml-$(LIBYAML_VERSION).tar.gz

WHEEL := $(OUT_DIR)/pyyaml-$(PYYAML_VERSION)-cp314-cp314-wasi_wasm32.whl

EXT_SUFFIX := .cpython-314-wasm32-wasi.so

# ---- compile / link flags ------------------------------------------------

LIBYAML_CFLAGS := --target=wasm32-wasip2 --sysroot=$(WASI_SYSROOT) -fPIC -O2

# _yaml.c (Cython output) #includes _yaml.h, which #includes <yaml.h>
# (libyaml's). Both include paths are needed.
CFLAGS_PYYAML := --target=wasm32-wasip2 \
                 --sysroot=$(WASI_SYSROOT) \
                 -fPIC -fvisibility=default -O2 \
                 -isystem $(CPYTHON_INCLUDE) \
                 -I$(LIBYAML_INCLUDE) \
                 -I$(PYYAML_SRC)/yaml \
                 -Wno-unused-function -Wno-unused-variable

# Same link pattern as cffi/regex: leave Python C API + libc symbols
# as env imports resolved at .so-load time by our dyld.
COMPILER_RT := $(WASI_SDK_PATH)/lib/clang/21/lib/wasm32-unknown-wasip2/libclang_rt.builtins.a

LDFLAGS := --target=wasm32-wasip2 \
           -nostdlib -nodefaultlibs \
           -Wl,--shared \
           -Wl,--no-entry \
           -Wl,--unresolved-symbols=import-dynamic \
           -Wl,--allow-undefined \
           -L$(WASI_SYSROOT)/lib/wasm32-wasip2 \
           -Wl,-l:libc.so

# ---- phony entry points --------------------------------------------------

.PHONY: all clean cpython check-host-deps

all: $(WHEEL)
	@printf '✓  pyyaml wheel: '
	@ls -lh $(WHEEL) | awk '{print $$5"  "$$NF}'

check-host-deps:
	@command -v curl >/dev/null || { echo "ERROR: curl not on PATH"; exit 1; }
	@command -v tar  >/dev/null || { echo "ERROR: tar not on PATH";  exit 1; }
	@command -v go   >/dev/null || { echo "ERROR: go not on PATH (zip-wheel.go)"; exit 1; }
	@command -v uv   >/dev/null || { echo "ERROR: uv not on PATH (Cython host venv)"; exit 1; }
	@test -d $(WASI_SDK_PATH)   || { echo "ERROR: WASI_SDK_PATH=$(WASI_SDK_PATH) missing"; exit 1; }

cpython:
	$(MAKE) -C $(CPYTHON_DIR)

# ---- sdist fetch ---------------------------------------------------------

$(PYYAML_TARBALL): | check-host-deps
	@mkdir -p $(CACHE_DIR)
	curl -fsSL -o $@.tmp $(PYYAML_TARBALL_URL)
	mv $@.tmp $@

$(LIBYAML_TARBALL): | check-host-deps
	@mkdir -p $(CACHE_DIR)
	curl -fsSL -o $@.tmp $(LIBYAML_TARBALL_URL)
	mv $@.tmp $@

# Order-only tarball prereqs — see cffi/Makefile for the rationale.
# Cached source trees should not be wiped + re-extracted just because
# the in-tree tarballs didn't survive the CI runner.
$(PYYAML_SRC)/setup.py: | $(PYYAML_TARBALL)
	@mkdir -p $(WORKDIR)
	rm -rf $(PYYAML_SRC)
	tar -xzf $(PYYAML_TARBALL) -C $(WORKDIR)
	@touch $@

$(LIBYAML_SRC)/configure: | $(LIBYAML_TARBALL)
	@mkdir -p $(WORKDIR)
	rm -rf $(LIBYAML_SRC)
	tar -xzf $(LIBYAML_TARBALL) -C $(WORKDIR)
	@touch $@

# ---- libyaml cross-build -------------------------------------------------
#
# autotools cross-compile: --host=wasm32-wasi tells autoconf we're
# cross-compiling so it skips run-time feature tests. We only need
# libyaml.a + yaml.h; copy them out by hand rather than `make install`
# (which would also build the test programs and may fail under wasm).

$(LIBYAML_LIB): $(LIBYAML_SRC)/configure
	@mkdir -p $(LIBYAML_INSTALL)/lib $(LIBYAML_INSTALL)/include
	@# libyaml 0.2.5 ships a config.sub from 2020 that predates wasm32
	@# and rejects --host=wasm32-*. Replace with a stub that always
	@# emits the canonical 3-field form. configure's host-detection
	@# output only drives `cross_compiling=yes` here — CC/CFLAGS below
	@# are what actually decides the produced code.
	@printf '#!/bin/sh\necho wasm32-unknown-wasi\n' > $(LIBYAML_SRC)/config/config.sub
	@chmod +x $(LIBYAML_SRC)/config/config.sub
	cd $(LIBYAML_SRC) && \
	  CC=$(WASI_SDK_PATH)/bin/clang \
	  AR=$(WASI_SDK_PATH)/bin/ar \
	  RANLIB=$(WASI_SDK_PATH)/bin/ranlib \
	  CFLAGS='$(LIBYAML_CFLAGS)' \
	  ./configure --host=wasm32-unknown-wasi \
	    --prefix=$(LIBYAML_INSTALL) \
	    --enable-static --disable-shared
	$(MAKE) -C $(LIBYAML_SRC)/src libyaml.la
	cp $(LIBYAML_SRC)/src/.libs/libyaml.a $(LIBYAML_INSTALL)/lib/
	cp $(LIBYAML_SRC)/include/yaml.h $(LIBYAML_INSTALL)/include/

# ---- host venv with Cython -----------------------------------------------
#
# uv provisions the interpreter; the venv carries only Cython. PyYAML
# 6.0.3 needs Cython >= 3.0 (see its pyproject.toml).

$(CYTHON): | check-host-deps
	@mkdir -p $(TOOLCHAIN_DIR)
	uv venv --python 3.14 $(HOST_PYVENV)
	uv pip install --python $(HOST_PYTHON) cython==$(CYTHON_VERSION)

# ---- Cython _yaml.pyx → _yaml.c ------------------------------------------

$(BUILD_DIR)/_yaml.c: $(PYYAML_SRC)/setup.py $(CYTHON)
	@mkdir -p $(BUILD_DIR)
	$(CYTHON) -3 $(PYYAML_SRC)/yaml/_yaml.pyx -o $@

# ---- compile + link _yaml.so ---------------------------------------------

$(BUILD_DIR)/_yaml.so: $(BUILD_DIR)/_yaml.c $(LIBYAML_LIB) | cpython
	$(WASI_SDK_PATH)/bin/clang $(CFLAGS_PYYAML) -c $(BUILD_DIR)/_yaml.c -o $(BUILD_DIR)/_yaml.o
	$(WASI_SDK_PATH)/bin/clang $(LDFLAGS) -o $@ \
	  $(BUILD_DIR)/_yaml.o \
	  $(LIBYAML_LIB) \
	  $(COMPILER_RT)
	@printf '✓  _yaml.so: '
	@ls -lh $@ | awk '{print $$5"  "$$NF}'

# ---- wheel assembly ------------------------------------------------------
# Layout matches what PyPI's PyYAML wheels produce:
#   yaml/            pure-Python loader/dumper + cyaml.py
#   yaml/_yaml.*.so  compiled extension (imported by cyaml.py)
#   _yaml/           backcompat stub for code doing `import _yaml`

$(WHEEL): $(BUILD_DIR)/_yaml.so
	@rm -rf $(BUILD_DIR)/wheel
	@mkdir -p $(BUILD_DIR)/wheel/yaml
	@mkdir -p $(BUILD_DIR)/wheel/_yaml
	@mkdir -p $(BUILD_DIR)/wheel/pyyaml-$(PYYAML_VERSION).dist-info
	cp -r $(PYYAML_SRC)/lib/yaml/. $(BUILD_DIR)/wheel/yaml/
	cp $(PYYAML_SRC)/lib/_yaml/__init__.py $(BUILD_DIR)/wheel/_yaml/__init__.py
	cp $(BUILD_DIR)/_yaml.so $(BUILD_DIR)/wheel/yaml/_yaml$(EXT_SUFFIX)
	find $(BUILD_DIR)/wheel -name '__pycache__' -type d -exec rm -rf {} +
	printf 'Metadata-Version: 2.1\nName: PyYAML\nVersion: %s\nSummary: YAML parser and emitter cross-built for wasm32-wasip2 (libyaml-backed)\nRequires-Python: >=3.10\nLicense: MIT\n' "$(PYYAML_VERSION)" > $(BUILD_DIR)/wheel/pyyaml-$(PYYAML_VERSION).dist-info/METADATA
	printf 'Wheel-Version: 1.0\nGenerator: particle-wheels-pyyaml 0.1\nRoot-Is-Purelib: false\nTag: cp314-cp314-wasi_wasm32\n' > $(BUILD_DIR)/wheel/pyyaml-$(PYYAML_VERSION).dist-info/WHEEL
	@mkdir -p $(OUT_DIR)
	cd $(THIS_DIR) && go run zip-wheel.go $(BUILD_DIR)/wheel $(WHEEL)

# ---- clean ---------------------------------------------------------------

clean:
	rm -rf $(WORKDIR) $(OUT_DIR)
	@echo "kept: $(CACHE_DIR) (upstream tarballs)"
