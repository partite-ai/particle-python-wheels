# wheels/cffi/Makefile — cross-build cffi-2.0.0 for wasm32-wasip2.
#
# Replaces the previous pure-Python stub. Compiles _cffi_backend.c
# directly against:
#   - wasi-sdk's clang + sysroot
#   - the wasm CPython headers (Python.h + friends)
#   - our wheels/libffi-wasi/out/{include,libffi.a} (the wasi port
#     whose EM_JS bodies were replaced with host-import declarations)
#
# Builds skip setuptools cross-compile (fragile) — we invoke clang
# ourselves, then assemble the wheel zip by hand.
#
#   make            # build out/cffi-2.0.0-cp314-abi3-any.whl
#   make clean      # drop sources, archives, output
#
# Outputs:
#   out/cffi-2.0.0-cp314-abi3-any.whl

THIS_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

# Pinned upstream version. CFFI's tarball lives on files.pythonhosted.
CFFI_VERSION := 2.0.0
CFFI_TARBALL := https://files.pythonhosted.org/packages/source/c/cffi/cffi-$(CFFI_VERSION).tar.gz

WASI_SDK_PATH ?= /opt/wasi-sdk
WASI_SYSROOT  := $(WASI_SDK_PATH)/share/wasi-sysroot

# Wasm CPython we cross-compile against (same one wheels/cryptography
# uses). Headers ship with the CPython build. Built by sibling
# ../cpython — order-only dep below (`| $(CPYTHON_INCLUDE_MARKER)`)
# triggers `make -C ../cpython` on a fresh checkout.
CPYTHON_DIR     := $(THIS_DIR)../cpython
CPYTHON_REV     := 0e13686da8bb881b059d35e23c32bcd2e6440099
CPYTHON_INCLUDE := $(HOME)/cargo-target/cpython-$(CPYTHON_REV)/cpython/builddir/wasi/install/include/python3.14

# Our libffi port — has to be built first (separate sibling Makefile).
LIBFFI_DIR     := $(THIS_DIR)../libffi-wasi
LIBFFI_INCLUDE := $(LIBFFI_DIR)/out/include
LIBFFI_LIB     := $(LIBFFI_DIR)/out/libffi.a

WORKDIR    ?= $(HOME)/.cache/particle/wheels-cffi
SRC_DIR    := $(WORKDIR)/cffi-$(CFFI_VERSION)
BUILD_DIR  := $(WORKDIR)/build
CACHE_DIR  := $(THIS_DIR).cache
OUT_DIR    := $(THIS_DIR)out

WHEEL      := $(OUT_DIR)/cffi-$(CFFI_VERSION)-cp314-abi3-wasi_wasm32.whl
TARBALL    := $(CACHE_DIR)/cffi-$(CFFI_VERSION).tar.gz

# wasm32-wasip2 + sysroot + the wasm CPython's include dir + our libffi.
CFLAGS := --target=wasm32-wasip2 \
          --sysroot=$(WASI_SYSROOT) \
          -fPIC -fvisibility=default \
          -O2 \
          -D_WASI_EMULATED_MMAN \
          -isystem $(CPYTHON_INCLUDE) \
          -I$(LIBFFI_INCLUDE) \
          -I$(SRC_DIR)/src/c \
          -I$(SRC_DIR)/src/cffi \
          -Wno-unused-function \
          -Wno-unused-variable \
          -Wno-implicit-function-declaration \
          -Wno-incompatible-pointer-types

# Link as a shared object (.so) — wasm32-wasip2 cdylib.
#
# - -lc via libc.so (shared, not static) so malloc/free etc. resolve
#   to the same heap libpython uses.
# - libwasi-emulated-mman.a supplies mmap/munmap for cffi's
#   malloc_closure.h (executable pages in cffi-speak; for us they're
#   just anonymous memory holding ffi_closure structs — the actual
#   trampolines live in the host's wasm function table). Static
#   link, not the .so — our dyld doesn't ship libwasi-emulated-mman
#   as a side module, and the static archive is small (a few KB of
#   malloc-backed anonymous-mmap emulation).
# - --unresolved-symbols=import-dynamic + --allow-undefined leave
#   __wasi_libffi_* (resolved at component composition to the
#   libffi-wasi-bridge side module) and dlopen/dlsym/dlclose/dlerror
#   (resolved at dyld load time to our libdl.so) as env imports.
#
# We also pull in clang's compiler-rt builtins for wasm32-unknown-wasip2.
# cffi's _cffi_backend processes long double (128-bit IEEE quad on this
# target) which generates __extenddftf2 / __trunctfdf2 / etc. calls that
# only compiler-rt provides. wasi-libc-shared doesn't ship them.
COMPILER_RT := $(WASI_SDK_PATH)/lib/clang/21/lib/wasm32-unknown-wasip2/libclang_rt.builtins.a

LDFLAGS := --target=wasm32-wasip2 \
           -nostdlib -nodefaultlibs \
           -Wl,--shared \
           -Wl,--no-entry \
           -Wl,--unresolved-symbols=import-dynamic \
           -Wl,--allow-undefined \
           -L$(WASI_SYSROOT)/lib/wasm32-wasip2 \
           -Wl,-l:libc.so \
           -Wl,-l:libwasi-emulated-mman.a

.PHONY: all clean
all: $(WHEEL)
	@printf '✓  cffi wheel: '
	@ls -lh $(WHEEL) | awk '{print $$5"  "$$NF}'

# ---- source fetch ----

$(TARBALL):
	@mkdir -p $(CACHE_DIR)
	curl -sL -o $@ $(CFFI_TARBALL)
	@ls -la $@

# Order-only on the tarball: when $(SRC_DIR) is restored from the
# cffi cache, we don't want a missing tarball (in the in-tree,
# uncached .cache/) to drag us through re-download + re-extract,
# which would also `rm -rf` the cached source — wiping the patched
# tree and forcing a downstream _cffi_backend.so rebuild.
$(SRC_DIR)/setup.py: | $(TARBALL)
	@mkdir -p $(WORKDIR)
	@rm -rf $(SRC_DIR)
	tar xzf $< -C $(WORKDIR)
	@touch $@

# ---- libffi.a + CPython built first ----

$(LIBFFI_LIB):
	$(MAKE) -C $(LIBFFI_DIR)

# CPython is built by ../cpython on demand. Phony so each invocation
# re-enters that Makefile (which has its own up-to-date check); the
# first call from a fresh checkout populates $(CPYTHON_INCLUDE) and
# subsequent ones are near-instant.
.PHONY: cpython
cpython:
	$(MAKE) -C $(CPYTHON_DIR)

# ---- patch the cffi sdist before compiling ----
#
# patches-cffi/file_emulator.h replaces upstream's PyFile_AsFile (which
# calls dup() to clone an fd — wasi has no fd-clone primitive). The
# replacement defines PyFile_Check as 0, making cffi's CT_IS_FILE
# branches dead code; PyFile_AsFile becomes unreachable but exists
# lexically. See the header itself for the long-form rationale.
#
# Sentinel file at $(SRC_DIR)/.particle-patched gates re-application
# so `make` re-runs the copy when our replacement header changes.
$(SRC_DIR)/.particle-patched: $(SRC_DIR)/setup.py $(THIS_DIR)patches-cffi/file_emulator.h
	cp $(THIS_DIR)patches-cffi/file_emulator.h $(SRC_DIR)/src/c/file_emulator.h
	touch $@

# ---- compile _cffi_backend.so ----
#
# cffi-2.0.0's setup.py drives setuptools — instead we invoke clang
# directly on _cffi_backend.c. The file #includes a bunch of helper
# .c via the preprocessor; we just compile one TU.

$(BUILD_DIR)/_cffi_backend.so: $(SRC_DIR)/.particle-patched $(LIBFFI_LIB) | cpython
	@mkdir -p $(BUILD_DIR)
	$(WASI_SDK_PATH)/bin/clang \
	  $(CFLAGS) \
	  -c $(SRC_DIR)/src/c/_cffi_backend.c \
	  -o $(BUILD_DIR)/_cffi_backend.o
	$(WASI_SDK_PATH)/bin/clang \
	  $(LDFLAGS) \
	  -o $@ \
	  $(BUILD_DIR)/_cffi_backend.o \
	  $(LIBFFI_LIB) \
	  $(COMPILER_RT)
	@printf '✓  _cffi_backend.so: '
	@ls -lh $@ | awk '{print $$5"  "$$NF}'

# ---- assemble the wheel ----
# Wheels are zip files with package files + dist-info/{METADATA,WHEEL,RECORD}.
# We hand-assemble rather than involve `wheel pack` so we don't need
# host-side python tooling.

$(WHEEL): $(BUILD_DIR)/_cffi_backend.so $(SRC_DIR)/setup.py
	@mkdir -p $(BUILD_DIR)/wheel/cffi
	@mkdir -p $(BUILD_DIR)/wheel/cffi-$(CFFI_VERSION).dist-info
	# Copy the upstream cffi Python package as-is.
	cp -r $(SRC_DIR)/src/cffi/. $(BUILD_DIR)/wheel/cffi/
	# Place our compiled _cffi_backend.so at the top of site-packages.
	cp $(BUILD_DIR)/_cffi_backend.so $(BUILD_DIR)/wheel/_cffi_backend.abi3.so
	# Write minimal METADATA + WHEEL files (skip RECORD; cpython doesn't
	# enforce it and we don't have sha256-summing tooling here).
	printf 'Metadata-Version: 2.1\nName: cffi\nVersion: %s\nSummary: cffi cross-built for wasm32-wasip2 (uses our libffi-wasi)\nRequires-Python: >=3.9\nLicense: MIT\nRequires-Dist: pycparser\n' "$(CFFI_VERSION)" > $(BUILD_DIR)/wheel/cffi-$(CFFI_VERSION).dist-info/METADATA
	printf 'Wheel-Version: 1.0\nGenerator: particle-wheels-cffi 0.1\nRoot-Is-Purelib: false\nTag: cp314-abi3-wasi_wasm32\n' > $(BUILD_DIR)/wheel/cffi-$(CFFI_VERSION).dist-info/WHEEL
	@mkdir -p $(OUT_DIR)
	cd $(THIS_DIR) && go run zip-wheel.go $(BUILD_DIR)/wheel $(WHEEL)

clean:
	rm -rf $(OUT_DIR) $(BUILD_DIR) $(SRC_DIR)
	@echo "kept: $(CACHE_DIR) (upstream tarball)"
