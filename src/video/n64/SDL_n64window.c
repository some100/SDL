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

#include <display.h>
#include <n64sys.h>

#include "SDL_n64video.h"
#include "SDL_n64window.h"
#include "../SDL_sysvideo.h"
#include "../../events/SDL_keyboard_c.h"

int N64_CreateWindow(_THIS, SDL_Window * window)
{
    resolution_t res;
    SDL_VideoData *data = (SDL_VideoData *)_this->driverdata;

    window->x = 0;
    window->y = 0;
    window->w = SDL_clamp(window->w, 0, 800);
    window->h = SDL_clamp(window->h, 0, 720);

    window->flags &= ~SDL_WINDOW_RESIZABLE;     /* window is NEVER resizeable */
    window->flags &= ~SDL_WINDOW_HIDDEN;
    window->flags |= SDL_WINDOW_SHOWN;          /* only one window on N64 */
    window->flags |= SDL_WINDOW_INPUT_FOCUS;    /* always has input focus */
    window->flags |= SDL_WINDOW_OPENGL;

    res.width = window->w;
    res.height = window->h;
    res.interlaced = false;
    res.aspect_ratio = 0.0f; // 4:3
    res.overscan_margin = 0.0f;
    res.pal60 = false;

    display_init(res, DEPTH_16_BPP, 2, GAMMA_NONE, res.width > 320 ? FILTERS_DEDITHER : FILTERS_RESAMPLE);
    data->window = window;
    data->refresh_rate = get_tv_type() == TV_PAL ? 50.0f : 59.94f;
    data->swap_interval = 1;

    SDL_SetKeyboardFocus(window);

    return 0;
}

void N64_SetWindowTitle(_THIS, SDL_Window * window)
{
}

void N64_DestroyWindow(_THIS, SDL_Window * window)
{
    SDL_VideoData *data = (SDL_VideoData *)_this->driverdata;
    if (data->window == window) {
        data->window = NULL;
    }
    display_close();
}

#endif /* SDL_VIDEO_DRIVER_N64 */

/* vi: set ts=4 sw=4 expandtab: */
