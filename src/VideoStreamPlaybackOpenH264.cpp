#define MINIMP4_IMPLEMENTATION
#include "VideoStreamPlaybackOpenH264.hpp"
#include "OpenH264Loader.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

constexpr uint8_t VideoStreamPlaybackOpenH264::START_CODE[4];

int VideoStreamPlaybackOpenH264::_mp4_read_cb(int64_t offset, void *buf,
                                               size_t size, void *token) {
    auto *self = static_cast<VideoStreamPlaybackOpenH264 *>(token);
    if (offset < 0 || (size_t)offset + size > (size_t)self->_file_data.size()) {
        return -1;
    }
    memcpy(buf, self->_file_data.ptr() + offset, size);
    return 0;
}

VideoStreamPlaybackOpenH264::VideoStreamPlaybackOpenH264() {
    _texture.instantiate();
}

VideoStreamPlaybackOpenH264::~VideoStreamPlaybackOpenH264() {
    _close_file();
}

void VideoStreamPlaybackOpenH264::_bind_methods() {}

void VideoStreamPlaybackOpenH264::set_file(const String &p_file) {
    _file_path = p_file;
}

bool VideoStreamPlaybackOpenH264::_open_file() {
    _file_data = FileAccess::get_file_as_bytes(_file_path);
    if (_file_data.is_empty()) {
        UtilityFunctions::printerr("[openh264] Cannot read file: ", _file_path);
        return false;
    }

    if (MP4D_open(&_mp4, _mp4_read_cb, this, _file_data.size()) != 1) {
        UtilityFunctions::printerr("[openh264] MP4D_open failed for: ", _file_path);
        return false;
    }
    _mp4_open = true;

    for (unsigned i = 0; i < _mp4.track_count; ++i) {
        const auto &t = _mp4.track[i];
        if (t.handler_type != MP4D_HANDLER_TYPE_VIDE || t.object_type_indication != 0x21) {
            continue;
        }
        _track_idx     = (int)i;
        _total_samples = t.sample_count;

        if (_total_samples > 0 && t.timescale > 0) {
            double total_dur = (double)t.duration_hi * (double)(1ull << 32)
                             + (double)t.duration_lo;
            _frame_time = (total_dur / t.timescale) / _total_samples;
        }
        break;
    }

    if (_track_idx < 0) {
        UtilityFunctions::printerr("[openh264] No H.264 video track found in: ", _file_path);
        return false;
    }

    if (_decoder.init() != OK) {
        return false;
    }

    _send_sps_pps();
    _sample_idx = 0;
    return true;
}

void VideoStreamPlaybackOpenH264::_close_file() {
    _decoder.uninit();
    if (_mp4_open) {
        MP4D_close(&_mp4);
        _mp4_open = false;
    }
    _file_data           = PackedByteArray();
    _pending_frame       = Ref<Image>();
    _texture_initialized = false;
    _track_idx           = -1;
    _sample_idx          = 0;
}

void VideoStreamPlaybackOpenH264::_send_sps_pps() {
    for (int idx = 0; ; ++idx) {
        int            sps_size = 0;
        const uint8_t *sps      = static_cast<const uint8_t *>(
                MP4D_read_sps(&_mp4, _track_idx, idx, &sps_size));
        if (!sps || sps_size <= 0) {
            break;
        }
        _annexb_buf.resize(4 + sps_size);
        uint8_t *w = _annexb_buf.ptrw();
        memcpy(w, START_CODE, 4);
        memcpy(w + 4, sps, sps_size);
        _decoder.decode_nal(_annexb_buf.ptr(), (int)_annexb_buf.size());
    }

    for (int idx = 0; ; ++idx) {
        int            pps_size = 0;
        const uint8_t *pps      = static_cast<const uint8_t *>(
                MP4D_read_pps(&_mp4, _track_idx, idx, &pps_size));
        if (!pps || pps_size <= 0) {
            break;
        }
        _annexb_buf.resize(4 + pps_size);
        uint8_t *w = _annexb_buf.ptrw();
        memcpy(w, START_CODE, 4);
        memcpy(w + 4, pps, pps_size);
        _decoder.decode_nal(_annexb_buf.ptr(), (int)_annexb_buf.size());
    }
}

void VideoStreamPlaybackOpenH264::_advance_frame() {
    if (!_mp4_open || _track_idx < 0 || _sample_idx >= _total_samples) {
        return;
    }

    MP4D_file_offset_t ofs;
    unsigned           size         = 0;
    unsigned           timestamp_ms = 0;
    unsigned           duration_ms  = 0;

    ofs = MP4D_frame_offset(&_mp4, _track_idx, _sample_idx, &size, &timestamp_ms, &duration_ms);

    if (size == 0 || ofs + size > (MP4D_file_offset_t)_file_data.size()) {
        ++_sample_idx;
        return;
    }

    const uint8_t *raw = _file_data.ptr() + ofs;

    _annexb_buf.resize(size);
    uint8_t *dst      = _annexb_buf.ptrw();
    int      written  = 0;
    size_t   remaining = size;
    const uint8_t *p   = raw;

    while (remaining >= 4) {
        uint32_t nal_size = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                            ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
        p         += 4;
        remaining -= 4;

        if (nal_size > remaining) {
            break;
        }

        memcpy(dst + written, START_CODE, 4);
        written += 4;
        memcpy(dst + written, p, nal_size);
        written += nal_size;

        p         += nal_size;
        remaining -= nal_size;
    }

    _annexb_buf.resize(written);

    Ref<Image> img = _decoder.decode_nal(_annexb_buf.ptr(), (int)_annexb_buf.size());
    if (img.is_valid()) {
        _pending_frame = img;
    }

    ++_sample_idx;
}

void VideoStreamPlaybackOpenH264::_play() {
    _playing    = true;
    _paused     = false;
    _time       = 0.0;
    _sample_idx = 0;
}

void VideoStreamPlaybackOpenH264::_stop() {
    _playing = false;
    _paused  = false;
    _time    = 0.0;
    _close_file();
}

bool VideoStreamPlaybackOpenH264::_is_playing() const {
    return _playing && !_paused;
}

void VideoStreamPlaybackOpenH264::_set_paused(bool p_paused) {
    _paused = p_paused;
}

bool VideoStreamPlaybackOpenH264::_is_paused() const {
    return _paused;
}

double VideoStreamPlaybackOpenH264::_get_length() const {
    if (!_mp4_open || _track_idx < 0) {
        return 0.0;
    }
    return _frame_time * _total_samples;
}

double VideoStreamPlaybackOpenH264::_get_playback_position() const {
    return _time;
}

void VideoStreamPlaybackOpenH264::_seek(double p_time) {
    if (!_mp4_open || _track_idx < 0 || _frame_time <= 0.0) {
        return;
    }
    _time       = p_time;
    _sample_idx = (uint32_t)(p_time / _frame_time);
    if (_sample_idx >= _total_samples) {
        _sample_idx = _total_samples;
    }
}

void VideoStreamPlaybackOpenH264::_update(double p_delta) {
    if (!_playing || _paused) {
        return;
    }
    if (!OpenH264Loader::get_singleton()->is_loaded()) {
        return;
    }
    if (!_mp4_open) {
        _open_file();
        if (!_mp4_open) {
            return;
        }
    }

    _time += p_delta;

    while (_frame_time > 0.0 && _sample_idx < _total_samples &&
           (double)_sample_idx * _frame_time <= _time) {
        _advance_frame();
    }

    if (_pending_frame.is_valid()) {
        if (!_texture_initialized) {
            _texture->set_image(_pending_frame);
            _texture_initialized = true;
        } else {
            _texture->update(_pending_frame);
        }
        _pending_frame = Ref<Image>();
    }

    if (_sample_idx >= _total_samples) {
        _sample_idx = 0;
        _time       = 0.0;
    }
}

Ref<Texture2D> VideoStreamPlaybackOpenH264::_get_texture() const {
    return _texture;
}
