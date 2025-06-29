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

#ifdef SDL_AUDIO_DRIVER_N64

#include "SDL_audio.h"
#include "../SDL_sysaudio.h"
#include "SDL_n64audio.h"

#include <audio.h>

static SDL_AudioDevice *device; /* because we have no threads and callback doesnt have a userdata param */

static void audio_callback(short *buffer, size_t numsamples)
{
    if (!device || !device->callbackspec.callback) {
        return;
    }

    const int len = 2 * numsamples * sizeof(short);
    SDL_AudioDevice *this = device;

    if (SDL_AtomicGet(&this->paused)) {
        SDL_memset(buffer, this->spec.silence, len);
        return;
    }

    if (!this->stream) { /* no conversion necessary. */
        this->callbackspec.callback(this->callbackspec.userdata, (Uint8 *)buffer, len);
    } else { /* streaming/converting */
        const int stream_len = this->callbackspec.size;
        while (SDL_AudioStreamAvailable(this->stream) < len) {
            this->callbackspec.callback(this->callbackspec.userdata, this->work_buffer, stream_len);
            if (SDL_AudioStreamPut(this->stream, this->work_buffer, stream_len) == -1) {
                SDL_AudioStreamClear(this->stream);
                SDL_AtomicSet(&this->enabled, 0);
                break;
            }
        }

        if (SDL_AudioStreamGet(this->stream, (void *)buffer, len) != len) {
            SDL_memset(buffer, this->spec.silence, len);
        }
    }
}

static int N64AUDIO_OpenDevice(_THIS, const char *devname)
{
    audio_init(this->spec.freq, 2);

    this->spec.channels = 2;
    this->spec.format = AUDIO_S16;

    SDL_CalculateAudioSpec(&this->spec);

    device = this;
    audio_set_buffer_callback(audio_callback);
    audio_write_silence(); /* callback won't run without this */

    return 0;
}

static void N64AUDIO_CloseDevice(_THIS)
{
    audio_close();
}

static void N64AUDIO_NoOp(_THIS)
{
}

static SDL_bool N64AUDIO_Init(SDL_AudioDriverImpl *impl)
{
    impl->OpenDevice = N64AUDIO_OpenDevice;
    impl->CloseDevice = N64AUDIO_CloseDevice;
    impl->LockDevice = N64AUDIO_NoOp;
    impl->UnlockDevice = N64AUDIO_NoOp;
    impl->OnlyHasDefaultOutputDevice = SDL_TRUE;
    impl->ProvidesOwnCallbackThread = SDL_TRUE;
    return SDL_TRUE;
}

AudioBootStrap N64AUDIO_bootstrap = {
    "n64", "N64 audio driver", N64AUDIO_Init, SDL_FALSE
};

#endif

/* vi: set ts=4 sw=4 expandtab: */
