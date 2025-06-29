SDL_DIR := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
SDL_LIB = $(SDL_DIR)/build/libSDL2.a

N64_C_AND_CXX_FLAGS += -D__N64__ -I$(SDL_DIR)/include
N64_LDFLAGS := $(SDL_LIB) $(N64_LDFLAGS) $(SDL_LIB)