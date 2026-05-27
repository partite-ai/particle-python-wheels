#!/bin/sh
# bindgen-cli wrapper: appends `--sysroot=$WASI_SYSROOT` to the
# *clang* args (everything after `--`) so libclang resolves
# <stdint.h> / <stddef.h> / etc. against wasi-libc instead of falling
# back to the host's /usr/include.
#
# openssl-sys's build/run_bindgen.rs invokes `bindgen` with an
# explicit `--target=$TARGET` after `--` but does not pass --sysroot.
# Without sysroot, clang + target still picks up host glibc headers
# and trips on `bits/libc-header-start.h`. BINDGEN_EXTRA_CLANG_ARGS
# would normally cover this, but doesn't reach the CLI invocation
# reliably across our maturin → cargo → build-script chain. Hence
# this shim.
#
# We only append the sysroot when the caller passed a `--` separator;
# without one, the trailing args would be interpreted as bindgen
# flags (and bindgen rejects unknown ones). Also: if --sysroot is
# already present, leave it alone.
set -eu

DIR="$(cd "$(dirname "$0")" && pwd)"
WASI_SYSROOT="${WASI_SYSROOT:-/opt/wasi-sdk/share/wasi-sysroot}"

# Walk args looking for `--`. If found AND no existing --sysroot, append.
seen_sep=0
has_sysroot=0
for arg in "$@"; do
    case "$arg" in
        --)            seen_sep=1 ;;
        --sysroot=*|--sysroot) has_sysroot=1 ;;
    esac
done

if [ "$seen_sep" = "1" ] && [ "$has_sysroot" = "0" ]; then
    exec "$DIR/bindgen-real" "$@" "--sysroot=$WASI_SYSROOT"
else
    exec "$DIR/bindgen-real" "$@"
fi
