// wasi-mman-stub.c — local definition of madvise() for the wasi DuckDB module.
//
// wasi-libc ships NO madvise() implementation — only the declaration in
// <sys/mman.h>. DuckDB's storage/block_allocator.cpp calls it purely as an
// advisory memory hint (MADV_DONTNEED / MADV_FREE_REUSABLE) on regions it
// got from mmap(). Under the wasi emulated-mman (mmap == malloc + pread),
// such a region is plain heap memory, so "release these pages early" is a
// no-op and returning 0 ("advice accepted") is the correct, safe answer.
//
// This is linked INTO _duckdb.so so the symbol is defined locally rather
// than imported from env — the embedding (dicej CPython) does not provide
// madvise (nor mmap/munmap: libpython.so links the emulated signal/getpid/
// process-clocks libs but NOT emulated-mman; those come from whole-archiving
// libwasi-emulated-mman.a in the module link — see ../duckdb-python/Makefile).

#include <sys/mman.h>
#include <stddef.h>

int madvise(void *addr, size_t length, int advice) {
	(void)addr;
	(void)length;
	(void)advice;
	return 0;
}
