/* 
Copyright (c) 2014, Steven Lu
All rights reserved.
*/

#include "tracker.h"
#include "RaspiTexUtil.h"
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>

// global state for keys to modify via raspistill
GLfloat tracker_zoom = 1.0f;
GLfloat tracker_zpos_x = 0.0f;
GLfloat tracker_zpos_y = 0.0f;

GLuint tracker_texture;

// viewer shader
static RASPITEXUTIL_SHADER_PROGRAM_T tracker_shader = {
   .vertex_source = 
      "attribute vec2 vertex;\n"
      "uniform float zoom;\n" // magnification 
      "uniform vec2 zpos;\n" // center of magnif in ndc style coordinates
      "varying vec2 texcoord;\n"
      "void main(void) {\n"
      "  vec2 zoffset = (1.0 - 1.0/zoom) * vec2(0.5, 0.5);\n"
      "  texcoord = (0.5 * (vertex + 1.0 + zpos))/zoom + zoffset;\n" // TODO: aspect
      "  gl_Position = vec4(vertex, 0.0, 1.0);\n"
      "}\n",
   .fragment_source = 
      "#extension GL_OES_EGL_image_external : require\n"
      "uniform samplerExternalOES tex;\n"
      "varying vec2 texcoord;\n"
      "void main() {\n"
      "  gl_FragColor = texture2D(tex, texcoord);\n"
      "}\n",
   .uniform_names = {"tex", "zoom", "zpos"},
   .attribute_names = {"vertex"},
};

// blob detection shader
static RASPITEXUTIL_SHADER_PROGRAM_T tracker_blob_shader = {
   .vertex_source =
      "attribute vec2 vertex;\n"
      "uniform float zoom;\n" // magnification 
      "uniform vec2 zpos;\n" // center of magnif in ndc style coordinates
      "varying vec2 texcoord;\n"
      "void main(void) {\n"
      "  vec2 zoffset = (1.0 - 1.0/zoom) * vec2(0.5, 0.5);\n"
      "  texcoord = (0.5 * (vertex + 1.0 + zpos))/zoom + zoffset;\n" // TODO: aspect
      "  gl_Position = vec4(vertex, 0.0, 1.0);\n"
      "}\n",
   .fragment_source = 
      "uniform sampler2D tex;\n"
      "varying vec2 texcoord;\n"
      "void main() {\n"
      "  gl_FragColor = texture2D(tex, texcoord);\n"
      "}\n",
   .uniform_names = {"tex", "zoom", "zpos"},
   .attribute_names = {"vertex"},
};

static unsigned char blob_tex[] = {
   255,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,255,0,
   0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0,
   0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0,
   0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0,
   0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0,
   0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0,
   0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0,
   0,0,255, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 255,255,0
};

static int tracker_init(RASPITEX_STATE *state)
{
    int rc = raspitexutil_gl_init_2_0(state);
    if (rc != 0)
       goto end;

    rc = raspitexutil_build_shader_program(&tracker_shader);
    // TODO: Make this return value properly combine the two shader compilations
    rc = raspitexutil_build_shader_program(&tracker_blob_shader);

    // load texture
    glGenTextures(1, &tracker_texture);
    glBindTexture(GL_TEXTURE_2D, tracker_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 8, 8, 0, GL_RGB, GL_UNSIGNED_BYTE, blob_tex);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLfloat)GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLfloat)GL_NEAREST);
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
   GLCHK(glUniform1f(tracker_shader.uniform_locations[1], tracker_zoom));
   GLCHK(glUniform2f(tracker_shader.uniform_locations[2], tracker_zpos_x, tracker_zpos_y));
   GLCHK(glDrawArrays(GL_TRIANGLES, 0, 6));

   GLCHK(glUseProgram(tracker_blob_shader.program));

   // render the texture just to be able to see it
   GLCHK(glVertexAttribPointer(tracker_blob_shader.attribute_locations[0], 2, GL_FLOAT, GL_FALSE, 0, varray));
   GLCHK(glUniform1i(tracker_blob_shader.uniform_locations[0], 0));
   GLCHK(glUniform1f(tracker_blob_shader.uniform_locations[1], 0.2f));
   GLCHK(glUniform2f(tracker_blob_shader.uniform_locations[2], tracker_zpos_x, tracker_zpos_y));
   GLCHK(glActiveTexture(GL_TEXTURE0));
   GLCHK(glBindTexture(GL_TEXTURE_2D, tracker_texture);
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

