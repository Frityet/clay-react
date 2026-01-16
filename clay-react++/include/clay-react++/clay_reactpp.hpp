#pragma once

#include <clay.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace clay::reactpp {

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    static constexpr Color rgb(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
        return { red, green, blue, 255 };
    }

    static constexpr Color rgba(std::uint8_t red, std::uint8_t green, std::uint8_t blue, std::uint8_t alpha) {
        return { red, green, blue, alpha };
    }

    constexpr Color with_alpha(std::uint8_t alpha) const {
        return { r, g, b, alpha };
    }

    constexpr Clay_Color to_clay() const {
        return Clay_Color{
            static_cast<float>(r),
            static_cast<float>(g),
            static_cast<float>(b),
            static_cast<float>(a),
        };
    }
};

struct Padding {
    std::uint16_t left = 0;
    std::uint16_t right = 0;
    std::uint16_t top = 0;
    std::uint16_t bottom = 0;

    static constexpr Padding all(std::uint16_t value) {
        return { value, value, value, value };
    }

    static constexpr Padding xy(std::uint16_t x, std::uint16_t y) {
        return { x, x, y, y };
    }

    constexpr Clay_Padding to_clay() const {
        return Clay_Padding{ left, right, top, bottom };
    }
};

struct CornerRadius {
    float top_left = 0.0f;
    float top_right = 0.0f;
    float bottom_left = 0.0f;
    float bottom_right = 0.0f;

    static constexpr CornerRadius all(float radius) {
        return { radius, radius, radius, radius };
    }

    constexpr Clay_CornerRadius to_clay() const {
        return Clay_CornerRadius{ top_left, top_right, bottom_left, bottom_right };
    }
};

struct Border {
    std::uint16_t left = 0;
    std::uint16_t right = 0;
    std::uint16_t top = 0;
    std::uint16_t bottom = 0;
    std::uint16_t between = 0;
    Color color = {};

    static constexpr Border outside(std::uint16_t width, Color color) {
        return { width, width, width, width, 0, color };
    }

    constexpr Clay_BorderElementConfig to_clay() const {
        return Clay_BorderElementConfig{
            color.to_clay(),
            Clay_BorderWidth{ left, right, top, bottom, between },
        };
    }
};

struct Sizing {
    Clay_SizingAxis width = CLAY_SIZING_GROW(0.0f, 0.0f);
    Clay_SizingAxis height = CLAY_SIZING_GROW(0.0f, 0.0f);

    static Sizing fill() {
        return { CLAY_SIZING_GROW(0.0f, 0.0f), CLAY_SIZING_GROW(0.0f, 0.0f) };
    }

    static Sizing fixed(float width_value, float height_value) {
        return { CLAY_SIZING_FIXED(width_value), CLAY_SIZING_FIXED(height_value) };
    }

    static Sizing fit(float min_width, float max_width, float min_height, float max_height) {
        return {
            CLAY_SIZING_FIT(min_width, max_width),
            CLAY_SIZING_FIT(min_height, max_height),
        };
    }

    static Sizing percent(float width_value, float height_value) {
        return { CLAY_SIZING_PERCENT(width_value), CLAY_SIZING_PERCENT(height_value) };
    }
};

struct Layout {
    Clay_LayoutConfig value;

    Layout();

    static Layout row(std::uint16_t gap = 0);
    static Layout column(std::uint16_t gap = 0);

    Layout &padding(Padding padding_value);
    Layout &gap(std::uint16_t gap_value);
    Layout &align(Clay_LayoutAlignmentX x, Clay_LayoutAlignmentY y);
    Layout &sizing(Sizing sizing_value);
};

struct TextStyle {
    std::uint16_t font_id = 0;
    std::uint16_t font_size = 16;
    std::uint16_t line_height = 0;
    std::uint16_t letter_spacing = 0;
    Clay_TextElementConfigWrapMode wrap = CLAY_TEXT_WRAP_WORDS;
    Clay_TextAlignment alignment = CLAY_TEXT_ALIGN_LEFT;
    Color color = Color::rgb(30, 35, 45);
};

struct BoxStyle {
    Layout layout = {};
    std::optional<Color> background;
    std::optional<Color> hover_background;
    std::optional<Border> border;
    std::optional<CornerRadius> corner_radius;
};

struct ButtonStyle {
    BoxStyle box = {};
    TextStyle text = {};
};

struct TextInputStyle {
    BoxStyle box = {};
    TextStyle text = {};
    TextStyle placeholder = {};
    std::string_view placeholder_text;
};

struct TextInput {
    std::string text;
    bool focused = false;
};

struct TextInputResult {
    bool changed = false;
    bool submitted = false;
};

struct Id {
    std::string_view name;
    std::optional<std::uint32_t> index;
};

struct InputState {
    Clay_Vector2 pointer_position = { 0.0f, 0.0f };
    bool pointer_down = false;
    bool pointer_pressed = false;
    bool pointer_released = false;
    std::string text_input;
    bool key_backspace = false;
    bool key_enter = false;
    Clay_ElementId active_id = {};
    Clay_ElementId focused_id = {};
};

class UI {
public:
    explicit UI(InputState &input);

    void begin_frame();
    Clay_RenderCommandArray end_frame();

    void text(std::string_view text, const TextStyle &style = {});
    void box(const BoxStyle &style, std::move_only_function<void()> children = {});
    void row(const BoxStyle &style, std::move_only_function<void()> children = {});
    void column(const BoxStyle &style, std::move_only_function<void()> children = {});
    bool button(const Id &id, std::string_view label, const ButtonStyle &style = {});
    TextInputResult text_input(const Id &id, TextInput &state, const TextInputStyle &style = {});

    InputState &input();
    const InputState &input() const;

private:
    InputState &input_;
    bool focus_claimed_ = false;
    std::vector<std::string> scratch_strings_;
};

struct AppConfig {
    std::string title = "clay-react++";
    int width = 1024;
    int height = 768;
    std::string font_path = "resources/Roboto-Regular.ttf";
    int font_size = 24;
    std::move_only_function<void(UI &)> render;
    std::move_only_function<Color()> background;
    std::move_only_function<void(Clay_Dimensions)> on_viewport;
};

int run_app(AppConfig config);

} // namespace clay::reactpp
