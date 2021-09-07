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
layout (location = 0) out vec4 frag_color;
layout (location = 1) out vec4 bright_color;

void main() {
  frag_color = color;
  bright_color = color * (1.0 - step(color.b, 0.8));
}
@end

@program mesh mesh_vs mesh_fs
