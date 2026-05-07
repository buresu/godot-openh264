#include "VideoStreamOpenH264.hpp"
#include "VideoStreamPlaybackOpenH264.hpp"

using namespace godot;

void VideoStreamOpenH264::_bind_methods() {}

Ref<VideoStreamPlayback> VideoStreamOpenH264::_instantiate_playback() {
    Ref<VideoStreamPlaybackOpenH264> pb;
    pb.instantiate();
    pb->set_file(get_file());
    return pb;
}
