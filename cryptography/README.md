# cryptography wheel for wasm32-wasip2 + CPython 3.14

Standalone experiment: build pyca/cryptography as an installable wheel for
the wasm32-wasip2 CPython 3.14 we link into particle-python-runtime. This
folder is intended to be lifted into its own repo later — every input
(maturin, cmake, source tarballs) is bootstrapped here rather than
relying on the rest of this project.

Locations:

  - `.cache/`       — curl-downloaded source/binary tarballs. In-workspace,
                      keyed on upstream version, survives `make clean`.
  - `out/`          — final wheel goes here.
  - `$WORKDIR/`     — bootstrapped maturin + cmake, extracted sources, CMake
                      build trees. Defaults to
                      `$HOME/.cache/particle/wheels-cryptography/`. Lives
                      off the workspace mount because tar populates symlinks
                      with utime() calls, which virtiofs blocks on the
                      workspace volume in this dev container. Override by
                      setting `WORKDIR=...` if you don't have that
                      restriction.

## Status

Work-in-progress. The build pipeline is in three stages, each tracked
by a Makefile target:

  1. **Toolchain bootstrap** (`make toolchain`) — installs maturin via
     `cargo install --root`, downloads a CMake binary release, copies
     the PyO3 cross-compile config in place. Idempotent.
  2. **AWS-LC for wasm32-wasip2** (`make aws-lc`) — clones AWS-LC at a
     pinned revision, drives its CMake build with WASI SDK's
     `wasi-sdk-p2.cmake` toolchain file, produces static
     libcrypto.a / libssl.a / libpki.a for the cryptography crate to
     link against.
  3. **cryptography wheel** (`make wheel`) — extracts cryptography
     48.0.0 sdist, patches `[patch.crates-io]` to route openssl-sys at
     an AWS-LC-aware fork, runs maturin with PYO3_CONFIG_FILE set so
     the build cross-compiles cleanly without a host CPython.

## Why cryptography first?

It's the worst-case Python wheel:

  - PyO3-driven Rust extension (cross-compile via PYO3_CONFIG_FILE)
  - C glue via the cffi crate (cross-compile via cc-rs honoring our
    CC env)
  - Hard dep on a TLS/crypto C library (the AWS-LC bootstrap above)
  - 9-crate Rust workspace inside the sdist

If this builds end-to-end, the simpler wheels (cffi-free pure Python +
PyO3 extensions like ed25519, msgpack, etc.) come along trivially.

## Dependencies on the surrounding project

Exactly one read-only dependency: the wasm32-wasip2 CPython 3.14 built
by the repo's root `make python-lib`. We pick it up from
`$HOME/cargo-target/cpython-$(CPYTHON_REV)/cpython/builddir/wasi/`.

To move this folder to a separate repo we'd swap that for either
(a) a downloadable CPython tarball, or (b) a brief recipe that rebuilds
CPython here. Either is a single Makefile section.

## Open questions

  - **Wheel platform tag.** maturin doesn't know `wasm32-wasip2` as a
    pip-installable platform. We'll likely have to re-tag the wheel
    with a custom tag (e.g. `cp314-cp314-wasi_wasm32` or just
    `any.wasi_wasm32`) and adjust the runtime's wheel-install path to
    recognize it. The runtime currently mounts wheels via
    `/particle/_deps/site-packages/` — no real pip in the loop.
  - **openssl-sys → aws-lc-sys swap.** Upstream cryptography supports
    AWS-LC behind `CRYPTOGRAPHY_IS_AWSLC` cfg but still depends on the
    `openssl` / `openssl-sys` crates. We'll need a `[patch.crates-io]`
    pointing those at AWS-LC-compatible forks. The PyCA CI scripts at
    .github/workflows/ci.yml show the exact patch.
  - **wasm32-wasi vs wasm32-wasip2.** AWS-LC's CI tests against
    wasm32-wasi (preview1). wasi-sdk-p2.cmake targets preview2. ABI
    diverges at the syscall layer; we'll find out at link time
    whether the static lib travels.
  - **Threading.** AWS-LC defaults to threads-enabled. Single-threaded
    wasm needs `-DDISABLE_PERL=ON -DBUILD_LIBSSL=OFF -DBORINGSSL_PREFIX=...`
    plus `-DOPENSSL_NO_THREADS_BOULDER=1` or similar. To be
    discovered as the cmake configure step lands.
