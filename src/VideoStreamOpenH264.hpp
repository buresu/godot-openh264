#pragma once

#include <godot_cpp/classes/video_stream.hpp>
#include <godot_cpp/classes/video_stream_playback.hpp>

namespace godot {

class VideoStreamPlaybackOpenH264;

class VideoStreamOpenH264 : public VideoStream {
    GDCLASS(VideoStreamOpenH264, VideoStream)

public:
    Ref<VideoStreamPlayback> _instantiate_playback() override;

protected:
    static void _bind_methods();
};

} // namespace godot
