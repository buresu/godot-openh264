#define MINIMP4_IMPLEMENTATION
#include "OpenH264VideoStreamPlayback.hpp"

#include <godot_cpp/classes/file_access.hpp>
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

    // Reuse the existing singleton loader if available, otherwise create one.
    // The loader starts the background download automatically in its constructor.
    if (OpenH264Loader::get_singleton()) {
        loader_ = Ref<OpenH264Loader>(OpenH264Loader::get_singleton());
    } else {
        loader_.instantiate();
    }
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

    // Find the first AVC (H.264) video track.
    // object_type_indication 0x21 = AVC per ISO 14496-1.
    for (unsigned i = 0; i < mp4.track_count; ++i) {
        const auto &t = mp4.track[i];
        if (t.handler_type != MP4D_HANDLER_TYPE_VIDE) {
            continue;
        }
        if (t.object_type_indication != 0x21) {
            continue;
        }
        track_idx     = (int)i;
        total_samples = t.sample_count;

        // Compute per-frame duration from total track duration / sample count.
        if (total_samples > 0 && t.timescale > 0) {
            double total_dur = (double)t.duration_hi * (double)(1ull << 32)
                             + (double)t.duration_lo;
            frame_time = (total_dur / t.timescale) / total_samples;
        }
        break;
    }

    if (track_idx < 0) {
        UtilityFunctions::printerr("[openh264] No H.264 video track found in: ", file_path);
        return false;
    }

    if (decoder.init() != OK) {
        return false;
    }

    // Feed SPS and PPS from the avcC box to the decoder before any frame.
    // Without this the decoder cannot output any picture.
    _send_sps_pps();

    sample_idx = 0;
    return true;
}

void OpenH264VideoStreamPlayback::close_file() {
    decoder.uninit();
    if (mp4_open) {
        MP4D_close(&mp4);
        mp4_open = false;
    }
    file_data           = PackedByteArray();
    pending_frame       = Ref<Image>();
    texture_initialized = false;
    track_idx           = -1;
    sample_idx          = 0;
}

// ---------------------------------------------------------------------------
// SPS / PPS initialization
// ---------------------------------------------------------------------------

void OpenH264VideoStreamPlayback::_send_sps_pps() {
    // MP4D_read_sps / MP4D_read_pps return pointers into the in-memory file data.
    // We wrap each NAL in an Annex-B start code and feed it to the decoder.
    for (int idx = 0; ; ++idx) {
        int sps_size = 0;
        const uint8_t *sps = static_cast<const uint8_t *>(
                MP4D_read_sps(&mp4, track_idx, idx, &sps_size));
        if (!sps || sps_size <= 0) {
            break;
        }
        PackedByteArray nal;
        for (uint8_t b : START_CODE) {
            nal.append(b);
        }
        for (int i = 0; i < sps_size; ++i) {
            nal.append(sps[i]);
        }
        decoder.decode_nal(nal.ptr(), nal.size()); // output ignored — no picture yet
    }

    for (int idx = 0; ; ++idx) {
        int pps_size = 0;
        const uint8_t *pps = static_cast<const uint8_t *>(
                MP4D_read_pps(&mp4, track_idx, idx, &pps_size));
        if (!pps || pps_size <= 0) {
            break;
        }
        PackedByteArray nal;
        for (uint8_t b : START_CODE) {
            nal.append(b);
        }
        for (int i = 0; i < pps_size; ++i) {
            nal.append(pps[i]);
        }
        decoder.decode_nal(nal.ptr(), nal.size());
    }
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
        pending_frame = img; // texture upload is done in _update()
    }

    ++sample_idx;
}

// ---------------------------------------------------------------------------
// VideoStreamPlayback interface
// ---------------------------------------------------------------------------

void OpenH264VideoStreamPlayback::_play() {
    playing    = true;
    paused     = false;
    time       = 0.0;
    sample_idx = 0;
    // Actual file open is deferred to _update() once the library is ready.
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
    if (!playing || paused) {
        return;
    }
    // Wait until openh264 is loaded before opening/decoding.
    if (!loader_.is_valid() || !loader_->is_loaded()) {
        return;
    }
    if (!mp4_open) {
        open_file();
        if (!mp4_open) {
            return;
        }
    }

    time += p_delta;

    // Decode frames that fall within the elapsed time window.
    while (frame_time > 0.0 && sample_idx < total_samples &&
           (double)sample_idx * frame_time <= time) {
        advance_frame();
    }

    // Upload the latest decoded frame to the GPU once per _update() call.
    // Use set_image() on first frame (allocates texture), update() thereafter
    // (in-place update, no reallocation — significantly cheaper for video).
    if (pending_frame.is_valid()) {
        if (!texture_initialized) {
            texture->set_image(pending_frame);
            texture_initialized = true;
        } else {
            texture->update(pending_frame);
        }
        pending_frame = Ref<Image>(); // release reference
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
