N64
======
SDL2 port for the Nintendo 64

Credit to
* The authors of the PS2, N3DS, and Emscripten ports since I'm using their 
  code as reference
* libdragon devs

Compiling:
----------

The libdragon development environment (specifically, the preview branch) is 
required. Install it either through the docker container or by compiling the
toolchain. Afterwards, run either `libdragon make -f Makefile.n64`
or `N64_INST=/opt/libdragon make -f Makefile.n64`, where 
/opt/libdragon is where you chose to install the toolchain to
depending on if you installed using the Docker or compiling the 
toolchain.

Installing:
-----------

Place `include $(N64_INST)/include/sdl2.mk` at the top of your Makefile 
below your n64.mk include. Alternatively, you can include this repository 
as a submodule and  include the path to this repository's n64.mk.

Notes:
------

* Debugging output is not done by default. To get messages printed to
  stderr to appear on your console, you have to include `debug.h`, and
  call `debug_init_isviewer()` at the beginning of your main function.
* This is still very much a work in progress, and may be quite
  slow if using audio or the 2D rendering. At its current state, it 
  should only really be used if it isn't possible to port to the N64 any 
  other way.
* This only supports OpenGL 1.1. For efficiency reasons, use display
  lists as often as possible.
* Filesystem has only read only support, with the "rom:/" prefix. This
  should be handled with the SDL_GetBasePath function.
* Threads will never work due to issues with thread safety.
* Left shoulder and Z trigger are mapped to the same key. This is 
  because if you hold the N64 controller with the middle handle on 
  your left hand then the Z trigger would act as a left trigger, 
  and there weren't really any other good places to map it to.
