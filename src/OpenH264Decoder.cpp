#include "OpenH264Decoder.hpp"
#include "OpenH264Loader.hpp"

#include <godot_cpp/variant/utility_functions.hpp>

#include <libyuv.h>

// Additional openh264 function pointer types resolved at runtime
using FnInitialize   = long (ISVCDecoder::*)(const SDecodingParam *);
using FnUninitialize = long (ISVCDecoder::*)();
using FnDecodeFrame  = DECODING_STATE (ISVCDecoder::*)(const unsigned char *, int,
                                                        unsigned char **, SBufferInfo *);

using namespace godot;

Error OpenH264Decoder::init() {
    OpenH264Loader *loader = OpenH264Loader::get_singleton();
    if (!loader || !loader->is_loaded()) {
        UtilityFunctions::printerr("[openh264] Loader not ready — call ensure_ready() first");
        return ERR_UNCONFIGURED;
    }

    long ret = loader->fn_create_decoder(&decoder);
    if (ret != 0 || !decoder) {
        UtilityFunctions::printerr("[openh264] WelsCreateDecoder failed: ", (int64_t)ret);
        return FAILED;
    }

    SDecodingParam param{};
    param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_DEFAULT;
    param.bParseOnly                  = false;

    ret = decoder->Initialize(&param);
    if (ret != 0) {
        UtilityFunctions::printerr("[openh264] ISVCDecoder::Initialize failed: ", (int64_t)ret);
        loader->fn_destroy_decoder(decoder);
        decoder = nullptr;
        return FAILED;
    }

    return OK;
}

void OpenH264Decoder::uninit() {
    OpenH264Loader *loader = OpenH264Loader::get_singleton();
    if (!loader || !decoder) {
        return;
    }
    decoder->Uninitialize();
    loader->fn_destroy_decoder(decoder);
    decoder = nullptr;
}

Ref<Image> OpenH264Decoder::decode_nal(const uint8_t *data, int size) {
    if (!decoder) {
        return {};
    }

    uint8_t *yuv[3] = {};
    SBufferInfo buf_info{};

    DECODING_STATE state = decoder->DecodeFrameNoDelay(data, size, yuv, &buf_info);
    if (state != dsErrorFree) {
        // Non-fatal: decoder may not have enough data yet (e.g., SPS/PPS only)
        return {};
    }
    if (buf_info.iBufferStatus != 1) {
        return {}; // no output frame this call
    }

    return yuv420_to_image(buf_info, yuv);
}

Ref<Image> OpenH264Decoder::flush() {
    if (!decoder) {
        return {};
    }
    // Send empty NAL to flush
    return decode_nal(nullptr, 0);
}

// ---------------------------------------------------------------------------
// YUV420P → RGB conversion
// ---------------------------------------------------------------------------

Ref<Image> OpenH264Decoder::yuv420_to_image(const SBufferInfo &info,
                                             uint8_t *const *yuv) const {
    const SSysMEMBuffer &mem = info.UsrData.sSystemBuffer;
    const int width           = mem.iWidth;
    const int height          = mem.iHeight;
    const int stride_y        = mem.iStride[0];
    const int stride_uv       = mem.iStride[1];

    PackedByteArray rgb;
    rgb.resize(width * height * 3);
    uint8_t *dst = rgb.ptrw();

    const uint8_t *y_plane  = yuv[0];
    const uint8_t *u_plane  = yuv[1];
    const uint8_t *v_plane  = yuv[2];

    // libyuv: SIMD-optimized I420 → RGB (SSE2/AVX2/NEON selected at runtime)
    // I420ToRAW outputs R,G,B order (matching FORMAT_RGB8).
    // I420ToRGB24 outputs B,G,R order — do NOT use that one.
    libyuv::I420ToRAW(
            yuv[0], stride_y,
            yuv[1], stride_uv,
            yuv[2], stride_uv,
            dst, width * 3,
            width, height);

    return Image::create_from_data(width, height, false, Image::FORMAT_RGB8, rgb);
}
