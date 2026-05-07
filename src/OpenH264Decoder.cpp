#include "OpenH264Decoder.hpp"
#include "OpenH264Loader.hpp"

#include <godot_cpp/variant/utility_functions.hpp>

#include <libyuv.h>

using namespace godot;

Error OpenH264Decoder::init() {
    OpenH264Loader *loader = OpenH264Loader::get_singleton();
    if (!loader || !loader->is_loaded()) {
        UtilityFunctions::printerr("[openh264] Loader not ready");
        return ERR_UNCONFIGURED;
    }

    long ret = loader->_fn_create_decoder(&_decoder);
    if (ret != 0 || !_decoder) {
        UtilityFunctions::printerr("[openh264] WelsCreateDecoder failed: ", (int64_t)ret);
        return FAILED;
    }

    SDecodingParam param{};
    param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_DEFAULT;
    param.bParseOnly                  = false;

    ret = _decoder->Initialize(&param);
    if (ret != 0) {
        UtilityFunctions::printerr("[openh264] Initialize failed: ", (int64_t)ret);
        loader->_fn_destroy_decoder(_decoder);
        _decoder = nullptr;
        return FAILED;
    }

    return OK;
}

void OpenH264Decoder::uninit() {
    OpenH264Loader *loader = OpenH264Loader::get_singleton();
    if (!loader || !_decoder) {
        return;
    }
    _decoder->Uninitialize();
    loader->_fn_destroy_decoder(_decoder);
    _decoder = nullptr;
}

Ref<Image> OpenH264Decoder::decode_nal(const uint8_t *data, int size) {
    if (!_decoder) {
        return {};
    }

    uint8_t     *yuv[3] = {};
    SBufferInfo  buf_info{};

    DECODING_STATE state = _decoder->DecodeFrameNoDelay(data, size, yuv, &buf_info);
    if (state != dsErrorFree || buf_info.iBufferStatus != 1) {
        return {};
    }

    return _yuv420_to_image(buf_info, yuv);
}

Ref<Image> OpenH264Decoder::flush() {
    if (!_decoder) {
        return {};
    }
    return decode_nal(nullptr, 0);
}

Ref<Image> OpenH264Decoder::_yuv420_to_image(const SBufferInfo &info,
                                              uint8_t *const *yuv) const {
    const SSysMEMBuffer &mem      = info.UsrData.sSystemBuffer;
    const int            width    = mem.iWidth;
    const int            height   = mem.iHeight;
    const int            stride_y = mem.iStride[0];
    const int            stride_uv = mem.iStride[1];

    PackedByteArray rgb;
    rgb.resize(width * height * 3);

    libyuv::I420ToRAW(
            yuv[0], stride_y,
            yuv[1], stride_uv,
            yuv[2], stride_uv,
            rgb.ptrw(), width * 3,
            width, height);

    return Image::create_from_data(width, height, false, Image::FORMAT_RGB8, rgb);
}
