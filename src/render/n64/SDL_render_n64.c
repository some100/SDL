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

#if SDL_VIDEO_RENDER_N64

#include "SDL_hints.h"
#include "../SDL_sysrender.h"

#include <display.h>
#include <rdpq.h>
#include <rdpq_attach.h>
#include <rdpq_mode.h>
#include <rdpq_rect.h>
#include <rdpq_tex.h>
#include <rdpq_tri.h>

#define COLOR_T_TO_FLOATS(col) ((float)col.r)/255.0f, \
                               ((float)col.g)/255.0f, \
                               ((float)col.b)/255.0f, \
                               ((float)col.a)/255.0f

#define NUM_TILES 8

typedef struct
{
    float x, y, w, h;
    float u, v;
    color_t col;
} N64_Vertex;

typedef struct
{
    N64_Vertex srcrect;
    N64_Vertex dstrect;
    float angle;
    SDL_FPoint center;
    SDL_RendererFlip flip;
    float scale_x;
    float scale_y;
} N64_CopyExData;

typedef struct
{
    surface_t *surf;
    surface_t *current_surf;
    SDL_Rect viewport;
    color_t col;
} N64_RenderData;

typedef struct
{
    rdpq_tile_t tile;
    surface_t surf;
    SDL_Rect dirty_rect;
    int dirty;
} N64_TextureData;

static SDL_bool tiles[NUM_TILES] = { SDL_FALSE };

static int get_tile(void)
{
    int i;

    for (i = 0; i < NUM_TILES; i++) {
        if (!tiles[i]) {
            tiles[i] = SDL_TRUE;
            return (rdpq_tile_t)i;
        }
    }
    return -1;
}

static void free_tile(rdpq_tile_t tile)
{
    SDL_assert((int)tile < NUM_TILES);
    tiles[(int)tile] = SDL_FALSE;
}

/* no rdpq_line function in libdragon, so make an approximation of a line with two triangles */
static void rdpq_line(float x0, float y0, float x1, float y1)
{
    if (y0 == y1) {
        rdpq_fill_rectangle(x0, y0, x1, y0 + 1);
    } else if (x0 == x1) {
        rdpq_fill_rectangle(x0, y0, x0 + 1, y1);
    } else {
        float dx = (x1 - x0);
        float dy = (y1 - y0);
        float length = sqrtf((dx * dx) + (dy * dy));

        if (length == 0.0f) {
            return;
        }

        float nx = -dy / length * 0.5f;
        float ny = dx / length * 0.5f;

        float v1[] = { x0 - nx, y0 + ny };
        float v2[] = { x0 + nx, y0 - ny };
        float v3[] = { x1 - nx, y1 + ny };
        float v4[] = { x1 + nx, y1 - ny };

        rdpq_triangle(&TRIFMT_FILL, v1, v2, v3);
        rdpq_triangle(&TRIFMT_FILL, v3, v2, v4);
    }
}

static void set_draw_color(N64_RenderData *data, const SDL_RenderCommand *cmd)
{
    Uint8 r, g, b, a;

    r = cmd->data.draw.r;
    g = cmd->data.draw.g;
    b = cmd->data.draw.b;
    a = cmd->data.draw.a;

    data->col = RGBA32(r, g, b, a);
}

/*
  In theory, I should be able to set the blender mode directly inside of this function.
  In practice, however...
*/
static rdpq_blender_t set_blend_mode(const SDL_RenderCommand *cmd)
{
    switch (cmd->data.draw.blend) {
        case SDL_BLENDMODE_NONE:
            return 0;
        case SDL_BLENDMODE_BLEND:
            return RDPQ_BLENDER_MULTIPLY;
        case SDL_BLENDMODE_ADD:
            return RDPQ_BLENDER_ADDITIVE;
        case SDL_BLENDMODE_MOD:
        case SDL_BLENDMODE_MUL:
            rdpq_blender_t blender = RDPQ_BLENDER(
                (MEMORY_RGB, IN_ALPHA, IN_RGB, INV_MUX_ALPHA)
            ); /* this isn't a modulate or a multiply formula, but its the best we can do */
            return blender;
        default:
            return 0;
    }
}

static void upload_texture(N64_TextureData *tex)
{
    SDL_assert(!(tex->tile < 0));
    if (tex->dirty) {
        rdpq_tex_upload_sub(tex->tile, &tex->surf, 0,
            tex->dirty_rect.x,
            tex->dirty_rect.y,
            tex->dirty_rect.x + tex->dirty_rect.w,
            tex->dirty_rect.y + tex->dirty_rect.h
        );
        tex->dirty = 0;
    }
}

/* this just initializes the basic state for most functions */
static void init_rdp_state(SDL_RenderCommand *cmd, SDL_bool fill, color_t *col)
{
    rdpq_set_mode_standard();
    if (fill) {
        rdpq_mode_combiner(RDPQ_COMBINER_FLAT);
        rdpq_set_prim_color(*col);
    }
    rdpq_mode_blender(set_blend_mode(cmd));
    rdpq_mode_dithering(DITHER_SQUARE_SQUARE);
}

static void N64_WindowEvent(SDL_Renderer *renderer, const SDL_WindowEvent *event)
{
}

static void N64_UnlockTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    N64_TextureData *data = (N64_TextureData *)texture->driverdata;
    data->dirty = 1;
}

static int N64_LockTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                           const SDL_Rect *rect, void **pixels, int *pitch)
{
    N64_TextureData *data = (N64_TextureData *)texture->driverdata;
    surface_t *tex = &data->surf;

    int bpp = SDL_BYTESPERPIXEL(texture->format);
    *pixels =
        (void *)((Uint8 *)tex->buffer + rect->y * tex->width * bpp +
                 rect->x * bpp);
    *pitch = tex->width * bpp;
    return 0;
}


static int N64_UpdateTexture(SDL_Renderer *renderer, SDL_Texture *texture,
                             const SDL_Rect *rect, const void *pixels, int pitch)
{
    N64_TextureData *data = (N64_TextureData *)texture->driverdata;

    const Uint8 *src;
    Uint8 *dst;
    int row, length, dpitch;
    src = pixels;

    SDL_copyp(&data->dirty_rect, rect);

    N64_LockTexture(renderer, texture, rect, (void**)&dst, &dpitch);
    length = rect->w * SDL_BYTESPERPIXEL(texture->format);
    if (length == pitch && length == dpitch) {
        SDL_memcpy(dst, src, length * rect->h);
    } else {
        for (row = 0; row < rect->h; ++row) {
            SDL_memcpy(dst, src, length);
            src += pitch;
            dst += dpitch;
        }
    }

    N64_UnlockTexture(renderer, texture);

    return 0;
}

static int N64_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
    if (rdpq_is_attached()) {
        rdpq_detach();
    }
    surface_t *target;
    if (!texture) {
        N64_RenderData *data = (N64_RenderData *)renderer->driverdata;
        target = data->surf;
    } else {
        N64_TextureData *tex = (N64_TextureData *)texture->driverdata;
        target = &tex->surf;
    }
    ((N64_RenderData *)renderer->driverdata)->current_surf = target;
    return 0;
}

static int N64_QueueSetViewport(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    N64_RenderData *data = (N64_RenderData *)renderer->driverdata;
    SDL_Rect viewport = cmd->data.viewport.rect;
    data->viewport = viewport;

    return 0;
}

static int N64_QueueCopy(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
                         const SDL_Rect *srcrect, const SDL_FRect *dstrect)
{
    N64_Vertex *verts = (N64_Vertex *)SDL_AllocateRenderVertices(renderer, 2 * sizeof(N64_Vertex), 0, &cmd->data.draw.first);
    if (!verts) {
        return SDL_OutOfMemory();
    }

    verts->x = srcrect->x;
    verts->y = srcrect->y;
    verts->w = srcrect->w;
    verts->h = srcrect->h;

    verts++;

    verts->x = dstrect->x;
    verts->y = dstrect->y;
    verts->w = dstrect->w;
    verts->h = dstrect->h;

    return 0;
}

static int N64_QueueCopyEx(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
                           const SDL_Rect *srcrect, const SDL_FRect *dstrect,
                           const double angle, const SDL_FPoint *center, const SDL_RendererFlip flip, float scale_x, float scale_y)
{
    N64_CopyExData *verts = (N64_CopyExData *)SDL_AllocateRenderVertices(renderer, sizeof(N64_CopyExData), 0, &cmd->data.draw.first);
    if (!verts) {
        return SDL_OutOfMemory();
    }

    verts->srcrect.x = srcrect->x;
    verts->srcrect.y = srcrect->y;
    verts->srcrect.w = srcrect->w;
    verts->srcrect.h = srcrect->h;
    
    verts->dstrect.x = dstrect->x;
    verts->dstrect.y = dstrect->y;
    verts->dstrect.w = dstrect->w;
    verts->dstrect.h = dstrect->h;

    verts->angle = (float)angle;
    verts->center = *center;
    verts->flip = flip;
    verts->scale_x = scale_x;
    verts->scale_y = scale_y;

    return 0;
}

static int N64_QueueDrawPoints(SDL_Renderer *renderer, SDL_RenderCommand *cmd, const SDL_FPoint *points, int count)
{
    int i;

    N64_Vertex *verts = (N64_Vertex *)SDL_AllocateRenderVertices(renderer, count * sizeof(N64_Vertex), 0, &cmd->data.draw.first);
    if (!verts) {
        return SDL_OutOfMemory();
    }

    cmd->data.draw.count = count;

    for (i = 0; i < count; i++, verts++, points++) {
        verts->x = points->x;
        verts->y = points->y;
    }

    return 0;
}

static int N64_QueueFillRects(SDL_Renderer *renderer, SDL_RenderCommand *cmd, const SDL_FRect *rects, int count)
{
    int i;

    N64_Vertex *verts = (N64_Vertex *)SDL_AllocateRenderVertices(renderer, count * sizeof(N64_Vertex), 0, &cmd->data.draw.first);
    if (!verts) {
        return SDL_OutOfMemory();
    }

    cmd->data.draw.count = count;

    for (i = 0; i < count; i++, verts++, rects++) {
        verts->x = rects->x;
        verts->y = rects->y;
        verts->w = rects->w;
        verts->h = rects->h;
    }

    return 0;
}

static int N64_QueueGeometry(SDL_Renderer *renderer, SDL_RenderCommand *cmd, SDL_Texture *texture,
                             const float *xy, int xy_stride, const SDL_Color *color, int color_stride, const float *uv, int uv_stride,
                             int num_vertices, const void *indices, int num_indices, int size_indices,
                             float scale_x, float scale_y)
{
    int i;
    int count = indices ? num_indices : num_vertices;

    N64_Vertex *verts = (N64_Vertex *)SDL_AllocateRenderVertices(renderer, count * sizeof(N64_Vertex), 0, &cmd->data.draw.first);
    if (!verts) {
        return SDL_OutOfMemory();
    }

    N64_TextureData *texdata = NULL;
    if (texture) {
        texdata = (N64_TextureData *)texture->driverdata;
    }

    cmd->data.draw.count = count;
    size_indices = indices ? size_indices : 0;

    for (i = 0; i < count; i++, verts++) {
        int j;
        float *xy_;
        SDL_Color col_;
        if (size_indices == 4) {
            j = ((const Uint32 *)indices)[i];
        } else if (size_indices == 2) {
            j = ((const Uint16 *)indices)[i];
        } else if (size_indices == 1) {
            j = ((const Uint8 *)indices)[i];
        } else {
            j = i;
        }

        xy_ = (float *)((char *)xy + j * xy_stride);
        col_ = *(SDL_Color *)((char *)color + j * color_stride);

        verts->x = xy_[0] * scale_x;
        verts->y = xy_[1] * scale_y;
        verts->col = RGBA32(col_.r, col_.g, col_.b, col_.a);

        verts->u = 0.0f;
        verts->v = 0.0f;
        if (texdata) {
            float *uv_ = (float *)((char *)uv + j * uv_stride);
            verts->u = uv_[0] * texdata->surf.width;
            verts->v = uv_[1] * texdata->surf.height;
        }
    }

    return 0;
}

static int N64_QueueNoOp(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    return 0;
}

static int N64_RenderClear(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    N64_RenderData *data = (N64_RenderData *)renderer->driverdata;

    set_draw_color(data, cmd);
    rdpq_clear(data->col);

    return 0;
}

static int N64_RenderCopy(SDL_Renderer *renderer, void *vertices, SDL_RenderCommand *cmd)
{
    rdpq_blitparms_t parms;
    SDL_zero(parms);

    const N64_Vertex *verts = (N64_Vertex *)((Uint8 *)vertices + cmd->data.draw.first);
    N64_TextureData *tex = (N64_TextureData *)cmd->data.draw.texture->driverdata;

    const N64_Vertex *srcrect = verts;
    const N64_Vertex *dstrect = verts + 1;

    init_rdp_state(cmd, SDL_FALSE, NULL);

    parms.scale_x = dstrect->w / srcrect->w;
    parms.scale_y = dstrect->h / srcrect->h;

    rdpq_tex_blit(&tex->surf, dstrect->x, dstrect->y, &parms);

    return 0;
}

static int N64_RenderCopyEx(SDL_Renderer *renderer, void *vertices, SDL_RenderCommand *cmd)
{
    rdpq_blitparms_t parms;
    SDL_zero(parms);

    const N64_CopyExData *verts = (N64_CopyExData *)((Uint8 *)vertices + cmd->data.draw.first);
    N64_TextureData *tex = (N64_TextureData *)cmd->data.draw.texture->driverdata;

    init_rdp_state(cmd, SDL_FALSE, NULL);

    parms.tile = tex->tile;
    parms.width = verts->srcrect.w;
    parms.height = verts->srcrect.h;
    if (verts->flip & SDL_FLIP_HORIZONTAL) {
        parms.flip_x = true;
    }
    if (verts->flip & SDL_FLIP_VERTICAL) {
        parms.flip_y = true;
    }
    parms.cx = verts->center.x;
    parms.cy = verts->center.y;
    parms.theta = verts->angle * M_PI / -180.0f;
    parms.scale_x = verts->scale_x * (verts->dstrect.w / verts->srcrect.w);
    parms.scale_y = verts->scale_y * (verts->dstrect.h / verts->srcrect.h);

    rdpq_tex_blit(&tex->surf, verts->dstrect.x, verts->dstrect.y, &parms);

    return 0;
}

static int N64_RenderGeometry(SDL_Renderer *renderer, void *vertices, SDL_RenderCommand *cmd)
{
    int i;

    const size_t count = cmd->data.draw.count;
    const N64_Vertex *verts = (N64_Vertex *)((Uint8 *)vertices + cmd->data.draw.first);

    init_rdp_state(cmd, SDL_FALSE, NULL);
    if (cmd->data.draw.texture) {
        N64_TextureData *tex = (N64_TextureData *)cmd->data.draw.texture->driverdata;
        upload_texture(tex);

        rdpq_trifmt_t trifmt;
        SDL_zero(trifmt);

        trifmt.shade_offset = 2;
        trifmt.tex_offset = 6;
        trifmt.tex_tile = tex->tile;
        trifmt.z_offset = -1;

        rdpq_mode_combiner(RDPQ_COMBINER_TEX_SHADE);
        for (i = 0; i + 2 < count; i += 3) {
            rdpq_triangle(&trifmt, 
                (float[]){ verts[i].x, verts[i].y, COLOR_T_TO_FLOATS(verts[i].col), verts[i].u, verts[i].v }, 
                (float[]){ verts[i+1].x, verts[i+1].y, COLOR_T_TO_FLOATS(verts[i+1].col), verts[i+1].u, verts[i+1].v }, 
                (float[]){ verts[i+2].x, verts[i+2].y, COLOR_T_TO_FLOATS(verts[i+2].col), verts[i+2].u, verts[i+2].v }
            );
        }
    } else {
        rdpq_mode_combiner(RDPQ_COMBINER_SHADE);
        for (i = 0; i + 2 < count; i += 3) {
            rdpq_triangle(&TRIFMT_SHADE,
                (float[]){ verts[i].x, verts[i].y, COLOR_T_TO_FLOATS(verts[i].col) },
                (float[]){ verts[i+1].x, verts[i+1].y, COLOR_T_TO_FLOATS(verts[i+1].col) },
                (float[]){ verts[i+2].x, verts[i+2].y, COLOR_T_TO_FLOATS(verts[i+2].col) }
            );
        }
    }

    return 0;
}

static int N64_RenderLines(SDL_Renderer *renderer, void *vertices, SDL_RenderCommand *cmd)
{
    int i;

    N64_RenderData *data = (N64_RenderData *)renderer->driverdata;
    const size_t count = cmd->data.draw.count;
    const N64_Vertex *verts = (N64_Vertex *)((Uint8 *)vertices + cmd->data.draw.first);

    set_draw_color(data, cmd);

    init_rdp_state(cmd, SDL_TRUE, &data->col);

    for (i = 0; i < count; i += 2) {
        float x0 = verts[i].x;
        float y0 = verts[i].y;
        float x1 = verts[i+1].x;
        float y1 = verts[i+1].y;
        rdpq_line(x0, y0, x1, y1);
    }

    return 0;
}

static int N64_RenderPoints(SDL_Renderer *renderer, void *vertices, SDL_RenderCommand *cmd)
{
    int i;

    N64_RenderData *data = (N64_RenderData *)renderer->driverdata;
    const size_t count = cmd->data.draw.count;
    const N64_Vertex *verts = (N64_Vertex *)((Uint8 *)vertices + cmd->data.draw.first);

    set_draw_color(data, cmd);

    init_rdp_state(cmd, SDL_TRUE, &data->col);

    for (i = 0; i < count; i++, verts++) {
        rdpq_fill_rectangle(verts->x, verts->y, verts->x+1, verts->y+1);
    }

    return 0;
}

static int N64_RenderPresent(SDL_Renderer *renderer)
{
    N64_RenderData *data = (N64_RenderData *)renderer->driverdata;
    rdpq_detach_cb((void(*)(void *))display_show, (void *)data->surf);

    surface_t *surf = display_get();
    data->surf = surf;
    data->current_surf = data->surf;

    return 0;
}

static int N64_RenderRects(SDL_Renderer *renderer, void *vertices, SDL_RenderCommand *cmd)
{
    int i;

    N64_RenderData *data = (N64_RenderData *)renderer->driverdata;
    const size_t count = cmd->data.draw.count;
    const N64_Vertex *verts = (N64_Vertex *)((Uint8 *)vertices + cmd->data.draw.first);

    set_draw_color(data, cmd);

    init_rdp_state(cmd, SDL_TRUE, &data->col);
    
    for (i = 0; i < count; i++, verts++) {
        float x0 = verts->x;
        float y0 = verts->y;
        float x1 = x0 + verts->w;
        float y1 = y0 + verts->h;
        rdpq_fill_rectangle(x0, y0, x1, y1);
    }

    return 0;
}

static int N64_SetRenderClipRect(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    N64_RenderData *data = (N64_RenderData *)renderer->driverdata;
    SDL_Rect viewport = data->viewport;
    SDL_Rect rect = cmd->data.cliprect.rect;

    if (cmd->data.cliprect.enabled) {
        viewport.x += rect.x;
        viewport.y += rect.y;
        viewport.w = SDL_min(viewport.w, rect.w);
        viewport.h = SDL_min(viewport.h, rect.h);
    }
    rdpq_set_scissor(viewport.x, viewport.y, viewport.x + viewport.w, viewport.y + viewport.h);

    return 0;
}

static int N64_SetRenderDrawColor(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    N64_RenderData *data = (N64_RenderData *)renderer->driverdata;

    Uint32 r, g, b, a;
    r = cmd->data.draw.r;
    g = cmd->data.draw.g;
    b = cmd->data.draw.b;
    a = cmd->data.draw.a;

    data->col = RGBA32(r, g, b, a);

    return 0;
}

static int N64_SetRenderViewPort(SDL_Renderer *renderer, SDL_RenderCommand *cmd)
{
    N64_RenderData *data = (N64_RenderData *)renderer->driverdata;
    SDL_Rect viewport = data->viewport;

    const SDL_Rect rect = cmd->data.viewport.rect;
    viewport.x = rect.x;
    viewport.y = rect.y;
    viewport.w = rect.w;
    viewport.h = rect.h;
    rdpq_set_scissor(viewport.x, viewport.y, viewport.x + viewport.w, viewport.y + viewport.h);

    return 0;
}

static int N64_RunCommandQueue(SDL_Renderer *renderer, SDL_RenderCommand *cmd, void *vertices, size_t vertsize)
{
    N64_RenderData *data = (N64_RenderData *)renderer->driverdata;
    if (!data) {
        return SDL_OutOfMemory();
    }

    rdpq_attach(data->current_surf, NULL);
    while (cmd) {
        switch (cmd->command) {
        case SDL_RENDERCMD_CLEAR:
            N64_RenderClear(renderer, cmd);
            break;
        case SDL_RENDERCMD_COPY:
            N64_RenderCopy(renderer, vertices, cmd);
            break;
        case SDL_RENDERCMD_COPY_EX:
            N64_RenderCopyEx(renderer, vertices, cmd);
            break;
        case SDL_RENDERCMD_DRAW_POINTS:
            N64_RenderPoints(renderer, vertices, cmd);
            break;
        case SDL_RENDERCMD_SETCLIPRECT:
            N64_SetRenderClipRect(renderer, cmd);
            break;
        case SDL_RENDERCMD_SETDRAWCOLOR:
            N64_SetRenderDrawColor(renderer, cmd);
            break;
        case SDL_RENDERCMD_SETVIEWPORT:
            N64_SetRenderViewPort(renderer, cmd);
            break;
        case SDL_RENDERCMD_DRAW_LINES:
            N64_RenderLines(renderer, vertices, cmd);
            break;
        case SDL_RENDERCMD_FILL_RECTS:
            N64_RenderRects(renderer, vertices, cmd);
            break;
        case SDL_RENDERCMD_GEOMETRY:
            N64_RenderGeometry(renderer, vertices, cmd);
            break;
        case SDL_RENDERCMD_NO_OP:
            break;
        }
        cmd = cmd->next;
    }

    return 0;
}

static int N64_CreateTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    N64_TextureData *data = (N64_TextureData *)SDL_malloc(sizeof(N64_TextureData));
    if (!data) {
        return SDL_OutOfMemory();
    }

    tex_format_t format;

    switch (texture->format) {
    case SDL_PIXELFORMAT_RGBA5551:
        format = FMT_RGBA16;
        break;
    case SDL_PIXELFORMAT_RGBA8888:
        format = FMT_RGBA32;
        break;
    default:
        SDL_free(data);
        return SDL_SetError("Unsupported pixel format");
    }

    data->tile = get_tile();
    data->surf = surface_alloc(format, texture->w, texture->h);
    data->dirty = 1;
    texture->driverdata = data;

    return 0;
}

static void N64_DestroyTexture(SDL_Renderer *renderer, SDL_Texture *texture)
{
    N64_TextureData *data = (N64_TextureData *)texture->driverdata;

    if (data) {
        free_tile(data->tile);
        surface_free(&data->surf);
        SDL_free(data);
        texture->driverdata = 0;
    }
}

static void N64_DestroyRenderer(SDL_Renderer *renderer)
{
    N64_RenderData *data = (N64_RenderData *)renderer->driverdata;

    if (data) {
        /* data->surf is freed when window is destroyed */
        SDL_free(data);
        renderer->driverdata = 0;
    }
    SDL_zero(tiles);
}

static int N64_SetVSync(SDL_Renderer *renderer, int vsync)
{
    return 0; /* vsync is forced */
}

static int N64_CreateRenderer(SDL_Renderer *renderer, SDL_Window *window, Uint32 flags)
{
    N64_RenderData *data = (N64_RenderData *)SDL_malloc(sizeof(N64_RenderData));
    if (!data) {
        return SDL_OutOfMemory();
    }

    /* display_init is called when window is created */
    rdpq_init();

    data->surf = display_get();
    data->current_surf = data->surf;

    data->col = RGBA32(0, 0, 0, 255);

    renderer->WindowEvent = N64_WindowEvent;
    renderer->CreateTexture = N64_CreateTexture;
    renderer->UpdateTexture = N64_UpdateTexture;
    renderer->LockTexture = N64_LockTexture;
    renderer->UnlockTexture = N64_UnlockTexture;
    renderer->SetRenderTarget = N64_SetRenderTarget;
    renderer->QueueSetViewport = N64_QueueSetViewport;
    renderer->QueueSetDrawColor = N64_QueueNoOp;
    renderer->QueueDrawPoints = N64_QueueDrawPoints;
    renderer->QueueDrawLines = N64_QueueDrawPoints; /* points and lines are queued the same way */
    renderer->QueueFillRects = N64_QueueFillRects;
    renderer->QueueCopy = N64_QueueCopy;
    renderer->QueueCopyEx = N64_QueueCopyEx;
    renderer->QueueGeometry = N64_QueueGeometry;
    renderer->RunCommandQueue = N64_RunCommandQueue;
    renderer->RenderPresent = N64_RenderPresent;
    renderer->DestroyTexture = N64_DestroyTexture;
    renderer->DestroyRenderer = N64_DestroyRenderer;
    renderer->SetVSync = N64_SetVSync;
    renderer->info = N64_RenderDriver.info;
    renderer->driverdata = data;
    renderer->window = window;

    return 0;
}

SDL_RenderDriver N64_RenderDriver = {
    .CreateRenderer = N64_CreateRenderer,
    {
        "N64 rdpq", /* name */
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE, /* flags */
        2, /* number of texture formats */
        { /* texture formats */
            SDL_PIXELFORMAT_RGBA5551,
            SDL_PIXELFORMAT_RGBA8888
        },
        256, /* max texture width */
        256, /* max texture height */
    }
};

#endif /* SDL_VIDEO_RENDER_N64 */

/* vi: set ts=4 sw=4 expandtab: */
