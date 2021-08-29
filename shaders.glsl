@ctype mat4 Mat4
@ctype vec2 Vec2

@vs mesh_vs
uniform mesh_vs_params {
    mat4 mvp;
};

in vec4 position;
in vec4 color0;

out vec4 color;

void main() {
  gl_Position = mvp * position;
  color = color0;
}
@end

@fs mesh_fs
in vec4 color;
out vec4 frag_color;

void main() {
  frag_color = color;
}
@end

@program mesh mesh_vs mesh_fs

@vs fsq_vs
@glsl_options flip_vert_y

in vec2 pos;

out vec2 uv;

void main() {
    gl_Position = vec4(pos*2.0-1.0, 0.5, 1.0);
    uv = pos;
}
@end

@fs fsq_fs
uniform sampler2D tex;

in vec2 uv;

out vec4 frag_color;

void main() {
    frag_color = vec4(texture(tex, uv).rgb, 1);
}
@end
@program fsq fsq_vs fsq_fs

@vs blur_vs
@glsl_options flip_vert_y

in vec2 pos;

out vec2 uv;

void main() {
    gl_Position = vec4(pos*2.0-1.0, 0.5, 1.0);
    uv = pos;
}
@end

@fs blur_fs
in vec2 uv;
out vec4 frag_color;

uniform sampler2D tex;
uniform blur_fs_params {
  vec2 hori;
};
const float weight[] = { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };

void main() {
  vec2 tex_offset = 1.0 / textureSize(tex, 0);
  vec3 result = texture(tex, uv).rgb * weight[0]; // current fragment's contribution
  for (int i = 1; i < 5; ++i) {
    result += texture(tex, uv + hori * tex_offset * i).rgb * weight[i];
    result += texture(tex, uv - hori * tex_offset * i).rgb * weight[i];
  }
  frag_color = vec4(result, 1.0);
}
@end
@program blur blur_vs blur_fs
