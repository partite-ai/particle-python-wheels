// wasi-mman-stub.c — local definitions of a few libc/runtime symbols that
// neither wasi-sdk nor the embedding provides, so the module is self-sufficient
// for them instead of failing to load on an unresolved import.
//
// All are linked INTO _duckdb.so (see ../duckdb-python/Makefile, DPY_MODULE_LDFLAGS).
//
//  * madvise / mlock / munlock — wasi has no paging or memory-locking, and
//    wasi-libc ships no implementation (only declarations). DuckDB calls them
//    as advisory/best-effort (block_allocator memory hints; encryption_key_
//    manager locking key pages). No-ops returning 0 ("done/accepted") are the
//    correct, safe answer on a single linear-memory target.
//  * __cxa_thread_atexit_impl — the libc backend for C++ thread_local
//    destructors; absent from wasi-libc. wasip2 is single-threaded, so a
//    thread_local's lifetime == the program's: forward to __cxa_atexit (which
//    the embedding provides) so destructors still run at teardown.

#include <stddef.h>

// madvise advice args / mlock are declared in <sys/mman.h>, but it #errors
// without _WASI_EMULATED_MMAN and we don't need the macros here — declare the
// POSIX prototypes directly to keep this TU self-contained.
int madvise(void *addr, size_t length, int advice) {
	(void)addr;
	(void)length;
	(void)advice;
	return 0;
}

int mlock(const void *addr, size_t len) {
	(void)addr;
	(void)len;
	return 0;
}

int munlock(const void *addr, size_t len) {
	(void)addr;
	(void)len;
	return 0;
}

// __cxa_atexit is provided by the embedding's libc; same signature, so a
// single-threaded thread-atexit is just a global atexit.
extern int __cxa_atexit(void (*func)(void *), void *arg, void *dso);

int __cxa_thread_atexit_impl(void (*func)(void *), void *obj, void *dso) {
	return __cxa_atexit(func, obj, dso);
}
