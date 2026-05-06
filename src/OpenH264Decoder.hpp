#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

// openh264 types (from lib/openh264/codec/api/wels/)
#include "codec_api.h"
#include "codec_app_def.h"

namespace godot {

// Thin C++ wrapper around ISVCDecoder, calling the openh264 API exclusively
// through function pointers obtained from OpenH264Loader.
class OpenH264Decoder {
    ISVCDecoder *decoder = nullptr;

    // Convert YUV420P (planar) to RGB packed into a godot Image.
    Ref<Image> yuv420_to_image(const SBufferInfo &buf_info,
                               uint8_t *const *yuv_planes) const;

public:
    OpenH264Decoder()  = default;
    ~OpenH264Decoder() = default;

    // Initialize the decoder. Call after OpenH264Loader::ensure_ready().
    Error init();

    // Decode one H.264 Annex-B / AVCC NAL unit.
    // Returns a valid Image on success, null on error or if no output frame yet.
    Ref<Image> decode_nal(const uint8_t *data, int size);

    // Flush any buffered frames (call at end-of-stream).
    Ref<Image> flush();

    void uninit();

    bool is_initialized() const { return decoder != nullptr; }
};

} // namespace godot
