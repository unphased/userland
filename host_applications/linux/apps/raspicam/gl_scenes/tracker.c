/* 
Copyright (c) 2014, Steven Lu
All rights reserved.
*/

#include "tracker.h"
#include "RaspiTexUtil.h"
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

// viewer shader
static RASPITEXUTIL_SHADER_PROGRAM_T tracker_shader = {
   .vertex_source = 
      "attribute vec2 vertex;\n"
      "varying vec2 texcoord;\n"
      "void main(void) {\n"
      "  texcoord = 0.5 * (vertex + 1.0);\n"
      "  gl_Position = vec4(vertex, 0.0, 1.0);\n"
      "}\n",
   .fragment_source = 
      "#extension GL_OES_EGL_image_external : require\n"
      "uniform samplerExternalOES tex;\n"
      "uniform vec2 position;\n"
      "varying vec2 texcoord;\n"
      "void main() {\n"
      "  gl_FragColor = texture2D(tex, texcoord);\n"
      "}\n",
   .uniform_names = {},
   .attribute_names = {"vertex"},
};

// blob detection shader
static RASPITEXUTIL_SHADER_PROGRAM_T tracker_blob_shader = {
   .vertex_source =
      "",
   .fragment_source = 
      "",
   .uniform_names = {},
   .attribute_names = {},
};

static int tracker_init(RASPITEX_STATE *state)
{
    int rc = raspitexutil_gl_init_2_0(state);
    if (rc != 0)
       goto end;

    rc = raspitexutil_build_shader_program(&tracker_shader);
    // TODO: Make this return value properly combine the two shader compilations
    // rc = raspitexutil_build_shader_program(&tracker_blob_shader);
end:
    return rc;
}

static int tracker_redraw(RASPITEX_STATE * raspitex_state) {
    // Start with a clear screen
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Bind the OES texture which is used to render the camera preview
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, raspitex_state->texture);

    GLCHK(glUseProgram(tracker_shader.program));
    GLCHK(glEnableVertexAttribArray(tracker_shader.attribute_locations[0]));
    GLfloat varray[] = { // two tris for full screen NDC quad
        -1.0f, -1.0f,
        1.0f,  1.0f,
        1.0f, -1.0f,

        -1.0f,  1.0f,
        1.0f,  1.0f,
        -1.0f, -1.0f,
   };
   GLCHK(glVertexAttribPointer(tracker_shader.attribute_locations[0], 2, GL_FLOAT, GL_FALSE, 0, varray));
   // GLCHK(glUniform1f(tracker_shader.uniform_locations[1], offset));
   GLCHK(glDrawArrays(GL_TRIANGLES, 0, 6));

   GLCHK(glDisableVertexAttribArray(tracker_shader.attribute_locations[0]));

   // do a pass for blob detection
   GLCHK(glUseProgram(0));
   return 0;
}

int tracker_open(RASPITEX_STATE *state)
{
   state->ops.gl_init = tracker_init;
   state->ops.redraw = tracker_redraw;
   state->ops.update_texture = raspitexutil_update_texture;
   return 0;
}

