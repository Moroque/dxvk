#version 460

#extension GL_EXT_samplerless_texture_functions : require

layout(set = 0, binding = 0)
uniform texture2DArray s_image;

layout(location = 0) out vec4 o_color;

layout(push_constant)
uniform u_info_t {
  ivec2 offset;
} u_info;

void main() {
  o_color = texelFetch(s_image,
    ivec3(gl_FragCoord.xy + u_info.offset, gl_Layer), 0);
}