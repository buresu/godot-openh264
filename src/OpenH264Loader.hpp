#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/string.hpp>

struct ISVCDecoder;

using FnWelsCreateDecoder  = long (*)(ISVCDecoder **ppDecoder);
using FnWelsDestroyDecoder = void (*)(ISVCDecoder *pDecoder);

namespace godot {

class OpenH264Loader : public RefCounted {
    GDCLASS(OpenH264Loader, RefCounted)

    static constexpr const char *OPENH264_VERSION = "2.6.0";

    static OpenH264Loader *_singleton;

    void *_lib_handle = nullptr;

    Ref<OpenH264Loader> _self_ref;

    String _get_lib_filename() const;
    String _get_bz2_filename() const;
    String _get_lib_user_path() const;
    String _get_cdn_url() const;
    String _get_md5_url() const;

    void _start_background_load();
    void _background_load_task();

    PackedByteArray _download_bytes(const String &url) const;
    PackedByteArray _compute_md5(const PackedByteArray &data) const;
    bool            _verify_md5(const PackedByteArray &data,
                                const PackedByteArray &signed_md5_txt) const;
    Error           _extract_and_save(const PackedByteArray &bz2_data,
                                      const String &dst_path) const;

    Error  _load_library();
    void   _on_load_complete(int error);
    void  *_get_proc(const String &name) const;

protected:
    static void _bind_methods();

public:
    FnWelsCreateDecoder  _fn_create_decoder  = nullptr;
    FnWelsDestroyDecoder _fn_destroy_decoder = nullptr;

    static OpenH264Loader *get_singleton();

    OpenH264Loader();
    ~OpenH264Loader() override;

    bool is_loaded() const { return _lib_handle != nullptr; }
};

} // namespace godot
