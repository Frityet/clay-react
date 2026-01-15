#include <clay.h>
#include "clay_react/app.h"
#include "clay_react/clay_react.h"
#include "demo/todo_app.h"

static Clay_Dimensions g_layout_dimensions = { 1024.0f, 768.0f };

static void todo_set_viewport(Clay_Dimensions dimensions, void *user_data) {
    (void)user_data;
    g_layout_dimensions = dimensions;
    TodoAppSetViewport(dimensions);
}

static Clay_Color todo_background(void *user_data) {
    (void)user_data;
    return TodoAppTheme()->background;
}

static void todo_render(void *user_data) {
    (void)user_data;
    const TodoTheme *theme = TodoAppTheme();
    float width = g_layout_dimensions.width;
    float height = g_layout_dimensions.height;
    float min_side = width < height ? width : height;
    float padding = min_side * 0.05f;
    if (padding < 16.0f) padding = 16.0f;
    if (padding > 40.0f) padding = 40.0f;

    Center((BoxParams){
        .style = {
            .layout = {
                .sizing = $fill(),
                .padding = $pad((uint16_t)(padding + 0.5f)),
            },
            .background = theme->background,
        },
    }, ^{ TodoApp(); });
}

int main(void) {
    CR_AppConfig config = {
        .title = "Todo App - Clay React",
        .width = 1024,
        .height = 768,
        .font_path = "resources/Roboto-Regular.ttf",
        .font_size = 24,
        .render = todo_render,
        .on_viewport = todo_set_viewport,
        .background = todo_background,
        .user_data = NULL,
    };

    return cr_run_app(&config);
}
