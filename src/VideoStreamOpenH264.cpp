#include "VideoStreamOpenH264.hpp"
#include "VideoStreamPlaybackOpenH264.hpp"

using namespace godot;

void VideoStreamOpenH264::_bind_methods() {
    ClassDB::bind_method(D_METHOD("get_use_shader_decode"), &VideoStreamOpenH264::get_use_shader_decode);
    ClassDB::bind_method(D_METHOD("set_use_shader_decode", "use_shader_decode"), &VideoStreamOpenH264::set_use_shader_decode);
    ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_shader_decode"), "set_use_shader_decode", "get_use_shader_decode");
}

Ref<VideoStreamPlayback> VideoStreamOpenH264::_instantiate_playback() {
    Ref<VideoStreamPlaybackOpenH264> pb;
    pb.instantiate();
    pb->set_file(get_file());
    pb->set_use_shader_decode(_use_shader_decode);
    return pb;
}
