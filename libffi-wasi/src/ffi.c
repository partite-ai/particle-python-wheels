/* -----------------------------------------------------------------------
   ffi.c — wasm32-wasip2 Foreign Function Interface
   Adapted from libffi 3.5.2's src/wasm/ffi.c (the emscripten port).

   The emscripten port's EM_JS bodies do three things:
     1. Inspect the ffi_cif's argument types and unbox single-field structs.
     2. Generate (at runtime) a JS-wrapped wasm function that marshals
        a libffi-style avalue[] array into wasm function arguments,
        does call_indirect, and stores the result at rvalue.
     3. Drop that wasm function into the shared __indirect_function_table
        via emscripten's `convertJsFunctionToWasm` + `getEmptyTableSlot`.

   On wasi we have no JS host. Instead, we delegate every code-generation
   step to the runtime host (Go) via WIT imports. The Go host uses
   wasm-encoder to materialize the same kind of marshaling trampoline,
   instantiates it as a tiny wasm module that imports the shared
   __indirect_function_table, and reports back a table slot index.
   Subsequent calls then call_indirect through that slot.

   This file keeps the public libffi entry points (ffi_call,
   ffi_closure_alloc, ffi_prep_closure_loc, etc.) as thin shells over
   `__wasi_libffi_*` host imports. The C types + struct layouts match
   the upstream emscripten port byte-for-byte so cffi (which sees
   libffi via headers) doesn't notice the difference.
   ----------------------------------------------------------------------- */

#include <ffi.h>
#include <ffi_common.h>

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define MAX_ARGS 1024
#define VARARGS_FLAG 1

/* These are imported from the runtime host. Resolved by the dyld env
 * shim at .so-load time to functions in main.wasm that lower into the
 * `particle:host/libffi` WIT interface. See
 * components/python-runtime/src/libffi_host.rs for the Rust side and
 * internal/runtime/libffi/ for the Go-side trampoline generator. */
extern int   __wasi_libffi_call(
    ffi_cif *cif, void (*fn)(void), void *rvalue, void **avalue);
extern void *__wasi_libffi_closure_alloc(size_t size, void **code);
extern void  __wasi_libffi_closure_free(void *closure);
extern int   __wasi_libffi_prep_closure_loc(
    ffi_closure *closure, ffi_cif *cif,
    void (*fun)(ffi_cif *, void *, void **, void *),
    void *user_data, void *codeloc);

/* ----------------------------------------------------------------------
 * ffi_prep_cif_machdep / _var:
 * The portable prep_cif.c calls into these to fill in target-specific
 * bits of the cif. For wasm32-wasi we use FFI_WASM32 (the "raw" ABI
 * — same shape as emscripten's, just renamed) and stash nfixedargs so
 * variadic detection works.
 * ---------------------------------------------------------------------- */

ffi_status FFI_HIDDEN
ffi_prep_cif_machdep(ffi_cif *cif)
{
    if (cif->abi != FFI_WASM32 && cif->abi != FFI_DEFAULT_ABI)
        return FFI_BAD_ABI;
    if (!(cif->flags & VARARGS_FLAG))
        cif->nfixedargs = cif->nargs;
    if (cif->nargs > MAX_ARGS)
        return FFI_BAD_TYPEDEF;
    if (cif->rtype->type == FFI_TYPE_COMPLEX)
        return FFI_BAD_TYPEDEF;
    for (unsigned i = 0; i < cif->nargs; i++)
        if (cif->arg_types[i]->type == FFI_TYPE_COMPLEX)
            return FFI_BAD_TYPEDEF;
    return FFI_OK;
}

ffi_status FFI_HIDDEN
ffi_prep_cif_machdep_var(ffi_cif *cif, unsigned nfixedargs, unsigned ntotalargs)
{
    (void)ntotalargs;
    cif->flags |= VARARGS_FLAG;
    cif->nfixedargs = nfixedargs;
    if (cif->nfixedargs + 1 > MAX_ARGS)
        return FFI_BAD_TYPEDEF;
    return FFI_OK;
}

/* ----------------------------------------------------------------------
 * Public entry points — thin shells that delegate to the host.
 * The host caches a trampoline per unique cif signature, so the cost
 * of the first call_indirect setup is amortized across all subsequent
 * calls with the same signature.
 * ---------------------------------------------------------------------- */

void
ffi_call(ffi_cif *cif, void (*fn)(void), void *rvalue, void **avalue)
{
    __wasi_libffi_call(cif, fn, rvalue, avalue);
}

void *__attribute__((visibility("default")))
ffi_closure_alloc(size_t size, void **code)
{
    return __wasi_libffi_closure_alloc(size, code);
}

void __attribute__((visibility("default")))
ffi_closure_free(void *closure)
{
    __wasi_libffi_closure_free(closure);
}

ffi_status
ffi_prep_closure_loc(ffi_closure *closure, ffi_cif *cif,
                     void (*fun)(ffi_cif *, void *, void **, void *),
                     void *user_data, void *codeloc)
{
    return (ffi_status)__wasi_libffi_prep_closure_loc(
        closure, cif, fun, user_data, codeloc);
}

/* `ffi_prep_closure` (the legacy non-loc variant) is defined for us
 * in prep_cif.c as a portable wrapper around ffi_prep_closure_loc;
 * we don't redefine it here. */
