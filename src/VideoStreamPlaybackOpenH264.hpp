#pragma once

#include "OpenH264Decoder.hpp"

#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/video_stream_playback.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

#define MP4D_64BIT_SUPPORTED 1
#include <minimp4.h>

namespace godot {

class VideoStreamPlaybackOpenH264 : public VideoStreamPlayback {
    GDCLASS(VideoStreamPlaybackOpenH264, VideoStreamPlayback)

public:
    VideoStreamPlaybackOpenH264();
    ~VideoStreamPlaybackOpenH264() override;

    void set_file(const String &p_file);

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

protected:
    static void _bind_methods();

private:
    static constexpr uint8_t START_CODE[4] = { 0x00, 0x00, 0x00, 0x01 };

    MP4D_demux_t    _mp4{};
    bool            _mp4_open      = false;
    int             _track_idx     = -1;
    uint32_t        _sample_idx    = 0;
    uint32_t        _total_samples = 0;

    PackedByteArray _file_data;
    PackedByteArray _annexb_buf;

    OpenH264Decoder     _decoder;

    String            _file_path;
    bool              _playing             = false;
    bool              _paused              = false;
    double            _time                = 0.0;
    double            _frame_time          = 0.0;
    Ref<ImageTexture> _texture;
    Ref<Image>        _pending_frame;
    bool              _texture_initialized = false;

    static int _mp4_read_cb(int64_t offset, void *buf, size_t size, void *token);

    bool _open_file();
    void _close_file();
    void _send_sps_pps();
    void _advance_frame();
};

} // namespace godot
