#pragma once

#include <clay.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*CR_AppRenderFn)(void *user_data);
typedef void (*CR_AppViewportFn)(Clay_Dimensions dimensions, void *user_data);
typedef Clay_Color (*CR_AppBackgroundFn)(void *user_data);

typedef struct CR_AppConfig {
    const char *title;
    int width;
    int height;
    const char *font_path;
    int font_size;
    CR_AppRenderFn render;
    CR_AppViewportFn on_viewport;
    CR_AppBackgroundFn background;
    void *user_data;
} CR_AppConfig;

int cr_run_app(const CR_AppConfig *config);

#ifdef __cplusplus
}
#endif
