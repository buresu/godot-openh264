#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/templates/vector.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

#include "codec_api.h"
#include "codec_app_def.h"

namespace godot {

class OpenH264Decoder {
public:
    OpenH264Decoder()  = default;
    ~OpenH264Decoder() = default;

    Error      init();
    Ref<Image> decode_nal(const uint8_t *data, int size);
    Ref<Image> flush();
    void       uninit();

    bool is_initialized() const { return _decoder != nullptr; }

private:
    ISVCDecoder *_decoder = nullptr;

    Ref<Image> _yuv420_to_image(const SBufferInfo &buf_info,
                                uint8_t *const *yuv_planes) const;
};

} // namespace godot
