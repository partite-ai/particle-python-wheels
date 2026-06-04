// wasi-operator-new.cpp — STRONG operator new/delete for the wasi duckdb module.
//
// libc++abi defines these as WEAK (replaceable per [new.delete]). In the dicej
// PIC dynamic-linking model, a weak symbol is emitted as an *import* so the main
// module can interpose a strong one — but the embedding (CPython, C) provides no
// operator new, so the import is unresolved at load. Providing STRONG definitions
// here makes the linker bind duckdb's (and libc++'s) references to these local,
// and they call the imported, shared-heap malloc/free/aligned_alloc — so the C++
// allocator and Python keep one heap. (--Bsymbolic / --whole-archive do NOT fix
// the weak case; only a strong definition does.)

#include <cstddef>
#include <cstdlib>
#include <new>

namespace {
inline void *do_alloc(std::size_t size) {
	return std::malloc(size ? size : 1);
}
inline void *do_alloc_aligned(std::size_t size, std::size_t align) {
	if (align < sizeof(void *)) {
		align = sizeof(void *);
	}
	if (size == 0) {
		size = 1;
	}
	// C aligned_alloc requires size to be a multiple of alignment.
	std::size_t rounded = (size + align - 1) & ~(align - 1);
	return std::aligned_alloc(align, rounded);
}
} // namespace

// ---- throwing new ---------------------------------------------------------
void *operator new(std::size_t size) {
	if (void *p = do_alloc(size)) {
		return p;
	}
	throw std::bad_alloc();
}
void *operator new[](std::size_t size) { return ::operator new(size); }
void *operator new(std::size_t size, std::align_val_t a) {
	if (void *p = do_alloc_aligned(size, static_cast<std::size_t>(a))) {
		return p;
	}
	throw std::bad_alloc();
}
void *operator new[](std::size_t size, std::align_val_t a) { return ::operator new(size, a); }

// ---- nothrow new ----------------------------------------------------------
void *operator new(std::size_t size, const std::nothrow_t &) noexcept { return do_alloc(size); }
void *operator new[](std::size_t size, const std::nothrow_t &) noexcept { return do_alloc(size); }
void *operator new(std::size_t size, std::align_val_t a, const std::nothrow_t &) noexcept {
	return do_alloc_aligned(size, static_cast<std::size_t>(a));
}
void *operator new[](std::size_t size, std::align_val_t a, const std::nothrow_t &) noexcept {
	return do_alloc_aligned(size, static_cast<std::size_t>(a));
}

// ---- delete (all overloads free the same way; aligned_alloc/malloc → free) -
void operator delete(void *p) noexcept { std::free(p); }
void operator delete[](void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t) noexcept { std::free(p); }
void operator delete(void *p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void *p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void *p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void *p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete(void *p, const std::nothrow_t &) noexcept { std::free(p); }
void operator delete[](void *p, const std::nothrow_t &) noexcept { std::free(p); }
void operator delete(void *p, std::align_val_t, const std::nothrow_t &) noexcept { std::free(p); }
void operator delete[](void *p, std::align_val_t, const std::nothrow_t &) noexcept { std::free(p); }
