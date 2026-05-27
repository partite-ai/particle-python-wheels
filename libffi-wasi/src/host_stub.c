/* host_stub.c — temporary stand-ins for the host imports declared in
 * ffi.c. These let libffi.a build and link standalone so we can
 * iterate on the C side without the runtime host plumbed in. Each
 * stub calls abort() — the build artifact is correct but any actual
 * FFI dispatch traps at runtime with a clear signal.
 *
 * Once the WIT interface is implemented in the python-runtime crate
 * + the Go-side trampoline generator is online, this file is
 * replaced (or left as a fallback for unit tests of the C side).
 *
 * Marked WEAK so the eventual main.wasm-side implementations (which
 * lower into `particle:host/libffi`) take precedence when linking
 * the full python-runtime composition. Standalone builds (e.g.,
 * unit-testing libffi.a in isolation) get the abort() stubs.
 */

#include <ffi.h>
#include <stddef.h>
#include <stdlib.h>

__attribute__((weak))
int __wasi_libffi_call(ffi_cif *cif, void (*fn)(void), void *rvalue, void **avalue)
{
    (void)cif; (void)fn; (void)rvalue; (void)avalue;
    abort();
    return FFI_BAD_ABI;
}

__attribute__((weak))
void *__wasi_libffi_closure_alloc(size_t size, void **code)
{
    (void)size; (void)code;
    abort();
    return NULL;
}

__attribute__((weak))
void __wasi_libffi_closure_free(void *closure)
{
    (void)closure;
    abort();
}

__attribute__((weak))
int __wasi_libffi_prep_closure_loc(
    ffi_closure *closure, ffi_cif *cif,
    void (*fun)(ffi_cif *, void *, void **, void *),
    void *user_data, void *codeloc)
{
    (void)closure; (void)cif; (void)fun; (void)user_data; (void)codeloc;
    abort();
    return FFI_BAD_ABI;
}
