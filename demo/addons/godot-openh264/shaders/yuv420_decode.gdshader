shader_type canvas_item;

// Packed R8 texture layout (width x height*3/2):
//   rows [0, H)        : Y plane, full width
//   rows [H, H*3/2)    : U (left half) + V (right half) interleaved per row
//
// UV.y in [0,1] maps across the full texture in canvas_item shaders,
// so remap to the correct region for each plane.

void fragment() {
    float y = texture(TEXTURE, vec2(UV.x, UV.y * 2.0 / 3.0)).r;

    float chroma_y = 2.0 / 3.0 + UV.y / 3.0;
    float u = texture(TEXTURE, vec2(UV.x * 0.5, chroma_y)).r - 0.5;
    float v = texture(TEXTURE, vec2(0.5 + UV.x * 0.5, chroma_y)).r - 0.5;

    vec3 rgb;
    rgb.r = y + 1.402 * v;
    rgb.g = y - 0.344 * u - 0.714 * v;
    rgb.b = y + 1.772 * u;

    COLOR = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
