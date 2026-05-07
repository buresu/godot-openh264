#pragma once

#include <godot_cpp/classes/video_stream.hpp>
#include <godot_cpp/classes/video_stream_playback.hpp>

namespace godot {

class VideoStreamPlaybackOpenH264;

class VideoStreamOpenH264 : public VideoStream {
    GDCLASS(VideoStreamOpenH264, VideoStream)

protected:
    static void _bind_methods();

public:
    Ref<VideoStreamPlayback> _instantiate_playback() override;
};

} // namespace godot
