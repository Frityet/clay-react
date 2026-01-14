/**
 * Todo App Example - Using the new Clay React idiomatic API
 *
 * Demonstrates:
 * - Reactive hooks for state, memo, effects, and callbacks
 * - TextInput for editable text fields
 * - Column/Row for layout
 * - Textf for formatted text
 * - Button/Clickable for interaction
 */

#include <SDL3/SDL_oldnames.h>
#include <SDL3/SDL_video.h>
#include <stdint.h>
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <clay.h>
#include "clay_react/clay_react.h"
#include "demo/todo_app.h"
#include "renderer/sdl3_renderer.h"

#include <stdio.h>

// ============================================================================
// APP STATE
// ============================================================================

typedef struct {
    SDL_Window *window;
    Clay_SDL3RendererData rendererData;
} AppState;

static AppState *g_app = NULL;

// ============================================================================
// LAYOUT
// ============================================================================

static Clay_RenderCommandArray create_layout(void) {
    cr_begin_frame();

    // Root container
    const TodoTheme *theme = TodoAppTheme();
    Center((BoxParams){
        .style = {
            .layout = {
                .sizing = $fill(),
                .padding = $pad(40),
            },
            .background = theme->background,
        },
    }, ^{ TodoApp(); });

    return cr_end_frame();
}

// ============================================================================
// SDL CALLBACKS
// ============================================================================

static Clay_Dimensions measure_text(Clay_StringSlice text, Clay_TextElementConfig *config, void *userData) {
    TTF_Font **fonts = userData;
    TTF_Font *font = fonts[config->fontId];
    if (font && config->fontSize > 0) {
        TTF_SetFontSize(font, config->fontSize);
    }
    int width, height;
    TTF_GetStringSize(font, text.chars, text.length, &width, &height);
    return (Clay_Dimensions){ (float)width, (float)height };
}

static void handle_errors(Clay_ErrorData error) {
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "[Clay Error] %s", error.errorText.chars);
}

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    (void)argc; (void)argv;

    if (!TTF_Init()) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to init TTF: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    g_app = SDL_calloc(1, sizeof(AppState));
    if (!g_app) return SDL_APP_FAILURE;
    *appstate = g_app;

    if (!SDL_CreateWindowAndRenderer("Todo App - Clay React", 1024, 1024,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
            &g_app->window, &g_app->rendererData.renderer)) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create window: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    g_app->rendererData.textEngine = TTF_CreateRendererTextEngine(g_app->rendererData.renderer);
    if (!g_app->rendererData.textEngine) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create text engine: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    g_app->rendererData.fonts = SDL_calloc(1, sizeof(TTF_Font *));
    TTF_Font *font = TTF_OpenFont("resources/Roboto-Regular.ttf", 24);
    if (!font) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load font: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    g_app->rendererData.fonts[0] = font;

    // Initialize Clay
    uint64_t memory_size = Clay_MinMemorySize();
    Clay_Arena arena = {
        .memory = SDL_calloc(1, memory_size),
        .capacity = memory_size,
    };

    int width, height;
    SDL_GetWindowSize(g_app->window, &width, &height);
    Clay_Initialize(arena, (Clay_Dimensions){ (float)width, (float)height },
                   (Clay_ErrorHandler){ .errorHandlerFunction = handle_errors });
    Clay_SetMeasureTextFunction(measure_text, g_app->rendererData.fonts);

    // Initialize Clay React runtime
    cr_init();

    // Start text input
    SDL_StartTextInput(g_app->window);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    (void)appstate;

    switch (event->type) {
        case SDL_EVENT_QUIT:
            return SDL_APP_SUCCESS;

        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            Clay_SetLayoutDimensions((Clay_Dimensions){
                event->window.data1, event->window.data2
            });
            break;

        case SDL_EVENT_MOUSE_MOTION:
            Clay_SetPointerState(
                (Clay_Vector2){ event->motion.x, event->motion.y },
                (event->motion.state & SDL_BUTTON_LMASK) != 0
            );
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event->button.button == SDL_BUTTON_LEFT) {
                Clay_SetPointerState(
                    (Clay_Vector2){ event->button.x, event->button.y },
                    true
                );
                _cr_dispatch_clicks();
            }
            break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event->button.button == SDL_BUTTON_LEFT) {
                Clay_SetPointerState(
                    (Clay_Vector2){ event->button.x, event->button.y },
                    false
                );
            }
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            Clay_UpdateScrollContainers(true,
                (Clay_Vector2){ event->wheel.x * 30, event->wheel.y * 30 },
                0.016f);
            break;

        // Text input events
        case SDL_EVENT_TEXT_INPUT:
            _cr_handle_text_event(event->text.text);
            break;

        case SDL_EVENT_KEY_DOWN:
            _cr_handle_key_event(event->key.key, true);
            break;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    AppState *state = appstate;

    Clay_RenderCommandArray commands = create_layout();

    SDL_SetRenderDrawColor(state->rendererData.renderer, 245, 247, 250, 255);
    SDL_RenderClear(state->rendererData.renderer);

    SDL_Clay_RenderClayCommands(&state->rendererData, &commands);

    SDL_RenderPresent(state->rendererData.renderer);

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    AppState *state = appstate;
    (void)result;

    // Shutdown Clay React
    cr_shutdown();

    if (state) {
        if (state->rendererData.renderer) SDL_DestroyRenderer(state->rendererData.renderer);
        if (state->window) {
            SDL_StopTextInput(state->window);
            SDL_DestroyWindow(state->window);
        }
        if (state->rendererData.fonts) {
            TTF_CloseFont(state->rendererData.fonts[0]);
            SDL_free(state->rendererData.fonts);
        }
        if (state->rendererData.textEngine) {
            TTF_DestroyRendererTextEngine(state->rendererData.textEngine);
        }
        SDL_free(state);
    }

    TTF_Quit();
    SDL_Quit();
}
