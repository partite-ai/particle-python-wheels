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
  cffi cryptography
```

`--extra-index-url` keeps PyPI as the primary source. Our index only
gets picked when pip is resolving a wasi-compatible wheel of a package
we ship.

## Packaged versions

| Package      | Version | Wheel tag                    |
| ------------ | ------- | ---------------------------- |
| cffi         | 2.0.0   | `cp314-abi3-wasi_wasm32`     |
| cryptography | 48.0.0  | `cp314-abi3-wasi_wasm32`     |

Versions are pinned in each subdir's Makefile (`CFFI_VERSION`,
`CRYPTOGRAPHY_VERSION`). Bump there, push a new `v*` tag, and the
release workflow rebuilds.

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
