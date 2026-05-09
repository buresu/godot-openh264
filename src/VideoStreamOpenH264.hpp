#pragma once

#include <godot_cpp/classes/video_stream.hpp>
#include <godot_cpp/classes/video_stream_playback.hpp>

namespace godot {

class VideoStreamPlaybackOpenH264;

class VideoStreamOpenH264 : public VideoStream {
    GDCLASS(VideoStreamOpenH264, VideoStream)

public:
    Ref<VideoStreamPlayback> _instantiate_playback() override;

    bool get_use_shader_decode() const { return _use_shader_decode; }
    void set_use_shader_decode(bool p_use) { _use_shader_decode = p_use; }

protected:
    static void _bind_methods();

private:
    bool _use_shader_decode = false;
};

} // namespace godot
