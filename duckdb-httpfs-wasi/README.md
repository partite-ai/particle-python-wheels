# duckdb-httpfs-wasi — remote file access for the wasi DuckDB wheel

DuckDB's `httpfs` extension can't be built for wasm32-wasip2 the normal way:
it depends on `curl` + `openssl`, i.e. real TLS over real sockets, and
**wasi-libc has no BSD sockets**. That's exactly why duckdb-wasm throws
httpfs's transport away and reimplements it in JavaScript
([`lib/src/http_wasm.cc`](https://github.com/duckdb/duckdb-wasm/blob/v1.33.0/lib/src/http_wasm.cc)).

This component does the wasi equivalent: **reuse httpfs's `HTTPFileSystem`
(range reads, globbing, S3, metadata caching) and swap only the transport**
for one backed by [`wasi:http`](https://github.com/WebAssembly/wasi-http).
`wasi:http`'s outgoing-handler is request/response-with-headers-and-a-body-
stream — i.e. `fetch` — so the mapping onto DuckDB's `HTTPClient` is direct.

## How DuckDB makes this possible (the seam)

- `HTTPClient` (core `duckdb/common/http_util.hpp`) is the transport:
  `Get/Head/Put/Post/Delete` → `HTTPResponse` (status + headers + body).
- `HTTPUtil::InitializeClient()` is the factory; `httpfs`'s `HTTPFileSystem`
  asks the registered `HTTPUtil` for a client per request.
- `DBConfig::SetHTTPUtil()` swaps the util at runtime. duckdb-wasm registers
  its fetch client this way; `RegisterWasiHTTPUtil()` here does the same with
  a `wasi:http` client.

So we change **no** DuckDB filesystem logic — only which `HTTPClient` the
`HTTPFileSystem` gets.

## Files

- `wit/client.wit` — a minimal `wasi:http` *client* world (imports only:
  `outgoing-handler` + `types` + `wasi:io` streams/poll).
- `wasi_http_client.cpp` — `WasiHTTPClient : HTTPClient` (Get/Head over
  `wasi:http`), `WasiHTTPUtil : HTTPFSUtil`, and `RegisterWasiHTTPUtil()`.

## Generating the bindings

```
# wit-bindgen-cli (cargo install wit-bindgen-cli) + the wasi-http WIT tree
wit-bindgen c --world http-client <wasi-http>/wit/ --out-dir gen
```
→ `gen/http_client.{c,h}`. The `wasi_http_*` calls become wasm imports
(`__wasm_import_wasi_http_outgoing_handler_handle`, …) that the host
runtime satisfies.

## Status — BUILT (2026-06-03)

Integrated and compiled into the duckdb wheel. `../duckdb-python` builds the
`httpfs` extension for wasi (via `BUILD_EXTENSIONS`, patches in
`../duckdb-python/patches-httpfs/`) with this client as its transport.
Verified in the built `_duckdb.cpython-314-wasm32-wasi.so`:
- all 7 extensions registered incl. `httpfs`;
- **15 `wasi:http` Component imports** incl.
  `(import "wasi:http/outgoing-handler@0.2.0" "handle")` + the request/
  response/fields/body-stream methods, plus `wasi:io/poll` & `streams`;
- **zero openssl/curl symbols** (crypto.cpp excluded — see below).

Built + structurally validated; not yet runtime-tested in a wasi:http-
providing host (the repo-wide caveat).

## How it was integrated (done)

`httpfs` is built for wasi as a `BUILD_EXTENSIONS` entry (out-of-tree, ref
`52afb42`), patched to follow httpfs's existing EMSCRIPTEN seam for
`__wasi__` (patches in `../duckdb-python/patches-httpfs/`):

1. **CMake** (`0001`, `0002`): on `CMAKE_SYSTEM_NAME == WASI`, skip
   `find_package(OpenSSL/CURL)`, drop the curl/httplib client sources, link
   only core's `duckdb_mbedtls`, and add this client + the vendored bindings.
2. **No transport crypto.** `wasi:http` runs on the host, which terminates
   TLS — the module never does a handshake/cert/cipher. We also **exclude
   `crypto.cpp`** (the openssl AES-GCM encrypted-files path, used only under
   `OVERRIDE_ENCRYPTION_UTILS`, off on wasi) → **zero openssl/curl in the
   module** (verified). S3-signing hashes, if used, come from
   `hash_functions.cpp` over core's mbedtls — not openssl, not the transport.
3. **Transport swap** (`0003`): `httpfs_extension.cpp` registers
   `CreateWasiHTTPUtil()` on `__wasi__` via `DBConfig::SetHTTPUtil` — the same
   seam duckdb-wasm uses. `HTTPFileSystem` (range reads, globbing, …) is
   reused unchanged.
4. `wasi_http_client.cpp` + the generated `http_client.c` compile + link in;
   the `wasi:http` calls become the 15 Component imports above.

Current scope: read-only `https://`/`http://` (ranged GET + HEAD) — enough to
`SELECT * FROM 'https://.../data.parquet'` once a host provides `wasi:http`.
S3 auth and writes are future work (Put/Post/Delete throw today).
