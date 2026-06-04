# Python wheels for WASI

Cross-builds of selected Python packages for the **`wasm32-wasip2`**
target, plus a PEP 503 simple index that `pip` can consume alongside
PyPI.

The wheels are intended for embedding in WASI-hosted CPython
environments — specifically [dicej's CPython 3.14 fork][cpython-fork]
with dynamic-linking patches, which is what componentize-py uses. They
are tagged `cp314-abi3-wasi_wasm32` so pip ignores them on non-WASI
hosts.

## Consume

From a wasi-targeted pip (i.e. running inside a `wasm32-wasip2` CPython):

```
pip install \
  --extra-index-url https://<owner>.github.io/<repo>/simple/ \
  cffi cryptography pydantic-core regex pyyaml duckdb numpy pandas
```

`--extra-index-url` keeps PyPI as the primary source. Our index only
gets picked when pip is resolving a wasi-compatible wheel of a package
we ship.

## Packaged versions

| Package       | Version    | Wheel tag                   |
| ------------- | ---------- | --------------------------- |
| cffi          | 2.0.0      | `cp314-abi3-wasi_wasm32`    |
| cryptography  | 48.0.0     | `cp314-abi3-wasi_wasm32`    |
| pydantic-core | 2.47.0     | `cp314-abi3-wasi_wasm32`    |
| regex         | 2026.5.9   | `cp314-cp314-wasi_wasm32`   |
| PyYAML        | 6.0.3      | `cp314-cp314-wasi_wasm32`   |
| duckdb        | 1.5.3      | `cp314-cp314-wasi_wasm32`   |
| numpy         | 2.4.6      | `cp314-cp314-wasi_wasm32`   |
| pandas        | 3.0.3      | `cp314-cp314-wasi_wasm32`   |

Versions are pinned in each subdir's Makefile (`CFFI_VERSION`,
`CRYPTOGRAPHY_VERSION`, `PYDANTIC_CORE_VERSION`, `REGEX_VERSION`,
`PYYAML_VERSION`, `DUCKDB_PYTHON_VERSION`, `NUMPY_VERSION`,
`PANDAS_VERSION`). Bump there, push a new `v*` tag, and the release
workflow rebuilds.

duckdb, numpy and pandas form a data stack: duckdb bundles 6 extensions
(parquet/json/icu/inet/excel + remote reads over `wasi:http`); numpy is
BLAS-less with wasm SIMD128; pandas builds on numpy. duckdb's numpy/
pandas/pyarrow integrations are runtime-optional (the methods raise
`ImportError` if the module is absent).

Note: regex and PyYAML are tagged `cp314-cp314` rather than
`cp314-abi3` because their C extensions use CPython internals and
are recompiled per minor version on PyPI. The other three wheels
use abi3, so they remain installable on a hypothetical future cp315+
wasi interpreter.

## Build locally

Prerequisites (the [`.devcontainer/`](./.devcontainer) ships all of
these):

- WASI SDK 30 at `/opt/wasi-sdk` (or set `WASI_SDK_PATH`)
- Rust stable with the `wasm32-wasip2` target
- `cargo`, `uv` (Astral), `curl`, `tar` on `PATH`

```
make            # build wheels, collect into ./dist/, emit ./dist/simple/
make wheels     # just wheels into ./dist/ (skip the index)
make cffi       # one component (recurses into the subdir)
make clean      # wipe ./dist/ and each subdir's outputs
```

First run is slow — CPython cross-build + AWS-LC compile dominate.
Incremental rebuilds are near-instant; child Makefiles handle their
own up-to-date checks. Heavy build trees live under `$HOME/.cache/`
and `$HOME/cargo-target/` (not the workspace) so they survive
`make clean` and don't fight with virtiofs.

## Publishing

Push a `v*` tag. [`.github/workflows/release.yml`](./.github/workflows/release.yml)
then:

1. Cross-builds the wheels (same path as local `make`).
2. Creates a GitHub Release for the tag with the wheels and a
   `SHA256SUMS` manifest as assets.
3. Regenerates the PEP 503 index against the **latest** release and
   deploys it to GitHub Pages at
   `https://partite-ai.github.io/particle-python-wheels/simple/`.

```
git tag v0.1.0
git push --tags
```

**One-time repo setup:** Settings → Pages → Source: *GitHub Actions*.
The default `GITHUB_TOKEN` covers everything else (Releases + Pages
deploy).

To publish a build without advancing the index, tag with a pre-release
suffix (e.g. `v0.2.0rc1`) and mark the resulting Release as a
pre-release. `scripts/build-pages-index.sh` resolves "latest" via
GitHub's non-prerelease-non-draft pointer and will skip it.

`.github/workflows/build.yml` runs on every push to `main` and every PR
as a CI gate — same toolchain, same build steps, but uploads `dist/`
as a workflow artifact instead of publishing.

## Repo layout

```
cpython/         CPython 3.14 cross-build (libpython3.14.so + headers).
                 Supporting input for cffi + cryptography; not a wheel.
                 Builds into $HOME/cargo-target/ — this directory only
                 holds the Makefile.

libffi-wasi/     libffi static archive + headers for wasm32-wasip2.
                 Supporting input for cffi; not a wheel.

cffi/            cffi 2.0.0 wheel. Compiled against the wasm CPython
                 above and our libffi port; assembled by hand
                 (zip-wheel.go) so we don't drag in host setuptools.

cryptography/    pyca/cryptography 48.0.0 wheel. Pulls in AWS-LC as
                 its OpenSSL replacement; driven by maturin.

pydantic-core/   pydantic-core 2.47.0 wheel. Pure-Rust + PyO3; the
                 lightest of the wheel builds (no C deps, no host
                 codegen step).

regex/           regex 2026.5.9 wheel (mrab-regex). Hand-compiled C
                 extension against the wasm CPython, assembled
                 directly (no setuptools); pattern closest to cffi.

pyyaml/          PyYAML 6.0.3 wheel. Also cross-builds libyaml 0.2.5
                 (autotools) and runs Cython on _yaml.pyx → _yaml.c
                 inline — PyYAML 6.0.3's sdist dropped the
                 pre-generated .c. Heaviest of the hand-built wheels.

duckdb/          DuckDB v1.5.3 cross-build as libduckdb_static.a +
                 headers for wasm32-wasip2 (CMake + wasi-sdk;
                 -fwasm-exceptions, single-threaded, no builtin
                 extensions, no httplib). Phase 1: supporting static
                 lib + the three core porting patches; not a wheel.

duckdb-python/   DuckDB Python wheel (cp314-cp314, wasm32-wasip2).
                 Phase 2: rebuilds core and cross-compiles the upstream
                 pybind11 binding against the wasm CPython. No
                 numpy/pandas/pyarrow at compile time — they're
                 runtime-optional (.df()/.arrow()/.fetch_numpy()
                 raise ImportError if absent). Reuses duckdb/'s core
                 tarball + patches.
                 Statically links 6 extensions (no runtime download):
                 core_functions, parquet, json (in-tree); icu (in-tree,
                 +2 ICU-vendored wasi patches); inet (out-of-tree, 0
                 deps); excel (out-of-tree, needs cross-built expat +
                 minizip-ng + cpython's zlib). httpfs is intentionally
                 absent — it needs sockets/TLS wasi-libc lacks; a
                 wasi:http-backed replacement is the path there.

numpy/           NumPy 2.4.6 wheel (cp314-cp314, wasm32-wasip2).
                 meson-python cross-build against the wasm CPython:
                 BLAS-less (reference fallbacks), single-threaded;
                 masquerades as emscripten (-D__EMSCRIPTEN__=1 +
                 cross-file system=emscripten) to reuse numpy's only
                 wasm-capable code paths. -msimd128 lets LLVM
                 auto-vectorize the scalar loops (numpy's own
                 universal-intrinsics have no wasm backend). A clang
                 wrapper strips GNU-ld --start-group (wasm-ld rejects
                 it). One patch disables the dladdr-based temp elision.

pandas/          pandas 3.0.3 wheel (cp314-cp314, wasm32-wasip2).
                 meson-python cross-build reusing the numpy/ machinery
                 (same cross-file, clang wrapper, import-dynamic link,
                 -msimd128). Compiles against a HOST numpy 2.4.6's C
                 headers (numpy is pandas' build-time C-API dep and
                 runtime companion — shipped as the separate numpy/
                 wheel). numpy 2.4's npy_cpu.h already knows __wasm__,
                 so no numpy-header patching is needed.

scripts/         build-pages-index.sh — emits the PEP 503 simple/ tree
                 at release time, pointing at the latest Release's
                 wheels.

.github/         build.yml (CI gate), release.yml (publish on v* tag),
                 actions/setup-wasi-toolchain/ (shared composite).

.devcontainer/   Docker setup for local dev. Mirrors the toolchain the
                 CI composite installs, so a green CI run implies the
                 container also builds clean.

Makefile         Drives the four sub-builds, collects wheels into
                 ./dist/, emits ./dist/simple/.
```

## Notes

**Why this and not just PyPI?** PyPI ships no `wasm32-wasip2` wheels
for `cffi` or `cryptography`. Cross-compiling them is nontrivial —
cryptography wants OpenSSL or a replacement (we use AWS-LC); cffi wants
libffi (we port it via [`libffi-wasi/`](./libffi-wasi)). This repo
collects those builds in one place behind a normal pip index.

**Why dicej's CPython fork?** Stock CPython doesn't expose the symbols
needed to dynamically load extension modules on wasi. The patches at
[`0e13686`][cpython-rev] add `wasm32-wasi` plus a dynamic-linking path,
and the wheel ABI here is built against that exact revision
(`CPYTHON_REV` in [`cpython/Makefile`](./cpython/Makefile)).

**Why platform tag `wasi_wasm32`?** It marks the wheels as
WASI-specific so pip on non-WASI hosts treats them as incompatible
and falls through to PyPI. Without this tag the wheels would be
picked on regular linux/mac/win interpreters too — and fail to load,
because the `.so`s inside are wasm.

[cpython-fork]: https://github.com/dicej/cpython
[cpython-rev]: https://github.com/dicej/cpython/commit/0e13686da8bb881b059d35e23c32bcd2e6440099
