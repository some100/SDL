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

/* N64 SDL video driver implementation; this is just enough to make an
 *  SDL-based application THINK it's got a working video driver, for
 *  applications that call SDL_Init(SDL_INIT_VIDEO) when they don't need it,
 *  and also for use as a collection of stubs when porting SDL to a new
 *  platform for which you haven't yet written a valid video driver.
 *
 * This is also a great way to determine bottlenecks: if you think that SDL
 *  is a performance problem for a given platform, enable this driver, and
 *  then see if your application runs faster without video overhead.
 *
 * Initial work by Ryan C. Gordon (icculus@icculus.org). A good portion
 *  of this was cut-and-pasted from Stephane Peter's work in the AAlib
 *  SDL video driver.
 */

#include "SDL_n64video.h"
#include "SDL_n64gl.h"
#include "SDL_n64window.h"
#include "SDL_video.h"
#include "../SDL_sysvideo.h"
#include "../SDL_pixels_c.h"
#include "../../events/SDL_events_c.h"

/* N64 driver bootstrap functions */

static int N64_SetDisplayMode(_THIS, SDL_VideoDisplay *display, SDL_DisplayMode *mode)
{
    return 0;
}

static void N64_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device);
}

/* initialize a fake display so that sdl is happy */
static int N64_VideoInit(_THIS)
{
    SDL_VideoDisplay display;
    SDL_DisplayMode current_mode;

    SDL_zero(current_mode);

    current_mode.w = 640;
    current_mode.h = 480;
    current_mode.refresh_rate = 60;

    /* 32 bpp for default */
    current_mode.format = SDL_PIXELFORMAT_RGBA4444;
    current_mode.driverdata = NULL;

    SDL_zero(display);
    display.desktop_mode = current_mode;
    display.current_mode = current_mode;
    display.driverdata = NULL;
    SDL_AddDisplayMode(&display, &current_mode);

    SDL_AddVideoDisplay(&display, SDL_FALSE);

    SDL_VideoData *data = (SDL_VideoData *)SDL_calloc(1, sizeof(SDL_VideoData));
    if (!data) {
        SDL_OutOfMemory();
        return -1;
    }
    _this->driverdata = data;

    return 0;
}

static void N64_VideoQuit(_THIS)
{
}

static void N64_PumpEvents(_THIS)
{
}

static SDL_VideoDevice *N64_CreateDevice(void)
{
    SDL_VideoDevice *device;

    /* Initialize all variables that we clean on shutdown */
    device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (!device) {
        SDL_OutOfMemory();
        return 0;
    }

    /* Set the function pointers */
    device->VideoInit = N64_VideoInit;
    device->VideoQuit = N64_VideoQuit;
    device->SetDisplayMode = N64_SetDisplayMode;
    device->CreateSDLWindow = N64_CreateWindow;
    device->SetWindowTitle = N64_SetWindowTitle;
    device->DestroyWindow = N64_DestroyWindow;
    device->PumpEvents = N64_PumpEvents;
    device->free = N64_DeleteDevice;

    /* GL pointers */
    device->GL_LoadLibrary = N64_GL_LoadLibrary;
    device->GL_GetProcAddress = N64_GL_GetProcAddress;
    device->GL_UnloadLibrary = N64_GL_UnloadLibrary;
    device->GL_CreateContext = N64_GL_CreateContext;
    device->GL_MakeCurrent = N64_GL_MakeCurrent;
    device->GL_SetSwapInterval = N64_GL_SetSwapInterval;
    device->GL_GetSwapInterval = N64_GL_GetSwapInterval;
    device->GL_SwapWindow = N64_GL_SwapWindow;
    device->GL_DeleteContext = N64_GL_DeleteContext;

    return device;
}

VideoBootStrap N64_bootstrap = {
    "N64",
    "N64 Video Driver",
    N64_CreateDevice,
    NULL /* no ShowMessageBox implementation */
};

#endif /* SDL_VIDEO_DRIVER_N64 */

/* vi: set ts=4 sw=4 expandtab: */
