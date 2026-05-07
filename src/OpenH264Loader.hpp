#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/string.hpp>

// openh264 C API forward declarations
struct ISVCDecoder;

using FnWelsCreateDecoder  = long (*)(ISVCDecoder **ppDecoder);
using FnWelsDestroyDecoder = void (*)(ISVCDecoder *pDecoder);

namespace godot {

class OpenH264Loader : public RefCounted {
    GDCLASS(OpenH264Loader, RefCounted)

    static constexpr const char *OPENH264_VERSION = "2.6.0";

    static OpenH264Loader *singleton;

    void *lib_handle = nullptr;

    // Holds a reference to ourselves while the background thread is running
    // to prevent premature deallocation.
    Ref<OpenH264Loader> self_ref;

    // ---- Platform helpers ----
    String _get_lib_filename() const;
    String _get_bz2_filename() const;
    String _get_lib_user_path() const;
    String _get_cdn_url() const;
    String _get_md5_url() const;

    // ---- Background task (runs on WorkerThreadPool) ----
    void _start_background_load();
    void _background_load_task();

    // ---- Download / verify / extract ----
    PackedByteArray _download_bytes(const String &url) const;
    PackedByteArray _compute_md5(const PackedByteArray &data) const;
    bool            _verify_md5(const PackedByteArray &data,
                                const PackedByteArray &signed_md5_txt) const;
    Error           _extract_and_save(const PackedByteArray &bz2_data,
                                      const String &dst_path) const;

    // ---- Load shared library from disk ----
    Error _load_library();

    // ---- Called on main thread after background task finishes ----
    void _on_load_complete(int error);

protected:
    static void _bind_methods();

public:
    FnWelsCreateDecoder  fn_create_decoder  = nullptr;
    FnWelsDestroyDecoder fn_destroy_decoder = nullptr;

    static OpenH264Loader *get_singleton();

    OpenH264Loader();
    ~OpenH264Loader() override;

    bool is_loaded() const { return lib_handle != nullptr; }

    void *get_proc(const String &name) const;

    // Signal: library_ready(error: int)
};

} // namespace godot
