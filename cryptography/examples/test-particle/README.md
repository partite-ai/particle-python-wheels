# crypto-version-probe — end-to-end test for the local-wheels path

Smallest particle that proves the locally-built `cryptography` wheel
(`wheels/cryptography/out/cryptography-48.0.0-cp314-abi3-any.whl`)
flows through the build pipeline, lands in the artifact's
`_deps/site-packages`, and is importable from inside the wasm
CPython runtime.

## What it exercises

  - `build.Options.LocalWheelDirs` (defaults to `$PARTICLE_LOCAL_WHEELS`)
    picks up the wheel from disk.
  - Resolver pre-stages it instead of going to PyPI.
  - cryptography's `Requires-Dist: cffi>=2.0.0` is satisfied by the
    stub wheel at `wheels/cffi/out/` (real wasm-native cffi is on
    the followups list).
  - The Python runtime loads `cryptography/__init__.py` from the
    unpacked wheel and the tool returns `cryptography.__version__`.

What it *doesn't* yet exercise: anything that actually pokes the
`_rust.abi3.so` (hashes, ciphers, etc.). The wheel's .so is in the
artifact; loading it via dlopen is the next test.

## Run

Prereqs (from repo root):

  make python-runtime python-bootstrap-zip python-stdlib-zip runtime-embed
  cd wheels/cryptography && make wheel
  cd ../cffi && go run build-stub.go

Then:

  cd wheels/cryptography/examples/test-particle
  PARTICLE_LOCAL_WHEELS=$PWD/../../out:$PWD/../../../cffi/out \
    go run ./run

Expected tail:

  build: ok
  particle: instantiated
  version: {"version": "48.0.0"}

The runner sidesteps the `particle` CLI's keyring sealer (which
needs dbus-launch) by wiring in-memory credential + KV stores
directly through the runtime API. Same code path otherwise.
