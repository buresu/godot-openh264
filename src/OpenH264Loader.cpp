#include "OpenH264Loader.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/hashing_context.hpp>
#include <godot_cpp/classes/http_client.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/worker_thread_pool.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <bzlib.h>
#include <cstdio>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

using namespace godot;

OpenH264Loader *OpenH264Loader::singleton = nullptr;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

OpenH264Loader::OpenH264Loader() {
    singleton = this;
    _start_background_load();
}

OpenH264Loader::~OpenH264Loader() {
    if (lib_handle) {
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(lib_handle));
#else
        dlclose(lib_handle);
#endif
        lib_handle = nullptr;
    }
    singleton = nullptr;
}

OpenH264Loader *OpenH264Loader::get_singleton() {
    return singleton;
}

void OpenH264Loader::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_loaded"), &OpenH264Loader::is_loaded);
    ClassDB::bind_method(D_METHOD("_background_load_task"),
                         &OpenH264Loader::_background_load_task);
    ClassDB::bind_method(D_METHOD("_on_load_complete", "error"),
                         &OpenH264Loader::_on_load_complete);

    ADD_SIGNAL(MethodInfo("library_ready",
                          PropertyInfo(Variant::INT, "error")));
}

// ---------------------------------------------------------------------------
// Platform helpers
// ---------------------------------------------------------------------------

String OpenH264Loader::_get_lib_filename() const {
#if defined(_WIN32)
    return String("openh264-") + OPENH264_VERSION + "-win64.dll";
#elif defined(__APPLE__)
    return String("libopenh264-") + OPENH264_VERSION + "-mac-arm64.dylib";
#else
#  if defined(__aarch64__)
    return String("libopenh264-") + OPENH264_VERSION + "-linux-arm64.so.2";
#  else
    return String("libopenh264-") + OPENH264_VERSION + "-linux64.8.so";
#  endif
#endif
}

String OpenH264Loader::_get_bz2_filename() const {
    return _get_lib_filename() + ".bz2";
}

String OpenH264Loader::_get_lib_user_path() const {
    return String("user://openh264/") + _get_lib_filename();
}

String OpenH264Loader::_get_cdn_url() const {
    return String("http://ciscobinary.openh264.org/") + _get_bz2_filename();
}

String OpenH264Loader::_get_md5_url() const {
    return _get_cdn_url() + ".signed.md5.txt";
}

// ---------------------------------------------------------------------------
// Background load orchestration
// ---------------------------------------------------------------------------

void OpenH264Loader::_start_background_load() {
    // Hold a reference so the object stays alive while the thread runs.
    self_ref = Ref<OpenH264Loader>(this);

    WorkerThreadPool::get_singleton()->add_task(
            Callable(this, "_background_load_task"),
            false,
            "openh264_load");
}

void OpenH264Loader::_background_load_task() {
    const String lib_path = _get_lib_user_path();

    // Already on disk — verify MD5 then load directly.
    if (FileAccess::file_exists(lib_path)) {
        UtilityFunctions::print("[openh264] Library found on disk, loading...");
        Error err = _load_library();
        call_deferred("_on_load_complete", (int)err);
        return;
    }

    // Ensure destination directory exists.
    DirAccess::make_dir_recursive_absolute(
            OS::get_singleton()->get_user_data_dir() + "/openh264");

    // Download bz2.
    UtilityFunctions::print("[openh264] Downloading: ", _get_cdn_url());
    PackedByteArray bz2_data = _download_bytes(_get_cdn_url());
    if (bz2_data.is_empty()) {
        UtilityFunctions::printerr("[openh264] Download failed");
        call_deferred("_on_load_complete", (int)FAILED);
        return;
    }

    // Download signed MD5.
    PackedByteArray md5_data = _download_bytes(_get_md5_url());
    if (md5_data.is_empty()) {
        UtilityFunctions::printerr("[openh264] MD5 download failed");
        call_deferred("_on_load_complete", (int)FAILED);
        return;
    }

    // Verify.
    if (!_verify_md5(bz2_data, md5_data)) {
        UtilityFunctions::printerr("[openh264] MD5 mismatch — aborting");
        call_deferred("_on_load_complete", (int)ERR_INVALID_DATA);
        return;
    }

    // Extract and save.
    Error err = _extract_and_save(bz2_data, lib_path);
    if (err != OK) {
        call_deferred("_on_load_complete", (int)err);
        return;
    }

    // Load into process.
    err = _load_library();
    call_deferred("_on_load_complete", (int)err);
}

// ---------------------------------------------------------------------------
// HTTP download (polling loop — runs in worker thread)
// ---------------------------------------------------------------------------

PackedByteArray OpenH264Loader::_download_bytes(const String &url) const {
    // Parse "http://host/path"
    String stripped = url.trim_prefix("http://").trim_prefix("https://");
    int slash = stripped.find("/");
    if (slash < 0) {
        return {};
    }
    String host = stripped.substr(0, slash);
    String path = stripped.substr(slash);
    bool   use_tls = url.begins_with("https://");
    int    port    = use_tls ? 443 : 80;

    Ref<HTTPClient> client;
    client.instantiate();

    Error err = client->connect_to_host(host, port);
    if (err != OK) {
        return {};
    }

    // Wait for connection.
    while (client->get_status() == HTTPClient::STATUS_CONNECTING ||
           client->get_status() == HTTPClient::STATUS_RESOLVING) {
        client->poll();
        OS::get_singleton()->delay_msec(10);
    }
    if (client->get_status() != HTTPClient::STATUS_CONNECTED) {
        return {};
    }

    // Send GET request.
    PackedStringArray headers;
    headers.push_back("User-Agent: godot-openh264");
    err = client->request(HTTPClient::METHOD_GET, path, headers);
    if (err != OK) {
        return {};
    }

    // Wait for response headers.
    while (client->get_status() == HTTPClient::STATUS_REQUESTING) {
        client->poll();
        OS::get_singleton()->delay_msec(10);
    }
    if (!client->has_response()) {
        return {};
    }
    if (client->get_response_code() != 200) {
        UtilityFunctions::printerr("[openh264] HTTP ", client->get_response_code(),
                                   " for: ", url);
        return {};
    }

    // Read body.
    PackedByteArray result;
    while (client->get_status() == HTTPClient::STATUS_BODY) {
        client->poll();
        PackedByteArray chunk = client->read_response_body_chunk();
        if (!chunk.is_empty()) {
            result.append_array(chunk);
        } else {
            OS::get_singleton()->delay_msec(1);
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// MD5 verification
// ---------------------------------------------------------------------------

PackedByteArray OpenH264Loader::_compute_md5(const PackedByteArray &data) const {
    Ref<HashingContext> ctx;
    ctx.instantiate();
    ctx->start(HashingContext::HASH_MD5);
    ctx->update(data);
    return ctx->finish();
}

bool OpenH264Loader::_verify_md5(const PackedByteArray &data,
                                   const PackedByteArray &signed_md5_txt) const {
    // Parse: first whitespace-separated token is the MD5 hex string.
    String txt = String::utf8(reinterpret_cast<const char *>(signed_md5_txt.ptr()),
                              signed_md5_txt.size());
    String expected = txt.split(" ")[0].split("\t")[0].strip_edges().to_lower();

    if (expected.length() != 32) {
        UtilityFunctions::printerr("[openh264] Unexpected MD5 format: ", txt.left(64));
        return false;
    }

    // Compute actual MD5.
    PackedByteArray hash = _compute_md5(data);
    String actual;
    for (int i = 0; i < hash.size(); i++) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", hash[i]);
        actual += String(buf);
    }

    UtilityFunctions::print("[openh264] MD5 expected=", expected, " actual=", actual);
    return actual == expected;
}

// ---------------------------------------------------------------------------
// bz2 extraction
// ---------------------------------------------------------------------------

Error OpenH264Loader::_extract_and_save(const PackedByteArray &bz2_data,
                                         const String &dst_path) const {
    const unsigned int max_out = 32 * 1024 * 1024; // 32 MB upper bound
    PackedByteArray out;
    out.resize(max_out);

    unsigned int out_len = max_out;
    int bz_err = BZ2_bzBuffToBuffDecompress(
            reinterpret_cast<char *>(out.ptrw()),
            &out_len,
            const_cast<char *>(reinterpret_cast<const char *>(bz2_data.ptr())),
            (unsigned int)bz2_data.size(),
            0, 0);

    if (bz_err != BZ_OK) {
        UtilityFunctions::printerr("[openh264] bz2 decompression failed: ", bz_err);
        return FAILED;
    }

    out.resize(out_len);

    Ref<FileAccess> f = FileAccess::open(dst_path, FileAccess::WRITE);
    if (!f.is_valid()) {
        UtilityFunctions::printerr("[openh264] Cannot write: ", dst_path);
        return ERR_FILE_CANT_WRITE;
    }
    f->store_buffer(out);

    UtilityFunctions::print("[openh264] Extracted ", (int)out_len, " bytes → ", dst_path);
    return OK;
}

// ---------------------------------------------------------------------------
// dlopen / LoadLibrary
// ---------------------------------------------------------------------------

Error OpenH264Loader::_load_library() {
    const String lib_path_global =
            ProjectSettings::get_singleton()->globalize_path(_get_lib_user_path());

#if defined(_WIN32)
    lib_handle = static_cast<void *>(LoadLibraryA(lib_path_global.utf8().get_data()));
    if (!lib_handle) {
        UtilityFunctions::printerr("[openh264] LoadLibrary failed: ", (int64_t)GetLastError());
        return ERR_CANT_OPEN;
    }
#else
    lib_handle = dlopen(lib_path_global.utf8().get_data(), RTLD_LAZY | RTLD_LOCAL);
    if (!lib_handle) {
        UtilityFunctions::printerr("[openh264] dlopen failed: ", String(dlerror()));
        return ERR_CANT_OPEN;
    }
#endif

    fn_create_decoder  = reinterpret_cast<FnWelsCreateDecoder>(get_proc("WelsCreateDecoder"));
    fn_destroy_decoder = reinterpret_cast<FnWelsDestroyDecoder>(get_proc("WelsDestroyDecoder"));

    if (!fn_create_decoder || !fn_destroy_decoder) {
        UtilityFunctions::printerr("[openh264] Failed to resolve required symbols");
        return ERR_CANT_OPEN;
    }

    UtilityFunctions::print("[openh264] Library loaded (v", OPENH264_VERSION, ")");
    return OK;
}

// ---------------------------------------------------------------------------
// Main-thread completion handler
// ---------------------------------------------------------------------------

void OpenH264Loader::_on_load_complete(int error) {
    // Keep alive through emit_signal, then release the circular ref.
    Ref<OpenH264Loader> keep_alive = self_ref;
    self_ref                       = Ref<OpenH264Loader>();
    emit_signal("library_ready", error);
}

// ---------------------------------------------------------------------------
// Symbol resolution
// ---------------------------------------------------------------------------

void *OpenH264Loader::get_proc(const String &name) const {
    if (!lib_handle) {
        return nullptr;
    }
#if defined(_WIN32)
    return reinterpret_cast<void *>(
            GetProcAddress(static_cast<HMODULE>(lib_handle), name.utf8().get_data()));
#else
    return dlsym(lib_handle, name.utf8().get_data());
#endif
}
