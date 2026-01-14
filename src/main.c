/**
 * Todo App Example - Using the new Clay React idiomatic API
 *
 * Demonstrates:
 * - $use_state for reactive state management
 * - $use_text_input for editable text fields
 * - $col, $row for layout
 * - $text for formatted text
 * - $clickable, $icon_button for interaction
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
#include "renderer/sdl3_renderer.h"

#include <stdio.h>
#include <string.h>

// ============================================================================
// THEME
// ============================================================================

static const Clay_Color COL_BG         = { 245, 247, 250, 255 };
static const Clay_Color COL_SURFACE    = { 255, 255, 255, 255 };
static const Clay_Color COL_TEXT       = { 30, 35, 45, 255 };
static const Clay_Color COL_TEXT_MUTED = { 100, 110, 125, 255 };
static const Clay_Color COL_ACCENT     = { 59, 130, 246, 255 };
static const Clay_Color COL_DANGER     = { 239, 68, 68, 255 };

// ============================================================================
// APP STATE
// ============================================================================

typedef struct {
    SDL_Window *window;
    Clay_SDL3RendererData rendererData;
} AppState;

static AppState *g_app = NULL;

// ============================================================================
// TODO DATA TYPES
// ============================================================================

#define MAX_TODOS 32

typedef struct TodoItem {
    char title[64];
    bool done;
    CR_TextInputState *input;  // Text input state for editing
} TodoItem;

typedef struct TodoState {
    TodoItem items[MAX_TODOS];
    int count;
    int next_id;
    int editing_index;  // -1 if not editing any
} TodoState;

// ============================================================================
// TEXT CONFIGS (for dynamic text only)
// ============================================================================

static Clay_TextElementConfig* textconfig_body(void) {
    return CLAY_TEXT_CONFIG({
        .fontId = 0, .fontSize = 16, .lineHeight = 24, .textColor = COL_TEXT,
    });
}

static Clay_TextElementConfig* textconfig_muted(void) {
    return CLAY_TEXT_CONFIG({
        .fontId = 0, .fontSize = 14, .lineHeight = 20, .textColor = COL_TEXT_MUTED,
    });
}

static Clay_TextElementConfig* textconfig_strikethrough(void) {
    return CLAY_TEXT_CONFIG({
        .fontId = 0, .fontSize = 16, .lineHeight = 24, .textColor = COL_TEXT_MUTED,
    });
}

// ============================================================================
// TODO COMPONENT - Using $use_state for state management
// ============================================================================

static void TodoApp(void) {
    // All todo state managed via $use_state
    auto state = $use_state(TodoState, {
        .count = 3,
        .next_id = 4,
        .editing_index = -1,
    });

    // One-time initialization of items
    static bool initialized = false;
    if (!initialized) {
        strcpy(state->value->items[0].title, "Welcome to the Todo App!");
        state->value->items[0].done = false;
        state->value->items[0].input = _cr_alloc_text_input(64);
        _cr_text_input_set_text(state->value->items[0].input, state->value->items[0].title);

        strcpy(state->value->items[1].title, "Click to edit, check to toggle");
        state->value->items[1].done = false;
        state->value->items[1].input = _cr_alloc_text_input(64);
        _cr_text_input_set_text(state->value->items[1].input, state->value->items[1].title);

        strcpy(state->value->items[2].title, "Click x to delete");
        state->value->items[2].done = false;
        state->value->items[2].input = _cr_alloc_text_input(64);
        _cr_text_input_set_text(state->value->items[2].input, state->value->items[2].title);

        initialized = true;
    }

    // New todo input
    auto new_input = $use_text_input(64);

    // Compute stats
    int completed = 0;
    for (int i = 0; i < state->value->count; i++) {
        if (state->value->items[i].done) completed++;
    }

    // Main container using $col for vertical layout
    $col(
        .sizing = { .width = $fixed(600), .height = $grow(0) },
        .padding = $pad_lrtb(40, 40, 32, 32),
        .gap = 24,
        .bg = COL_SURFACE,
        .corner_radius = $radius(16),
    ) {
        // Title
        $label_ex((.fontSize = 32, .lineHeight = 40, .textColor = COL_TEXT), "Todo App");

        // Stats
        $text("%d items | %d active | %d done",
            state->value->count, state->value->count - completed, completed);

        // New todo input row
        $row(
            .sizing = { .width = $grow(0) },
            .gap = 12,
        ) {
            // Text input for new todo
            $text_input("NewTodoInput", new_input) {}

            // Add button
            $clickable("AddTodo", ^{
                    if (state->value->count < MAX_TODOS && new_input->length > 0) {
                        TodoItem *item = &state->value->items[state->value->count];
                        strncpy(item->title, new_input->buffer, 63);
                        item->title[63] = '\0';
                        item->done = false;
                        item->input = _cr_alloc_text_input(64);
                        _cr_text_input_set_text(item->input, item->title);
                        state->value->count++;
                        state->value->next_id++;
                        // Clear input
                        new_input->buffer[0] = '\0';
                        new_input->length = 0;
                        _cr_unfocus_input();
                    }
                },
                .layout = {
                    .padding = $pad_lrtb(16, 16, 10, 10),
                    .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                },
                .backgroundColor = $hover_style(
                    $alpha(COL_ACCENT, 200),
                    COL_ACCENT
                ),
                .cornerRadius = $radius(6),
            ) {
                $label_ex((.fontSize = 16, .textColor = $WHITE), "+ Add");
            }
        }

        // Todo list container
        $col(.sizing = { .width = $grow(0) }, .gap = 8) {
            // Iterate over todos
            for (int i = 0; i < state->value->count; i++) {
                TodoItem *item = &state->value->items[i];
                bool done = item->done;
                int idx = i;
                bool is_editing = (state->value->editing_index == i) && item->input->focused;

                // Todo item row
                $row(
                    .sizing = { .width = $grow(0) },
                    .padding = $pad_lrtb(16, 16, 12, 12),
                    .gap = 12,
                    .bg = $hover_style(COL_BG, $alpha(COL_ACCENT, 20)),
                    .corner_radius = $radius(8),
                ) {
                    // Checkbox
                    $clickable_i("TodoCheck", i, ^{
                            state->value->items[idx].done = !state->value->items[idx].done;
                        },
                        .layout = {
                            .sizing = { .width = $fixed(24), .height = $fixed(24) },
                            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                        },
                        .backgroundColor = done ? COL_ACCENT : COL_BG,
                        .cornerRadius = $radius(4),
                        .border = done ? (Clay_BorderElementConfig){0} :
                            (Clay_BorderElementConfig){ .width = $border_outside(2), .color = COL_TEXT_MUTED },
                    ) {
                        if (done) {
                            $label("*");
                        }
                    }

                    // Title - editable text input or display
                    if (is_editing) {
                        $text_input_i("TodoEdit", i, item->input) {}
                    } else {
                        // Click to edit
                        $clickable_i("TodoTitle", i, ^{
                                state->value->editing_index = idx;
                                // Sync text to input
                                _cr_text_input_set_text(state->value->items[idx].input, state->value->items[idx].title);
                                _cr_focus_input(state->value->items[idx].input, 0);
                            },
                            .layout = {
                                .sizing = { .width = $grow(0) },
                                .padding = $pad_lrtb(8, 8, 6, 6),
                            },
                            .cornerRadius = $radius(4),
                        ) {
                            Clay__OpenTextElement($cstr(item->title),
                                done ? textconfig_strikethrough() : textconfig_body());
                        }
                    }

                    // Save button (when editing)
                    if (is_editing) {
                        $clickable_i("TodoSave", i, ^{
                                // Copy from input to title
                                strncpy(state->value->items[idx].title, state->value->items[idx].input->buffer, 63);
                                state->value->items[idx].title[63] = '\0';
                                state->value->editing_index = -1;
                                _cr_unfocus_input();
                            },
                            .layout = {
                                .sizing = { .width = $fixed(28), .height = $fixed(28) },
                                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                            },
                            .backgroundColor = $hover_style(
                                $alpha(COL_ACCENT, 180),
                                COL_ACCENT
                            ),
                            .cornerRadius = $radius(4),
                        ) {
                            $label_ex((.fontSize = 14, .textColor = $WHITE), "OK");
                        }
                    }

                    // Delete button
                    $icon_button("TodoDelete", i, "x", ^{
                            // Free the text input
                            if (state->value->items[idx].input) {
                                free(state->value->items[idx].input->buffer);
                                free(state->value->items[idx].input);
                            }
                            for (int k = idx; k < state->value->count - 1; k++) {
                                state->value->items[k] = state->value->items[k + 1];
                            }
                            state->value->count--;
                            if (state->value->editing_index == idx) {
                                state->value->editing_index = -1;
                            } else if (state->value->editing_index > idx) {
                                state->value->editing_index--;
                            }
                        },
                        .backgroundColor = $hover_style(
                            $alpha(COL_DANGER, 60),
                            COL_DANGER
                        ),
                    ) {}
                }
            }

            // Empty state
            if (state->value->count == 0) {
                $center(.padding = $pad_lrtb(32, 32, 24, 24)) {
                    $label_ex((.fontSize = 14, .lineHeight = 20, .textColor = COL_TEXT_MUTED),
                        "No todos yet. Add one above!");
                }
            }
        }
    }
}

// ============================================================================
// LAYOUT
// ============================================================================

static Clay_RenderCommandArray create_layout(void) {
    cr_begin_frame();

    // Root container
    $center(
        .sizing = $fill(),
        .padding = $pad(40),
        .bg = COL_BG,
    ) { TodoApp(); }

    return cr_end_frame();
}

// ============================================================================
// SDL CALLBACKS
// ============================================================================

static Clay_Dimensions measure_text(Clay_StringSlice text, Clay_TextElementConfig *config, void *userData) {
    TTF_Font **fonts = userData;
    TTF_Font *font = fonts[config->fontId];
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

    if (!SDL_CreateWindowAndRenderer("Todo App - Clay React", 1024, 768,
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
