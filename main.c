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

#define OFFSCREEN_SAMPLE_COUNT (4)

static struct {
  float rx, ry;
  sg_pipeline pip;
  sg_buffer ibuf, vbuf, quad_vbuf;
  struct {
    sg_image pingpong_imgs[2], color_img, depth_img;
    sg_pass_action pass_action;
    sg_pass pass, pingpong_passes[2];
    sg_pipeline blur_pip, pip;
  } offscreen;
} state;

/* must be called on resize */
void offscreen_create_pass(void) {
  /* destroy previous resource (can be called for invalid id) */
  sg_destroy_pass(state.offscreen.pass);
  sg_destroy_image(state.offscreen.color_img);
  sg_destroy_image(state.offscreen.depth_img);
  for (int i = 0; i < 2; i++) {
    sg_destroy_image(state.offscreen.pingpong_imgs[i]);
    sg_destroy_pass(state.offscreen.pingpong_passes[i]);
  }

  /* a render pass with one color- and one depth-attachment image */
  sg_image_desc img_desc = {
    .render_target = true,
    .width = sapp_widthf(),
    .height = sapp_heightf(),
    .pixel_format = SG_PIXELFORMAT_RGBA8,
    .min_filter = SG_FILTER_LINEAR,
    .mag_filter = SG_FILTER_LINEAR,
    .wrap_u = SG_WRAP_CLAMP_TO_EDGE,
    .wrap_v = SG_WRAP_CLAMP_TO_EDGE,
    .sample_count = OFFSCREEN_SAMPLE_COUNT,
    .label = "color-image"
  };
  sg_image color_img = state.offscreen.color_img = sg_make_image(&img_desc);

  for (int i = 0; i < 2; i++)
    state.offscreen.pingpong_imgs[i] = sg_make_image(&img_desc);
  for (int i = 0; i < 2; i++)
    state.offscreen.pingpong_passes[i] = sg_make_pass(&(sg_pass_desc) {
      .color_attachments[0].image = state.offscreen.pingpong_imgs[i]
    });

  img_desc.pixel_format = SG_PIXELFORMAT_DEPTH;
  img_desc.label = "depth-image";
  sg_image depth_img = state.offscreen.depth_img = sg_make_image(&img_desc);
  state.offscreen.pass = sg_make_pass(&(sg_pass_desc){
    .color_attachments[0].image = color_img,
    .depth_stencil_attachment.image = depth_img,
    .label = "offscreen-pass"
  });
}

/* can be called once on initialization */
void offscreen_init(void) {
  offscreen_create_pass();

  /* offscreen pass action */
  state.offscreen.pass_action = (sg_pass_action) {
    .colors[0] = { .action = SG_ACTION_CLEAR, .value = { 0.25f, 0.25f, 0.25f, 1.0f } }
  };

  state.offscreen.pip = sg_make_pipeline(&(sg_pipeline_desc) {
    .layout = {
      .attrs = {
        [ATTR_mesh_vs_position].format = SG_VERTEXFORMAT_FLOAT3,
        [ATTR_mesh_vs_color0].format   = SG_VERTEXFORMAT_FLOAT4
      }
    },
    .shader = sg_make_shader(mesh_shader_desc(sg_query_backend())),
    .index_type = SG_INDEXTYPE_UINT16,
    .cull_mode = SG_CULLMODE_FRONT,
    .depth = {
      .pixel_format = SG_PIXELFORMAT_DEPTH,
      .write_enabled = true,
      .compare = SG_COMPAREFUNC_LESS_EQUAL,
    },
  });

  state.offscreen.blur_pip = sg_make_pipeline(&(sg_pipeline_desc){
    .layout = {
      .attrs[ATTR_fsq_vs_pos].format = SG_VERTEXFORMAT_FLOAT2
    },
    .shader = sg_make_shader(blur_shader_desc(sg_query_backend())),
    .primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP,
    .depth.pixel_format = SG_PIXELFORMAT_NONE,
    .label = "blur pipeline"
  });
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
  state.vbuf = sg_make_buffer(&(sg_buffer_desc){
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
  state.ibuf = sg_make_buffer(&(sg_buffer_desc){
    .type = SG_BUFFERTYPE_INDEXBUFFER,
    .data = SG_RANGE(indices),
    .label = "cube-indices"
  });

  /* a vertex buffer to render a fullscreen rectangle */
  state.quad_vbuf = sg_make_buffer(&(sg_buffer_desc){
    .data = SG_RANGE(((float[]) { 0.0f, 0.0f,  1.0f, 0.0f,  0.0f, 1.0f,  1.0f, 1.0f })),
    .label = "quad vertices"
  });

  offscreen_init();

  /* create pipeline object */
  state.pip = sg_make_pipeline(&(sg_pipeline_desc){
    .layout = {
      .attrs[ATTR_fsq_vs_pos].format = SG_VERTEXFORMAT_FLOAT2
    },
    .shader = sg_make_shader(fsq_shader_desc(sg_query_backend())),
    .primitive_type = SG_PRIMITIVETYPE_TRIANGLE_STRIP,
    .label = "fullscreen quad pipeline"
  });
}

static void event(const sapp_event *ev) {
  switch (ev->type) {
    case (SAPP_EVENTTYPE_KEY_DOWN): {
      if (ev->key_code == SAPP_KEYCODE_ESCAPE)
        sapp_request_quit();
    } break;
    case (SAPP_EVENTTYPE_RESIZED): {
      offscreen_create_pass();
    } break;
  }
}

void frame(void) {
  /* NOTE: the vs_params_t struct has been code-generated by the shader-code-gen */
  mesh_vs_params_t vs_params;
  const float w = sapp_widthf();
  const float h = sapp_heightf();
  Mat4 proj = perspective4x4(1.047f, w/h, 0.01f, 10.0f);
  Mat4 view = look_at4x4(vec3(0.0f, 1.5f, 6.0f), vec3_f(0.0f), vec3_y);
  Mat4 view_proj = mul4x4(proj, view);

  state.rx += 0.01f; state.ry += 0.02f;
  Mat4 rxm = rotate4x4(vec3_x, state.rx);
  Mat4 rym = rotate4x4(vec3_y, state.ry);
  Mat4 model = mul4x4(rxm, rym);
  vs_params.mvp = mul4x4(view_proj, model);

  sg_begin_pass(state.offscreen.pass, &state.offscreen.pass_action);
  sg_apply_pipeline(state.offscreen.pip);
  sg_apply_bindings(&(sg_bindings) {
    .vertex_buffers[0] = state.vbuf,
    .index_buffer = state.ibuf
  });
  sg_apply_uniforms(SG_SHADERSTAGE_VS, SLOT_mesh_vs_params, &SG_RANGE(vs_params));
  sg_draw(0, 36, 1);
  sg_end_pass();

  int horizontal = 1;
  for (int i = 0; i < 10; i++) {
    sg_begin_pass(state.offscreen.pingpong_passes[horizontal], &(sg_pass_action) {
      .colors[0] = { .action = SG_ACTION_LOAD }
    });
    sg_apply_pipeline(state.offscreen.blur_pip);
    sg_apply_bindings(&(sg_bindings) {
      .vertex_buffers[0] = state.quad_vbuf,
      .fs_images = { [SLOT_tex] = i ? state.offscreen.pingpong_imgs[!horizontal] : state.offscreen.color_img }
    });
    blur_fs_params_t fs_params = { .hori = vec2(horizontal, !horizontal) };
    sg_apply_uniforms(SG_SHADERSTAGE_FS, SLOT_blur_fs_params, &SG_RANGE(fs_params));
    sg_draw(0, 4, 1);
    sg_end_pass();
    horizontal = !horizontal;
  }

  sg_pass_action pass_action = {
    .colors[0] = {
      .action = SG_ACTION_CLEAR,
      .value = { 0.25f / 15.f, 0.5f / 15.f, 0.75f / 15.f, 1.0f }
    }
  };
  sg_begin_default_pass(&pass_action, (int)w, (int)h);
  sg_apply_pipeline(state.pip);
  sg_apply_bindings(&(sg_bindings) {
    .vertex_buffers[0] = state.quad_vbuf,
    .fs_images = { [SLOT_tex] = state.offscreen.pingpong_imgs[!horizontal] }
  });
  sg_draw(0, 4, 1);
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
    .window_title = "Bloom MVP",
    .icon.sokol_default = true,
  };
}
