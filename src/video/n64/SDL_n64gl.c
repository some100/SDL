/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "../../SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_N64

/* N64 SDL video OpenGL 1.1 driver implementation */

#include <display.h>
#include <rdpq.h>
#include <rdpq_attach.h>
#include <GL/gl_integration.h>

#include "SDL_n64gl.h"
#include "SDL_n64video.h"
#include "SDL_video.h"

int N64_GL_LoadLibrary(_THIS, const char *path)
{
    gl_init();
    return 0;
}

void *N64_GL_GetProcAddress(_THIS, const char *proc)
{
    return NULL; /* nothing to see here */
}

void N64_GL_UnloadLibrary(_THIS)
{
    gl_close();
}

int N64_GL_MakeCurrent(_THIS, SDL_Window *window, SDL_GLContext sdl_context)
{
    SDL_GLDriverData *data = (SDL_GLDriverData *)sdl_context;
    rdpq_attach(data->surf, data->zbuf);
    gl_context_begin();
    return 0;
}

SDL_GLContext N64_GL_CreateContext(_THIS, SDL_Window *window)
{
    SDL_GLDriverData *data = (SDL_GLDriverData *)SDL_malloc(sizeof(SDL_GLDriverData));
    if (!data) {
        SDL_OutOfMemory();
        return 0;
    }
    data->surf = display_get();
    data->zbuf = display_get_zbuf();
    data->window = window;

    _this->gl_data = data;

    N64_GL_MakeCurrent(_this, window, (SDL_GLContext) data);

    return (SDL_GLContext) data;
}

int N64_GL_SetSwapInterval(_THIS, int interval)
{
    interval = SDL_max(1, interval);
    SDL_VideoData *data = (SDL_VideoData *)_this->driverdata;
    data->swap_interval = interval;
    display_set_fps_limit(data->refresh_rate / (float) data->swap_interval);
    return 0;
}

int N64_GL_GetSwapInterval(_THIS)
{
    SDL_VideoData *data = (SDL_VideoData *)_this->driverdata;
    if (data->swap_interval) {
        return data->swap_interval;
    }
    return 0;
}

int N64_GL_SwapWindow(_THIS, SDL_Window *window)
{
    SDL_GLDriverData *data = _this->gl_data;
    gl_context_end();
    rdpq_detach_show();

    data->surf = display_get();
    data->zbuf = display_get_zbuf();
    rdpq_attach(data->surf, data->zbuf);
    gl_context_begin();
    return 0;
}

void N64_GL_DeleteContext(_THIS, SDL_GLContext context)
{
    SDL_GLDriverData *data = (SDL_GLDriverData *)context;
    /* data->surf and data->zbuf are freed when window is destroyed */
    SDL_free(data);
    _this->gl_data = NULL;
}

#endif /* SDL_VIDEO_DRIVER_N64 */

/* vi: set ts=4 sw=4 expandtab: */
