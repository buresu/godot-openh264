#include "OpenH264Loader.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/hashing_context.hpp>
#include <godot_cpp/classes/http_client.hpp>
#include <godot_cpp/classes/engine.hpp>
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

OpenH264Loader *OpenH264Loader::_singleton = nullptr;

OpenH264Loader::OpenH264Loader() {
    _singleton = this;
    _start_background_download();
}

OpenH264Loader::~OpenH264Loader() {
    if (_lib_handle) {
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(_lib_handle));
#else
        dlclose(_lib_handle);
#endif
        _lib_handle = nullptr;
    }
    _singleton = nullptr;
}

OpenH264Loader *OpenH264Loader::get_singleton() {
    return _singleton;
}

void OpenH264Loader::_bind_methods() {
    ClassDB::bind_method(D_METHOD("is_loaded"), &OpenH264Loader::is_loaded);
    ClassDB::bind_method(D_METHOD("is_downloaded"), &OpenH264Loader::is_downloaded);
    ClassDB::bind_method(D_METHOD("is_enabled"), &OpenH264Loader::is_enabled);
    ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &OpenH264Loader::set_enabled);
    ClassDB::bind_method(D_METHOD("_background_download_task"),
                         &OpenH264Loader::_background_download_task);
    ClassDB::bind_method(D_METHOD("_load_library_task"),
                         &OpenH264Loader::_load_library_task);
    ClassDB::bind_method(D_METHOD("_on_download_complete", "error"),
                         &OpenH264Loader::_on_download_complete);
    ClassDB::bind_method(D_METHOD("_on_library_load_complete", "error"),
                         &OpenH264Loader::_on_library_load_complete);

    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
    ADD_SIGNAL(MethodInfo("library_ready", PropertyInfo(Variant::INT, "error")));
}

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
    return String("http://ciscobinary.openh264.org/") + _get_lib_filename() + ".signed.md5.txt";
}

String OpenH264Loader::_get_license_url() const {
    return "http://www.openh264.org/BINARY_LICENSE.txt";
}

String OpenH264Loader::_get_license_user_path() const {
    return "user://openh264/BINARY_LICENSE.txt";
}

void OpenH264Loader::_start_background_download() {
    WorkerThreadPool::get_singleton()->add_task(
            Callable(this, "_background_download_task"),
            false,
            "openh264_load");
}

void OpenH264Loader::_background_download_task() {
    const String lib_path = _get_lib_user_path();

    DirAccess::make_dir_recursive_absolute(
            OS::get_singleton()->get_user_data_dir() + "/openh264");

    // Download license file if not already present
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
                UtilityFunctions::print("[openh264] License saved to: ", license_path);
            }
        } else {
            UtilityFunctions::printerr("[openh264] License download failed");
        }
    }

    if (FileAccess::file_exists(lib_path)) {
        UtilityFunctions::print("[openh264] Library found on disk.");
        call_deferred("_on_download_complete", (int)OK);
        return;
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
        DirAccess::remove_absolute(
                ProjectSettings::get_singleton()->globalize_path(lib_path));
        call_deferred("_on_download_complete", (int)ERR_INVALID_DATA);
        return;
    }

    call_deferred("_on_download_complete", (int)OK);
}

void OpenH264Loader::_load_library_task() {
    Error err = _load_library();
    call_deferred("_on_library_load_complete", (int)err);
}

PackedByteArray OpenH264Loader::_download_bytes(const String &url) const {
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

PackedByteArray OpenH264Loader::_compute_md5(const PackedByteArray &data) const {
    Ref<HashingContext> ctx;
    ctx.instantiate();
    ctx->start(HashingContext::HASH_MD5);
    ctx->update(data);
    return ctx->finish();
}

bool OpenH264Loader::_verify_md5(const PackedByteArray &data,
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

Error OpenH264Loader::_extract_and_save(const PackedByteArray &bz2_data,
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
        UtilityFunctions::printerr("[openh264] Cannot write: ", dst_path);
        return ERR_FILE_CANT_WRITE;
    }
    f->store_buffer(out);

    UtilityFunctions::print("[openh264] Extracted ", (int)out_len, " bytes → ", dst_path);
    return OK;
}

Error OpenH264Loader::_load_library() {
    const String lib_path_global =
            ProjectSettings::get_singleton()->globalize_path(_get_lib_user_path());

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

void OpenH264Loader::set_enabled(bool p_enabled) {
    if (_enabled == p_enabled) {
        return;
    }
    _enabled = p_enabled;
    if (_enabled && _downloaded && !is_loaded()) {
        WorkerThreadPool::get_singleton()->add_task(
                Callable(this, "_load_library_task"),
                false,
                "openh264_load_library");
    }
}

void OpenH264Loader::_on_download_complete(int error) {
    _downloaded = true;
    if (error == OK && _enabled) {
        WorkerThreadPool::get_singleton()->add_task(
                Callable(this, "_load_library_task"),
                false,
                "openh264_load_library");
        return;
    }
    if (error != OK) {
        emit_signal("library_ready", error);
    }
}

void OpenH264Loader::_on_library_load_complete(int error) {
    emit_signal("library_ready", error);
}

void *OpenH264Loader::_get_proc(const String &name) const {
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
