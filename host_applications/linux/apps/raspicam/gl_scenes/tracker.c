/* 
Copyright (c) 2014, Steven Lu
All rights reserved.
*/

#include "tracker.h"
#include "RaspiTexUtil.h"
#include <GLES2/gl2.h>
#include <EGL/egl.h>
#include <EGL/glext.h>

int mirror_open(RASPITEX_STATE *state)
{
   state->ops.gl_init = tracker_init;
   state->ops.redraw = tracker_redraw;
   state->ops.update_texture = raspitexutil_update_texture;
   return 0;
}

