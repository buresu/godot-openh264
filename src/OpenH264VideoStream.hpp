#pragma once

#include <godot_cpp/classes/video_stream.hpp>
#include <godot_cpp/classes/video_stream_playback.hpp>

namespace godot {

class OpenH264VideoStreamPlayback;

class OpenH264VideoStream : public VideoStream {
    GDCLASS(OpenH264VideoStream, VideoStream)

protected:
    static void _bind_methods();

public:
    Ref<VideoStreamPlayback> _instantiate_playback() override;
};

} // namespace godot
