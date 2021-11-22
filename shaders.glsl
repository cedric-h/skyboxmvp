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


@vs skybox_vs
uniform mesh_vs_params {
    mat4 mvp;
};

in vec3 position;
out vec3 tex_coord;

void main() {
  tex_coord = position;
  gl_Position = (mvp * vec4(position, 1)).xyww;
}
@end

@fs skybox_fs
uniform samplerCube skybox;

out vec4 frag_color;
in vec3 tex_coord;

void main() {
  frag_color = texture(skybox, tex_coord);
}
@end

@program skybox skybox_vs skybox_fs
