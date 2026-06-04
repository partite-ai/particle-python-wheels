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
		return Request(info, /*method_get=*/true, info.headers, info.url, &info.content_handler,
		               &info.response_handler);
	}
	unique_ptr<HTTPResponse> Head(HeadRequestInfo &info) override {
		return Request(info, /*method_get=*/false, info.headers, info.url, nullptr, nullptr);
	}
	// Read-only filesystem for the spike — writes/deletes are not wired.
	unique_ptr<HTTPResponse> Put(PutRequestInfo &info) override {
		throw NotImplementedException("wasi:http client: PUT not supported");
	}
	unique_ptr<HTTPResponse> Post(PostRequestInfo &info) override {
		throw NotImplementedException("wasi:http client: POST not supported");
	}
	unique_ptr<HTTPResponse> Delete(DeleteRequestInfo &info) override {
		throw NotImplementedException("wasi:http client: DELETE not supported");
	}

private:
	HTTPFSParams &params;

	unique_ptr<HTTPResponse>
	Request(BaseRequest &req, bool method_get, const HTTPHeaders &headers, const string &url,
	        std::function<bool(const_data_ptr_t, idx_t)> *content_handler,
	        std::function<bool(const HTTPResponse &)> *response_handler) {
		auto parsed = ParseUrl(url);

		// 1. headers → wasi:http fields
		auto fields = wasi_http_types_constructor_fields();
		{
			wasi_http_types_borrow_fields_t bf = {fields.__handle};
			for (auto &h : headers) {
				auto k = WStr(h.first);
				// field-value is list<u8>
				http_client_list_u8_t val {(uint8_t *)h.second.data(), h.second.size()};
				wasi_http_types_header_error_t herr;
				wasi_http_types_method_fields_append(bf, (wasi_http_types_field_key_t *)&k,
				                                     (wasi_http_types_field_value_t *)&val, &herr);
			}
		}

		// 2. outgoing-request + method/scheme/authority/path
		auto request = wasi_http_types_constructor_outgoing_request(fields);
		wasi_http_types_borrow_outgoing_request_t breq = {request.__handle};

		wasi_http_types_method_t method;
		method.tag = method_get ? WASI_HTTP_TYPES_METHOD_GET : WASI_HTTP_TYPES_METHOD_HEAD;
		wasi_http_types_method_outgoing_request_set_method(breq, &method);

		wasi_http_types_scheme_t scheme;
		scheme.tag = parsed.https ? WASI_HTTP_TYPES_SCHEME_HTTPS : WASI_HTTP_TYPES_SCHEME_HTTP;
		wasi_http_types_method_outgoing_request_set_scheme(breq, &scheme);

		auto authority = WStr(parsed.authority);
		wasi_http_types_method_outgoing_request_set_authority(breq, &authority);
		auto pwq = WStr(parsed.path);
		wasi_http_types_method_outgoing_request_set_path_with_query(breq, &pwq);

		// 3. dispatch (no per-request options for now)
		wasi_http_outgoing_handler_own_future_incoming_response_t future;
		wasi_http_outgoing_handler_error_code_t send_err;
		if (!wasi_http_outgoing_handler_handle(
		        *(wasi_http_outgoing_handler_own_outgoing_request_t *)&request, nullptr, &future, &send_err)) {
			auto resp = make_uniq<HTTPResponse>(HTTPStatusCode::InternalServerError_500);
			resp->success = false;
			resp->request_error = "wasi:http outgoing-handler.handle failed";
			return resp;
		}

		// 4. block on the future until the response head is ready
		wasi_http_types_borrow_future_incoming_response_t bfut = {future.__handle};
		auto pollable = wasi_http_types_method_future_incoming_response_subscribe(bfut);
		wasi_io_poll_borrow_pollable_t bpoll = {pollable.__handle};
		wasi_io_poll_method_pollable_block(bpoll); // blocking wait (single-threaded wasip2)

		wasi_http_types_result_result_own_incoming_response_error_code_void_t got;
		if (!wasi_http_types_method_future_incoming_response_get(bfut, &got) || got.is_err /*outer*/ ||
		    got.val.ok.is_err /*inner: transport error*/) {
			auto resp = make_uniq<HTTPResponse>(HTTPStatusCode::InternalServerError_500);
			resp->success = false;
			resp->request_error = "wasi:http request failed";
			return resp;
		}
		auto incoming = got.val.ok.val.ok; // own<incoming-response>
		wasi_http_types_borrow_incoming_response_t bresp = {incoming.__handle};

		// 5. status + headers
		auto status = wasi_http_types_method_incoming_response_status(bresp);
		auto resp = make_uniq<HTTPResponse>(static_cast<HTTPStatusCode>(status));
		resp->url = url;
		{
			auto rh = wasi_http_types_method_incoming_response_headers(bresp);
			wasi_http_types_borrow_fields_t brh = {rh.__handle};
			http_client_list_tuple2_field_key_field_value_t entries;
			wasi_http_types_method_fields_entries(brh, &entries);
			for (size_t i = 0; i < entries.len; i++) {
				string key((char *)entries.ptr[i].f0.ptr, entries.ptr[i].f0.len);
				string val((char *)entries.ptr[i].f1.ptr, entries.ptr[i].f1.len);
				resp->headers.Insert(key, val);
			}
		}
		if (response_handler) {
			(*response_handler)(*resp);
		}

		// 6. body: consume → incoming-body → input-stream → blocking-read loop.
		//    HEAD has no body; DuckDB only needs the headers (Content-Length /
		//    Content-Range) for OpenFile/GetFileSize.
		if (method_get) {
			wasi_http_types_own_incoming_body_t body;
			if (wasi_http_types_method_incoming_response_consume(bresp, &body)) {
				wasi_http_types_borrow_incoming_body_t bbody = {body.__handle};
				wasi_http_types_own_input_stream_t stream;
				if (wasi_http_types_method_incoming_body_stream(bbody, &stream)) {
					wasi_io_streams_borrow_input_stream_t bstream = {stream.__handle};
					for (;;) {
						http_client_list_u8_t chunk;
						wasi_io_streams_stream_error_t serr;
						if (!wasi_io_streams_method_input_stream_blocking_read(bstream, 1u << 16, &chunk,
						                                                       &serr)) {
							break; // closed (EOF) or error → done
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
				}
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
