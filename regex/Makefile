# regex/Makefile — cross-build mrab-regex for wasm32-wasip2.
#
# regex is a setuptools-based C extension by Matthew Barnett. We skip
# setuptools (fragile cross-compile) and invoke wasi-sdk clang directly
# on the two TUs (src/_regex.c + src/_regex_unicode.c), then assemble
# the wheel by hand the same way wheels/cffi/ does.
#
# Outputs (consumed by the root Makefile):
#   out/regex-$(REGEX_VERSION)-cp314-cp314-wasi_wasm32.whl
#
# Tagged cp314-cp314 (not abi3) because the upstream extension isn't
# built against the limited Python API — it uses CPython internals and
# is recompiled per minor version on PyPI.
#
#   make            build the wheel
#   make clean      drop build trees + output (keeps .cache/ tarball)

THIS_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

REGEX_VERSION := 2026.5.9
REGEX_TARBALL_URL := https://files.pythonhosted.org/packages/source/r/regex/regex-$(REGEX_VERSION).tar.gz

WASI_SDK_PATH ?= /opt/wasi-sdk
WASI_SYSROOT  := $(WASI_SDK_PATH)/share/wasi-sysroot

# Wasm CPython we cross-compile against. See cffi/Makefile for the
# CPYTHON_REV convention.
CPYTHON_DIR     := $(THIS_DIR)../cpython
CPYTHON_REV     := 0e13686da8bb881b059d35e23c32bcd2e6440099
CPYTHON_INCLUDE := $(HOME)/cargo-target/cpython-$(CPYTHON_REV)/cpython/builddir/wasi/install/include/python3.14

WORKDIR    ?= $(HOME)/.cache/particle/wheels-regex
SRC_DIR    := $(WORKDIR)/regex-$(REGEX_VERSION)
BUILD_DIR  := $(WORKDIR)/build
CACHE_DIR  := $(THIS_DIR).cache
OUT_DIR    := $(THIS_DIR)out

WHEEL   := $(OUT_DIR)/regex-$(REGEX_VERSION)-cp314-cp314-wasi_wasm32.whl
TARBALL := $(CACHE_DIR)/regex-$(REGEX_VERSION).tar.gz

# cp314 ext-suffix the host CPython will look for when importing
# `regex._regex` on wasi.
EXT_SUFFIX := .cpython-314-wasm32-wasi.so

CFLAGS := --target=wasm32-wasip2 \
          --sysroot=$(WASI_SYSROOT) \
          -fPIC -fvisibility=default \
          -O2 \
          -isystem $(CPYTHON_INCLUDE) \
          -I$(SRC_DIR)/src

# Link the .so the same way cffi does:
#   - libc.so (shared) so malloc/free etc. resolve to the same heap
#     libpython uses (rather than this wheel getting its own private
#     dlmalloc, which would corrupt pymalloc-allocated structs).
#   - --unresolved-symbols=import-dynamic + --allow-undefined leave
#     the Python C API symbols (PyObject_*, PyArg_*, …) as env imports
#     that dyld resolves to libpython.so at .so-load time.
#   - clang's compiler-rt builtins for wasm32-wasip2 cover any wasm
#     intrinsics the compiled code may reference (cheap to include).
COMPILER_RT := $(shell $(WASI_SDK_PATH)/bin/clang --target=wasm32-wasip2 -print-runtime-dir)/libclang_rt.builtins.a

LDFLAGS := --target=wasm32-wasip2 \
           -nostdlib -nodefaultlibs \
           -Wl,--shared \
           -Wl,--no-entry \
           -Wl,--unresolved-symbols=import-dynamic \
           -Wl,--allow-undefined \
           -L$(WASI_SYSROOT)/lib/wasm32-wasip2 \
           -Wl,-l:libc.so

.PHONY: all clean cpython
all: $(WHEEL)
	@printf '✓  regex wheel: '
	@ls -lh $(WHEEL) | awk '{print $$5"  "$$NF}'

# Recursively kick the cpython sub-build; it owns its own up-to-date
# check, so this is near-instant once libpython3.14.so is in place.
cpython:
	$(MAKE) -C $(CPYTHON_DIR)

# ---- source fetch ----

$(TARBALL):
	@mkdir -p $(CACHE_DIR)
	curl -fsSL -o $@.tmp $(REGEX_TARBALL_URL)
	mv $@.tmp $@

# Order-only on the tarball — see cffi/Makefile for the rationale.
# Cached $(SRC_DIR) should not be wiped + re-extracted just because
# the in-tree tarball didn't survive the CI runner.
$(SRC_DIR)/setup.py: | $(TARBALL)
	@mkdir -p $(WORKDIR)
	@rm -rf $(SRC_DIR)
	tar xzf $(TARBALL) -C $(WORKDIR)
	@touch $@

# ---- compile + link the .so ----
#
# Two TUs (src/_regex.c, src/_regex_unicode.c). They share headers but
# don't #include each other — compile separately, then link together.

$(BUILD_DIR)/_regex.o: $(SRC_DIR)/setup.py | cpython
	@mkdir -p $(BUILD_DIR)
	$(WASI_SDK_PATH)/bin/clang $(CFLAGS) -c $(SRC_DIR)/src/_regex.c -o $@

$(BUILD_DIR)/_regex_unicode.o: $(SRC_DIR)/setup.py | cpython
	@mkdir -p $(BUILD_DIR)
	$(WASI_SDK_PATH)/bin/clang $(CFLAGS) -c $(SRC_DIR)/src/_regex_unicode.c -o $@

$(BUILD_DIR)/_regex.so: $(BUILD_DIR)/_regex.o $(BUILD_DIR)/_regex_unicode.o
	$(WASI_SDK_PATH)/bin/clang $(LDFLAGS) -o $@ \
	  $(BUILD_DIR)/_regex.o \
	  $(BUILD_DIR)/_regex_unicode.o \
	  $(COMPILER_RT)
	@printf '✓  _regex.so: '
	@ls -lh $@ | awk '{print $$5"  "$$NF}'

# ---- assemble the wheel ----
# Wheels are zip files with package files + dist-info/{METADATA,WHEEL}.
# We hand-assemble (matching cffi's approach); RECORD is skipped — our
# CPython doesn't enforce it and we don't have sha256-summing tooling
# in this Makefile.

$(WHEEL): $(BUILD_DIR)/_regex.so $(SRC_DIR)/setup.py
	@rm -rf $(BUILD_DIR)/wheel
	@mkdir -p $(BUILD_DIR)/wheel/regex
	@mkdir -p $(BUILD_DIR)/wheel/regex-$(REGEX_VERSION).dist-info
	# Copy the upstream Python package as-is (regex/{__init__,_main,
	# _regex_core}.py + tests/). Source .c/.h live in src/, not regex/,
	# so the package itself is pure Python.
	cp -r $(SRC_DIR)/regex/. $(BUILD_DIR)/wheel/regex/
	# Drop bytecode if upstream included any.
	find $(BUILD_DIR)/wheel/regex -name '__pycache__' -type d -exec rm -rf {} +
	# Place compiled .so where Python's importer will find it.
	cp $(BUILD_DIR)/_regex.so $(BUILD_DIR)/wheel/regex/_regex$(EXT_SUFFIX)
	# Minimal METADATA + WHEEL. License pulled from upstream's
	# pyproject (Apache-2.0 AND CNRI-Python).
	printf 'Metadata-Version: 2.1\nName: regex\nVersion: %s\nSummary: regex cross-built for wasm32-wasip2 (CPython 3.14)\nRequires-Python: >=3.10\nLicense: Apache-2.0 AND CNRI-Python\n' "$(REGEX_VERSION)" > $(BUILD_DIR)/wheel/regex-$(REGEX_VERSION).dist-info/METADATA
	printf 'Wheel-Version: 1.0\nGenerator: particle-wheels-regex 0.1\nRoot-Is-Purelib: false\nTag: cp314-cp314-wasi_wasm32\n' > $(BUILD_DIR)/wheel/regex-$(REGEX_VERSION).dist-info/WHEEL
	@mkdir -p $(OUT_DIR)
	cd $(THIS_DIR) && go run zip-wheel.go $(BUILD_DIR)/wheel $(WHEEL)

# ---- clean ---------------------------------------------------------------

clean:
	rm -rf $(WORKDIR) $(OUT_DIR)
	@echo "kept: $(CACHE_DIR) (upstream tarball)"
