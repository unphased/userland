/* 
Copyright (c) 2014, Steven Lu
All rights reserved.
*/

#include "tracker.h"
#include "RaspiTexUtil.h"
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <EGL/glext.h>

static RASPITEXUTIL_SHADER_PROGRAM_T tracker_shader = {
   .vertex_source = 
      "attribute vec2 vertex;\n"
      "varying vec2 texcoord;\n"
      "void main(void) {\n"
      "  texcoord = vec2(0,0);\n"
      "  gl_Position = vec4(vertex, 0.0, 1.0);\n"
      "}\n",
   .fragment_source = 
      "#extension GL_OES_EGL_image_external : require\n"
      "uniform samplerExternalOES tex;\n",
   .uniform_names = {},
   .attribute_names = {},
};

static int tracker_init(RASPITEX_STATE *state)
{
    int rc = raspitexutil_gl_init_2_0(state);
    if (rc != 0)
       goto end;

    rc = raspitexutil_build_shader_program(&tracker_shader);
end:
    return rc;
}

static int tracker_redraw(RASPITEX_STATE * raspitex_state) {
}

int mirror_open(RASPITEX_STATE *state)
{
   state->ops.gl_init = tracker_init;
   state->ops.redraw = tracker_redraw;
   state->ops.update_texture = raspitexutil_update_texture;
   return 0;
}

