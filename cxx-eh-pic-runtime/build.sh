#!/usr/bin/env bash
# build.sh — build a PIC + exceptions C++ runtime (libc++ / libc++abi /
# libunwind) for wasm32-wasip2, which stock wasi-sdk does NOT ship.
#
# WHY: wasi-sdk 33 ships a noeh/ (PIC, no exceptions) and an eh/ (exceptions,
# but NON-PIC) multilib. DuckDB needs C++ exceptions AND our wheel is a
# -fPIC -shared side module (dicej dynamic linking), so neither stock variant
# works. wasi-sdk deliberately builds eh non-PIC (cmake/wasi-sdk-sysroot.cmake:
# "lots of builds fail with shared libraries and -fPIC ... left for a future
# endeavor"). We rebuild the LLVM runtimes with pic=ON + exceptions=ON, static
# only, mirroring wasi-sdk's define_libcxx_sub() settings otherwise.
set -euo pipefail

# cmake: use the one the duckdb-python Makefile provisions (passed in as $CMAKE),
# falling back to PATH. We drive it with cmake's default generator (Unix
# Makefiles) so no ninja is required — matching the rest of this repo.
CMAKE="${CMAKE:-cmake}"

SDK="${WASI_SDK_PATH:-/opt/wasi-sdk}"             # devcontainer wasi-sdk (33+)
LLVM_SRC="${LLVM_SRC:-$HOME/llvm-project}"        # llvmorg-22.1.0, patched
BUILD="${BUILD:-$HOME/cxx-eh-pic-build}"
PREFIX="${PREFIX:-$HOME/cxx-eh-pic-install}"

SYSROOT="$SDK/share/wasi-sysroot"
RESDIR="$($SDK/bin/clang -print-resource-dir)"
# -fPIC is the whole point; -fwasm-exceptions selects the EH codegen + ABI.
# -I$LLVM_SRC/libc: libcxx's charconv (from_chars_floating_point.h) includes the
# llvm-libc "shared/" headers (shared/fp_bits.h → src/__support/FPUtil/...).
FLAGS="-mcpu=lime1 -fwasm-exceptions -mllvm -wasm-use-legacy-eh=false -fPIC --sysroot $SYSROOT -resource-dir $RESDIR -I$LLVM_SRC/libc"

JOBS="${JOBS:-$(nproc)}"
WASI_SDK_SRC="${WASI_SDK_SRC:-$HOME/wasi-sdk-src}"   # wasi-sdk repo (for the patches)

# Fetch the LLVM runtimes + wasi-sdk patch sources if absent, at versions that
# MATCH the installed SDK (so a fresh devcontainer can rebuild this from clean).
LLVM_VER="$(awk '/^llvm-version:/{print $2}' "$SDK/VERSION")"        # e.g. 22.1.0
WASI_SDK_VER="$(head -1 "$SDK/VERSION" | cut -d. -f1)"               # e.g. 33
if [ ! -d "$LLVM_SRC/runtimes" ]; then
  git clone --depth 1 --branch "llvmorg-$LLVM_VER" --filter=blob:none --sparse \
    https://github.com/llvm/llvm-project.git "$LLVM_SRC"
  git -C "$LLVM_SRC" sparse-checkout set \
    libcxx libcxxabi libunwind runtimes cmake llvm/cmake libc
fi
if [ ! -d "$WASI_SDK_SRC/src" ]; then
  git clone --depth 1 --branch "wasi-sdk-$WASI_SDK_VER" \
    https://github.com/WebAssembly/wasi-sdk.git "$WASI_SDK_SRC"
fi

# Apply the LLVM patches wasi-sdk applies to the runtimes (idempotent):
#  168449 libunwind/config.h; 186054 libcxxabi cxa_thread_atexit; 185770 moves
#  the __cpp_exception tag into libunwind (only the libunwind hunk — the
#  compiler-rt hunks need a source tree we don't check out). Without 185770 the
#  wasm EH tag is imported and unsatisfiable in a self-contained module.
( cd "$LLVM_SRC"
  for p in 168449 186054; do
    git apply --check "$WASI_SDK_SRC/src/llvm-pr-$p.patch" 2>/dev/null && \
      git apply "$WASI_SDK_SRC/src/llvm-pr-$p.patch"
  done
  git apply --check --include='libunwind/src/Unwind-wasm.c' "$WASI_SDK_SRC/src/llvm-pr-185770.patch" 2>/dev/null && \
    git apply --include='libunwind/src/Unwind-wasm.c' "$WASI_SDK_SRC/src/llvm-pr-185770.patch"
  true )

rm -rf "$BUILD"
"$CMAKE" -S "$LLVM_SRC/runtimes" -B "$BUILD" \
  -DCMAKE_TOOLCHAIN_FILE="$SDK/share/cmake/wasi-sdk-p2.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PREFIX" \
  -DCMAKE_C_COMPILER_WORKS=ON -DCMAKE_CXX_COMPILER_WORKS=ON \
  -DCMAKE_SYSROOT="$SYSROOT" \
  -DCMAKE_C_LINKER_DEPFILE_SUPPORTED=OFF -DCMAKE_CXX_LINKER_DEPFILE_SUPPORTED=OFF \
  -DLLVM_ENABLE_RUNTIMES="libunwind;libcxx;libcxxabi" \
  -DLLVM_COMPILER_CHECKED=ON \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_INCLUDE_DOCS=OFF \
  -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DLIBCXX_ENABLE_SHARED=OFF \
  -DLIBCXX_ENABLE_EXCEPTIONS=ON \
  -DLIBCXX_ENABLE_FILESYSTEM=ON \
  -DLIBCXX_ENABLE_ABI_LINKER_SCRIPT=OFF \
  -DLIBCXX_CXX_ABI=libcxxabi \
  -DLIBCXX_HAS_MUSL_LIBC=OFF \
  -DLIBCXX_ABI_VERSION=2 \
  -DLIBCXX_ENABLE_THREADS=ON -DLIBCXX_HAS_PTHREAD_API=ON \
  -DLIBCXX_HAS_EXTERNAL_THREAD_API=OFF -DLIBCXX_HAS_WIN32_THREAD_API=OFF \
  -DLIBCXX_INCLUDE_TESTS=OFF -DLIBCXX_INCLUDE_BENCHMARKS=OFF \
  -DLIBCXXABI_ENABLE_SHARED=OFF \
  -DLIBCXXABI_ENABLE_EXCEPTIONS=ON \
  -DLIBCXXABI_SILENT_TERMINATE=ON \
  -DLIBCXXABI_USE_LLVM_UNWINDER=ON \
  -DLIBCXXABI_ENABLE_THREADS=ON -DLIBCXXABI_HAS_PTHREAD_API=ON \
  -DLIBCXXABI_HAS_EXTERNAL_THREAD_API=OFF -DLIBCXXABI_HAS_WIN32_THREAD_API=OFF \
  -DLIBUNWIND_ENABLE_SHARED=OFF \
  -DLIBUNWIND_ENABLE_THREADS=ON \
  -DLIBUNWIND_USE_COMPILER_RT=ON \
  -DLIBUNWIND_INCLUDE_TESTS=OFF \
  -DUNIX=ON \
  -DCMAKE_C_FLAGS="$FLAGS" -DCMAKE_ASM_FLAGS="$FLAGS" -DCMAKE_CXX_FLAGS="$FLAGS"

"$CMAKE" --build "$BUILD" --parallel "$JOBS"
"$CMAKE" --install "$BUILD"
echo "=== installed PIC+eh runtime to $PREFIX ==="
find "$PREFIX" -name '*.a' | while read -r a; do
  echo "  $a  __cxa_throw=$("$SDK/bin/llvm-nm" "$a" 2>/dev/null | grep -cwE 'T __cxa_throw')"
done
