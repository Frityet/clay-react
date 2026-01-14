/**
 * Todo App demo - backend-agnostic UI
 */
#pragma once

#include <clay.h>
#include "clay_react/clay_react.h"

typedef struct {
    Clay_Color background;
    Clay_Color surface;
    Clay_Color surface_alt;
    Clay_Color text;
    Clay_Color text_muted;
    Clay_Color accent;
    Clay_Color accent_soft;
    Clay_Color danger;
    Clay_Color success;
    Clay_Color warning;
} TodoTheme;

const TodoTheme *TodoAppTheme(void);
void TodoApp(void);
