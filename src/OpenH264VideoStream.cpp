#include "OpenH264VideoStream.hpp"
#include "OpenH264VideoStreamPlayback.hpp"

using namespace godot;

void OpenH264VideoStream::_bind_methods() {}

Ref<VideoStreamPlayback> OpenH264VideoStream::_instantiate_playback() {
    Ref<OpenH264VideoStreamPlayback> pb;
    pb.instantiate();
    pb->set_file(get_file());
    return pb;
}
