# wheels/libffi-wasi/Makefile — build libffi.a for wasm32-wasip2.
#
# Adapted from libffi 3.5.2 src/wasm/, the emscripten port. The C
# entry points are kept; their EM_JS bodies (which generated
# trampolines on demand via JavaScript) are replaced with extern
# host-import declarations. The actual codegen happens Go-side in
# internal/runtime/libffi/ and is wired via the WIT interface
# `particle:host/libffi`.
#
# Outputs (consumed by wheels/cffi/Makefile via -L/-I):
#   out/libffi.a       — static archive
#   out/include/ffi.h
#   out/include/ffitarget.h
#   out/include/fficonfig.h
#
# Usage:
#   make           # build libffi.a + headers
#   make clean     # drop out/ and build/

THIS_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))

WASI_SDK_PATH   ?= /opt/wasi-sdk
WASI_SYSROOT    := $(WASI_SDK_PATH)/share/wasi-sysroot

OUT_DIR   := $(THIS_DIR)out
BUILD_DIR := $(THIS_DIR)build
INC_DIR   := $(THIS_DIR)include
SRC_DIR   := $(THIS_DIR)src

# Sources that go into libffi.a. types.c + prep_cif.c are pulled in
# from upstream libffi 3.5.2 as-is (they're target-independent). ffi.c
# is our adapted version (EM_JS stripped, host imports declared
# extern). host_stub.c is intentionally NOT linked into libffi.a —
# the weak abort() stubs would otherwise resolve internally inside
# the wheel and shadow main.wasm's real implementations. The shell
# of ffi.c references __wasi_libffi_* as plain `extern`, so wasm-ld
# leaves them undefined; the cffi wheel's link uses
# `--unresolved-symbols=import-dynamic` which turns them into env
# imports that dyld resolves to main.wasm at .so-load time.
SOURCES := ffi.c types.c prep_cif.c
OBJECTS := $(SOURCES:%.c=$(BUILD_DIR)/%.o)

CC      := $(WASI_SDK_PATH)/bin/clang
AR      := $(WASI_SDK_PATH)/bin/ar
RANLIB  := $(WASI_SDK_PATH)/bin/ranlib

CFLAGS  := --target=wasm32-wasip2 \
           --sysroot=$(WASI_SYSROOT) \
           -fPIC -fvisibility=hidden \
           -O2 -Wall -Wno-unused-parameter \
           -I$(OUT_DIR)/include \
           -DFFI_BUILDING \
           -DHAVE_AS_X86_PCREL=0

# ffi.h substitutions. libffi's autoconf normally produces these; we
# hand-fill them. PTR_SIZE etc. come from the wasm32-wasip2 ABI.
FFI_VERSION         := 3.5.2
FFI_VERSION_STRING  := 3.5.2
FFI_VERSION_NUMBER  := 30502
FFI_TARGET          := X86  # any non-empty value satisfies the #define guard

.PHONY: all clean
all: $(OUT_DIR)/libffi.a $(OUT_DIR)/include/ffi.h $(OUT_DIR)/include/ffitarget.h $(OUT_DIR)/include/fficonfig.h
	@printf '✓  libffi-wasi: '
	@ls -lh $(OUT_DIR)/libffi.a | awk '{print $$5"  "$$NF}'

# ---- header generation ----

$(OUT_DIR)/include/ffi.h: $(INC_DIR)/ffi.h.in | $(OUT_DIR)/include
	sed -e 's|@VERSION@|$(FFI_VERSION)|g' \
	    -e 's|@TARGET@|$(FFI_TARGET)|g' \
	    -e 's|@HAVE_LONG_DOUBLE@|1|g' \
	    -e 's|@FFI_VERSION_STRING@|$(FFI_VERSION_STRING)|g' \
	    -e 's|@FFI_VERSION_NUMBER@|$(FFI_VERSION_NUMBER)|g' \
	    -e 's|@FFI_EXEC_TRAMPOLINE_TABLE@|0|g' \
	    $< > $@

$(OUT_DIR)/include/ffitarget.h: $(INC_DIR)/ffitarget.h | $(OUT_DIR)/include
	cp $< $@

$(OUT_DIR)/include/fficonfig.h: $(INC_DIR)/fficonfig.h | $(OUT_DIR)/include
	cp $< $@

# ffi_common.h is included by prep_cif.c and types.c. cffi never sees
# it. Copy alongside the public headers since CC's -I points at the
# same dir.
$(OUT_DIR)/include/ffi_common.h: $(INC_DIR)/ffi_common.h | $(OUT_DIR)/include
	cp $< $@

# ---- object compilation ----

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(OUT_DIR)/include/ffi.h $(OUT_DIR)/include/ffitarget.h $(OUT_DIR)/include/fficonfig.h $(OUT_DIR)/include/ffi_common.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# ---- archive ----

$(OUT_DIR)/libffi.a: $(OBJECTS) | $(OUT_DIR)
	$(AR) rcs $@ $(OBJECTS)
	$(RANLIB) $@

$(OUT_DIR):
	mkdir -p $@

$(OUT_DIR)/include:
	mkdir -p $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(OUT_DIR) $(BUILD_DIR)
