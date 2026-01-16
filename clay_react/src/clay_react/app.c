#include "clay_react/app.h"
#include "clay_react/clay_react.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    CR_APP_DEFAULT_WIDTH = 1024,
    CR_APP_DEFAULT_HEIGHT = 768,
    CR_APP_DEFAULT_FONT_SIZE = 24,
};

static const CR_AppConfig *g_app_config = NULL;

static const char *cr_app_title(void) {
    if (g_app_config && g_app_config->title && g_app_config->title[0] != '\0') {
        return g_app_config->title;
    }
    return "Clay React App";
}

static int cr_app_width(void) {
    if (g_app_config && g_app_config->width > 0) {
        return g_app_config->width;
    }
    return CR_APP_DEFAULT_WIDTH;
}

static int cr_app_height(void) {
    if (g_app_config && g_app_config->height > 0) {
        return g_app_config->height;
    }
    return CR_APP_DEFAULT_HEIGHT;
}

static const char *cr_app_font_path(void) {
    if (g_app_config && g_app_config->font_path && g_app_config->font_path[0] != '\0') {
        return g_app_config->font_path;
    }
    return "resources/Roboto-Regular.ttf";
}

static int cr_app_font_size(void) {
    if (g_app_config && g_app_config->font_size > 0) {
        return g_app_config->font_size;
    }
    return CR_APP_DEFAULT_FONT_SIZE;
}

static Clay_Color cr_app_background_color(void) {
    Clay_Color color = { 0, 0, 0, 255 };
    if (g_app_config && g_app_config->background) {
        color = g_app_config->background(g_app_config->user_data);
    }
    return color;
}

static void cr_app_set_layout_dimensions(Clay_Dimensions dimensions) {
    Clay_SetLayoutDimensions(dimensions);
    if (g_app_config && g_app_config->on_viewport) {
        g_app_config->on_viewport(dimensions, g_app_config->user_data);
    }
}

static Clay_RenderCommandArray cr_app_build_layout(void) {
    cr_begin_frame();
    if (g_app_config && g_app_config->render) {
        g_app_config->render(g_app_config->user_data);
    }
    return cr_end_frame();
}

#if defined(CLAY_RENDERER_SDL3)

#define SDL_MAIN_HANDLED
#include <SDL3/SDL_oldnames.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <clay/renderers/SDL3/clay_renderer_SDL3.c>

typedef struct {
    SDL_Window *window;
    Clay_SDL3RendererData rendererData;
} AppState;

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

static void sdl3_shutdown(AppState *state) {
    if (!state) return;

    cr_shutdown();

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

    TTF_Quit();
    SDL_Quit();
}

static int run_sdl3(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    if (!TTF_Init()) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to init TTF: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    AppState state = {0};

    if (!SDL_CreateWindowAndRenderer(cr_app_title(), cr_app_width(), cr_app_height(),
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY,
            &state.window, &state.rendererData.renderer)) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create window: %s", SDL_GetError());
        sdl3_shutdown(&state);
        return 1;
    }

    state.rendererData.textEngine = TTF_CreateRendererTextEngine(state.rendererData.renderer);
    if (!state.rendererData.textEngine) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to create text engine: %s", SDL_GetError());
        sdl3_shutdown(&state);
        return 1;
    }

    state.rendererData.fonts = SDL_calloc(1, sizeof(TTF_Font *));
    if (!state.rendererData.fonts) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to allocate fonts");
        sdl3_shutdown(&state);
        return 1;
    }

    TTF_Font *font = TTF_OpenFont(cr_app_font_path(), cr_app_font_size());
    if (!font) {
        SDL_LogError(SDL_LOG_CATEGORY_ERROR, "Failed to load font: %s", SDL_GetError());
        sdl3_shutdown(&state);
        return 1;
    }
    state.rendererData.fonts[0] = font;

    uint64_t memory_size = Clay_MinMemorySize();
    Clay_Arena arena = {
        .memory = SDL_calloc(1, memory_size),
        .capacity = memory_size,
    };

    int width, height;
    SDL_GetWindowSize(state.window, &width, &height);
    Clay_Initialize(arena, (Clay_Dimensions){ (float)width, (float)height },
        (Clay_ErrorHandler){ .errorHandlerFunction = handle_errors });
    cr_app_set_layout_dimensions((Clay_Dimensions){ (float)width, (float)height });
    Clay_SetMeasureTextFunction(measure_text, state.rendererData.fonts);

    cr_init();
    SDL_StartTextInput(state.window);

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;

                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    cr_app_set_layout_dimensions((Clay_Dimensions){
                        event.window.data1, event.window.data2
                    });
                    break;

                case SDL_EVENT_MOUSE_MOTION:
                    Clay_SetPointerState(
                        (Clay_Vector2){ event.motion.x, event.motion.y },
                        (event.motion.state & SDL_BUTTON_LMASK) != 0
                    );
                    break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        Clay_SetPointerState(
                            (Clay_Vector2){ event.button.x, event.button.y },
                            true
                        );
                        _cr_dispatch_clicks();
                    }
                    break;

                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        Clay_SetPointerState(
                            (Clay_Vector2){ event.button.x, event.button.y },
                            false
                        );
                    }
                    break;

                case SDL_EVENT_MOUSE_WHEEL:
                    Clay_UpdateScrollContainers(true,
                        (Clay_Vector2){ event.wheel.x * 30, event.wheel.y * 30 },
                        0.016f);
                    break;

                case SDL_EVENT_TEXT_INPUT:
                    _cr_handle_text_event(event.text.text);
                    break;

                case SDL_EVENT_KEY_DOWN:
                    _cr_handle_key_event(event.key.key, true);
                    break;
            }
        }

        Clay_RenderCommandArray commands = cr_app_build_layout();
        Clay_Color background = cr_app_background_color();

        SDL_SetRenderDrawColor(state.rendererData.renderer,
            background.r, background.g, background.b, background.a);
        SDL_RenderClear(state.rendererData.renderer);

        SDL_Clay_RenderClayCommands(&state.rendererData, &commands);

        SDL_RenderPresent(state.rendererData.renderer);
    }

    sdl3_shutdown(&state);
    return 0;
}

#elif defined(CLAY_RENDERER_SDL2)

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_ttf.h>
#include <SDL_image.h>

#include <clay/renderers/SDL2/clay_renderer_SDL2.c>

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL2_Font *fonts;
} AppState;

static void handle_errors(Clay_ErrorData error) {
    fprintf(stderr, "[Clay Error] %.*s\n", (int)error.errorText.length, error.errorText.chars);
}

static void sdl2_shutdown(AppState *state) {
    if (!state) return;

    cr_shutdown();

    if (state->fonts) {
        if (state->fonts[0].font) {
            TTF_CloseFont(state->fonts[0].font);
        }
        free(state->fonts);
    }
    if (state->renderer) SDL_DestroyRenderer(state->renderer);
    if (state->window) SDL_DestroyWindow(state->window);

    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
}

static int run_sdl2(void) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "TTF_Init failed: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }
    IMG_Init(IMG_INIT_PNG);

    AppState state = {0};
    state.window = SDL_CreateWindow(cr_app_title(), SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED, cr_app_width(), cr_app_height(), SDL_WINDOW_RESIZABLE);
    if (!state.window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        sdl2_shutdown(&state);
        return 1;
    }
    state.renderer = SDL_CreateRenderer(state.window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!state.renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        sdl2_shutdown(&state);
        return 1;
    }

    state.fonts = calloc(1, sizeof(SDL2_Font));
    if (!state.fonts) {
        fprintf(stderr, "Failed to allocate fonts\n");
        sdl2_shutdown(&state);
        return 1;
    }
    state.fonts[0].fontId = 0;
    state.fonts[0].font = TTF_OpenFont(cr_app_font_path(), cr_app_font_size());
    if (!state.fonts[0].font) {
        fprintf(stderr, "Failed to load font: %s\n", TTF_GetError());
        sdl2_shutdown(&state);
        return 1;
    }

    uint64_t memory_size = Clay_MinMemorySize();
    Clay_Arena arena = {
        .memory = calloc(1, memory_size),
        .capacity = memory_size,
    };

    int width, height;
    SDL_GetWindowSize(state.window, &width, &height);
    Clay_Initialize(arena, (Clay_Dimensions){ (float)width, (float)height },
        (Clay_ErrorHandler){ .errorHandlerFunction = handle_errors });
    cr_app_set_layout_dimensions((Clay_Dimensions){ (float)width, (float)height });
    Clay_SetMeasureTextFunction(SDL2_MeasureText, state.fonts);

    cr_init();
    SDL_StartTextInput();

    bool running = true;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_QUIT:
                    running = false;
                    break;
                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        cr_app_set_layout_dimensions((Clay_Dimensions){
                            event.window.data1, event.window.data2
                        });
                    }
                    break;
                case SDL_MOUSEMOTION:
                    Clay_SetPointerState(
                        (Clay_Vector2){ event.motion.x, event.motion.y },
                        (event.motion.state & SDL_BUTTON_LMASK) != 0
                    );
                    break;
                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        Clay_SetPointerState(
                            (Clay_Vector2){ event.button.x, event.button.y },
                            true
                        );
                        _cr_dispatch_clicks();
                    }
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        Clay_SetPointerState(
                            (Clay_Vector2){ event.button.x, event.button.y },
                            false
                        );
                    }
                    break;
                case SDL_MOUSEWHEEL:
                    Clay_UpdateScrollContainers(true,
                        (Clay_Vector2){ event.wheel.x * 30, event.wheel.y * 30 },
                        0.016f);
                    break;
                case SDL_TEXTINPUT:
                    _cr_handle_text_event(event.text.text);
                    break;
                case SDL_KEYDOWN:
                    _cr_handle_key_event(event.key.keysym.sym, true);
                    break;
            }
        }

        Clay_RenderCommandArray commands = cr_app_build_layout();
        Clay_Color background = cr_app_background_color();

        SDL_SetRenderDrawColor(state.renderer,
            background.r, background.g, background.b, background.a);
        SDL_RenderClear(state.renderer);

        Clay_SDL2_Render(state.renderer, commands, state.fonts);

        SDL_RenderPresent(state.renderer);
    }

    SDL_StopTextInput();
    sdl2_shutdown(&state);

    return 0;
}

#elif defined(CLAY_RENDERER_RAYLIB)

#include <raylib.h>

#include <clay/renderers/raylib/clay_renderer_raylib.c>

static void handle_errors(Clay_ErrorData error) {
    fprintf(stderr, "[Clay Error] %.*s\n", (int)error.errorText.length, error.errorText.chars);
}

static void raylib_emit_text(int codepoint) {
    char utf8[5] = {0};
    int len = 0;

    if (codepoint <= 0x7F) {
        utf8[0] = (char)codepoint;
        len = 1;
    } else if (codepoint <= 0x7FF) {
        utf8[0] = (char)(0xC0 | ((codepoint >> 6) & 0x1F));
        utf8[1] = (char)(0x80 | (codepoint & 0x3F));
        len = 2;
    } else if (codepoint <= 0xFFFF) {
        utf8[0] = (char)(0xE0 | ((codepoint >> 12) & 0x0F));
        utf8[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        utf8[2] = (char)(0x80 | (codepoint & 0x3F));
        len = 3;
    } else if (codepoint <= 0x10FFFF) {
        utf8[0] = (char)(0xF0 | ((codepoint >> 18) & 0x07));
        utf8[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
        utf8[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
        utf8[3] = (char)(0x80 | (codepoint & 0x3F));
        len = 4;
    }

    if (len > 0) {
        _cr_handle_text_event(utf8);
    }
}

static void raylib_handle_text_input(void) {
    int codepoint = GetCharPressed();
    while (codepoint > 0) {
        raylib_emit_text(codepoint);
        codepoint = GetCharPressed();
    }

    if (IsKeyPressed(KEY_BACKSPACE)) {
        _cr_handle_key_event(8, true);
    }
    if (IsKeyPressed(KEY_DELETE)) {
        _cr_handle_key_event(127, true);
    }
    if (IsKeyPressed(KEY_LEFT)) {
        _cr_handle_key_event(1073741904, true);
    }
    if (IsKeyPressed(KEY_RIGHT)) {
        _cr_handle_key_event(1073741903, true);
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
        _cr_handle_key_event(27, true);
    }
    if (IsKeyPressed(KEY_ENTER)) {
        _cr_handle_key_event(13, true);
    }
}

static int run_raylib(void) {
    InitWindow(cr_app_width(), cr_app_height(), cr_app_title());
    SetTargetFPS(60);

    Font fonts[1] = {0};
    fonts[0] = LoadFontEx(cr_app_font_path(), cr_app_font_size(), NULL, 0);

    uint64_t memory_size = Clay_MinMemorySize();
    Clay_Arena arena = {
        .memory = calloc(1, memory_size),
        .capacity = memory_size,
    };

    Clay_Initialize(arena, (Clay_Dimensions){ (float)GetScreenWidth(), (float)GetScreenHeight() },
        (Clay_ErrorHandler){ .errorHandlerFunction = handle_errors });
    cr_app_set_layout_dimensions((Clay_Dimensions){ (float)GetScreenWidth(), (float)GetScreenHeight() });
    Clay_SetMeasureTextFunction(Raylib_MeasureText, fonts);

    cr_init();

    while (!WindowShouldClose()) {
        if (IsWindowResized()) {
            cr_app_set_layout_dimensions((Clay_Dimensions){
                (float)GetScreenWidth(), (float)GetScreenHeight()
            });
        }

        Vector2 mouse = GetMousePosition();
        bool down = IsMouseButtonDown(MOUSE_LEFT_BUTTON);
        Clay_SetPointerState((Clay_Vector2){ mouse.x, mouse.y }, down);
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            _cr_dispatch_clicks();
        }

        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            Clay_UpdateScrollContainers(true, (Clay_Vector2){ 0.0f, wheel * 30.0f }, GetFrameTime());
        }

        raylib_handle_text_input();

        Clay_RenderCommandArray commands = cr_app_build_layout();
        Clay_Color background = cr_app_background_color();

        BeginDrawing();
        ClearBackground((Color){ background.r, background.g, background.b, background.a });
        Clay_Raylib_Render(commands, fonts);
        EndDrawing();
    }

    cr_shutdown();
    UnloadFont(fonts[0]);
    CloseWindow();

    return 0;
}

#elif defined(CLAY_RENDERER_CAIRO)

#include <cairo/cairo.h>
#include <cairo/cairo-xcb.h>
#include <X11/keysym.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

#include <clay/renderers/cairo/clay_renderer_cairo.c>

static void handle_errors(Clay_ErrorData error) {
    fprintf(stderr, "[Clay Error] %.*s\n", (int)error.errorText.length, error.errorText.chars);
}

static xcb_visualtype_t *xcb_find_visual(xcb_screen_t *screen, xcb_visualid_t visual_id) {
    xcb_depth_iterator_t depth_iter = xcb_screen_allowed_depths_iterator(screen);
    for (; depth_iter.rem; xcb_depth_next(&depth_iter)) {
        xcb_visualtype_iterator_t visual_iter = xcb_depth_visuals_iterator(depth_iter.data);
        for (; visual_iter.rem; xcb_visualtype_next(&visual_iter)) {
            if (visual_iter.data->visual_id == visual_id) {
                return visual_iter.data;
            }
        }
    }
    return NULL;
}

static void xcb_handle_key_press(xcb_key_press_event_t *event, xcb_key_symbols_t *keysyms) {
    int shift = (event->state & XCB_MOD_MASK_SHIFT) ? 1 : 0;
    xcb_keysym_t keysym = xcb_key_symbols_get_keysym(keysyms, event->detail, shift);

    if (keysym >= XK_space && keysym <= XK_asciitilde) {
        char text[2] = { (char)keysym, '\0' };
        _cr_handle_text_event(text);
        return;
    }

    switch (keysym) {
        case XK_Return:
        case XK_KP_Enter:
            _cr_handle_key_event(13, true);
            break;
        case XK_BackSpace:
            _cr_handle_key_event(8, true);
            break;
        case XK_Delete:
            _cr_handle_key_event(127, true);
            break;
        case XK_Left:
            _cr_handle_key_event(1073741904, true);
            break;
        case XK_Right:
            _cr_handle_key_event(1073741903, true);
            break;
        case XK_Escape:
            _cr_handle_key_event(27, true);
            break;
        default:
            break;
    }
}

static int run_xcb_cairo(void) {
    int width = cr_app_width();
    int height = cr_app_height();

    xcb_connection_t *connection = xcb_connect(NULL, NULL);
    if (!connection || xcb_connection_has_error(connection)) {
        fprintf(stderr, "Failed to connect to X server\n");
        if (connection) {
            xcb_disconnect(connection);
        }
        return 1;
    }

    const xcb_setup_t *setup = xcb_get_setup(connection);
    xcb_screen_iterator_t screen_iter = xcb_setup_roots_iterator(setup);
    xcb_screen_t *screen = screen_iter.data;
    xcb_visualtype_t *visual = xcb_find_visual(screen, screen->root_visual);
    if (!visual) {
        fprintf(stderr, "Failed to find visual for X screen\n");
        xcb_disconnect(connection);
        return 1;
    }

    xcb_window_t window = xcb_generate_id(connection);
    uint32_t values[] = {
        screen->white_pixel,
        XCB_EVENT_MASK_EXPOSURE |
        XCB_EVENT_MASK_KEY_PRESS |
        XCB_EVENT_MASK_BUTTON_PRESS |
        XCB_EVENT_MASK_BUTTON_RELEASE |
        XCB_EVENT_MASK_POINTER_MOTION |
        XCB_EVENT_MASK_STRUCTURE_NOTIFY
    };

    xcb_create_window(
        connection,
        XCB_COPY_FROM_PARENT,
        window,
        screen->root,
        0, 0,
        width, height,
        0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        screen->root_visual,
        XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
        values
    );

    xcb_intern_atom_cookie_t wm_protocols_cookie =
        xcb_intern_atom(connection, 1, 12, "WM_PROTOCOLS");
    xcb_intern_atom_cookie_t wm_delete_cookie =
        xcb_intern_atom(connection, 0, 16, "WM_DELETE_WINDOW");
    xcb_intern_atom_reply_t *wm_protocols_reply =
        xcb_intern_atom_reply(connection, wm_protocols_cookie, NULL);
    xcb_intern_atom_reply_t *wm_delete_reply =
        xcb_intern_atom_reply(connection, wm_delete_cookie, NULL);

    if (wm_protocols_reply && wm_delete_reply) {
        xcb_change_property(
            connection,
            XCB_PROP_MODE_REPLACE,
            window,
            wm_protocols_reply->atom,
            XCB_ATOM_ATOM,
            32,
            1,
            &wm_delete_reply->atom
        );
    }

    xcb_map_window(connection, window);
    xcb_flush(connection);

    cairo_surface_t *surface = cairo_xcb_surface_create(connection, window, visual, width, height);
    cairo_t *cr = cairo_create(surface);
    Clay_Cairo_Initialize(cr);
    xcb_key_symbols_t *keysyms = xcb_key_symbols_alloc(connection);
    if (!keysyms) {
        fprintf(stderr, "Failed to init xcb key symbols; text input disabled\n");
    }

    char *fonts[] = { strdup("Sans") };

    uint64_t memory_size = Clay_MinMemorySize();
    Clay_Arena arena = {
        .memory = calloc(1, memory_size),
        .capacity = memory_size,
    };

    Clay_Initialize(arena, (Clay_Dimensions){ (float)width, (float)height },
        (Clay_ErrorHandler){ .errorHandlerFunction = handle_errors });
    cr_app_set_layout_dimensions((Clay_Dimensions){ (float)width, (float)height });
    Clay_SetMeasureTextFunction(Clay_Cairo_MeasureText, fonts);

    cr_init();

    bool running = true;
    bool needs_redraw = true;
    while (running) {
        xcb_generic_event_t *event = NULL;
        while ((event = xcb_poll_for_event(connection)) != NULL) {
            uint8_t type = event->response_type & ~0x80;
            switch (type) {
                case XCB_EXPOSE:
                    needs_redraw = true;
                    break;
                case XCB_CONFIGURE_NOTIFY: {
                    xcb_configure_notify_event_t *configure =
                        (xcb_configure_notify_event_t *)event;
                    if (configure->width != width || configure->height != height) {
                        width = configure->width;
                        height = configure->height;
                        cairo_xcb_surface_set_size(surface, width, height);
                        cr_app_set_layout_dimensions((Clay_Dimensions){ (float)width, (float)height });
                        needs_redraw = true;
                    }
                    break;
                }
                case XCB_MOTION_NOTIFY: {
                    xcb_motion_notify_event_t *motion = (xcb_motion_notify_event_t *)event;
                    bool down = (motion->state & XCB_BUTTON_MASK_1) != 0;
                    Clay_SetPointerState(
                        (Clay_Vector2){ (float)motion->event_x, (float)motion->event_y },
                        down
                    );
                    needs_redraw = true;
                    break;
                }
                case XCB_BUTTON_PRESS: {
                    xcb_button_press_event_t *button = (xcb_button_press_event_t *)event;
                    if (button->detail == 1) {
                        Clay_SetPointerState(
                            (Clay_Vector2){ (float)button->event_x, (float)button->event_y },
                            true
                        );
                        _cr_dispatch_clicks();
                        needs_redraw = true;
                    } else if (button->detail == 4 || button->detail == 5) {
                        float delta = (button->detail == 4) ? 30.0f : -30.0f;
                        Clay_UpdateScrollContainers(true,
                            (Clay_Vector2){ 0.0f, delta }, 0.016f);
                        needs_redraw = true;
                    } else if (button->detail == 6 || button->detail == 7) {
                        float delta = (button->detail == 6) ? 30.0f : -30.0f;
                        Clay_UpdateScrollContainers(true,
                            (Clay_Vector2){ delta, 0.0f }, 0.016f);
                        needs_redraw = true;
                    }
                    break;
                }
                case XCB_BUTTON_RELEASE: {
                    xcb_button_release_event_t *button = (xcb_button_release_event_t *)event;
                    if (button->detail == 1) {
                        Clay_SetPointerState(
                            (Clay_Vector2){ (float)button->event_x, (float)button->event_y },
                            false
                        );
                        needs_redraw = true;
                    }
                    break;
                }
                case XCB_KEY_PRESS:
                    if (keysyms) {
                        xcb_handle_key_press((xcb_key_press_event_t *)event, keysyms);
                        needs_redraw = true;
                    }
                    break;
                case XCB_CLIENT_MESSAGE: {
                    xcb_client_message_event_t *client =
                        (xcb_client_message_event_t *)event;
                    if (wm_protocols_reply && wm_delete_reply &&
                        client->type == wm_protocols_reply->atom &&
                        client->data.data32[0] == wm_delete_reply->atom) {
                        running = false;
                    }
                    break;
                }
                default:
                    break;
            }
            free(event);
        }

        if (needs_redraw) {
            Clay_RenderCommandArray commands = cr_app_build_layout();
            Clay_Color background = cr_app_background_color();

            cairo_save(cr);
            cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
            cairo_set_source_rgba(cr,
                background.r / 255.0,
                background.g / 255.0,
                background.b / 255.0,
                background.a / 255.0);
            cairo_paint(cr);
            cairo_restore(cr);

            Clay_Cairo_Render(commands, fonts);
            cairo_surface_flush(surface);
            xcb_flush(connection);
            needs_redraw = false;
        }

        struct timespec sleep_time = { 0, 16 * 1000 * 1000 };
        nanosleep(&sleep_time, NULL);
    }

    cr_shutdown();

    if (keysyms) {
        xcb_key_symbols_free(keysyms);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    if (wm_protocols_reply) free(wm_protocols_reply);
    if (wm_delete_reply) free(wm_delete_reply);
    xcb_disconnect(connection);

    return 0;
}

#elif defined(CLAY_RENDERER_XCB)

#include <X11/keysym.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

#include <clay/renderers/xcb/clay_renderer_xcb.c>

static float xcb_parse_scale(const char *value) {
    if (!value || !*value) return 0.0f;
    char *end = NULL;
    float scale = strtof(value, &end);
    if (end == value || scale <= 0.0f) return 0.0f;
    return scale;
}

static float xcb_detect_scale(const xcb_screen_t *screen) {
    float env_scale = xcb_parse_scale(getenv("CLAY_XCB_SCALE"));
    if (env_scale > 0.0f) {
        return env_scale;
    }
    if (!screen || screen->width_in_millimeters <= 0 || screen->height_in_millimeters <= 0) {
        return 1.0f;
    }
    float dpi_x = (float)screen->width_in_pixels * 25.4f / (float)screen->width_in_millimeters;
    float dpi_y = (float)screen->height_in_pixels * 25.4f / (float)screen->height_in_millimeters;
    float dpi = (dpi_x + dpi_y) * 0.5f;
    if (dpi <= 0.0f) {
        return 1.0f;
    }
    float scale = dpi / 96.0f;
    if (scale < 1.0f) scale = 1.0f;
    if (scale > 4.0f) scale = 4.0f;
    return scale;
}

static void handle_errors(Clay_ErrorData error) {
    fprintf(stderr, "[Clay Error] %.*s\n", (int)error.errorText.length, error.errorText.chars);
}

static xcb_visualtype_t *xcb_find_visual(xcb_screen_t *screen, xcb_visualid_t visual_id) {
    xcb_depth_iterator_t depth_iter = xcb_screen_allowed_depths_iterator(screen);
    for (; depth_iter.rem; xcb_depth_next(&depth_iter)) {
        xcb_visualtype_iterator_t visual_iter = xcb_depth_visuals_iterator(depth_iter.data);
        for (; visual_iter.rem; xcb_visualtype_next(&visual_iter)) {
            if (visual_iter.data->visual_id == visual_id) {
                return visual_iter.data;
            }
        }
    }
    return NULL;
}

static void xcb_handle_key_press(xcb_key_press_event_t *event, xcb_key_symbols_t *keysyms) {
    int shift = (event->state & XCB_MOD_MASK_SHIFT) ? 1 : 0;
    xcb_keysym_t keysym = xcb_key_symbols_get_keysym(keysyms, event->detail, shift);

    if (keysym >= XK_space && keysym <= XK_asciitilde) {
        char text[2] = { (char)keysym, '\0' };
        _cr_handle_text_event(text);
        return;
    }

    switch (keysym) {
        case XK_Return:
        case XK_KP_Enter:
            _cr_handle_key_event(13, true);
            break;
        case XK_BackSpace:
            _cr_handle_key_event(8, true);
            break;
        case XK_Delete:
            _cr_handle_key_event(127, true);
            break;
        case XK_Left:
            _cr_handle_key_event(1073741904, true);
            break;
        case XK_Right:
            _cr_handle_key_event(1073741903, true);
            break;
        case XK_Escape:
            _cr_handle_key_event(27, true);
            break;
        default:
            break;
    }
}

static uint64_t xcb_now_ns(void) {
    struct timespec ts = {0};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int run_xcb(void) {
    int base_width = cr_app_width();
    int base_height = cr_app_height();

    xcb_connection_t *connection = xcb_connect(NULL, NULL);
    if (!connection || xcb_connection_has_error(connection)) {
        fprintf(stderr, "Failed to connect to X server\n");
        if (connection) {
            xcb_disconnect(connection);
        }
        return 1;
    }

    const xcb_setup_t *setup = xcb_get_setup(connection);
    xcb_screen_iterator_t screen_iter = xcb_setup_roots_iterator(setup);
    xcb_screen_t *screen = screen_iter.data;
    xcb_visualtype_t *visual = xcb_find_visual(screen, screen->root_visual);
    if (!visual) {
        fprintf(stderr, "Failed to find visual for X screen\n");
        xcb_disconnect(connection);
        return 1;
    }

    float ui_scale = xcb_detect_scale(screen);
    float window_scale = 1.0f;
    float logical_scale = 1.0f / ui_scale;
    bool scale_locked = false;
    int pixel_width = (int)lroundf((float)base_width * ui_scale);
    int pixel_height = (int)lroundf((float)base_height * ui_scale);

    xcb_window_t window = xcb_generate_id(connection);
    uint32_t values[] = {
        screen->white_pixel,
        XCB_EVENT_MASK_EXPOSURE |
        XCB_EVENT_MASK_KEY_PRESS |
        XCB_EVENT_MASK_BUTTON_PRESS |
        XCB_EVENT_MASK_BUTTON_RELEASE |
        XCB_EVENT_MASK_POINTER_MOTION |
        XCB_EVENT_MASK_STRUCTURE_NOTIFY
    };

    xcb_create_window(
        connection,
        XCB_COPY_FROM_PARENT,
        window,
        screen->root,
        0, 0,
        (uint16_t)pixel_width, (uint16_t)pixel_height,
        0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        screen->root_visual,
        XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
        values
    );

    xcb_intern_atom_cookie_t wm_protocols_cookie =
        xcb_intern_atom(connection, 1, 12, "WM_PROTOCOLS");
    xcb_intern_atom_cookie_t wm_delete_cookie =
        xcb_intern_atom(connection, 0, 16, "WM_DELETE_WINDOW");
    xcb_intern_atom_reply_t *wm_protocols_reply =
        xcb_intern_atom_reply(connection, wm_protocols_cookie, NULL);
    xcb_intern_atom_reply_t *wm_delete_reply =
        xcb_intern_atom_reply(connection, wm_delete_cookie, NULL);

    if (wm_protocols_reply && wm_delete_reply) {
        xcb_change_property(
            connection,
            XCB_PROP_MODE_REPLACE,
            window,
            wm_protocols_reply->atom,
            XCB_ATOM_ATOM,
            32,
            1,
            &wm_delete_reply->atom
        );
    }

    xcb_map_window(connection, window);
    xcb_flush(connection);

    Clay_XCB_Renderer renderer = {0};
    if (!Clay_XCB_Init(&renderer, connection, screen, visual, window, pixel_width, pixel_height)) {
        fprintf(stderr, "Failed to init XCB renderer\n");
        if (wm_protocols_reply) free(wm_protocols_reply);
        if (wm_delete_reply) free(wm_delete_reply);
        xcb_disconnect(connection);
        return 1;
    }
    renderer.scale = ui_scale;

    const char *font_paths[] = { cr_app_font_path() };
    Clay_XCB_FontCollection *fonts = Clay_XCB_LoadFonts(font_paths, 1);
    if (!fonts) {
        fprintf(stderr, "Failed to load fonts for XCB renderer\n");
        Clay_XCB_Shutdown(&renderer);
        if (wm_protocols_reply) free(wm_protocols_reply);
        if (wm_delete_reply) free(wm_delete_reply);
        xcb_disconnect(connection);
        return 1;
    }
    renderer.fonts = fonts;

    xcb_key_symbols_t *keysyms = xcb_key_symbols_alloc(connection);
    if (!keysyms) {
        fprintf(stderr, "Failed to init xcb key symbols; text input disabled\n");
    }

    uint64_t memory_size = Clay_MinMemorySize();
    Clay_Arena arena = {
        .memory = calloc(1, memory_size),
        .capacity = memory_size,
    };

    Clay_Initialize(arena, (Clay_Dimensions){ (float)pixel_width * logical_scale, (float)pixel_height * logical_scale },
        (Clay_ErrorHandler){ .errorHandlerFunction = handle_errors });
    cr_app_set_layout_dimensions((Clay_Dimensions){ (float)pixel_width * logical_scale, (float)pixel_height * logical_scale });
    Clay_SetMeasureTextFunction(Clay_XCB_MeasureText, fonts);

    cr_init();

    bool running = true;
    bool needs_redraw = true;
    bool pointer_down = false;
    uint64_t last_frame_ns = 0;
    int window_width = pixel_width;
    int window_height = pixel_height;
    while (running) {
        xcb_generic_event_t *event = NULL;
        while ((event = xcb_poll_for_event(connection)) != NULL) {
            uint8_t type = event->response_type & ~0x80;
            switch (type) {
                case XCB_EXPOSE:
                    needs_redraw = true;
                    break;
                case XCB_CONFIGURE_NOTIFY: {
                    xcb_configure_notify_event_t *configure =
                        (xcb_configure_notify_event_t *)event;
                    bool size_changed = (configure->width != window_width || configure->height != window_height);
                    bool scale_changed = false;
                    if (!scale_locked && renderer.scale > 1.0f) {
                        float ratio_w = (float)renderer.width / (float)configure->width;
                        float ratio_h = (float)renderer.height / (float)configure->height;
                        float ratio = 0.5f * (ratio_w + ratio_h);
                        if (fabsf(ratio - renderer.scale) < 0.15f) {
                            window_scale = renderer.scale;
                        } else {
                            window_scale = 1.0f;
                        }
                        logical_scale = window_scale / renderer.scale;
                        scale_locked = true;
                        scale_changed = true;
                    }
                    if (size_changed) {
                        window_width = configure->width;
                        window_height = configure->height;
                    }
                    if (size_changed || scale_changed) {
                        int pixel_w = (int)lroundf((float)window_width * window_scale);
                        int pixel_h = (int)lroundf((float)window_height * window_scale);
                        if (pixel_w != renderer.width || pixel_h != renderer.height) {
                            Clay_XCB_Resize(&renderer, pixel_w, pixel_h);
                        }
                        cr_app_set_layout_dimensions((Clay_Dimensions){
                            (float)window_width * logical_scale,
                            (float)window_height * logical_scale
                        });
                        needs_redraw = true;
                    }
                    break;
                }
                case XCB_MOTION_NOTIFY: {
                    xcb_motion_notify_event_t *motion = (xcb_motion_notify_event_t *)event;
                    bool down = (motion->state & XCB_BUTTON_MASK_1) != 0;
                    Clay_SetPointerState(
                        (Clay_Vector2){
                            (float)motion->event_x * logical_scale,
                            (float)motion->event_y * logical_scale
                        },
                        down
                    );
                    needs_redraw = true;
                    break;
                }
                case XCB_BUTTON_PRESS: {
                    xcb_button_press_event_t *button = (xcb_button_press_event_t *)event;
                    if (button->detail == 1) {
                        pointer_down = true;
                        Clay_SetPointerState(
                            (Clay_Vector2){
                                (float)button->event_x * logical_scale,
                                (float)button->event_y * logical_scale
                            },
                            true
                        );
                        _cr_dispatch_clicks();
                        needs_redraw = true;
                    } else if (button->detail == 4 || button->detail == 5) {
                        float delta = (button->detail == 4) ? 30.0f : -30.0f;
                        Clay_UpdateScrollContainers(true,
                            (Clay_Vector2){ 0.0f, delta }, 0.016f);
                        needs_redraw = true;
                    } else if (button->detail == 6 || button->detail == 7) {
                        float delta = (button->detail == 6) ? 30.0f : -30.0f;
                        Clay_UpdateScrollContainers(true,
                            (Clay_Vector2){ delta, 0.0f }, 0.016f);
                        needs_redraw = true;
                    }
                    break;
                }
                case XCB_BUTTON_RELEASE: {
                    xcb_button_release_event_t *button = (xcb_button_release_event_t *)event;
                    if (button->detail == 1) {
                        pointer_down = false;
                        Clay_SetPointerState(
                            (Clay_Vector2){
                                (float)button->event_x * logical_scale,
                                (float)button->event_y * logical_scale
                            },
                            false
                        );
                        needs_redraw = true;
                    }
                    break;
                }
                case XCB_KEY_PRESS:
                    if (keysyms) {
                        xcb_handle_key_press((xcb_key_press_event_t *)event, keysyms);
                        needs_redraw = true;
                    }
                    break;
                case XCB_CLIENT_MESSAGE: {
                    xcb_client_message_event_t *client =
                        (xcb_client_message_event_t *)event;
                    if (wm_protocols_reply && wm_delete_reply &&
                        client->type == wm_protocols_reply->atom &&
                        client->data.data32[0] == wm_delete_reply->atom) {
                        running = false;
                    }
                    break;
                }
                default:
                    break;
            }
            free(event);
        }

        if (needs_redraw) {
            uint64_t frame_ns = pointer_down ? 16666666ull : 33333333ull;
            uint64_t now_ns = xcb_now_ns();
            if (last_frame_ns != 0 && now_ns - last_frame_ns < frame_ns) {
                uint64_t sleep_ns = frame_ns - (now_ns - last_frame_ns);
                struct timespec sleep_time = {
                    .tv_sec = (time_t)(sleep_ns / 1000000000ull),
                    .tv_nsec = (long)(sleep_ns % 1000000000ull),
                };
                nanosleep(&sleep_time, NULL);
                continue;
            }

            Clay_RenderCommandArray commands = cr_app_build_layout();
            Clay_Color background = cr_app_background_color();

            Clay_XCB_Clear(&renderer, background);
            Clay_XCB_Render(&renderer, commands);
            Clay_XCB_Present(&renderer);
            needs_redraw = false;
            last_frame_ns = xcb_now_ns();
        }
        if (!needs_redraw) {
            struct timespec sleep_time = { 0, 4 * 1000 * 1000 };
            nanosleep(&sleep_time, NULL);
        }
    }

    cr_shutdown();

    if (keysyms) {
        xcb_key_symbols_free(keysyms);
    }
    Clay_XCB_Shutdown(&renderer);
    Clay_XCB_FreeFonts(fonts);
    if (wm_protocols_reply) free(wm_protocols_reply);
    if (wm_delete_reply) free(wm_delete_reply);
    xcb_disconnect(connection);

    return 0;
}

#elif defined(CLAY_RENDERER_TERMINAL)

#include <clay/renderers/terminal/clay_renderer_terminal_ansi.c>

static void handle_errors(Clay_ErrorData error) {
    fprintf(stderr, "[Clay Error] %.*s\n", (int)error.errorText.length, error.errorText.chars);
}

static int run_terminal(void) {
    const int width = 120;
    const int height = 40;
    int column_width = 1;

    uint64_t memory_size = Clay_MinMemorySize();
    Clay_Arena arena = {
        .memory = calloc(1, memory_size),
        .capacity = memory_size,
    };

    Clay_Initialize(arena, (Clay_Dimensions){ (float)width, (float)height },
        (Clay_ErrorHandler){ .errorHandlerFunction = handle_errors });
    cr_app_set_layout_dimensions((Clay_Dimensions){ (float)width, (float)height });
    Clay_SetMeasureTextFunction(Console_MeasureText, &column_width);

    cr_init();

    Clay_RenderCommandArray commands = cr_app_build_layout();
    Clay_Terminal_Render(commands, width, height, column_width);

    cr_shutdown();

    return 0;
}

#elif defined(CLAY_RENDERER_SOKOL)

static int run_sokol(void) {
    fprintf(stderr, "CLAY_RENDERER_SOKOL selected, but this demo backend is not wired yet.\n");
    return 1;
}

#elif defined(CLAY_RENDERER_WEB)

static int run_web(void) {
    fprintf(stderr, "CLAY_RENDERER_WEB selected. Use the web renderer assets in the Clay package.\n");
    return 1;
}

#elif defined(CLAY_RENDERER_WIN32_GDI)

static int run_win32_gdi(void) {
    fprintf(stderr, "CLAY_RENDERER_WIN32_GDI selected. Build on Windows with a Win32 entrypoint.\n");
    return 1;
}

#elif defined(CLAY_RENDERER_PLAYDATE)

static int run_playdate(void) {
    fprintf(stderr, "CLAY_RENDERER_PLAYDATE selected. Build with the Playdate SDK entrypoint.\n");
    return 1;
}

#else

#error "No CLAY_RENDERER_* backend defined. Set via xmake f --clay-backend=<backend>."

#endif

int cr_run_app(const CR_AppConfig *config) {
    if (!config || !config->render) {
        fprintf(stderr, "cr_run_app: missing app config or render callback\n");
        return 1;
    }

    g_app_config = config;

#if defined(CLAY_RENDERER_SDL3)
    return run_sdl3();
#elif defined(CLAY_RENDERER_SDL2)
    return run_sdl2();
#elif defined(CLAY_RENDERER_RAYLIB)
    return run_raylib();
#elif defined(CLAY_RENDERER_CAIRO)
    return run_xcb_cairo();
#elif defined(CLAY_RENDERER_XCB)
    return run_xcb();
#elif defined(CLAY_RENDERER_TERMINAL)
    return run_terminal();
#elif defined(CLAY_RENDERER_SOKOL)
    return run_sokol();
#elif defined(CLAY_RENDERER_WEB)
    return run_web();
#elif defined(CLAY_RENDERER_WIN32_GDI)
    return run_win32_gdi();
#elif defined(CLAY_RENDERER_PLAYDATE)
    return run_playdate();
#else
    return 1;
#endif
}
