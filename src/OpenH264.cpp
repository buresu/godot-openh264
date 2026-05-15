#include "OpenH264.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/engine.hpp>
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

OpenH264 *OpenH264::_singleton = nullptr;

OpenH264::OpenH264() {
    _singleton = this;
    _downloaded = _is_library_cached();
}

OpenH264::~OpenH264() {
    _unload_library();
    _singleton = nullptr;
}

OpenH264 *OpenH264::get_singleton() {
    return _singleton;
}

void OpenH264::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_loaded"), &OpenH264::is_loaded);
    ClassDB::bind_method(D_METHOD("is_downloaded"), &OpenH264::is_downloaded);
    ClassDB::bind_method(D_METHOD("is_enabled"), &OpenH264::is_enabled);
    ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &OpenH264::set_enabled);
    ClassDB::bind_method(D_METHOD("_background_download_task"), &OpenH264::_background_download_task);
    ClassDB::bind_method(D_METHOD("_load_library_task"), &OpenH264::_load_library_task);
    ClassDB::bind_method(D_METHOD("_on_download_complete", "error"), &OpenH264::_on_download_complete);
    ClassDB::bind_method(D_METHOD("_on_library_load_complete", "error"), &OpenH264::_on_library_load_complete);

    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
    ADD_SIGNAL(MethodInfo("library_ready", PropertyInfo(Variant::INT, "error")));
}

// ---------------------------------------------------------------------------
// Library management
// ---------------------------------------------------------------------------

String OpenH264::_get_lib_filename() const {
#if defined(_WIN32)
#  if defined(__aarch64__)
    return String("openh264-") + OPENH264_VERSION + "-arm64.dll";
#  else
    return String("openh264-") + OPENH264_VERSION + "-win64.dll";
#  endif
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

String OpenH264::_get_bz2_filename() const {
    return _get_lib_filename() + ".bz2";
}

String OpenH264::_get_lib_user_path() const {
    return String("user://openh264/") + _get_lib_filename();
}

String OpenH264::_get_cdn_url() const {
    return String("http://ciscobinary.openh264.org/") + _get_bz2_filename();
}

String OpenH264::_get_md5_url() const {
    return String("http://ciscobinary.openh264.org/") + _get_lib_filename() + ".signed.md5.txt";
}

String OpenH264::_get_license_url() const {
    return "http://www.openh264.org/BINARY_LICENSE.txt";
}

String OpenH264::_get_license_user_path() const {
    return "user://openh264/BINARY_LICENSE.txt";
}

String OpenH264::_globalize_user_path(const String &path) const {
    return ProjectSettings::get_singleton()->globalize_path(path);
}

bool OpenH264::_is_library_cached() const {
    return FileAccess::file_exists(_get_lib_user_path());
}

void OpenH264::_start_background_download() {
    if (_download_in_progress || _downloaded) {
        return;
    }
    _download_in_progress = true;
    WorkerThreadPool::get_singleton()->add_task(
            Callable(this, "_background_download_task"),
            false,
            "openh264_download");
}

void OpenH264::_start_background_load() {
    if (_load_in_progress || is_loaded()) {
        return;
    }
    _load_in_progress = true;
    WorkerThreadPool::get_singleton()->add_task(
            Callable(this, "_load_library_task"),
            false,
            "openh264_load_library");
}

void OpenH264::_background_download_task() {
    const String lib_path = _get_lib_user_path();

    if (FileAccess::file_exists(lib_path)) {
        UtilityFunctions::print("[openh264] Library found on disk: ",
                                _globalize_user_path(lib_path));
        call_deferred("_on_download_complete", (int)OK);
        return;
    }

    DirAccess::make_dir_recursive_absolute(
            OS::get_singleton()->get_user_data_dir() + "/openh264");

    const String license_path = _get_license_user_path();
    if (!FileAccess::file_exists(license_path)) {
        UtilityFunctions::print("[openh264] Downloading license: ", _get_license_url());
        PackedByteArray license_data = _download_bytes(_get_license_url());
        if (!license_data.is_empty()) {
            Ref<FileAccess> lf = FileAccess::open(license_path, FileAccess::WRITE);
            if (lf.is_valid()) {
                lf->store_buffer(license_data);
                UtilityFunctions::print(
                        "[openh264] OpenH264 Video Codec provided by Cisco Systems, Inc.");
                UtilityFunctions::print("[openh264] License saved to: ",
                                        _globalize_user_path(license_path));
            }
        } else {
            UtilityFunctions::printerr("[openh264] License download failed");
        }
    }

    UtilityFunctions::print("[openh264] Downloading: ", _get_cdn_url());
    PackedByteArray bz2_data = _download_bytes(_get_cdn_url());
    if (bz2_data.is_empty()) {
        UtilityFunctions::printerr("[openh264] Download failed");
        call_deferred("_on_download_complete", (int)FAILED);
        return;
    }

    PackedByteArray md5_data = _download_bytes(_get_md5_url());
    if (md5_data.is_empty()) {
        UtilityFunctions::printerr("[openh264] MD5 download failed");
        call_deferred("_on_download_complete", (int)FAILED);
        return;
    }

    Error err = _extract_and_save(bz2_data, lib_path);
    if (err != OK) {
        call_deferred("_on_download_complete", (int)err);
        return;
    }

    Ref<FileAccess> f = FileAccess::open(lib_path, FileAccess::READ);
    if (!f.is_valid()) {
        call_deferred("_on_download_complete", (int)ERR_FILE_CANT_READ);
        return;
    }
    PackedByteArray so_data = f->get_buffer(f->get_length());

    if (!_verify_md5(so_data, md5_data)) {
        UtilityFunctions::printerr("[openh264] MD5 mismatch — removing corrupted file");
        DirAccess::remove_absolute(_globalize_user_path(lib_path));
        call_deferred("_on_download_complete", (int)ERR_INVALID_DATA);
        return;
    }

    call_deferred("_on_download_complete", (int)OK);
}

void OpenH264::_load_library_task() {
    Error err = _load_library();
    call_deferred("_on_library_load_complete", (int)err);
}

void OpenH264::_unload_library() {
    if (_lib_handle) {
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(_lib_handle));
#else
        dlclose(_lib_handle);
#endif
        _lib_handle = nullptr;
        UtilityFunctions::print("[openh264] Library unloaded (v", OPENH264_VERSION, ")");
    }
}

PackedByteArray OpenH264::_download_bytes(const String &url) const {
    String stripped = url.trim_prefix("http://").trim_prefix("https://");
    int    slash    = stripped.find("/");
    if (slash < 0) {
        return {};
    }
    String host    = stripped.substr(0, slash);
    String path    = stripped.substr(slash);
    bool   use_tls = url.begins_with("https://");
    int    port    = use_tls ? 443 : 80;

    Ref<HTTPClient> client;
    client.instantiate();

    Error err = client->connect_to_host(host, port);
    if (err != OK) {
        return {};
    }

    while (client->get_status() == HTTPClient::STATUS_CONNECTING ||
           client->get_status() == HTTPClient::STATUS_RESOLVING) {
        client->poll();
        OS::get_singleton()->delay_msec(10);
    }
    if (client->get_status() != HTTPClient::STATUS_CONNECTED) {
        return {};
    }

    PackedStringArray headers;
    headers.push_back("User-Agent: godot-openh264");
    err = client->request(HTTPClient::METHOD_GET, path, headers);
    if (err != OK) {
        return {};
    }

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

PackedByteArray OpenH264::_compute_md5(const PackedByteArray &data) const {
    Ref<HashingContext> ctx;
    ctx.instantiate();
    ctx->start(HashingContext::HASH_MD5);
    ctx->update(data);
    return ctx->finish();
}

bool OpenH264::_verify_md5(const PackedByteArray &data,
                            const PackedByteArray &signed_md5_txt) const {
    String txt = String::utf8(reinterpret_cast<const char *>(signed_md5_txt.ptr()),
                              signed_md5_txt.size()).strip_edges();

    String expected;
    if (txt.contains(" = ")) {
        expected = txt.get_slice(" = ", txt.get_slice_count(" = ") - 1).strip_edges().to_lower();
    } else {
        expected = txt.split(" ")[0].strip_edges().to_lower();
    }

    if (expected.length() != 32) {
        UtilityFunctions::printerr("[openh264] Unexpected MD5 format: ", txt.left(64));
        return false;
    }

    PackedByteArray hash = _compute_md5(data);
    String          actual;
    for (int i = 0; i < hash.size(); i++) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", hash[i]);
        actual += String(buf);
    }

    UtilityFunctions::print("[openh264] MD5 expected=", expected, " actual=", actual);
    return actual == expected;
}

Error OpenH264::_extract_and_save(const PackedByteArray &bz2_data,
                                   const String &dst_path) const {
    const unsigned int max_out = 32 * 1024 * 1024;
    PackedByteArray    out;
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
        UtilityFunctions::printerr("[openh264] Cannot write: ",
                                   _globalize_user_path(dst_path));
        return ERR_FILE_CANT_WRITE;
    }
    f->store_buffer(out);

    UtilityFunctions::print("[openh264] Extracted ", (int)out_len, " bytes to ",
                            _globalize_user_path(dst_path));
    return OK;
}

Error OpenH264::_load_library() {
    const String lib_path_global = _globalize_user_path(_get_lib_user_path());

#if defined(_WIN32)
    _lib_handle = static_cast<void *>(LoadLibraryA(lib_path_global.utf8().get_data()));
    if (!_lib_handle) {
        UtilityFunctions::printerr("[openh264] LoadLibrary failed: ", (int64_t)GetLastError());
        return ERR_CANT_OPEN;
    }
#else
    _lib_handle = dlopen(lib_path_global.utf8().get_data(), RTLD_LAZY | RTLD_LOCAL);
    if (!_lib_handle) {
        UtilityFunctions::printerr("[openh264] dlopen failed: ", String(dlerror()));
        return ERR_CANT_OPEN;
    }
#endif

    _fn_create_decoder  = reinterpret_cast<FnWelsCreateDecoder>(_get_proc("WelsCreateDecoder"));
    _fn_destroy_decoder = reinterpret_cast<FnWelsDestroyDecoder>(_get_proc("WelsDestroyDecoder"));

    if (!_fn_create_decoder || !_fn_destroy_decoder) {
        UtilityFunctions::printerr("[openh264] Failed to resolve required symbols");
        return ERR_CANT_OPEN;
    }

    UtilityFunctions::print("[openh264] Library loaded (v", OPENH264_VERSION, ")");
    return OK;
}

void OpenH264::set_enabled(bool p_enabled) {
    if (_enabled == p_enabled) {
        return;
    }
    _enabled = p_enabled;

    if (_enabled) {
        _downloaded = _is_library_cached();
        if (_downloaded) {
            _start_background_load();
        } else {
            _start_background_download();
        }
    } else if (!_enabled && is_loaded()) {
        _unload_library();
    }
}

void OpenH264::_on_download_complete(int error) {
    _download_in_progress = false;
    _downloaded = error == OK;
    if (error == OK && _enabled) {
        _start_background_load();
        return;
    }
    if (error != OK && _enabled) {
        emit_signal("library_ready", error);
    }
}

void OpenH264::_on_library_load_complete(int error) {
    _load_in_progress = false;
    if (!_enabled) {
        if (error == OK) {
            _unload_library();
        }
        return;
    }
    emit_signal("library_ready", error);
}

void *OpenH264::_get_proc(const String &name) const {
    if (!_lib_handle) {
        return nullptr;
    }
#if defined(_WIN32)
    return reinterpret_cast<void *>(
            GetProcAddress(static_cast<HMODULE>(_lib_handle), name.utf8().get_data()));
#else
    return dlsym(_lib_handle, name.utf8().get_data());
#endif
}

Error OpenH264::create_decoder(ISVCDecoder **out) {
    if (!is_loaded()) {
        UtilityFunctions::printerr("[openh264] Library not ready");
        return ERR_UNCONFIGURED;
    }

    long ret = _fn_create_decoder(out);
    if (ret != 0 || !*out) {
        UtilityFunctions::printerr("[openh264] WelsCreateDecoder failed: ", (int64_t)ret);
        return FAILED;
    }

    SDecodingParam param{};
    param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
    param.bParseOnly = false;
    param.uiCpuLoad = 0;
    param.eEcActiveIdc = ERROR_CON_SLICE_COPY;

    ret = (*out)->Initialize(&param);
    if (ret != 0) {
        UtilityFunctions::printerr("[openh264] Initialize failed: ", (int64_t)ret);
        _fn_destroy_decoder(*out);
        *out = nullptr;
        return FAILED;
    }

    return OK;
}

void OpenH264::destroy_decoder(ISVCDecoder *decoder) {
    if (!decoder) {
        return;
    }
    decoder->Uninitialize();
    if (_fn_destroy_decoder) {
        _fn_destroy_decoder(decoder);
    }
}
