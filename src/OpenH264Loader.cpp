#include "OpenH264Loader.hpp"

#include <godot_cpp/classes/dir_access.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <bzlib.h>

#if defined(_WIN32)
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif

using namespace godot;

OpenH264Loader *OpenH264Loader::singleton = nullptr;

OpenH264Loader::OpenH264Loader() {
    singleton = this;
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
    ClassDB::bind_method(D_METHOD("ensure_ready_async", "version"),
                         &OpenH264Loader::ensure_ready_async, DEFVAL("2.4.1"));
    ClassDB::bind_method(D_METHOD("load_if_present", "version"),
                         &OpenH264Loader::load_if_present, DEFVAL("2.4.1"));
    ClassDB::bind_method(D_METHOD("is_loaded"), &OpenH264Loader::is_loaded);

    ClassDB::bind_method(D_METHOD("_on_request_completed", "result", "response_code",
                                  "headers", "body"),
                         &OpenH264Loader::_on_request_completed);

    ADD_SIGNAL(MethodInfo("library_ready",
                          PropertyInfo(Variant::INT, "error")));
}

void OpenH264Loader::_ready() {
    http_request = memnew(HTTPRequest);
    add_child(http_request);
    http_request->connect("request_completed",
                          Callable(this, "_on_request_completed"));
}

// ---------------------------------------------------------------------------
// Platform helpers
// ---------------------------------------------------------------------------

String OpenH264Loader::get_lib_filename(const String &version) const {
#if defined(_WIN32)
    return "openh264-" + version + "-win64.dll";
#elif defined(__APPLE__)
#  if defined(__aarch64__)
    return "libopenh264-" + version + "-mac-arm64.dylib";
#  else
    return "libopenh264-" + version + "-mac-x86_64.dylib";
#  endif
#else // Linux
#  if defined(__aarch64__)
    return "libopenh264-" + version + "-linux-arm64.so.2";
#  else
    return "libopenh264-" + version + "-linux64.8.so";
#  endif
#endif
}

String OpenH264Loader::get_bz2_filename(const String &version) const {
    return get_lib_filename(version) + ".bz2";
}

String OpenH264Loader::get_cdn_url(const String &version) const {
    return "http://ciscobinary.openh264.org/" + get_bz2_filename(version);
}

String OpenH264Loader::get_lib_user_path(const String &version) const {
    return "user://openh264/" + get_lib_filename(version);
}

// ---------------------------------------------------------------------------
// Async entry point
// ---------------------------------------------------------------------------

void OpenH264Loader::ensure_ready_async(const String &version) {
    if (lib_handle) {
        emit_signal("library_ready", (int)OK);
        return;
    }

    const String lib_path = get_lib_user_path(version);

    // Already downloaded — just load
    if (FileAccess::file_exists(lib_path)) {
        emit_signal("library_ready", (int)load_library(version));
        return;
    }

    // Ensure destination directory exists
    DirAccess::make_dir_recursive_absolute(
            OS::get_singleton()->get_user_data_dir() + "/openh264");

    pending_version = version;
    const String url = get_cdn_url(version);
    UtilityFunctions::print("[openh264] Downloading: ", url);

    Error err = (Error)http_request->request(url);
    if (err != OK) {
        UtilityFunctions::printerr("[openh264] HTTPRequest::request failed: ", (int)err);
        emit_signal("library_ready", (int)err);
    }
}

// ---------------------------------------------------------------------------
// HTTPRequest callback
// ---------------------------------------------------------------------------

void OpenH264Loader::_on_request_completed(int result, int response_code,
                                            const PackedStringArray & /*headers*/,
                                            const PackedByteArray &body) {
    if (result != HTTPRequest::RESULT_SUCCESS || response_code != 200) {
        UtilityFunctions::printerr("[openh264] Download failed — result=", result,
                                   " http=", response_code);
        emit_signal("library_ready", (int)FAILED);
        return;
    }

    UtilityFunctions::print("[openh264] Download complete (", body.size(), " bytes). Extracting...");

    const String lib_path = get_lib_user_path(pending_version);
    Error err = extract_and_save(body, lib_path);
    if (err != OK) {
        emit_signal("library_ready", (int)err);
        return;
    }

    emit_signal("library_ready", (int)load_library(pending_version));
}

// ---------------------------------------------------------------------------
// bz2 decompression in memory → FileAccess write
// ---------------------------------------------------------------------------

Error OpenH264Loader::extract_and_save(const PackedByteArray &bz2_data,
                                        const String &dst_path) {
    // libopenh264 decompressed is ~6 MB; 32 MB is a safe upper bound.
    const unsigned int max_out = 32 * 1024 * 1024;
    PackedByteArray out;
    out.resize(max_out);

    unsigned int out_len = max_out;
    int bz_err = BZ2_bzBuffToBuffDecompress(
            reinterpret_cast<char *>(out.ptrw()),
            &out_len,
            // bz2 API takes non-const but doesn't modify the input
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
        UtilityFunctions::printerr("[openh264] Cannot write to: ", dst_path);
        return ERR_FILE_CANT_WRITE;
    }
    f->store_buffer(out);

    UtilityFunctions::print("[openh264] Extracted ", out_len, " bytes → ", dst_path);
    return OK;
}

// ---------------------------------------------------------------------------
// Dynamic loading
// ---------------------------------------------------------------------------

Error OpenH264Loader::load_library(const String &version) {
    const String lib_path        = get_lib_user_path(version);
    const String lib_path_global = ProjectSettings::get_singleton()->globalize_path(lib_path);

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

    UtilityFunctions::print("[openh264] Library loaded successfully (v", version, ")");
    return OK;
}

Error OpenH264Loader::load_if_present(const String &version) {
    if (lib_handle) {
        return OK;
    }
    if (!FileAccess::file_exists(get_lib_user_path(version))) {
        return ERR_FILE_NOT_FOUND;
    }
    return load_library(version);
}

// ---------------------------------------------------------------------------
// SHA-256 verification (stub)
// ---------------------------------------------------------------------------

bool OpenH264Loader::verify_sha256(const String &path, const String &expected_hex) {
    (void)path;
    (void)expected_hex;
    UtilityFunctions::print("[openh264] WARNING: SHA-256 verification not yet implemented");
    return true;
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
