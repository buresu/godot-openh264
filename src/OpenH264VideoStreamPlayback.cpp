#define MINIMP4_IMPLEMENTATION
#include "OpenH264VideoStreamPlayback.hpp"

#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

using namespace godot;

constexpr uint8_t OpenH264VideoStreamPlayback::START_CODE[4];

// ---------------------------------------------------------------------------
// minimp4 IO callback
// ---------------------------------------------------------------------------

int OpenH264VideoStreamPlayback::mp4_read_cb(int64_t offset, void *buf,
                                              size_t size, void *token) {
    auto *self = static_cast<OpenH264VideoStreamPlayback *>(token);
    if (offset < 0 || (size_t)offset + size > (size_t)self->file_data.size()) {
        return -1;
    }
    memcpy(buf, self->file_data.ptr() + offset, size);
    return 0;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

OpenH264VideoStreamPlayback::OpenH264VideoStreamPlayback() {
    texture.instantiate();
}

OpenH264VideoStreamPlayback::~OpenH264VideoStreamPlayback() {
    close_file();
}

void OpenH264VideoStreamPlayback::_bind_methods() {}

// ---------------------------------------------------------------------------
// File open / close
// ---------------------------------------------------------------------------

void OpenH264VideoStreamPlayback::set_file(const String &p_file) {
    file_path = p_file;
}

bool OpenH264VideoStreamPlayback::open_file() {
    const String global = ProjectSettings::get_singleton()->globalize_path(file_path);
    file_data = FileAccess::get_file_as_bytes(file_path);
    if (file_data.is_empty()) {
        UtilityFunctions::printerr("[openh264] Cannot read file: ", file_path);
        return false;
    }

    if (MP4D_open(&mp4, mp4_read_cb, this, file_data.size()) != 1) {
        UtilityFunctions::printerr("[openh264] MP4D_open failed for: ", file_path);
        return false;
    }
    mp4_open = true;

    // Find the first H.264 video track
    for (unsigned i = 0; i < mp4.track_count; ++i) {
        if (mp4.track[i].handler_type == MP4D_HANDLER_TYPE_VIDE) {
            // Only AVC (H.264) supported
            if (mp4.track[i].object_type_indication == MP4_OBJECT_TYPE_AVC) {
                track_idx     = (int)i;
                total_samples = mp4.track[i].sample_count;
                // fps from track timescale / duration per sample
                if (total_samples > 0 && mp4.track[i].timescale > 0) {
                    double total_dur = (double)mp4.track[i].duration_hi;
                    total_dur        = total_dur * (double)(1ull << 32);
                    total_dur       += (double)mp4.track[i].duration_lo;
                    frame_time       = (total_dur / mp4.track[i].timescale) / total_samples;
                }
                break;
            }
        }
    }

    if (track_idx < 0) {
        UtilityFunctions::printerr("[openh264] No H.264 video track found in: ", file_path);
        return false;
    }

    if (decoder.init() != OK) {
        return false;
    }

    sample_idx = 0;
    return true;
}

void OpenH264VideoStreamPlayback::close_file() {
    decoder.uninit();
    if (mp4_open) {
        MP4D_close(&mp4);
        mp4_open = false;
    }
    file_data = PackedByteArray();
    track_idx  = -1;
    sample_idx = 0;
}

// ---------------------------------------------------------------------------
// Frame decoding
// ---------------------------------------------------------------------------

void OpenH264VideoStreamPlayback::advance_frame() {
    if (!mp4_open || track_idx < 0 || sample_idx >= total_samples) {
        return;
    }

    MP4D_file_offset_t ofs;
    unsigned           size = 0;
    unsigned           timestamp_ms = 0;
    unsigned           duration_ms  = 0;

    ofs = MP4D_frame_offset(&mp4, track_idx, sample_idx, &size, &timestamp_ms, &duration_ms);

    if (size == 0 || ofs + size > (MP4D_file_offset_t)file_data.size()) {
        ++sample_idx;
        return;
    }

    const uint8_t *raw = file_data.ptr() + ofs;

    // AVCC: length-prefixed NAL units → convert to Annex-B for openh264
    PackedByteArray annexb;
    size_t remaining = size;
    const uint8_t *p = raw;

    while (remaining >= 4) {
        uint32_t nal_size = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                            ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
        p         += 4;
        remaining -= 4;

        if (nal_size > remaining) {
            break;
        }

        // Append Annex-B start code + NAL
        for (uint8_t b : START_CODE) {
            annexb.append(b);
        }
        for (uint32_t i = 0; i < nal_size; ++i) {
            annexb.append(p[i]);
        }

        p         += nal_size;
        remaining -= nal_size;
    }

    Ref<Image> img = decoder.decode_nal(annexb.ptr(), annexb.size());
    if (img.is_valid()) {
        texture->set_image(img);
    }

    ++sample_idx;
}

// ---------------------------------------------------------------------------
// VideoStreamPlayback interface
// ---------------------------------------------------------------------------

void OpenH264VideoStreamPlayback::_play() {
    if (!mp4_open) {
        open_file();
    }
    playing = true;
    paused  = false;
    time    = 0.0;
    sample_idx = 0;
}

void OpenH264VideoStreamPlayback::_stop() {
    playing = false;
    paused  = false;
    time    = 0.0;
    close_file();
}

bool OpenH264VideoStreamPlayback::_is_playing() const {
    return playing && !paused;
}

void OpenH264VideoStreamPlayback::_set_paused(bool p_paused) {
    paused = p_paused;
}

bool OpenH264VideoStreamPlayback::_is_paused() const {
    return paused;
}

double OpenH264VideoStreamPlayback::_get_length() const {
    if (!mp4_open || track_idx < 0) {
        return 0.0;
    }
    return frame_time * total_samples;
}

double OpenH264VideoStreamPlayback::_get_playback_position() const {
    return time;
}

void OpenH264VideoStreamPlayback::_seek(double p_time) {
    if (!mp4_open || track_idx < 0 || frame_time <= 0.0) {
        return;
    }
    time       = p_time;
    sample_idx = (uint32_t)(p_time / frame_time);
    if (sample_idx >= total_samples) {
        sample_idx = total_samples;
    }
}

void OpenH264VideoStreamPlayback::_update(double p_delta) {
    if (!playing || paused || !mp4_open) {
        return;
    }

    time += p_delta;

    // Decode all frames that fall within the elapsed time window
    while (frame_time > 0.0 && sample_idx < total_samples &&
           (double)sample_idx * frame_time <= time) {
        advance_frame();
    }

    // Loop
    if (sample_idx >= total_samples) {
        sample_idx = 0;
        time       = 0.0;
    }
}

Ref<Texture2D> OpenH264VideoStreamPlayback::_get_texture() const {
    return texture;
}
