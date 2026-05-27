/* fficonfig.h — hand-written for wasm32-wasip2.
 *
 * libffi normally generates this via autoconf. We don't run configure
 * for the wasi target; instead we hand-pick the flags that match our
 * environment. wasm32-wasip2 has:
 *   - dlfcn.h (via libdl.so, our dyld implementation)
 *   - inttypes.h, stdint.h, string.h, stdlib.h
 *   - long double is 128-bit IEEE quad on wasm32-wasi (see clang docs)
 *   - no native trampoline support (we emulate via host-generated wasm)
 *   - no .eh_frame / .cfi pseudo-ops (no asm port)
 *
 * Anything we don't define here libffi treats as "feature off", which
 * is the right default for a target where we provide every runtime
 * dispatch via host imports rather than CPU-level trampolines.
 */

#ifndef FFICONFIG_H
#define FFICONFIG_H

#define EH_FRAME_FLAGS "a"

#define HAVE_ALLOCA 1
#define HAVE_ALLOCA_H 1
#define HAVE_DLFCN_H 1
#define HAVE_HIDDEN_VISIBILITY_ATTRIBUTE 1
#define HAVE_INTTYPES_H 1
#define HAVE_LONG_DOUBLE 1
#define HAVE_MEMCPY 1
#define HAVE_MEMORY_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1

#define LT_OBJDIR ".libs/"
#define PACKAGE "libffi"
#define PACKAGE_BUGREPORT "http://github.com/libffi/libffi/issues"
#define PACKAGE_NAME "libffi"
#define PACKAGE_STRING "libffi 3.5.2"
#define PACKAGE_TARNAME "libffi"
#define PACKAGE_URL ""
#define PACKAGE_VERSION "3.5.2"
#define STDC_HEADERS 1
#define VERSION "3.5.2"

/* libffi uses these to pick mmap-based JIT vs. alternative paths.
 * On wasi we have neither — the host generates wasm modules. So
 * leave both undefined; ffi_closure_alloc / ffi_prep_closure_loc
 * route to host imports regardless. */
/* #undef FFI_EXEC_TRAMPOLINE_TABLE */
/* #undef FFI_MMAP_EXEC_WRIT */

/* configure.ac chooses one of three FFI_HIDDEN forms based on
 * the compiler. clang on wasm32-wasip2 supports the GCC-style
 * visibility attribute, which is what we use. */
#define FFI_HIDDEN __attribute__ ((visibility ("hidden")))

#endif /* FFICONFIG_H */
