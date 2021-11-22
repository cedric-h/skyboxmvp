#define SOKOL_IMPL
#if defined(_MSC_VER)
#define SOKOL_D3D11
#define SOKOL_LOG(str) OutputDebugStringA(str)
#elif defined(__EMSCRIPTEN__)
#define SOKOL_GLES2
#elif defined(__APPLE__)
// NOTE: on macOS, sokol.c is compiled explicitly as ObjC 
#define SOKOL_METAL
#else
#define SOKOL_GLCORE33
#endif

#ifndef __GNUC__
#define __attribute__(unused)
#endif

#define SOKOL_WIN32_FORCE_MAIN
#include "sokol/sokol_app.h"
#include "sokol/sokol_time.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_glue.h"

#include <math.h>
#include "math.h"

#include "build/shaders.glsl.h"
#include "snoise3.h"
#define CUTE_PNG_IMPLEMENTATION
#include "cute_png.h"

#define OFFSCREEN_SAMPLE_COUNT (4)

static struct {
  float rx, ry;
  struct {
    sg_image tex;
    sg_pipeline pip;
  } skybox;
  struct {
    sg_buffer ibuf, vbuf;
    sg_pass_action pass_action;
    sg_pipeline pip;
  } mesh;
} state;

/* can be called once on initialization */
void mesh_init(void) {
  /* mesh pass action */
  state.mesh.pass_action = (sg_pass_action) {
    .colors[0] = { .action = SG_ACTION_CLEAR, .value = { 0.25f, 0.25f, 0.25f, 1.0f } },
    .colors[1] = { .action = SG_ACTION_CLEAR, .value = { 0.00f, 0.00f, 0.00f, 0.0f } },
  };

  sg_pipeline_desc desc = {
    .layout = {
      .attrs = {
        [ATTR_mesh_vs_position].format = SG_VERTEXFORMAT_FLOAT3,
        [ATTR_mesh_vs_color0].format   = SG_VERTEXFORMAT_FLOAT4
      }
    },
    .index_type = SG_INDEXTYPE_UINT16,
    .cull_mode = SG_CULLMODE_FRONT,
    .depth = {
      .write_enabled = true,
      .compare = SG_COMPAREFUNC_LESS_EQUAL,
    },
  };

  desc.shader = sg_make_shader(mesh_shader_desc(sg_query_backend()));
  state.mesh.pip = sg_make_pipeline(&desc);

  desc.shader = sg_make_shader(skybox_shader_desc(sg_query_backend()));
  desc.depth.write_enabled = false;
  desc.cull_mode = SG_CULLMODE_BACK;
  state.skybox.pip = sg_make_pipeline(&desc);
}

void init(void) {
  sg_setup(&(sg_desc){
    .context = sapp_sgcontext()
  });

  /* cube vertex buffer */
  float vertices[] = {
    -1.0, -1.0, -1.0,   1.0, 0.0, 0.0, 1.0,
    1.0, -1.0, -1.0,   1.0, 0.0, 0.0, 1.0,
    1.0,  1.0, -1.0,   1.0, 0.0, 0.0, 1.0,
    -1.0,  1.0, -1.0,   1.0, 0.0, 0.0, 1.0,

    -1.0, -1.0,  1.0,   0.0, 1.0, 0.0, 1.0,
    1.0, -1.0,  1.0,   0.0, 1.0, 0.0, 1.0,
    1.0,  1.0,  1.0,   0.0, 1.0, 0.0, 1.0,
    -1.0,  1.0,  1.0,   0.0, 1.0, 0.0, 1.0,

    -1.0, -1.0, -1.0,   0.0, 0.0, 1.0, 1.0,
    -1.0,  1.0, -1.0,   0.0, 0.0, 1.0, 1.0,
    -1.0,  1.0,  1.0,   0.0, 0.0, 1.0, 1.0,
    -1.0, -1.0,  1.0,   0.0, 0.0, 1.0, 1.0,

    1.0, -1.0, -1.0,    1.0, 0.5, 0.0, 1.0,
    1.0,  1.0, -1.0,    1.0, 0.5, 0.0, 1.0,
    1.0,  1.0,  1.0,    1.0, 0.5, 0.0, 1.0,
    1.0, -1.0,  1.0,    1.0, 0.5, 0.0, 1.0,

    -1.0, -1.0, -1.0,   0.0, 0.5, 1.0, 1.0,
    -1.0, -1.0,  1.0,   0.0, 0.5, 1.0, 1.0,
    1.0, -1.0,  1.0,   0.0, 0.5, 1.0, 1.0,
    1.0, -1.0, -1.0,   0.0, 0.5, 1.0, 1.0,

    -1.0,  1.0, -1.0,   1.0, 0.0, 0.5, 1.0,
    -1.0,  1.0,  1.0,   1.0, 0.0, 0.5, 1.0,
    1.0,  1.0,  1.0,   1.0, 0.0, 0.5, 1.0,
    1.0,  1.0, -1.0,   1.0, 0.0, 0.5, 1.0
  };
  state.mesh.vbuf = sg_make_buffer(&(sg_buffer_desc){
    .data = SG_RANGE(vertices),
    .label = "cube-vertices"
  });

  /* create an index buffer for the cube */
  uint16_t indices[] = {
    0, 1, 2,  0, 2, 3,
    6, 5, 4,  7, 6, 4,
    8, 9, 10,  8, 10, 11,
    14, 13, 12,  15, 14, 12,
    16, 17, 18,  16, 18, 19,
    22, 21, 20,  23, 22, 20
  };
  state.mesh.ibuf = sg_make_buffer(&(sg_buffer_desc){
    .type = SG_BUFFERTYPE_INDEXBUFFER,
    .data = SG_RANGE(indices),
    .label = "cube-indices"
  });

  sn3_sino_init();
  typedef struct { Vec4 tl, tr, bl, br; } Face;
  typedef struct { uint8_t r, g, b, a; } Byte4;
  typedef struct { Byte4 pixels[1024][1024]; } FaceImg;
  typedef struct { FaceImg faces[6]; } CubeImg;
  CubeImg *img = calloc(sizeof(CubeImg), 1);
  for (int x = 0; x < 1024; x++)
    for (int y = 0; y < 1024; y++)
      for (int i = 0; i < 6; i++) {
        float dy = lerp(-1.0f, 1.0f, (float)x / 1024.0f);
        float dx = lerp(-1.0f, 1.0f, (float)y / 1024.0f);
        Vec3 dir;
        switch (i) {
          case SG_CUBEFACE_POS_X: dir = vec3( 1.0f,    dx,    dy); break;
          case SG_CUBEFACE_NEG_X: dir = vec3(-1.0f,    dx,    dy); break;
          case SG_CUBEFACE_POS_Y: dir = vec3(   dy,  1.0f,    dx); break;
          case SG_CUBEFACE_NEG_Y: dir = vec3(   dy, -1.0f,    dx); break;
          case SG_CUBEFACE_POS_Z: dir = vec3(   dx,    dy,  1.0f); break;
          case SG_CUBEFACE_NEG_Z: dir = vec3(   dx,    dy, -1.0f); break;
          default: continue;
        }
        dir = norm3(dir);
        // int octaves = 8;
        // float frequency = 1.0f;
        // float amplitude = 1.0f;
        // float persistence = 0.5f;

        // float t = 0.0f;
        // for (int o = 0; o < octaves; o++) {
        //   float freq = frequency * powf(2, o);
        //   Vec3 coord = mul3_f(dir, freq);
        //   float sample = sn3_sample(coord.x, coord.y, coord.z);
        //   t += sample * amplitude * powf(persistence, o);
        // }
        // t = t / (2.0f - 1.0f / powf(2, octaves - 1));

        Vec3 color = lerp3(vec3(0.3f, 1.0f, 0.9f), vec3(0.0f, 0.0f, 1.0f), dir.y);
        // Vec3 color = vec3(dir.x, dir.y, dir.z); // vec3_f(t);
        img->faces[i].pixels[x][y] = (Byte4) { color.x*255.0f, color.y*255.0f, color.z*255.0f, 255 };
    }
  cp_save_png("pos_x.png", &(cp_image_t) { 1024, 1024, (cp_pixel_t *)img->faces[SG_CUBEFACE_POS_X].pixels });
  cp_save_png("pos_y.png", &(cp_image_t) { 1024, 1024, (cp_pixel_t *)img->faces[SG_CUBEFACE_POS_Y].pixels });

  sg_image_data skybox;
  for (int i = 0; i < 6; ++i) {
    skybox.subimage[i][0].ptr = &img->faces[i].pixels;
    skybox.subimage[i][0].size = 1024*1024*4;
  }
  state.skybox.tex = sg_make_image(&(sg_image_desc) {
    .type = SG_IMAGETYPE_CUBE,
    .width = 1024,
    .height = 1024,
    .pixel_format = SG_PIXELFORMAT_RGBA8,
    .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
    .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
    .wrap_w = SG_WRAP_CLAMP_TO_EDGE,
    .min_filter = SG_FILTER_LINEAR,
    .mag_filter = SG_FILTER_LINEAR,
    .data = skybox,
  });

  mesh_init();
}

static void event(const sapp_event *ev) {
  switch (ev->type) {
    case (SAPP_EVENTTYPE_KEY_DOWN): {
      if (ev->key_code == SAPP_KEYCODE_ESCAPE)
        sapp_request_quit();
      if (ev->key_code == SAPP_KEYCODE_W)
        state.rx += 0.03f;
      if (ev->key_code == SAPP_KEYCODE_S)
        state.rx -= 0.03f;
      if (ev->key_code == SAPP_KEYCODE_A)
        state.ry += 0.03f;
      if (ev->key_code == SAPP_KEYCODE_D)
        state.ry -= 0.03f;
    } break;
  }
}

void frame(void) {
  /* NOTE: the vs_params_t struct has been code-generated by the shader-code-gen */
  mesh_vs_params_t vs_params;
  const float w = sapp_widthf();
  const float h = sapp_heightf();
  Mat4 proj = perspective4x4(1.047f, w/h, 0.01f, 10.0f);
  Vec3 eye = mul3_f(vec3(
    cosf(state.rx)*sinf(state.ry),
    sinf(state.rx),
    cosf(state.rx)*cosf(state.ry)
  ), 6.0f);
  Mat4 view = look_at4x4(eye, vec3_f(0.0f), vec3_y);
  Mat4 view_proj = mul4x4(proj, view);

  vs_params.mvp = view_proj;

  sg_begin_default_pass(&state.mesh.pass_action, (int)w, (int)h);

  sg_apply_pipeline(state.mesh.pip);
  sg_apply_bindings(&(sg_bindings) {
    .vertex_buffers[0] = state.mesh.vbuf,
    .index_buffer = state.mesh.ibuf,
  });
  vs_params.mvp = mul4x4(proj, view);
  sg_apply_uniforms(SG_SHADERSTAGE_VS, SLOT_mesh_vs_params, &SG_RANGE(vs_params));
  sg_draw(0, 36, 1);

  sg_apply_pipeline(state.skybox.pip);
  sg_apply_bindings(&(sg_bindings) {
    .vertex_buffers[0] = state.mesh.vbuf,
    .index_buffer = state.mesh.ibuf,
    .fs_images[SLOT_skybox] = state.skybox.tex,
  });
  Mat4 skybox_view = view;
  skybox_view.w = vec4(0, 0, 0, 1);
  vs_params.mvp = mul4x4(proj, skybox_view);
  sg_apply_uniforms(SG_SHADERSTAGE_VS, SLOT_mesh_vs_params, &SG_RANGE(vs_params));
  sg_draw(0, 36, 1);

  sg_end_pass();

  sg_commit();
}

void cleanup(void) {
  sg_shutdown();
}

sapp_desc sokol_main(int argc, char* argv[]) {
  (void)argc;
  (void)argv;
  return (sapp_desc){
    .init_cb = init,
    .frame_cb = frame,
    .cleanup_cb = cleanup,
    .event_cb = event,
    .width = 800,
    .height = 600,
    .sample_count = OFFSCREEN_SAMPLE_COUNT,
    .gl_force_gles2 = true,
    .window_title = "Skybox MVP",
    .icon.sokol_default = true,
  };
}
