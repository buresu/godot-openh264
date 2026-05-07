#pragma once

#include "OpenH264Decoder.hpp"
#include "OpenH264Loader.hpp"

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/video_stream_playback.hpp>
#include <godot_cpp/variant/string.hpp>

// minimp4 type declarations only (implementation compiled in .cpp)
#define MP4D_64BIT_SUPPORTED 1
#include <minimp4.h>

namespace godot {

class OpenH264VideoStreamPlayback : public VideoStreamPlayback {
    GDCLASS(OpenH264VideoStreamPlayback, VideoStreamPlayback)

    // MP4 demux state
    MP4D_demux_t mp4{};
    bool         mp4_open   = false;
    int          track_idx  = -1;   // H264 track index
    uint32_t     sample_idx = 0;    // current sample within the track
    uint32_t     total_samples = 0;

    // Raw file bytes kept in memory for minimp4 callbacks
    PackedByteArray file_data;

    // Loader (singleton reuse or newly created)
    Ref<OpenH264Loader> loader_;

    // Decoder
    OpenH264Decoder decoder;

    // Playback state
    String           file_path;
    bool             playing    = false;
    bool             paused     = false;
    double           time       = 0.0;
    double           frame_time = 0.0; // seconds per frame (1/fps)
    Ref<ImageTexture> texture;
    Ref<Image>        pending_frame;      // latest decoded frame, uploaded in _update()
    bool              texture_initialized = false;

    // minimp4 IO callback (static trampoline)
    static int mp4_read_cb(int64_t offset, void *buf, size_t size, void *token);

    bool open_file();
    void close_file();

    // Feed SPS/PPS from avcC box to decoder before the first frame
    void _send_sps_pps();

    // Decode next frame and update texture
    void advance_frame();

    // Annex-B start code prefix
    static constexpr uint8_t START_CODE[4] = { 0x00, 0x00, 0x00, 0x01 };

protected:
    static void _bind_methods();

public:
    OpenH264VideoStreamPlayback();
    ~OpenH264VideoStreamPlayback() override;

    void set_file(const String &p_file);

    // VideoStreamPlayback overrides
    void   _play() override;
    void   _stop() override;
    bool   _is_playing() const override;
    void   _set_paused(bool p_paused) override;
    bool   _is_paused() const override;
    double _get_length() const override;
    double _get_playback_position() const override;
    void   _seek(double p_time) override;
    void   _update(double p_delta) override;
    int    _get_channels() const override { return 0; }
    int    _get_mix_rate() const override { return 0; }
    Ref<Texture2D> _get_texture() const override;
};

} // namespace godot
