#include <clay.h>
#include <xcb/xcb.h>

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "third_party/stb_truetype.h"

typedef struct {
    int size_px;
    int bitmap_w;
    int bitmap_h;
    unsigned char *bitmap;
    stbtt_bakedchar baked[96];
} Clay_XCB_FontSize;

typedef struct {
    unsigned char *ttf_buffer;
    size_t ttf_size;
    stbtt_fontinfo info;
    Clay_XCB_FontSize *sizes;
    int size_count;
    int size_capacity;
} Clay_XCB_FontFamily;

typedef struct {
    Clay_XCB_FontFamily *families;
    int family_count;
} Clay_XCB_FontCollection;

typedef struct {
    int x;
    int y;
    int w;
    int h;
} Clay_XCB_Rect;

typedef struct {
    xcb_connection_t *connection;
    xcb_screen_t *screen;
    xcb_visualtype_t *visual;
    xcb_window_t window;
    xcb_gcontext_t gc;
    int width;
    int height;
    float scale;
    int depth;
    int stride;
    int bytes_per_pixel;
    int image_byte_order;
    bool swap_bytes;
    uint8_t *buffer;
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t red_max;
    uint32_t green_max;
    uint32_t blue_max;
    int red_shift;
    int green_shift;
    int blue_shift;
    Clay_XCB_FontCollection *fonts;
    Clay_XCB_Rect clip_stack[32];
    int clip_count;
} Clay_XCB_Renderer;

static float clay_xcb_scale(const Clay_XCB_Renderer *renderer) {
    if (!renderer || renderer->scale <= 0.0f) {
        return 1.0f;
    }
    return renderer->scale;
}

static Clay_BoundingBox clay_xcb_scale_bb(Clay_BoundingBox bb, float scale) {
    return (Clay_BoundingBox){
        bb.x * scale,
        bb.y * scale,
        bb.width * scale,
        bb.height * scale,
    };
}

static Clay_CornerRadius clay_xcb_scale_radius(Clay_CornerRadius radius, float scale) {
    radius.topLeft *= scale;
    radius.topRight *= scale;
    radius.bottomRight *= scale;
    radius.bottomLeft *= scale;
    return radius;
}

static Clay_BorderWidth clay_xcb_scale_border(Clay_BorderWidth width, float scale) {
    width.left *= scale;
    width.right *= scale;
    width.top *= scale;
    width.bottom *= scale;
    return width;
}

static bool clay_xcb_is_big_endian(void) {
    const uint16_t value = 0x0102;
    return *((const uint8_t *)&value) == 0x01;
}

static int clay_xcb_mask_shift(uint32_t mask) {
    int shift = 0;
    while (mask && ((mask & 1u) == 0u)) {
        mask >>= 1u;
        shift++;
    }
    return shift;
}

static int clay_xcb_mask_bits(uint32_t mask) {
    int bits = 0;
    while (mask) {
        bits += (int)(mask & 1u);
        mask >>= 1u;
    }
    return bits;
}

static uint32_t clay_xcb_mask_max(uint32_t mask) {
    int bits = clay_xcb_mask_bits(mask);
    if (bits <= 0) return 0;
    return (1u << bits) - 1u;
}

static const xcb_format_t *clay_xcb_find_format(const xcb_setup_t *setup, int depth) {
    xcb_format_iterator_t it = xcb_setup_pixmap_formats_iterator(setup);
    for (; it.rem; xcb_format_next(&it)) {
        if (it.data->depth == depth) {
            return it.data;
        }
    }
    return NULL;
}

static uint32_t clay_xcb_color_to_pixel(const Clay_XCB_Renderer *renderer, Clay_Color color) {
    uint32_t r = (renderer->red_max == 0) ? 0 :
        (uint32_t)((color.r * renderer->red_max + 127) / 255);
    uint32_t g = (renderer->green_max == 0) ? 0 :
        (uint32_t)((color.g * renderer->green_max + 127) / 255);
    uint32_t b = (renderer->blue_max == 0) ? 0 :
        (uint32_t)((color.b * renderer->blue_max + 127) / 255);

    uint32_t pixel = 0;
    pixel |= (r << renderer->red_shift) & renderer->red_mask;
    pixel |= (g << renderer->green_shift) & renderer->green_mask;
    pixel |= (b << renderer->blue_shift) & renderer->blue_mask;
    return pixel;
}

static uint32_t clay_xcb_swap_u32(uint32_t value) {
    return (value >> 24) |
           ((value >> 8) & 0x0000FF00u) |
           ((value << 8) & 0x00FF0000u) |
           (value << 24);
}

static uint32_t clay_xcb_read_pixel(const Clay_XCB_Renderer *renderer, int x, int y) {
    const uint8_t *ptr = renderer->buffer + (size_t)y * renderer->stride + (size_t)x * renderer->bytes_per_pixel;
    uint32_t value = 0;
    if (renderer->bytes_per_pixel == 4) {
        memcpy(&value, ptr, 4);
        if (renderer->swap_bytes) {
            value = clay_xcb_swap_u32(value);
        }
    } else if (renderer->bytes_per_pixel == 3) {
        if (renderer->image_byte_order == XCB_IMAGE_ORDER_LSB_FIRST) {
            value = (uint32_t)ptr[0] |
                    ((uint32_t)ptr[1] << 8) |
                    ((uint32_t)ptr[2] << 16);
        } else {
            value = (uint32_t)ptr[2] |
                    ((uint32_t)ptr[1] << 8) |
                    ((uint32_t)ptr[0] << 16);
        }
    }
    return value;
}

static void clay_xcb_write_pixel(const Clay_XCB_Renderer *renderer, int x, int y, uint32_t value) {
    uint8_t *ptr = renderer->buffer + (size_t)y * renderer->stride + (size_t)x * renderer->bytes_per_pixel;
    if (renderer->bytes_per_pixel == 4) {
        if (renderer->swap_bytes) {
            value = clay_xcb_swap_u32(value);
        }
        memcpy(ptr, &value, 4);
    } else if (renderer->bytes_per_pixel == 3) {
        if (renderer->image_byte_order == XCB_IMAGE_ORDER_LSB_FIRST) {
            ptr[0] = (uint8_t)(value & 0xFF);
            ptr[1] = (uint8_t)((value >> 8) & 0xFF);
            ptr[2] = (uint8_t)((value >> 16) & 0xFF);
        } else {
            ptr[0] = (uint8_t)((value >> 16) & 0xFF);
            ptr[1] = (uint8_t)((value >> 8) & 0xFF);
            ptr[2] = (uint8_t)(value & 0xFF);
        }
    }
}

static uint8_t clay_xcb_component_from_pixel(uint32_t pixel, uint32_t mask, int shift, uint32_t max_value) {
    if (mask == 0 || max_value == 0) return 0;
    uint32_t value = (pixel & mask) >> shift;
    return (uint8_t)((value * 255 + (max_value / 2)) / max_value);
}

static Clay_XCB_Rect clay_xcb_rect_intersect(Clay_XCB_Rect a, Clay_XCB_Rect b) {
    int x0 = (a.x > b.x) ? a.x : b.x;
    int y0 = (a.y > b.y) ? a.y : b.y;
    int x1 = ((a.x + a.w) < (b.x + b.w)) ? (a.x + a.w) : (b.x + b.w);
    int y1 = ((a.y + a.h) < (b.y + b.h)) ? (a.y + a.h) : (b.y + b.h);
    Clay_XCB_Rect out = { x0, y0, x1 - x0, y1 - y0 };
    if (out.w < 0) out.w = 0;
    if (out.h < 0) out.h = 0;
    return out;
}

static Clay_XCB_Rect clay_xcb_current_clip(const Clay_XCB_Renderer *renderer) {
    if (renderer->clip_count > 0) {
        return renderer->clip_stack[renderer->clip_count - 1];
    }
    Clay_XCB_Rect full = { 0, 0, renderer->width, renderer->height };
    return full;
}

static void clay_xcb_fill_span(Clay_XCB_Renderer *renderer, int y, int x0, int x1, uint32_t pixel) {
    if (y < 0 || y >= renderer->height) return;
    if (x0 < 0) x0 = 0;
    if (x1 > renderer->width) x1 = renderer->width;
    if (x1 <= x0) return;

    if (renderer->bytes_per_pixel == 4) {
        uint32_t store = renderer->swap_bytes ? clay_xcb_swap_u32(pixel) : pixel;
        uint32_t *row = (uint32_t *)(renderer->buffer + (size_t)y * renderer->stride);
        for (int x = x0; x < x1; x++) {
            row[x] = store;
        }
    } else if (renderer->bytes_per_pixel == 3) {
        uint8_t *row = renderer->buffer + (size_t)y * renderer->stride + (size_t)x0 * 3;
        if (renderer->image_byte_order == XCB_IMAGE_ORDER_LSB_FIRST) {
            uint8_t b0 = (uint8_t)(pixel & 0xFF);
            uint8_t b1 = (uint8_t)((pixel >> 8) & 0xFF);
            uint8_t b2 = (uint8_t)((pixel >> 16) & 0xFF);
            for (int x = x0; x < x1; x++) {
                row[0] = b0;
                row[1] = b1;
                row[2] = b2;
                row += 3;
            }
        } else {
            uint8_t b0 = (uint8_t)((pixel >> 16) & 0xFF);
            uint8_t b1 = (uint8_t)((pixel >> 8) & 0xFF);
            uint8_t b2 = (uint8_t)(pixel & 0xFF);
            for (int x = x0; x < x1; x++) {
                row[0] = b0;
                row[1] = b1;
                row[2] = b2;
                row += 3;
            }
        }
    } else {
        for (int x = x0; x < x1; x++) {
            clay_xcb_write_pixel(renderer, x, y, pixel);
        }
    }
}

static void clay_xcb_fill_rect_clipped(Clay_XCB_Renderer *renderer,
                                       int x0, int y0, int x1, int y1,
                                       uint32_t pixel) {
    Clay_XCB_Rect clip = clay_xcb_current_clip(renderer);
    if (x0 < clip.x) x0 = clip.x;
    if (y0 < clip.y) y0 = clip.y;
    if (x1 > clip.x + clip.w) x1 = clip.x + clip.w;
    if (y1 > clip.y + clip.h) y1 = clip.y + clip.h;
    if (x1 <= x0 || y1 <= y0) return;

    for (int y = y0; y < y1; y++) {
        clay_xcb_fill_span(renderer, y, x0, x1, pixel);
    }
}

static bool clay_xcb_point_in_rounded_rect(float px, float py,
                                           float x, float y, float w, float h,
                                           float tl, float tr, float br, float bl) {
    float right = x + w;
    float bottom = y + h;

    if (px < x || px >= right || py < y || py >= bottom) {
        return false;
    }

    if (tl > 0.0f && px < x + tl && py < y + tl) {
        float dx = (x + tl) - px;
        float dy = (y + tl) - py;
        return (dx * dx + dy * dy) <= (tl * tl);
    }
    if (tr > 0.0f && px > right - tr && py < y + tr) {
        float dx = px - (right - tr);
        float dy = (y + tr) - py;
        return (dx * dx + dy * dy) <= (tr * tr);
    }
    if (br > 0.0f && px > right - br && py > bottom - br) {
        float dx = px - (right - br);
        float dy = py - (bottom - br);
        return (dx * dx + dy * dy) <= (br * br);
    }
    if (bl > 0.0f && px < x + bl && py > bottom - bl) {
        float dx = (x + bl) - px;
        float dy = py - (bottom - bl);
        return (dx * dx + dy * dy) <= (bl * bl);
    }

    return true;
}

static void clay_xcb_draw_rounded_rect(Clay_XCB_Renderer *renderer,
                                       float x, float y, float w, float h,
                                       Clay_CornerRadius radius, Clay_Color color) {
    if (w <= 0.0f || h <= 0.0f) return;

    float max_radius = fminf(w, h) * 0.5f;
    float tl = fminf(radius.topLeft, max_radius);
    float tr = fminf(radius.topRight, max_radius);
    float br = fminf(radius.bottomRight, max_radius);
    float bl = fminf(radius.bottomLeft, max_radius);

    uint32_t pixel = clay_xcb_color_to_pixel(renderer, color);

    if (tl <= 0.0f && tr <= 0.0f && br <= 0.0f && bl <= 0.0f) {
        int x0 = (int)floorf(x);
        int y0 = (int)floorf(y);
        int x1 = (int)ceilf(x + w);
        int y1 = (int)ceilf(y + h);
        clay_xcb_fill_rect_clipped(renderer, x0, y0, x1, y1, pixel);
        return;
    }

    Clay_XCB_Rect clip = clay_xcb_current_clip(renderer);
    int y0 = (int)floorf(y);
    int y1 = (int)ceilf(y + h);
    if (y0 < clip.y) y0 = clip.y;
    if (y1 > clip.y + clip.h) y1 = clip.y + clip.h;

    float left = x;
    float right = x + w;
    float top = y;
    float bottom = y + h;

    for (int yy = y0; yy < y1; yy++) {
        float y_center = (float)yy + 0.5f;
        float row_left = left;
        float row_right = right;

        if (tl > 0.0f && y_center < top + tl) {
            float dy = (top + tl) - y_center;
            float dx = sqrtf(fmaxf(0.0f, tl * tl - dy * dy));
            row_left = left + tl - dx;
        } else if (bl > 0.0f && y_center > bottom - bl) {
            float dy = y_center - (bottom - bl);
            float dx = sqrtf(fmaxf(0.0f, bl * bl - dy * dy));
            row_left = left + bl - dx;
        }

        if (tr > 0.0f && y_center < top + tr) {
            float dy = (top + tr) - y_center;
            float dx = sqrtf(fmaxf(0.0f, tr * tr - dy * dy));
            row_right = right - tr + dx;
        } else if (br > 0.0f && y_center > bottom - br) {
            float dy = y_center - (bottom - br);
            float dx = sqrtf(fmaxf(0.0f, br * br - dy * dy));
            row_right = right - br + dx;
        }

        int x0 = (int)ceilf(row_left - 0.5f);
        int x1 = (int)floorf(row_right - 0.5f) + 1;
        if (x0 < clip.x) x0 = clip.x;
        if (x1 > clip.x + clip.w) x1 = clip.x + clip.w;
        if (x1 <= x0) continue;
        clay_xcb_fill_span(renderer, yy, x0, x1, pixel);
    }
}

static void clay_xcb_draw_rounded_border(Clay_XCB_Renderer *renderer,
                                         float x, float y, float w, float h,
                                         Clay_CornerRadius radius, Clay_BorderWidth width,
                                         Clay_Color color) {
    if (w <= 0.0f || h <= 0.0f) return;

    float max_radius = fminf(w, h) * 0.5f;
    float tl = fminf(radius.topLeft, max_radius);
    float tr = fminf(radius.topRight, max_radius);
    float br = fminf(radius.bottomRight, max_radius);
    float bl = fminf(radius.bottomLeft, max_radius);

    float inner_x = x + width.left;
    float inner_y = y + width.top;
    float inner_w = w - (width.left + width.right);
    float inner_h = h - (width.top + width.bottom);

    float inner_tl = fmaxf(0.0f, tl - fmaxf(width.left, width.top));
    float inner_tr = fmaxf(0.0f, tr - fmaxf(width.right, width.top));
    float inner_br = fmaxf(0.0f, br - fmaxf(width.right, width.bottom));
    float inner_bl = fmaxf(0.0f, bl - fmaxf(width.left, width.bottom));

    uint32_t pixel = clay_xcb_color_to_pixel(renderer, color);

    if (tl <= 0.0f && tr <= 0.0f && br <= 0.0f && bl <= 0.0f) {
        int x0 = (int)floorf(x);
        int y0 = (int)floorf(y);
        int x1 = (int)ceilf(x + w);
        int y1 = (int)ceilf(y + h);

        int top = (int)width.top;
        int bottom = (int)width.bottom;
        int left = (int)width.left;
        int right = (int)width.right;

        if (top > 0) {
            clay_xcb_fill_rect_clipped(renderer, x0, y0, x1, y0 + top, pixel);
        }
        if (bottom > 0) {
            clay_xcb_fill_rect_clipped(renderer, x0, y1 - bottom, x1, y1, pixel);
        }

        int middle_top = y0 + top;
        int middle_bottom = y1 - bottom;
        if (middle_bottom > middle_top) {
            if (left > 0) {
                clay_xcb_fill_rect_clipped(renderer, x0, middle_top, x0 + left, middle_bottom, pixel);
            }
            if (right > 0) {
                clay_xcb_fill_rect_clipped(renderer, x1 - right, middle_top, x1, middle_bottom, pixel);
            }
        }
        return;
    }

    Clay_XCB_Rect clip = clay_xcb_current_clip(renderer);

    int x0 = (int)floorf(x);
    int y0 = (int)floorf(y);
    int x1 = (int)ceilf(x + w);
    int y1 = (int)ceilf(y + h);

    x0 = (x0 > clip.x) ? x0 : clip.x;
    y0 = (y0 > clip.y) ? y0 : clip.y;
    x1 = (x1 < clip.x + clip.w) ? x1 : (clip.x + clip.w);
    y1 = (y1 < clip.y + clip.h) ? y1 : (clip.y + clip.h);

    if (x1 <= x0 || y1 <= y0) return;

    for (int yy = y0; yy < y1; yy++) {
        for (int xx = x0; xx < x1; xx++) {
            float px = (float)xx + 0.5f;
            float py = (float)yy + 0.5f;
            if (!clay_xcb_point_in_rounded_rect(px, py, x, y, w, h, tl, tr, br, bl)) {
                continue;
            }
            bool inside_inner = false;
            if (inner_w > 0.0f && inner_h > 0.0f) {
                inside_inner = clay_xcb_point_in_rounded_rect(px, py,
                    inner_x, inner_y, inner_w, inner_h,
                    inner_tl, inner_tr, inner_br, inner_bl);
            }
            if (!inside_inner) {
                clay_xcb_write_pixel(renderer, xx, yy, pixel);
            }
        }
    }
}

static bool clay_xcb_read_file(const char *path, unsigned char **data, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file) return false;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return false;
    }
    long length = ftell(file);
    if (length <= 0) {
        fclose(file);
        return false;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }

    unsigned char *buffer = (unsigned char *)malloc((size_t)length);
    if (!buffer) {
        fclose(file);
        return false;
    }
    if (fread(buffer, 1, (size_t)length, file) != (size_t)length) {
        free(buffer);
        fclose(file);
        return false;
    }
    fclose(file);

    *data = buffer;
    *size = (size_t)length;
    return true;
}

static Clay_XCB_FontSize *clay_xcb_get_font_size(Clay_XCB_FontFamily *family, int size_px) {
    if (!family->ttf_buffer) {
        return NULL;
    }
    for (int i = 0; i < family->size_count; i++) {
        if (family->sizes[i].size_px == size_px) {
            return &family->sizes[i];
        }
    }

    if (family->size_count == family->size_capacity) {
        int new_capacity = (family->size_capacity == 0) ? 4 : family->size_capacity * 2;
        Clay_XCB_FontSize *next = (Clay_XCB_FontSize *)realloc(family->sizes, sizeof(Clay_XCB_FontSize) * (size_t)new_capacity);
        if (!next) return NULL;
        family->sizes = next;
        family->size_capacity = new_capacity;
    }

    Clay_XCB_FontSize *slot = &family->sizes[family->size_count];
    memset(slot, 0, sizeof(*slot));
    slot->size_px = size_px;

    int bitmap_w = 512;
    int bitmap_h = 512;
    unsigned char *bitmap = NULL;
    int baked = 0;

    for (int attempt = 0; attempt < 2; attempt++) {
        bitmap = (unsigned char *)calloc((size_t)bitmap_w * (size_t)bitmap_h, 1);
        if (!bitmap) return NULL;
        baked = stbtt_BakeFontBitmap(
            family->ttf_buffer,
            0,
            (float)size_px,
            bitmap,
            bitmap_w,
            bitmap_h,
            32,
            96,
            slot->baked
        );
        if (baked > 0) {
            break;
        }
        free(bitmap);
        bitmap = NULL;
        bitmap_w = 1024;
        bitmap_h = 1024;
    }

    if (baked <= 0 || !bitmap) {
        free(bitmap);
        return NULL;
    }

    slot->bitmap = bitmap;
    slot->bitmap_w = bitmap_w;
    slot->bitmap_h = bitmap_h;

    family->size_count++;
    return slot;
}

static Clay_XCB_FontCollection *Clay_XCB_LoadFonts(const char **paths, int count) {
    if (!paths || count <= 0) return NULL;

    Clay_XCB_FontCollection *collection = (Clay_XCB_FontCollection *)calloc(1, sizeof(Clay_XCB_FontCollection));
    if (!collection) return NULL;

    collection->families = (Clay_XCB_FontFamily *)calloc((size_t)count, sizeof(Clay_XCB_FontFamily));
    if (!collection->families) {
        free(collection);
        return NULL;
    }
    collection->family_count = count;

    for (int i = 0; i < count; i++) {
        Clay_XCB_FontFamily *family = &collection->families[i];
        unsigned char *data = NULL;
        size_t size = 0;
        if (!clay_xcb_read_file(paths[i], &data, &size)) {
            fprintf(stderr, "Failed to read font file: %s\n", paths[i]);
            continue;
        }
        family->ttf_buffer = data;
        family->ttf_size = size;
        if (!stbtt_InitFont(&family->info, family->ttf_buffer, stbtt_GetFontOffsetForIndex(family->ttf_buffer, 0))) {
            fprintf(stderr, "Failed to parse font file: %s\n", paths[i]);
            free(family->ttf_buffer);
            family->ttf_buffer = NULL;
            family->ttf_size = 0;
        }
    }

    return collection;
}

static void Clay_XCB_FreeFonts(Clay_XCB_FontCollection *collection) {
    if (!collection) return;
    for (int i = 0; i < collection->family_count; i++) {
        Clay_XCB_FontFamily *family = &collection->families[i];
        for (int j = 0; j < family->size_count; j++) {
            free(family->sizes[j].bitmap);
        }
        free(family->sizes);
        free(family->ttf_buffer);
    }
    free(collection->families);
    free(collection);
}

static Clay_Dimensions Clay_XCB_MeasureText(Clay_StringSlice text, Clay_TextElementConfig *config, void *userData) {
    Clay_XCB_FontCollection *fonts = (Clay_XCB_FontCollection *)userData;
    if (!fonts || config->fontId >= (uint16_t)fonts->family_count) {
        return (Clay_Dimensions){ 0, 0 };
    }

    Clay_XCB_FontFamily *family = &fonts->families[config->fontId];
    Clay_XCB_FontSize *size = clay_xcb_get_font_size(family, (int)config->fontSize);
    if (!size) {
        return (Clay_Dimensions){ 0, 0 };
    }

    float x = 0.0f;
    float y = 0.0f;
    for (int32_t i = 0; i < text.length; i++) {
        unsigned char c = (unsigned char)text.chars[i];
        if (c < 32 || c >= 128) {
            c = '?';
        }
        stbtt_aligned_quad quad;
        stbtt_GetBakedQuad(size->baked, size->bitmap_w, size->bitmap_h, c - 32, &x, &y, &quad, 1);
        x += (float)config->letterSpacing;
    }

    float height = (config->lineHeight > 0) ? (float)config->lineHeight : (float)config->fontSize;
    return (Clay_Dimensions){ x, height };
}

static void clay_xcb_draw_text(Clay_XCB_Renderer *renderer, Clay_TextRenderData *config, Clay_BoundingBox bb) {
    Clay_XCB_FontCollection *fonts = renderer->fonts;
    if (!fonts || config->fontId >= (uint16_t)fonts->family_count) return;

    float scale = clay_xcb_scale(renderer);
    int size_px = (int)lroundf((float)config->fontSize * scale);
    if (size_px <= 0) return;

    Clay_XCB_FontFamily *family = &fonts->families[config->fontId];
    Clay_XCB_FontSize *size = clay_xcb_get_font_size(family, size_px);
    if (!size || !size->bitmap) return;

    Clay_XCB_Rect clip = clay_xcb_current_clip(renderer);
    uint32_t color_pixel = clay_xcb_color_to_pixel(renderer, config->textColor);

    float x = bb.x;
    float y = bb.y + bb.height;
    float letter_spacing = (float)config->letterSpacing * scale;

    for (int32_t i = 0; i < config->stringContents.length; i++) {
        unsigned char c = (unsigned char)config->stringContents.chars[i];
        if (c < 32 || c >= 128) {
            c = '?';
        }

        stbtt_aligned_quad quad;
        stbtt_GetBakedQuad(size->baked, size->bitmap_w, size->bitmap_h, c - 32, &x, &y, &quad, 1);

        int bmp_x0 = (int)(quad.s0 * (float)size->bitmap_w);
        int bmp_y0 = (int)(quad.t0 * (float)size->bitmap_h);
        int bmp_x1 = (int)(quad.s1 * (float)size->bitmap_w);
        int bmp_y1 = (int)(quad.t1 * (float)size->bitmap_h);

        int dst_x0 = (int)floorf(quad.x0);
        int dst_y0 = (int)floorf(quad.y0);
        int glyph_w = bmp_x1 - bmp_x0;
        int glyph_h = bmp_y1 - bmp_y0;

        if (glyph_w <= 0 || glyph_h <= 0) {
            x += letter_spacing;
            continue;
        }

        for (int yy = 0; yy < glyph_h; yy++) {
            int dst_y = dst_y0 + yy;
            if (dst_y < clip.y || dst_y >= clip.y + clip.h) continue;
            int bmp_y = bmp_y0 + yy;
            for (int xx = 0; xx < glyph_w; xx++) {
                int dst_x = dst_x0 + xx;
                if (dst_x < clip.x || dst_x >= clip.x + clip.w) continue;
                int bmp_x = bmp_x0 + xx;
                unsigned char alpha = size->bitmap[bmp_y * size->bitmap_w + bmp_x];
                if (alpha == 0) continue;
                if (alpha == 255) {
                    clay_xcb_write_pixel(renderer, dst_x, dst_y, color_pixel);
                } else {
                    uint32_t dst_pixel = clay_xcb_read_pixel(renderer, dst_x, dst_y);
                    uint8_t dr = clay_xcb_component_from_pixel(dst_pixel, renderer->red_mask, renderer->red_shift, renderer->red_max);
                    uint8_t dg = clay_xcb_component_from_pixel(dst_pixel, renderer->green_mask, renderer->green_shift, renderer->green_max);
                    uint8_t db = clay_xcb_component_from_pixel(dst_pixel, renderer->blue_mask, renderer->blue_shift, renderer->blue_max);

                    uint8_t sr = config->textColor.r;
                    uint8_t sg = config->textColor.g;
                    uint8_t sb = config->textColor.b;

                    uint8_t inv = (uint8_t)(255 - alpha);
                    uint8_t out_r = (uint8_t)((sr * alpha + dr * inv) / 255);
                    uint8_t out_g = (uint8_t)((sg * alpha + dg * inv) / 255);
                    uint8_t out_b = (uint8_t)((sb * alpha + db * inv) / 255);

                    uint32_t out_pixel = clay_xcb_color_to_pixel(renderer, (Clay_Color){ out_r, out_g, out_b, 255 });
                    clay_xcb_write_pixel(renderer, dst_x, dst_y, out_pixel);
                }
            }
        }

        x += letter_spacing;
    }
}

static bool Clay_XCB_Init(Clay_XCB_Renderer *renderer,
                          xcb_connection_t *connection,
                          xcb_screen_t *screen,
                          xcb_visualtype_t *visual,
                          xcb_window_t window,
                          int width,
                          int height) {
    if (!renderer || !connection || !screen || !visual) return false;

    memset(renderer, 0, sizeof(*renderer));
    renderer->connection = connection;
    renderer->screen = screen;
    renderer->visual = visual;
    renderer->window = window;
    renderer->width = width;
    renderer->height = height;
    renderer->scale = 1.0f;
    renderer->depth = screen->root_depth;

    const xcb_setup_t *setup = xcb_get_setup(connection);
    const xcb_format_t *format = clay_xcb_find_format(setup, renderer->depth);
    if (!format) {
        fprintf(stderr, "Failed to find pixmap format for depth %d\n", renderer->depth);
        return false;
    }

    renderer->bytes_per_pixel = (int)(format->bits_per_pixel / 8);
    if (renderer->bytes_per_pixel <= 0) {
        fprintf(stderr, "Unsupported bits per pixel: %u\n", format->bits_per_pixel);
        return false;
    }

    int pad = format->scanline_pad;
    renderer->stride = ((renderer->width * (int)format->bits_per_pixel + pad - 1) & ~(pad - 1)) / 8;

    renderer->buffer = (uint8_t *)calloc((size_t)renderer->stride * (size_t)renderer->height, 1);
    if (!renderer->buffer) return false;

    renderer->image_byte_order = setup->image_byte_order;
    renderer->swap_bytes = (setup->image_byte_order == XCB_IMAGE_ORDER_MSB_FIRST) != clay_xcb_is_big_endian();

    renderer->red_mask = visual->red_mask;
    renderer->green_mask = visual->green_mask;
    renderer->blue_mask = visual->blue_mask;

    renderer->red_shift = clay_xcb_mask_shift(renderer->red_mask);
    renderer->green_shift = clay_xcb_mask_shift(renderer->green_mask);
    renderer->blue_shift = clay_xcb_mask_shift(renderer->blue_mask);

    renderer->red_max = clay_xcb_mask_max(renderer->red_mask);
    renderer->green_max = clay_xcb_mask_max(renderer->green_mask);
    renderer->blue_max = clay_xcb_mask_max(renderer->blue_mask);

    renderer->gc = xcb_generate_id(connection);
    uint32_t values[] = { screen->black_pixel, screen->white_pixel };
    xcb_create_gc(connection, renderer->gc, window, XCB_GC_FOREGROUND | XCB_GC_BACKGROUND, values);

    return true;
}

static void Clay_XCB_Shutdown(Clay_XCB_Renderer *renderer) {
    if (!renderer) return;
    if (renderer->gc) {
        xcb_free_gc(renderer->connection, renderer->gc);
    }
    free(renderer->buffer);
    renderer->buffer = NULL;
}

static bool Clay_XCB_Resize(Clay_XCB_Renderer *renderer, int width, int height) {
    if (!renderer || width <= 0 || height <= 0) return false;

    renderer->width = width;
    renderer->height = height;

    const xcb_setup_t *setup = xcb_get_setup(renderer->connection);
    const xcb_format_t *format = clay_xcb_find_format(setup, renderer->depth);
    if (!format) return false;

    int pad = format->scanline_pad;
    renderer->stride = ((renderer->width * (int)format->bits_per_pixel + pad - 1) & ~(pad - 1)) / 8;

    free(renderer->buffer);
    renderer->buffer = (uint8_t *)calloc((size_t)renderer->stride * (size_t)renderer->height, 1);
    return renderer->buffer != NULL;
}

static void Clay_XCB_Clear(Clay_XCB_Renderer *renderer, Clay_Color color) {
    if (!renderer || !renderer->buffer) return;

    uint32_t pixel = clay_xcb_color_to_pixel(renderer, color);
    for (int y = 0; y < renderer->height; y++) {
        clay_xcb_fill_span(renderer, y, 0, renderer->width, pixel);
    }
}

static void Clay_XCB_Present(Clay_XCB_Renderer *renderer) {
    if (!renderer || !renderer->buffer) return;
    size_t data_len = (size_t)renderer->stride * (size_t)renderer->height;
    xcb_put_image(
        renderer->connection,
        XCB_IMAGE_FORMAT_Z_PIXMAP,
        renderer->window,
        renderer->gc,
        (uint16_t)renderer->width,
        (uint16_t)renderer->height,
        0, 0,
        0,
        (uint8_t)renderer->depth,
        (uint32_t)data_len,
        renderer->buffer
    );
    xcb_flush(renderer->connection);
}

static void Clay_XCB_Render(Clay_XCB_Renderer *renderer, Clay_RenderCommandArray commands) {
    if (!renderer) return;
    renderer->clip_count = 0;
    float scale = clay_xcb_scale(renderer);

    for (int32_t i = 0; i < commands.length; i++) {
        Clay_RenderCommand *command = Clay_RenderCommandArray_Get(&commands, i);
        if (!command) continue;

        switch (command->commandType) {
            case CLAY_RENDER_COMMAND_TYPE_RECTANGLE: {
                Clay_RectangleRenderData *config = &command->renderData.rectangle;
                Clay_BoundingBox bb = clay_xcb_scale_bb(command->boundingBox, scale);
                Clay_CornerRadius radius = clay_xcb_scale_radius(config->cornerRadius, scale);
                clay_xcb_draw_rounded_rect(renderer, bb.x, bb.y, bb.width, bb.height, radius, config->backgroundColor);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_BORDER: {
                Clay_BorderRenderData *config = &command->renderData.border;
                Clay_BoundingBox bb = clay_xcb_scale_bb(command->boundingBox, scale);
                Clay_CornerRadius radius = clay_xcb_scale_radius(config->cornerRadius, scale);
                Clay_BorderWidth width = clay_xcb_scale_border(config->width, scale);
                clay_xcb_draw_rounded_border(renderer, bb.x, bb.y, bb.width, bb.height, radius, width, config->color);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_TEXT: {
                Clay_TextRenderData *config = &command->renderData.text;
                Clay_BoundingBox bb = clay_xcb_scale_bb(command->boundingBox, scale);
                clay_xcb_draw_text(renderer, config, bb);
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_START: {
                Clay_BoundingBox bb = clay_xcb_scale_bb(command->boundingBox, scale);
                Clay_XCB_Rect next = {
                    (int)floorf(bb.x),
                    (int)floorf(bb.y),
                    (int)ceilf(bb.width),
                    (int)ceilf(bb.height)
                };
                Clay_XCB_Rect current = clay_xcb_current_clip(renderer);
                Clay_XCB_Rect clipped = clay_xcb_rect_intersect(current, next);
                if (renderer->clip_count < (int)(sizeof(renderer->clip_stack) / sizeof(renderer->clip_stack[0]))) {
                    renderer->clip_stack[renderer->clip_count++] = clipped;
                }
                break;
            }
            case CLAY_RENDER_COMMAND_TYPE_SCISSOR_END:
                if (renderer->clip_count > 0) {
                    renderer->clip_count--;
                }
                break;
            case CLAY_RENDER_COMMAND_TYPE_IMAGE:
            case CLAY_RENDER_COMMAND_TYPE_CUSTOM:
            case CLAY_RENDER_COMMAND_TYPE_NONE:
            default:
                break;
        }
    }
}
