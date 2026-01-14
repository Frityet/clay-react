#pragma once

#include <clay.h>

typedef struct {
    struct SDL_Renderer *renderer;
    struct TTF_TextEngine *textEngine;
    struct TTF_Font **fonts;
} Clay_SDL3RendererData;

void SDL_Clay_RenderClayCommands(Clay_SDL3RendererData *rendererData, Clay_RenderCommandArray *rcommands);
