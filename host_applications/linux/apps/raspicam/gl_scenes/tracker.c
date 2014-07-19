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
GLuint tracker_rendertexture;

GLuint tracker_framebuffer;

// leave this off eventually so that it doesnt slow shit down
#define CHECK_GL_ERRORS

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
      "  gl_FragColor = vec4(texture2D(tex, texcoord).rgb, 0.4);\n"
      "}\n",
   .uniform_names = {"tex", "zoom", "zpos"},
   .attribute_names = {"vertex"},
};

static unsigned char blob_tex[] = {
   0,0,0, 0,0,0, 0,0,0, 0,255,0, 0,255,0, 0,0,0, 0,0,0, 0,0,0,
   0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0,
   0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0,
   0,255,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,255,0,
   0,255,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,255,0,
   0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0,
   0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0, 0,0,0,
   0,0,0, 0,0,0, 0,0,0, 0,255,0, 0,255,0, 0,0,0, 0,0,0, 0,0,0
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
    GLCHK(glGenTextures(1, &tracker_texture));
    GLCHK(glBindTexture(GL_TEXTURE_2D, tracker_texture));
    GLCHK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 8, 8, 0, GL_RGB, GL_UNSIGNED_BYTE, blob_tex));
    GLCHK(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLfloat)GL_LINEAR));
    GLCHK(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLfloat)GL_NEAREST));
    GLCHK(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLfloat)GL_CLAMP_TO_EDGE));
    GLCHK(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLfloat)GL_CLAMP_TO_EDGE));

    // set blending because that's always useful for showing more information
    GLCHK(glEnable(GL_BLEND));
    GLCHK(glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));

    // initialize the renderbuffer texture for render to texture use
    GLCHK(glGenTextures(1, &tracker_rendertexture));
    GLCHK(glBindTexture(GL_TEXTURE_2D, tracker_rendertexture));
    GLCHK(glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 8, 8, 0, GL_RGB, GL_UNSIGNED_BYTE, 0));
    GLCHK(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, (GLfloat)GL_LINEAR));
    GLCHK(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, (GLfloat)GL_NEAREST));
    GLCHK(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (GLfloat)GL_CLAMP_TO_EDGE));
    GLCHK(glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (GLfloat)GL_CLAMP_TO_EDGE));

    // initialize the renderbuffer for getting some GPGPU style work done
    // behind the scenes
    GLCHK(glGenFramebuffers(1, &tracker_framebuffer));
    GLCHK(glBindFramebuffer(GL_FRAMEBUFFER, tracker_framebuffer));
    // GLCHK(glGenRenderbuffers(1, &tracker_renderbuffer));
    // GLCHK(glBindRenderbuffer(GL_RENDERBUFFER, tracker_renderbuffer));
    GLCHK(glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tracker_rendertexture, 0));
    GLenum status;
    GLCHK(status = glCheckFramebufferStatus(GL_FRAMEBUFFER));
    if (status != GL_FRAMEBUFFER_COMPLETE) {
       fprintf(stderr, "FRAMEBUFFER ERROR: NOT COMPLETE\n");
    }

    // return to regular rendering
    GLCHK(glBindFramebuffer(GL_FRAMEBUFFER, 0));
end:
    return rc;
}

static int tracker_redraw(RASPITEX_STATE * raspitex_state) {
    // Start with a clear screen
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    GLCHK(glEnable(GL_BLEND));

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
   GLCHK(glBindTexture(GL_TEXTURE_2D, tracker_texture));
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

