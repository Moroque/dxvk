#version 460

#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0)
uniform texture1DArray s_image;

layout(location = 0) out vec4 o_color;

layout(push_constant)
uniform u_info_t {
  ivec2 offset;
} u_info;

void main() {
  o_color = texelFetch(s_image,
    ivec2(gl_FragCoord.x + u_info.offset.x, gl_Layer), 0);
}