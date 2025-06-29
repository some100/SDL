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

#ifdef SDL_JOYSTICK_N64

// This is the N64 implementation of the SDL joystick API

#include <joypad.h>
#include <debug.h>

#include "../SDL_sysjoystick.h"
#include "../SDL_joystick_c.h"

#include "SDL_events.h"

typedef struct joystick_hwdata
{
    joypad_port_t port;
    SDL_bool has_rumble;
} N64_JoystickData;

typedef enum
{
    BUTTON_A,
    BUTTON_B,
    BUTTON_ZL,
    BUTTON_START,
    BUTTON_UP,
    BUTTON_DOWN,
    BUTTON_LEFT,
    BUTTON_RIGHT,
    BUTTON_R,
    BUTTON_COUNT,
} N64_JoystickButton;

typedef enum
{
    AXIS_XL,
    AXIS_YL,
    AXIS_XR,
    AXIS_YR,
    AXIS_COUNT,
} N64_JoystickAxis;

static uint8_t controllers = 0;

static inline void get_pressed_buttons(joypad_buttons_t inputs, int *buttons)
{
    buttons[BUTTON_A] = inputs.a;
    buttons[BUTTON_B] = inputs.b;
    buttons[BUTTON_ZL] = (inputs.z || inputs.l);
    buttons[BUTTON_START] = inputs.start;
    buttons[BUTTON_UP] = inputs.d_up;
    buttons[BUTTON_DOWN] = inputs.d_down;
    buttons[BUTTON_LEFT] = inputs.d_left;
    buttons[BUTTON_RIGHT] = inputs.d_right;
    buttons[BUTTON_R] = inputs.r;
}

static inline void get_axes(joypad_inputs_t inputs, int8_t *axes)
{
    axes[AXIS_XL] = inputs.stick_x;
    axes[AXIS_YL] = inputs.stick_y * -1;
    axes[AXIS_XR] = inputs.cstick_x;
    axes[AXIS_YR] = inputs.cstick_y * -1;
}

static int N64_JoystickInit(void)
{
    joypad_port_t i;
    controllers = 0;

    joypad_init();
    for (i = 0; i < JOYPAD_PORT_COUNT; ++i) {
        if (joypad_is_connected(i)) {
            controllers++;
            SDL_PrivateJoystickAdded(i);
        }
    }
    return controllers > 0 ? 0 : -1;
}

static int N64_JoystickGetCount(void)
{
    return (int)controllers;
}

static void N64_JoystickDetect(void)
{
}

static Uint32 N64_JoystickGetCapabilities(SDL_Joystick *joystick)
{
    return SDL_JOYCAP_RUMBLE;
}

static const char *N64_JoystickGetDeviceName(int device_index)
{
    if (device_index >= 0 && device_index < controllers) {
        return "N64 Controller";
    }
    SDL_SetError("No joystick available with that index");
    return NULL;
}

static const char *N64_JoystickGetDevicePath(int device_index)
{
    return NULL;
}

static int N64_JoystickGetDeviceSteamVirtualGamepadSlot(int device_index)
{
    return -1;
}

static int N64_JoystickGetDevicePlayerIndex(int device_index)
{
    return -1;
}

static void N64_JoystickSetDevicePlayerIndex(int device_index, int player_index)
{
}

static SDL_JoystickGUID N64_JoystickGetDeviceGUID(int device_index)
{
    /* the GUID is just the name for now */
    const char *name = N64_JoystickGetDeviceName(device_index);
    return SDL_CreateJoystickGUIDForName(name);
}

static SDL_JoystickID N64_JoystickGetDeviceInstanceID(int device_index)
{
    return device_index;
}

static int N64_JoystickOpen(SDL_Joystick *joystick, int device_index)
{
    joypad_port_t port = (joypad_port_t) device_index;
    if (!joypad_is_connected(port)) {
        return -1;
    }
    N64_JoystickData *data = SDL_malloc(sizeof(N64_JoystickData));
    if (!data) {
        return SDL_OutOfMemory();
    }
    data->port = port;
    data->has_rumble = joypad_get_rumble_supported(data->port);
    joystick->hwdata = data;

    joystick->nbuttons = BUTTON_COUNT;
    joystick->naxes = AXIS_COUNT;
    joystick->nhats = 0;

    return 0;
}

static int N64_JoystickRumble(SDL_Joystick *joystick, Uint16 low_frequency_rumble, Uint16 high_frequency_rumble)
{
    N64_JoystickData *data = (N64_JoystickData *)joystick->hwdata;
    if (!data->has_rumble) {
        return -1;
    }
    /* rumble pak has only 2 states, so just activate rumble if either rumble is enabled */
    joypad_set_rumble_active(data->port, (low_frequency_rumble > 0 || high_frequency_rumble > 0));
    return 0;
}

static int N64_JoystickRumbleTriggers(SDL_Joystick *joystick, Uint16 left_rumble, Uint16 right_rumble)
{
    return -1;
}

static int N64_JoystickSetLED(SDL_Joystick *joystick, Uint8 red, Uint8 green, Uint8 blue)
{
    return -1;
}

static int N64_JoystickSendEffect(SDL_Joystick *joystick, const void *data, int size)
{
    return -1;
}

static int N64_JoystickSetSensorsEnabled(SDL_Joystick *joystick, SDL_bool enabled)
{
    return -1;
}

static void N64_JoystickUpdate(SDL_Joystick *joystick)
{
    int i;

    joypad_poll();

    N64_JoystickData *data = (N64_JoystickData *)joystick->hwdata;
    joypad_inputs_t inputs = joypad_get_inputs(data->port);

    int buttons[BUTTON_COUNT];
    int8_t axes[AXIS_COUNT];
    get_pressed_buttons(inputs.btn, buttons);
    get_axes(inputs, axes);

    for (i = 0; i < BUTTON_COUNT; ++i) {
        SDL_PrivateJoystickButton(joystick, i, buttons[i] ? SDL_PRESSED : SDL_RELEASED);
    }

    for (i = 0; i < AXIS_COUNT; ++i) {
        SDL_PrivateJoystickAxis(joystick, i, ((Sint16) axes[i]) * 32767/127);
    }
}

static void N64_JoystickClose(SDL_Joystick *joystick)
{
    SDL_free(joystick->hwdata);
}

static void N64_JoystickQuit(void)
{
    joypad_close();
}

static SDL_bool N64_JoystickGetGamepadMapping(int device_index, SDL_GamepadMapping *out)
{
    return SDL_FALSE;
}

SDL_JoystickDriver SDL_N64_JoystickDriver = {
    N64_JoystickInit,
    N64_JoystickGetCount,
    N64_JoystickDetect,
    N64_JoystickGetDeviceName,
    N64_JoystickGetDevicePath,
    N64_JoystickGetDeviceSteamVirtualGamepadSlot,
    N64_JoystickGetDevicePlayerIndex,
    N64_JoystickSetDevicePlayerIndex,
    N64_JoystickGetDeviceGUID,
    N64_JoystickGetDeviceInstanceID,
    N64_JoystickOpen,
    N64_JoystickRumble,
    N64_JoystickRumbleTriggers,
    N64_JoystickGetCapabilities,
    N64_JoystickSetLED,
    N64_JoystickSendEffect,
    N64_JoystickSetSensorsEnabled,
    N64_JoystickUpdate,
    N64_JoystickClose,
    N64_JoystickQuit,
    N64_JoystickGetGamepadMapping
};

#endif /* SDL_JOYSTICK_N64 */

/* vi: set ts=4 sw=4 expandtab: */
