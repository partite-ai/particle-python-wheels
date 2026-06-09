// wasi_http_client.cpp — a DuckDB HTTPClient transport backed by wasi:http.
//
// This is the wasi analogue of duckdb-wasm's lib/src/http_wasm.cc: rather
// than reimplement DuckDB's HTTPFileSystem (range reads, globbing, S3,
// metadata caching), we reuse it wholesale and only swap the *transport*.
// DuckDB makes the transport pluggable through HTTPUtil::InitializeClient,
// which returns an HTTPClient (Get/Head/Put/Post/Delete). duckdb-wasm
// registers a JS-fetch-backed client this way; we register a wasi:http one.
//
// wasi:http's outgoing-handler is request/response-with-headers-and-a-body-
// stream — i.e. fetch — so the mapping is direct.
//
// The `wasi_http_*` symbols come from the wit-bindgen-generated bindings
// (see wit/client.wit + the README). They compile to wasm Component-Model
// imports that the host runtime satisfies (wasmtime, jco, any host that
// wires wasi:http). Verified in the built wheel: the module carries 15
// `wasi:http` imports incl. `outgoing-handler.handle`, plus wasi:io poll/
// streams — and zero openssl/curl symbols.
//
// STATUS: compiled + linked into the duckdb wheel (../duckdb-python builds
// httpfs for wasi with this as its transport). Built + structurally
// validated; not yet runtime-tested in a wasi:http-providing host.

#include "httpfs_client.hpp" // httpfs: HTTPFSUtil, HTTPFSParams (+ core http_util.hpp)

extern "C" {
#include "http_client.h" // wit-bindgen output (vendored)
}

namespace duckdb {

namespace {

// ---- small helpers over the generated C ABI -------------------------------

// wit-bindgen represents `string` as a (ptr,len) view; the callee copies, so
// pointing at the std::string storage is safe for the duration of the call.
inline http_client_string_t WStr(const string &s) {
	http_client_string_t v;
	v.ptr = (uint8_t *)s.data();
	v.len = s.size();
	return v;
}

// A scoped view of a wasi `borrow<T>`. On the guest side of the canonical ABI a
// borrow is just the same i32 as the `own<T>` it views, so this only carries that
// handle — plus, in debug builds, a tally on the owning OwnHandle so it can
// assert no borrow is still live when it drops the handle. Converts implicitly to
// the raw wasi borrow<T> so it can be passed straight to the generated C calls.
template <class Borrow, class Owner>
class Borrowed {
public:
	Borrowed(Borrow value, const Owner &owner) : value(value), owner(&owner) {
		owner.AcquireBorrow();
	}
	Borrowed(Borrowed &&o) noexcept : value(o.value), owner(o.owner) {
		o.owner = nullptr;
	}
	Borrowed(const Borrowed &) = delete;
	Borrowed &operator=(Borrowed &&) = delete;
	Borrowed &operator=(const Borrowed &) = delete;
	~Borrowed() {
		if (owner) {
			owner->ReleaseBorrow();
		}
	}
	operator Borrow() const { // NOLINT: implicit on purpose — feeds the C ABI directly
		return value;
	}

private:
	Borrow value;
	const Owner *owner;
};

// Move-only owner for a wasi `own<T>` handle — unique_ptr-style RAII whose
// deleter is the resource's `*_drop_own`. Destroying it drops the handle; this
// is what keeps Request()'s many exit paths (early returns, the rethrow, handler
// exceptions) leak-free without hand-written cleanup at each one.
//
// borrow<B>() hands out a Borrowed view of the same i32 handle (wit-bindgen emits
// no borrow helper for these types). In debug builds the owner counts outstanding
// borrows and asserts the count is zero before it drops the handle, catching any
// borrow that would dangle past its own's destruction.
template <class Own, void (*DropOwn)(Own)>
class OwnHandle {
public:
	OwnHandle() = default;
	explicit OwnHandle(Own handle) : handle(handle), live(true) {
	}
	OwnHandle(OwnHandle &&o) noexcept : handle(o.handle), live(o.live) {
		D_ASSERT(o.outstanding_borrows == 0); // moving with live borrows would dangle them
		o.live = false;
	}
	OwnHandle &operator=(OwnHandle &&o) noexcept {
		if (this != &o) {
			D_ASSERT(o.outstanding_borrows == 0);
			Reset();
			handle = o.handle;
			live = o.live;
			o.live = false;
		}
		return *this;
	}
	OwnHandle(const OwnHandle &) = delete;
	OwnHandle &operator=(const OwnHandle &) = delete;
	~OwnHandle() {
		Reset();
	}

	// Borrow this handle. Only valid on a named (lvalue) owner — borrowing from a
	// temporary owner would dangle immediately, so that overload is deleted.
	template <class Borrow>
	Borrowed<Borrow, OwnHandle> borrow() const & {
		return Borrowed<Borrow, OwnHandle>(Borrow {handle.__handle}, *this);
	}
	template <class Borrow>
	Borrowed<Borrow, OwnHandle> borrow() const && = delete;

	// Relinquish ownership: the handle is consumed by a callee (a constructor
	// taking own<T>, or outgoing-handler.handle()).
	Own release() {
		live = false;
		return handle;
	}

	// Called only by Borrowed (debug bookkeeping; optimized away in release).
	void AcquireBorrow() const {
		++outstanding_borrows;
	}
	void ReleaseBorrow() const {
		--outstanding_borrows;
	}

private:
	void Reset() {
		if (live) {
			D_ASSERT(outstanding_borrows == 0); // a borrow must not outlive its own
			DropOwn(handle);
			live = false;
		}
	}
	Own handle {};
	bool live = false;
	mutable int outstanding_borrows = 0;
};

using OwnFields = OwnHandle<wasi_http_types_own_fields_t, wasi_http_types_fields_drop_own>;
using OwnRequest = OwnHandle<wasi_http_types_own_outgoing_request_t, wasi_http_types_outgoing_request_drop_own>;
using OwnFuture =
    OwnHandle<wasi_http_types_own_future_incoming_response_t, wasi_http_types_future_incoming_response_drop_own>;
using OwnPollable = OwnHandle<wasi_io_poll_own_pollable_t, wasi_io_poll_pollable_drop_own>;
using OwnResponse = OwnHandle<wasi_http_types_own_incoming_response_t, wasi_http_types_incoming_response_drop_own>;
using OwnBody = OwnHandle<wasi_http_types_own_incoming_body_t, wasi_http_types_incoming_body_drop_own>;
using OwnInputStream = OwnHandle<wasi_io_streams_own_input_stream_t, wasi_io_streams_input_stream_drop_own>;
using OwnOutgoingBody = OwnHandle<wasi_http_types_own_outgoing_body_t, wasi_http_types_outgoing_body_drop_own>;
using OwnOutputStream = OwnHandle<wasi_io_streams_own_output_stream_t, wasi_io_streams_output_stream_drop_own>;

// RAII for the by-value `entries` list returned from fields.entries(), which is
// released with a free(&list) call rather than a resource drop.
struct OwnedHeaderEntries {
	http_client_list_tuple2_field_key_field_value_t list {};
	OwnedHeaderEntries() = default;
	OwnedHeaderEntries(const OwnedHeaderEntries &) = delete;
	OwnedHeaderEntries &operator=(const OwnedHeaderEntries &) = delete;
	~OwnedHeaderEntries() {
		http_client_list_tuple2_field_key_field_value_free(&list);
	}
};

// Parse "https://host:port/path?query" into the pieces wasi:http wants.
struct ParsedUrl {
	bool https = true;
	string authority; // host[:port]
	string path;      // /path?query  (path-with-query)
};

ParsedUrl ParseUrl(const string &url) {
	ParsedUrl r;
	auto pos = url.find("://");
	idx_t after = 0;
	if (pos != string::npos) {
		r.https = url.compare(0, pos, "http") != 0; // "http" → false, else https
		after = pos + 3;
	}
	auto slash = url.find('/', after);
	if (slash == string::npos) {
		r.authority = url.substr(after);
		r.path = "/";
	} else {
		r.authority = url.substr(after, slash - after);
		r.path = url.substr(slash);
	}
	return r;
}

// ---- the wasi:http client -------------------------------------------------

class WasiHTTPClient : public HTTPClient {
public:
	explicit WasiHTTPClient(HTTPFSParams &params) : params(params) {
	}

	// No persistent state to set up (no connection pool — each request is a
	// fresh wasi:http exchange).
	void Initialize(HTTPParams &) override {
	}

	unique_ptr<HTTPResponse> Get(GetRequestInfo &info) override {
		return Request(WASI_HTTP_TYPES_METHOD_GET, info.headers, info.url, nullptr, 0, &info.content_handler,
		               &info.response_handler);
	}
	unique_ptr<HTTPResponse> Head(HeadRequestInfo &info) override {
		return Request(WASI_HTTP_TYPES_METHOD_HEAD, info.headers, info.url, nullptr, 0, nullptr, nullptr);
	}
	unique_ptr<HTTPResponse> Put(PutRequestInfo &info) override {
		// Match the curl transport: send the caller-supplied Content-Type.
		HTTPHeaders hdrs = info.headers;
		hdrs["Content-Type"] = info.content_type;
		return Request(WASI_HTTP_TYPES_METHOD_PUT, hdrs, info.url, info.buffer_in, info.buffer_in_len, nullptr,
		               nullptr);
	}
	unique_ptr<HTTPResponse> Post(PostRequestInfo &info) override {
		HTTPHeaders hdrs = info.headers;
		if (!hdrs.HasHeader("Content-Type")) {
			hdrs["Content-Type"] = "application/octet-stream";
		}
		// `send_post_as_get_request` is a GET carrying a body (non-standard, but
		// some servers want it) — mirror the curl client.
		uint8_t verb = info.send_post_as_get_request ? WASI_HTTP_TYPES_METHOD_GET : WASI_HTTP_TYPES_METHOD_POST;
		auto resp = Request(verb, hdrs, info.url, info.buffer_in, info.buffer_in_len, nullptr, nullptr);
		info.buffer_out = resp->body; // the caller reads the POST response from buffer_out
		return resp;
	}
	unique_ptr<HTTPResponse> Delete(DeleteRequestInfo &info) override {
		return Request(WASI_HTTP_TYPES_METHOD_DELETE, info.headers, info.url, nullptr, 0, nullptr, nullptr);
	}

private:
	HTTPFSParams &params;

	// method_tag is a WASI_HTTP_TYPES_METHOD_* value. When body_data is non-null a
	// request body of body_len bytes is streamed (PUT/POST). HEAD reads no response
	// body; every other method does.
	unique_ptr<HTTPResponse>
	Request(uint8_t method_tag, const HTTPHeaders &headers, const string &url, const_data_ptr_t body_data,
	        idx_t body_len, std::function<bool(const_data_ptr_t, idx_t)> *content_handler,
	        std::function<bool(const HTTPResponse &)> *response_handler) {
		auto parsed = ParseUrl(url);

		// Build a failed HTTPResponse carrying a diagnostic. DuckDB's retry layer
		// treats a non-empty request_error as retryable and, once retries are
		// exhausted, surfaces it — so failures here propagate to the user instead
		// of being silently swallowed.
		auto fail = [&](string msg) {
			auto resp = make_uniq<HTTPResponse>(HTTPStatusCode::InternalServerError_500);
			resp->success = false;
			resp->request_error = std::move(msg);
			return resp;
		};

		// 1. headers → wasi:http fields. `fields` is owned until
		//    constructor_outgoing_request consumes it below.
		OwnFields fields(wasi_http_types_constructor_fields());
		string header_error;
		{
			auto bf = fields.borrow<wasi_http_types_borrow_fields_t>();
			auto append = [&](const string &key, const string &value) -> bool {
				auto k = WStr(key);
				// field-value is list<u8>
				http_client_list_u8_t val {(uint8_t *)value.data(), value.size()};
				wasi_http_types_header_error_t herr {};
				if (!wasi_http_types_method_fields_append(bf, (wasi_http_types_field_key_t *)&k,
				                                          (wasi_http_types_field_value_t *)&val, &herr)) {
					const char *why = herr.tag == WASI_HTTP_TYPES_HEADER_ERROR_FORBIDDEN    ? "forbidden"
					                  : herr.tag == WASI_HTTP_TYPES_HEADER_ERROR_IMMUTABLE  ? "immutable"
					                                                                        : "invalid syntax";
					header_error = "wasi:http rejected header '" + key + "' (" + why + ")";
					return false;
				}
				return true;
			};
			for (auto &h : headers) {
				if (!append(h.first, h.second)) {
					break;
				}
			}
			// DuckDB's HTTP secret (`CREATE SECRET (TYPE http, BEARER_TOKEN ...)`)
			// carries the token out-of-band in HTTPFSParams::bearer_token, NOT as a
			// request header — the curl transport applies it via
			// CURLOPT_XOAUTH2_BEARER. Mirror that here, otherwise the token is
			// silently dropped and authenticated requests come back 403.
			if (header_error.empty() && !params.bearer_token.empty() && !headers.HasHeader("Authorization")) {
				string auth = "Bearer " + params.bearer_token;
				append("Authorization", auth);
			}
		}
		if (!header_error.empty()) {
			return fail(std::move(header_error)); // ~OwnFields drops `fields`
		}

		// 2. outgoing-request + method/scheme/authority/path. The setters reject
		//    malformed values (e.g. a bad authority/path); surface that instead of
		//    dispatching a malformed request. constructor_outgoing_request consumes
		//    `fields`.
		OwnRequest request(wasi_http_types_constructor_outgoing_request(fields.release()));
		auto breq = request.borrow<wasi_http_types_borrow_outgoing_request_t>();

		wasi_http_types_method_t method;
		method.tag = method_tag;
		wasi_http_types_scheme_t scheme;
		scheme.tag = parsed.https ? WASI_HTTP_TYPES_SCHEME_HTTPS : WASI_HTTP_TYPES_SCHEME_HTTP;
		auto authority = WStr(parsed.authority);
		auto pwq = WStr(parsed.path);
		if (!wasi_http_types_method_outgoing_request_set_method(breq, &method) ||
		    !wasi_http_types_method_outgoing_request_set_scheme(breq, &scheme) ||
		    !wasi_http_types_method_outgoing_request_set_authority(breq, &authority) ||
		    !wasi_http_types_method_outgoing_request_set_path_with_query(breq, &pwq)) {
			return fail("wasi:http: failed to build outgoing request for '" + url + "'");
		}

		// 2b. For PUT/POST, take the request's outgoing-body now — it must be
		//     acquired before handle() consumes the request; we stream into it
		//     after dispatch (step 3b).
		OwnOutgoingBody out_body;
		if (body_data) {
			wasi_http_types_own_outgoing_body_t ob_raw;
			if (!wasi_http_types_method_outgoing_request_body(breq, &ob_raw)) {
				return fail("wasi:http: failed to acquire request body for '" + url + "'");
			}
			out_body = OwnOutgoingBody(ob_raw);
		}

		// 3. dispatch (no per-request options for now). handle() consumes the
		//    request handle whether it succeeds or fails.
		wasi_http_types_own_future_incoming_response_t future_raw;
		wasi_http_outgoing_handler_error_code_t send_err;
		bool sent = wasi_http_outgoing_handler_handle(request.release(), nullptr, &future_raw, &send_err);
		if (!sent) {
			wasi_http_outgoing_handler_error_code_free(&send_err);
			return fail("wasi:http outgoing-handler.handle failed for '" + url + "'");
		}
		OwnFuture future(future_raw);

		// 3b. Stream the request body, then finish it. wasi:io's
		//     blocking-write-and-flush caps each call at 4096 bytes, so chunk it.
		//     The output-stream must be dropped before outgoing-body.finish().
		if (body_data) {
			wasi_http_types_own_output_stream_t os_raw;
			if (!wasi_http_types_method_outgoing_body_write(out_body.borrow<wasi_http_types_borrow_outgoing_body_t>(),
			                                                &os_raw)) {
				return fail("wasi:http: failed to open request body stream for '" + url + "'");
			}
			{
				OwnOutputStream out_stream(os_raw);
				auto bos = out_stream.borrow<wasi_io_streams_borrow_output_stream_t>();
				for (idx_t off = 0; off < body_len;) {
					idx_t n = body_len - off;
					if (n > 4096) {
						n = 4096;
					}
					http_client_list_u8_t chunk {(uint8_t *)(body_data + off), (size_t)n};
					wasi_io_streams_stream_error_t serr {};
					if (!wasi_io_streams_method_output_stream_blocking_write_and_flush(bos, &chunk, &serr)) {
						string msg = "wasi:http: failed to write request body for '" + url + "'";
						if (serr.tag == WASI_IO_STREAMS_STREAM_ERROR_LAST_OPERATION_FAILED) {
							auto berr = wasi_io_error_borrow_error(serr.val.last_operation_failed);
							http_client_string_t dbg {};
							wasi_io_error_method_error_to_debug_string(berr, &dbg);
							msg += ": " + string((char *)dbg.ptr, dbg.len);
							http_client_string_free(&dbg);
							wasi_io_streams_stream_error_free(&serr);
						}
						return fail(std::move(msg));
					}
					off += n;
				}
			} // out_stream dropped here — required before finish()
			wasi_http_types_error_code_t fin_err;
			if (!wasi_http_types_static_outgoing_body_finish(out_body.release(), nullptr, &fin_err)) {
				wasi_http_types_error_code_free(&fin_err);
				return fail("wasi:http: failed to finish request body for '" + url + "'");
			}
		}

		// 4. block on the future until the response head is ready
		auto bfut = future.borrow<wasi_http_types_borrow_future_incoming_response_t>();
		OwnPollable pollable(wasi_http_types_method_future_incoming_response_subscribe(bfut));
		wasi_io_poll_method_pollable_block(pollable.borrow<wasi_io_poll_borrow_pollable_t>());

		wasi_http_types_result_result_own_incoming_response_error_code_void_t got;
		if (!wasi_http_types_method_future_incoming_response_get(bfut, &got) || got.is_err /*outer*/ ||
		    got.val.ok.is_err /*inner: transport error*/) {
			// On failure `got` may own an error-code — free the whole result. (On
			// success we instead move the incoming-response out of it, below, so we
			// must NOT free it there.)
			wasi_http_types_result_result_own_incoming_response_error_code_void_free(&got);
			return fail("wasi:http request failed for '" + url + "'");
		}
		OwnResponse incoming(got.val.ok.val.ok); // own<incoming-response>, moved out of `got`
		auto bresp = incoming.borrow<wasi_http_types_borrow_incoming_response_t>();

		// 5. status + headers
		auto status = wasi_http_types_method_incoming_response_status(bresp);
		auto resp = make_uniq<HTTPResponse>(static_cast<HTTPStatusCode>(status));
		resp->url = url;
		resp->reason = HTTPUtil::GetStatusMessage(resp->status);
		{
			OwnFields rh(wasi_http_types_method_incoming_response_headers(bresp)); // headers = own<fields>
			OwnedHeaderEntries entries;
			wasi_http_types_method_fields_entries(rh.borrow<wasi_http_types_borrow_fields_t>(), &entries.list);
			for (size_t i = 0; i < entries.list.len; i++) {
				string key((char *)entries.list.ptr[i].f0.ptr, entries.list.ptr[i].f0.len);
				string val((char *)entries.list.ptr[i].f1.ptr, entries.list.ptr[i].f1.len);
				resp->headers.Insert(key, val);
			}
		}
		if (response_handler) {
			try {
				if (!(*response_handler)(*resp)) {
					// Handler asked us to stop reading the body (e.g. it will fall
					// back to a full download) — hand back the response as-is.
					return resp;
				}
			} catch (...) {
				// DuckDB's response handlers THROW on error statuses (>= 400). If we
				// let that unwind out of the transport, the request ends up with a
				// null response, which the retry layer — with try_request set, as
				// range requests use — turns into a status-0 response, surfacing as
				// the bogus "HTTP 0 Internal Server Error" instead of the real code.
				// For error statuses the handler does nothing but throw, so swallow
				// it and return the response so the true status (e.g. 403) is
				// reported. For success statuses the exception is meaningful (ETag /
				// version-id checks) — let it propagate.
				if (static_cast<int>(resp->status) >= 400) {
					resp->success = false;
					return resp;
				}
				throw;
			}
		}

		// 6. body: consume → incoming-body → input-stream → blocking-read loop.
		//    HEAD carries no body (DuckDB only needs the headers for
		//    OpenFile/GetFileSize); every other method may return one.
		if (method_tag != WASI_HTTP_TYPES_METHOD_HEAD) {
			wasi_http_types_own_incoming_body_t body_raw;
			if (!wasi_http_types_method_incoming_response_consume(bresp, &body_raw)) {
				return fail("wasi:http: failed to consume response body for '" + url + "'");
			}
			OwnBody body(body_raw);
			wasi_http_types_own_input_stream_t stream_raw;
			if (!wasi_http_types_method_incoming_body_stream(body.borrow<wasi_http_types_borrow_incoming_body_t>(),
			                                                 &stream_raw)) {
				return fail("wasi:http: failed to open response body stream for '" + url + "'");
			}
			OwnInputStream stream(stream_raw);
			auto bstream = stream.borrow<wasi_io_streams_borrow_input_stream_t>();
			string read_error;
			for (;;) {
				http_client_list_u8_t chunk;
				wasi_io_streams_stream_error_t serr {};
				if (!wasi_io_streams_method_input_stream_blocking_read(bstream, 1u << 16, &chunk, &serr)) {
					// A failed read is NOT necessarily EOF. `closed` means the body
					// ended cleanly; `last-operation-failed` is a real transport
					// error. Treating the latter as EOF would hand DuckDB a TRUNCATED
					// body as if it were complete — silent data corruption. So
					// distinguish them and fail loudly on a genuine error.
					if (serr.tag == WASI_IO_STREAMS_STREAM_ERROR_LAST_OPERATION_FAILED) {
						auto berr = wasi_io_error_borrow_error(serr.val.last_operation_failed);
						http_client_string_t dbg {};
						wasi_io_error_method_error_to_debug_string(berr, &dbg);
						read_error.assign((char *)dbg.ptr, dbg.len);
						http_client_string_free(&dbg);
						wasi_io_streams_stream_error_free(&serr);
					}
					break; // closed → clean EOF; last-operation-failed → read_error set
				}
				if (chunk.len) {
					if (content_handler) {
						(*content_handler)((const_data_ptr_t)chunk.ptr, chunk.len);
					} else {
						resp->body.append((char *)chunk.ptr, chunk.len);
					}
				}
				http_client_list_u8_free(&chunk);
			}
			if (!read_error.empty()) {
				resp->success = false;
				resp->request_error = "wasi:http body read failed for '" + url + "': " + read_error;
			}
		}
		return resp;
	}
};

// ---- the pluggable util that hands DuckDB our client ----------------------

class WasiHTTPUtil : public HTTPFSUtil {
public:
	unique_ptr<HTTPClient> InitializeClient(HTTPParams &http_params, const string &proto_host_port) override {
		return make_uniq<WasiHTTPClient>(http_params.Cast<HTTPFSParams>());
	}
	string GetName() const override {
		return "WasiHTTPUtil";
	}
};

} // namespace

// Built by the httpfs extension's Load (httpfs_extension.cpp) on __wasi__ and
// installed via DBConfig::SetHTTPUtil — the same seam duckdb-wasm uses to swap
// in its fetch transport. From then on httpfs's HTTPFileSystem talks wasi:http.
shared_ptr<HTTPUtil> CreateWasiHTTPUtil() {
	return make_shared_ptr<WasiHTTPUtil>();
}

} // namespace duckdb
