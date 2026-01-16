#pragma once

#include <clay.h>

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CRPP_SDL3Renderer CRPP_SDL3Renderer;

CRPP_SDL3Renderer *crpp_sdl3_renderer_create(SDL_Renderer *renderer,
                                             TTF_TextEngine *text_engine,
                                             TTF_Font **fonts);
void crpp_sdl3_renderer_destroy(CRPP_SDL3Renderer *renderer);
void crpp_sdl3_renderer_render(CRPP_SDL3Renderer *renderer, Clay_RenderCommandArray *commands);

#ifdef __cplusplus
}
#endif
