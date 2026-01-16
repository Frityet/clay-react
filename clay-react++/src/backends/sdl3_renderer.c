#include "clay-react++/backend_sdl3.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <renderers/SDL3/clay_renderer_SDL3.c>

struct CRPP_SDL3Renderer {
    Clay_SDL3RendererData data;
};

CRPP_SDL3Renderer *crpp_sdl3_renderer_create(SDL_Renderer *renderer,
                                             TTF_TextEngine *text_engine,
                                             TTF_Font **fonts) {
    CRPP_SDL3Renderer *handle = SDL_calloc(1, sizeof(CRPP_SDL3Renderer));
    if (!handle) {
        return NULL;
    }
    handle->data.renderer = renderer;
    handle->data.textEngine = text_engine;
    handle->data.fonts = fonts;
    return handle;
}

void crpp_sdl3_renderer_destroy(CRPP_SDL3Renderer *renderer) {
    if (!renderer) {
        return;
    }
    SDL_free(renderer);
}

void crpp_sdl3_renderer_render(CRPP_SDL3Renderer *renderer, Clay_RenderCommandArray *commands) {
    if (!renderer || !commands) {
        return;
    }
    SDL_Clay_RenderClayCommands(&renderer->data, commands);
}
