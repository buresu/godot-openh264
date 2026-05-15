#pragma once

#include <godot_cpp/classes/object.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "codec_api.h"

using FnWelsCreateDecoder  = long (*)(ISVCDecoder **ppDecoder);
using FnWelsDestroyDecoder = void (*)(ISVCDecoder *pDecoder);

namespace godot {

class OpenH264 : public Object {
    GDCLASS(OpenH264, Object)

public:
    static OpenH264 *get_singleton();

    OpenH264();
    ~OpenH264() override;

    // Library management
    bool  is_loaded() const { return _lib_handle != nullptr; }
    bool  is_downloaded() const { return _downloaded; }
    bool  is_enabled() const { return _enabled; }
    void  set_enabled(bool p_enabled);

    // Decoder factory — each caller owns its ISVCDecoder instance
    Error create_decoder(ISVCDecoder **out);
    void  destroy_decoder(ISVCDecoder *decoder);

protected:
    static void _bind_methods();

private:
    static constexpr const char *OPENH264_VERSION = "2.6.0";

    static OpenH264 *_singleton;

    // Library state
    bool  _enabled              = false;
    bool  _downloaded           = false;
    bool  _download_in_progress = false;
    bool  _load_in_progress     = false;
    void *_lib_handle           = nullptr;

    FnWelsCreateDecoder  _fn_create_decoder  = nullptr;
    FnWelsDestroyDecoder _fn_destroy_decoder = nullptr;

    // Library helpers
    String _get_lib_filename() const;
    String _get_bz2_filename() const;
    String _get_lib_user_path() const;
    String _get_cdn_url() const;
    String _get_md5_url() const;
    String _get_license_url() const;
    String _get_license_user_path() const;

    bool _is_library_cached() const;
    void _start_background_download();
    void _start_background_load();
    void _background_download_task();
    void _load_library_task();

    PackedByteArray _download_bytes(const String &url) const;
    PackedByteArray _compute_md5(const PackedByteArray &data) const;
    bool            _verify_md5(const PackedByteArray &data,
                                const PackedByteArray &signed_md5_txt) const;
    Error           _extract_and_save(const PackedByteArray &bz2_data,
                                      const String &dst_path) const;

    Error _load_library();
    void  _unload_library();
    void  _on_download_complete(int error);
    void  _on_library_load_complete(int error);
    void *_get_proc(const String &name) const;
};

} // namespace godot
