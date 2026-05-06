#pragma once

#include <godot_cpp/classes/http_request.hpp>
#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/string.hpp>

// openh264 C API forward declarations (avoid including full header here)
struct ISVCDecoder;

// Function pointer types for the openh264 C API
using FnWelsCreateDecoder  = long (*)(ISVCDecoder **ppDecoder);
using FnWelsDestroyDecoder = void (*)(ISVCDecoder *pDecoder);

namespace godot {

// Singleton that handles runtime download, bz2 extraction, and dlopen/LoadLibrary
// of libopenh264. Add as child node, then call ensure_ready_async() and await
// the library_ready(error: int) signal.
class OpenH264Loader : public Node {
    GDCLASS(OpenH264Loader, Node)

    static OpenH264Loader *singleton;
    void *lib_handle = nullptr;

    HTTPRequest *http_request = nullptr;
    String       pending_version;

    // Decompress bz2 bytes in memory and write the result to dst_path (user://)
    Error extract_and_save(const PackedByteArray &bz2_data, const String &dst_path);

    // Verify SHA-256 of the extracted library
    bool verify_sha256(const String &path, const String &expected_hex);

    // Platform helpers
    String get_lib_filename(const String &version) const;
    String get_bz2_filename(const String &version) const;
    String get_cdn_url(const String &version) const;
    String get_lib_user_path(const String &version) const;

    // Load the already-present library from disk
    Error load_library(const String &version);

    // HTTPRequest callback
    void _on_request_completed(int result, int response_code,
                               const PackedStringArray &headers,
                               const PackedByteArray &body);

protected:
    static void _bind_methods();

public:
    // openh264 function pointers (populated after library_ready is emitted with OK)
    FnWelsCreateDecoder  fn_create_decoder  = nullptr;
    FnWelsDestroyDecoder fn_destroy_decoder = nullptr;

    static OpenH264Loader *get_singleton();

    OpenH264Loader();
    ~OpenH264Loader() override;

    void _ready() override;

    // Async: starts download if needed; emits library_ready(error) when done.
    // await this from GDScript: await loader.library_ready
    void ensure_ready_async(const String &version = "2.4.1");

    // Sync: only loads from disk (no download). Returns ERR_FILE_NOT_FOUND if missing.
    Error load_if_present(const String &version = "2.4.1");

    bool is_loaded() const { return lib_handle != nullptr; }

    // Resolve a symbol from the loaded library
    void *get_proc(const String &name) const;
};

} // namespace godot
