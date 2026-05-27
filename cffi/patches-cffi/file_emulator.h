/* particle replacement for cffi's src/c/file_emulator.h.
 *
 * Upstream's PyFile_AsFile calls dup() to clone an fd so the wrapped
 * FILE* outlives the originating Python file object.
 * wasi:filesystem has no fd-clone primitive — wasi-libc's unistd.h
 * literally carries the comment "WASI has no dup". Rather than ship
 * a broken dup() stub that fails deep inside PyObject_CallMethod /
 * fdopen with a misleading OSError, we remove the code path:
 *
 *   - PyFile_Check is hard-defined as 0, so cffi's three CT_IS_FILE
 *     branches in _cffi_backend.c (the FILE*-argument and
 *     FILE*-cast paths) treat every object as "not a file" and fall
 *     through to their normal type-coercion paths.
 *   - init_file_emulator returns 0 so the module init still succeeds.
 *   - PyFile_AsFile traps. Lexical call sites remain in
 *     _cffi_backend.c but PyFile_Check guards every one, so they're
 *     dead code in the compiled wheel. If a new caller appears that
 *     bypasses the guard, the trap surfaces it loud and early.
 *
 * If a real consumer of cffi FILE* support surfaces, the fix is to
 * extend particle:host/filesystem with an fd-dup operation and
 * restore the upstream emulator.
 */

static int init_file_emulator(void) { return 0; }

#define PyFile_Check(p) 0

static FILE *PyFile_AsFile(PyObject *ob_file)
{
    (void)ob_file;
    __builtin_trap();
}
