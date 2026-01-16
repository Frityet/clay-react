#include "clay-react++/clay_reactpp.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <utility>

#if defined(CLAY_RENDERER_SDL3)
#include "clay-react++/backend_sdl3.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>
#include <SDL3_ttf/SDL_ttf.h>
#endif

namespace clay::reactpp {

namespace {

Clay_String to_clay_string(std::string_view text) {
    return Clay_String{
        false,
        static_cast<int32_t>(text.size()),
        text.data(),
    };
}

Clay_TextElementConfig to_text_config(const TextStyle &style) {
    Clay_TextElementConfig config = {};
    config.fontId = style.font_id;
    config.fontSize = style.font_size;
    config.lineHeight = style.line_height;
    config.letterSpacing = style.letter_spacing;
    config.wrapMode = style.wrap;
    config.textAlignment = style.alignment;
    config.textColor = style.color.to_clay();
    return config;
}

Clay_ElementDeclaration to_declaration(const BoxStyle &style, bool hovered, Clay_ElementId element_id) {
    Clay_ElementDeclaration decl = {};
    decl.id = element_id;
    decl.layout = style.layout.value;

    if (style.background || style.hover_background) {
        Color background = style.background.value_or(Color::rgba(0, 0, 0, 0));
        if (hovered && style.hover_background) {
            background = *style.hover_background;
        }
        decl.backgroundColor = background.to_clay();
    }

    if (style.border) {
        decl.border = style.border->to_clay();
    }

    if (style.corner_radius) {
        decl.cornerRadius = style.corner_radius->to_clay();
    }

    return decl;
}

bool same_id(Clay_ElementId left, Clay_ElementId right) {
    return left.id != 0 && left.id == right.id;
}

} // namespace

Layout::Layout() : value(CLAY_LAYOUT_DEFAULT) {}

Layout Layout::row(std::uint16_t gap) {
    Layout layout;
    layout.value.layoutDirection = CLAY_LEFT_TO_RIGHT;
    layout.value.childGap = gap;
    return layout;
}

Layout Layout::column(std::uint16_t gap) {
    Layout layout;
    layout.value.layoutDirection = CLAY_TOP_TO_BOTTOM;
    layout.value.childGap = gap;
    return layout;
}

Layout &Layout::padding(Padding padding_value) {
    value.padding = padding_value.to_clay();
    return *this;
}

Layout &Layout::gap(std::uint16_t gap_value) {
    value.childGap = gap_value;
    return *this;
}

Layout &Layout::align(Clay_LayoutAlignmentX x, Clay_LayoutAlignmentY y) {
    value.childAlignment = Clay_ChildAlignment{ x, y };
    return *this;
}

Layout &Layout::sizing(Sizing sizing_value) {
    value.sizing = Clay_Sizing{ sizing_value.width, sizing_value.height };
    return *this;
}

UI::UI(InputState &input) : input_(input) {}

void UI::begin_frame() {
    focus_claimed_ = false;
    scratch_strings_.clear();
    Clay_BeginLayout();
}

Clay_RenderCommandArray UI::end_frame() {
    Clay_RenderCommandArray commands = Clay_EndLayout();

    if (input_.pointer_pressed && !focus_claimed_) {
        input_.focused_id = {};
    }

    if (input_.pointer_released) {
        input_.active_id = {};
    }

    return commands;
}

void UI::text(std::string_view text, const TextStyle &style) {
    scratch_strings_.emplace_back(text);
    const std::string &stored = scratch_strings_.back();
    Clay_TextElementConfig config = to_text_config(style);
    Clay__OpenTextElement(to_clay_string(stored), CLAY_TEXT_CONFIG(config));
    Clay__CloseElement();
}

void UI::box(const BoxStyle &style, std::move_only_function<void()> children) {
    Clay_ElementDeclaration decl = to_declaration(style, false, {});
    Clay__OpenElement();
    Clay__ConfigureOpenElement(decl);
    if (children) {
        children();
    }
    Clay__CloseElement();
}

void UI::row(const BoxStyle &style, std::move_only_function<void()> children) {
    BoxStyle row_style = style;
    row_style.layout.value.layoutDirection = CLAY_LEFT_TO_RIGHT;
    box(row_style, std::move(children));
}

void UI::column(const BoxStyle &style, std::move_only_function<void()> children) {
    BoxStyle column_style = style;
    column_style.layout.value.layoutDirection = CLAY_TOP_TO_BOTTOM;
    box(column_style, std::move(children));
}

bool UI::button(const Id &id, std::string_view label, const ButtonStyle &style) {
    Clay_ElementId element_id = {};
    if (!id.name.empty()) {
        Clay_String name = to_clay_string(id.name);
        element_id = id.index ? Clay_GetElementIdWithIndex(name, *id.index) : Clay_GetElementId(name);
    }
    bool hovered = element_id.id != 0 && Clay_PointerOver(element_id);

    if (input_.pointer_pressed && hovered) {
        input_.active_id = element_id;
    }

    bool clicked = false;
    if (input_.pointer_released && hovered && same_id(input_.active_id, element_id)) {
        clicked = true;
    }

    Clay_ElementDeclaration decl = to_declaration(style.box, hovered, element_id);
    Clay__OpenElement();
    Clay__ConfigureOpenElement(decl);
    if (!label.empty()) {
        text(label, style.text);
    }
    Clay__CloseElement();

    return clicked;
}

TextInputResult UI::text_input(const Id &id, TextInput &state, const TextInputStyle &style) {
    TextInputResult result = {};

    Clay_ElementId element_id = {};
    if (!id.name.empty()) {
        Clay_String name = to_clay_string(id.name);
        element_id = id.index ? Clay_GetElementIdWithIndex(name, *id.index) : Clay_GetElementId(name);
    }
    bool hovered = element_id.id != 0 && Clay_PointerOver(element_id);

    if (input_.pointer_pressed && hovered) {
        input_.focused_id = element_id;
        focus_claimed_ = true;
    }

    bool focused = element_id.id != 0 && same_id(input_.focused_id, element_id);
    state.focused = focused;

    if (focused) {
        if (!input_.text_input.empty()) {
            state.text.append(input_.text_input);
            result.changed = true;
        }
        if (input_.key_backspace && !state.text.empty()) {
            state.text.pop_back();
            result.changed = true;
        }
        if (input_.key_enter) {
            result.submitted = true;
        }
    }

    std::string display;
    const TextStyle *display_style = &style.text;
    if (state.text.empty() && !focused && !style.placeholder_text.empty()) {
        display.assign(style.placeholder_text);
        display_style = &style.placeholder;
    } else {
        display = state.text;
        if (focused) {
            display.push_back('|');
        }
    }

    Clay_ElementDeclaration decl = to_declaration(style.box, hovered, element_id);
    Clay__OpenElement();
    Clay__ConfigureOpenElement(decl);
    text(display, *display_style);
    Clay__CloseElement();

    return result;
}

InputState &UI::input() {
    return input_;
}

const InputState &UI::input() const {
    return input_;
}

#if defined(CLAY_RENDERER_SDL3)

namespace {

struct SdlFree {
    void operator()(void *ptr) const {
        SDL_free(ptr);
    }
};

struct WindowDeleter {
    void operator()(SDL_Window *window) const {
        if (window) {
            SDL_DestroyWindow(window);
        }
    }
};

struct RendererDeleter {
    void operator()(SDL_Renderer *renderer) const {
        if (renderer) {
            SDL_DestroyRenderer(renderer);
        }
    }
};

struct TextEngineDeleter {
    void operator()(TTF_TextEngine *engine) const {
        if (engine) {
            TTF_DestroyRendererTextEngine(engine);
        }
    }
};

struct FontDeleter {
    void operator()(TTF_Font *font) const {
        if (font) {
            TTF_CloseFont(font);
        }
    }
};

struct RendererHandleDeleter {
    void operator()(CRPP_SDL3Renderer *renderer) const {
        crpp_sdl3_renderer_destroy(renderer);
    }
};

struct SdlContext {
    bool ok = false;
    SdlContext() { ok = SDL_Init(SDL_INIT_VIDEO) == 0; }
    ~SdlContext() {
        if (ok) {
            SDL_Quit();
        }
    }
};

struct TtfContext {
    bool ok = false;
    TtfContext() { ok = TTF_Init(); }
    ~TtfContext() {
        if (ok) {
            TTF_Quit();
        }
    }
};

Clay_Dimensions measure_text(Clay_StringSlice text, Clay_TextElementConfig *config, void *user_data) {
    auto *fonts = static_cast<TTF_Font **>(user_data);
    if (!fonts || !config) {
        return Clay_Dimensions{ 0.0f, 0.0f };
    }

    TTF_Font *font = fonts[config->fontId];
    if (!font) {
        return Clay_Dimensions{ 0.0f, 0.0f };
    }

    if (config->fontSize > 0) {
        TTF_SetFontSize(font, config->fontSize);
    }

    int width = 0;
    int height = 0;
    TTF_GetStringSize(font, text.chars, text.length, &width, &height);
    return Clay_Dimensions{ static_cast<float>(width), static_cast<float>(height) };
}

void handle_errors(Clay_ErrorData error) {
    std::string_view message(error.errorText.chars, static_cast<size_t>(error.errorText.length));
    std::cerr << "[clay-react++] " << message << "\n";
}

Color default_background() {
    return Color::rgb(244, 246, 249);
}

} // namespace

int run_app(AppConfig config) {
    if (!config.render) {
        std::cerr << "[clay-react++] render callback is required\n";
        return 1;
    }

    SdlContext sdl;
    if (!sdl.ok) {
        std::cerr << "[clay-react++] SDL_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    TtfContext ttf;
    if (!ttf.ok) {
        std::cerr << "[clay-react++] TTF_Init failed: " << SDL_GetError() << "\n";
        return 1;
    }

    SDL_Window *raw_window = nullptr;
    SDL_Renderer *raw_renderer = nullptr;
    if (!SDL_CreateWindowAndRenderer(config.title.c_str(), config.width, config.height,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY, &raw_window, &raw_renderer)) {
        std::cerr << "[clay-react++] SDL_CreateWindowAndRenderer failed: " << SDL_GetError() << "\n";
        return 1;
    }

    std::unique_ptr<SDL_Window, WindowDeleter> window(raw_window);
    std::unique_ptr<SDL_Renderer, RendererDeleter> renderer(raw_renderer);

    std::unique_ptr<TTF_TextEngine, TextEngineDeleter> text_engine(
        TTF_CreateRendererTextEngine(renderer.get())
    );
    if (!text_engine) {
        std::cerr << "[clay-react++] TTF_CreateRendererTextEngine failed: " << SDL_GetError() << "\n";
        return 1;
    }

    std::unique_ptr<TTF_Font, FontDeleter> font(
        TTF_OpenFont(config.font_path.c_str(), config.font_size)
    );
    if (!font) {
        std::cerr << "[clay-react++] Failed to load font: " << SDL_GetError() << "\n";
        return 1;
    }

    std::array<TTF_Font *, 1> fonts = { font.get() };

    std::unique_ptr<CRPP_SDL3Renderer, RendererHandleDeleter> renderer_handle(
        crpp_sdl3_renderer_create(renderer.get(), text_engine.get(), fonts.data())
    );
    if (!renderer_handle) {
        std::cerr << "[clay-react++] Failed to create renderer handle\n";
        return 1;
    }

    const std::uint64_t memory_size = Clay_MinMemorySize();
    std::unique_ptr<void, SdlFree> arena_memory(SDL_calloc(1, memory_size));
    if (!arena_memory) {
        std::cerr << "[clay-react++] Failed to allocate Clay arena\n";
        return 1;
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSize(window.get(), &width, &height);

    Clay_Arena arena = {};
    arena.memory = arena_memory.get();
    arena.capacity = memory_size;

    Clay_Initialize(arena, Clay_Dimensions{ static_cast<float>(width), static_cast<float>(height) },
        Clay_ErrorHandler{ handle_errors, nullptr });
    Clay_SetLayoutDimensions(Clay_Dimensions{ static_cast<float>(width), static_cast<float>(height) });
    Clay_SetMeasureTextFunction(measure_text, fonts.data());

    if (config.on_viewport) {
        config.on_viewport(Clay_Dimensions{ static_cast<float>(width), static_cast<float>(height) });
    }

    InputState input;
    UI ui(input);

    SDL_StartTextInput(window.get());

    bool running = true;
    auto last_tick = std::chrono::steady_clock::now();

    while (running) {
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<float> frame_delta = now - last_tick;
        last_tick = now;
        float dt = frame_delta.count();
        if (dt <= 0.0f) {
            dt = 0.016f;
        }

        input.pointer_pressed = false;
        input.pointer_released = false;
        input.text_input.clear();
        input.key_backspace = false;
        input.key_enter = false;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;

                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: {
                    Clay_Dimensions dims = {
                        static_cast<float>(event.window.data1),
                        static_cast<float>(event.window.data2),
                    };
                    Clay_SetLayoutDimensions(dims);
                    if (config.on_viewport) {
                        config.on_viewport(dims);
                    }
                    break;
                }

                case SDL_EVENT_MOUSE_MOTION:
                    input.pointer_position = Clay_Vector2{ event.motion.x, event.motion.y };
                    input.pointer_down = (event.motion.state & SDL_BUTTON_LMASK) != 0;
                    Clay_SetPointerState(input.pointer_position, input.pointer_down);
                    break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        input.pointer_down = true;
                        input.pointer_pressed = true;
                        input.pointer_position = Clay_Vector2{ event.button.x, event.button.y };
                        Clay_SetPointerState(input.pointer_position, true);
                    }
                    break;

                case SDL_EVENT_MOUSE_BUTTON_UP:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        input.pointer_down = false;
                        input.pointer_released = true;
                        input.pointer_position = Clay_Vector2{ event.button.x, event.button.y };
                        Clay_SetPointerState(input.pointer_position, false);
                    }
                    break;

                case SDL_EVENT_MOUSE_WHEEL:
                    Clay_UpdateScrollContainers(true,
                        Clay_Vector2{ event.wheel.x * 30.0f, event.wheel.y * 30.0f },
                        dt);
                    break;

                case SDL_EVENT_TEXT_INPUT:
                    input.text_input.append(event.text.text);
                    break;

                case SDL_EVENT_KEY_DOWN:
                    if (event.key.key == SDLK_BACKSPACE) {
                        input.key_backspace = true;
                    }
                    if (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER) {
                        input.key_enter = true;
                    }
                    break;
            }
        }

        ui.begin_frame();
        config.render(ui);
        Clay_RenderCommandArray commands = ui.end_frame();

        Color background = config.background ? config.background() : default_background();
        SDL_SetRenderDrawColor(renderer.get(), background.r, background.g, background.b, background.a);
        SDL_RenderClear(renderer.get());
        crpp_sdl3_renderer_render(renderer_handle.get(), &commands);
        SDL_RenderPresent(renderer.get());
    }

    SDL_StopTextInput(window.get());
    return 0;
}

#else

int run_app(AppConfig) {
    std::cerr << "[clay-react++] Only the SDL3 backend is currently supported.\n";
    return 1;
}

#endif

} // namespace clay::reactpp
