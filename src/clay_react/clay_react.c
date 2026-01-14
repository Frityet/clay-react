/**
 * Clay React - Implementation
 *
 * A modern, idiomatic C framework for building reactive UIs with Clay.
 * Requires: Clang with blocks support (-fblocks -lBlocksRuntime)
 */

#include "clay_react.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// ============================================================================
// GLOBAL STATE
// ============================================================================

CR_Runtime *cr_runtime = NULL;
CR_TextInputState *_cr_focused_input = NULL;

static uint64_t _cr_next_context_id = 1;

// ============================================================================
// MEMORY HELPERS
// ============================================================================

void *_cr_alloc(size_t size) {
    void *ptr = calloc(1, size);
    if (ptr && cr_runtime) {
        cr_runtime->allocated += size;
        if (cr_runtime->allocated > cr_runtime->peak_allocated) {
            cr_runtime->peak_allocated = cr_runtime->allocated;
        }
    }
    return ptr;
}

void _cr_free(void *ptr, size_t size) {
    if (ptr) {
        if (cr_runtime) {
            cr_runtime->allocated -= size;
        }
        free(ptr);
    }
}

// ============================================================================
// INITIALIZATION
// ============================================================================

void cr_init(void) {
    if (cr_runtime != NULL) {
        return; // Already initialized
    }

    cr_runtime = calloc(1, sizeof(CR_Runtime));
    if (!cr_runtime) {
        fprintf(stderr, "Clay React: Failed to allocate runtime\n");
        return;
    }

    cr_runtime->current_component = NULL;
    cr_runtime->root = NULL;
    cr_runtime->context_stack = NULL;
    cr_runtime->is_rendering = false;
    cr_runtime->frame = 0;
    cr_runtime->click_handlers = NULL;
    cr_runtime->click_handler_count = 0;
    cr_runtime->click_handler_capacity = 0;
}

void cr_shutdown(void) {
    if (!cr_runtime) {
        return;
    }

    // Free click handlers
    _cr_clear_handlers();
    if (cr_runtime->click_handlers) {
        free(cr_runtime->click_handlers);
    }

    // Free context stack
    while (cr_runtime->context_stack) {
        CR_ContextProvider *next = cr_runtime->context_stack->parent;
        free(cr_runtime->context_stack);
        cr_runtime->context_stack = next;
    }

    free(cr_runtime);
    cr_runtime = NULL;
}

void cr_begin_frame(void) {
    if (!cr_runtime) {
        cr_init();
    }

    cr_runtime->is_rendering = true;
    cr_runtime->frame++;

    // Clear handlers from previous frame
    _cr_clear_handlers();

    Clay_BeginLayout();
}

Clay_RenderCommandArray cr_end_frame(void) {
    Clay_RenderCommandArray commands = Clay_EndLayout();
    cr_runtime->is_rendering = false;
    return commands;
}

// ============================================================================
// STATE IMPLEMENTATION
// ============================================================================

CR_StateInternal *_cr_alloc_state(size_t size, void *initial, struct Type type) {
    CR_StateInternal *state = calloc(1, sizeof(CR_StateInternal));
    if (!state) return NULL;

    state->value = calloc(1, size);
    if (!state->value) {
        free(state);
        return NULL;
    }

    memcpy(state->value, initial, size);
    state->size = size;
    state->type = type;
    state->version = 0;

    return state;
}

void *_cr_state_get(CR_StateInternal *state) {
    return state ? state->value : NULL;
}

void _cr_state_set(CR_StateInternal *state, void *value) {
    if (!state || !value) return;
    memcpy(state->value, value, state->size);
    state->version++;
}

// ============================================================================
// EFFECT IMPLEMENTATION
// ============================================================================

void _cr_run_effect(CR_EffectInternal *effect) {
    if (!effect || !effect->effect) return;

    // Run cleanup if exists
    if (effect->cleanup) {
        effect->cleanup();
        Block_release(effect->cleanup);
        effect->cleanup = NULL;
    }

    // Run effect and capture cleanup
    effect->cleanup = effect->effect();
    if (effect->cleanup) {
        effect->cleanup = Block_copy(effect->cleanup);
    }
}

// ============================================================================
// SIGNAL IMPLEMENTATION
// ============================================================================

CR_SignalInternal *_cr_alloc_signal(size_t size, void *initial, struct Type type) {
    CR_SignalInternal *signal = calloc(1, sizeof(CR_SignalInternal));
    if (!signal) return NULL;

    signal->value = calloc(1, size);
    if (!signal->value) {
        free(signal);
        return NULL;
    }

    memcpy(signal->value, initial, size);
    signal->size = size;
    signal->type = type;
    signal->version = 0;
    signal->subscribers = NULL;
    signal->subscriber_count = 0;

    return signal;
}

void _cr_signal_notify(CR_SignalInternal *signal) {
    if (!signal) return;

    for (size_t i = 0; i < signal->subscriber_count; i++) {
        if (signal->subscribers[i].handler) {
            signal->subscribers[i].handler(signal->value);
        }
    }
}

// ============================================================================
// EVENT HANDLING
// ============================================================================

void _cr_register_click(uint32_t element_id, VoidBlock handler) {
    if (!cr_runtime) return;

    // Grow array if needed
    if (cr_runtime->click_handler_count >= cr_runtime->click_handler_capacity) {
        size_t new_cap = cr_runtime->click_handler_capacity == 0 ? 32 : cr_runtime->click_handler_capacity * 2;
        void *new_handlers = realloc(cr_runtime->click_handlers,
            new_cap * sizeof(cr_runtime->click_handlers[0]));
        if (!new_handlers) return;
        cr_runtime->click_handlers = new_handlers;
        cr_runtime->click_handler_capacity = new_cap;
    }

    cr_runtime->click_handlers[cr_runtime->click_handler_count++] = (typeof(cr_runtime->click_handlers[0])){
        .element_id = element_id,
        .handler = handler,
    };
}

void _cr_dispatch_clicks(void) {
    if (!cr_runtime) return;

    Clay_ElementIdArray hovered = Clay_GetPointerOverIds();
    for (int32_t i = 0; i < hovered.length; i++) {
        for (size_t j = 0; j < cr_runtime->click_handler_count; j++) {
            if (hovered.internalArray[i].id == cr_runtime->click_handlers[j].element_id) {
                cr_runtime->click_handlers[j].handler();
                return; // Only fire one handler per click
            }
        }
    }
}

void _cr_clear_handlers(void) {
    if (!cr_runtime) return;

    for (size_t i = 0; i < cr_runtime->click_handler_count; i++) {
        if (cr_runtime->click_handlers[i].handler) {
            Block_release(cr_runtime->click_handlers[i].handler);
        }
    }
    cr_runtime->click_handler_count = 0;
}

// ============================================================================
// CONTEXT IMPLEMENTATION
// ============================================================================

CR_Context *_cr_create_context_impl(const char *name, size_t size, void *default_value, struct Type type) {
    CR_Context *ctx = calloc(1, sizeof(CR_Context));
    if (!ctx) return NULL;

    ctx->id = _cr_next_context_id++;
    ctx->name = name;
    ctx->value_size = size;
    ctx->type = type;

    if (default_value) {
        ctx->default_value = calloc(1, size);
        if (ctx->default_value) {
            memcpy(ctx->default_value, default_value, size);
        }
    }

    return ctx;
}

void *_cr_use_context_impl(CR_Context *context) {
    if (!context || !cr_runtime) return NULL;

    // Walk up the context stack to find a provider
    CR_ContextProvider *provider = cr_runtime->context_stack;
    while (provider) {
        if (provider->context && provider->context->id == context->id) {
            return provider->value;
        }
        provider = provider->parent;
    }

    // No provider found - return default value
    return context->default_value;
}

CR_ContextProvider *_cr_push_context(CR_Context *context, void *value) {
    if (!cr_runtime) return NULL;

    CR_ContextProvider *provider = calloc(1, sizeof(CR_ContextProvider));
    if (!provider) return NULL;

    provider->context = context;
    provider->value = value;
    provider->parent = cr_runtime->context_stack;

    cr_runtime->context_stack = provider;

    return provider;
}

void _cr_pop_context(CR_ContextProvider *provider) {
    if (!provider || !cr_runtime) return;

    // Verify this is the top of the stack
    if (cr_runtime->context_stack == provider) {
        cr_runtime->context_stack = provider->parent;
    }

    free(provider);
}

// ============================================================================
// COMPONENT STUBS (for future use)
// ============================================================================

CR_Component *cr_create_component(const char *name, RenderBlock render) {
    // Minimal implementation - can be expanded
    (void)name;
    (void)render;
    return NULL;
}

void cr_destroy_component(CR_Component *component) {
    (void)component;
}

void cr_render_component(CR_Component *component) {
    (void)component;
}

CR_Hook *_cr_alloc_hook(int type) {
    (void)type;
    return NULL;
}

// ============================================================================
// TEXT INPUT IMPLEMENTATION
// ============================================================================

CR_TextInputState *_cr_alloc_text_input(size_t buffer_size) {
    CR_TextInputState *input = calloc(1, sizeof(CR_TextInputState));
    if (!input) return NULL;

    input->buffer = calloc(1, buffer_size);
    if (!input->buffer) {
        free(input);
        return NULL;
    }

    input->buffer_size = buffer_size;
    input->length = 0;
    input->cursor_pos = 0;
    input->focused = false;
    input->editing = false;
    input->element_id = 0;
    input->on_change = NULL;

    return input;
}

void _cr_text_input_set_text(CR_TextInputState *input, const char *text) {
    if (!input || !text) return;

    size_t len = strlen(text);
    if (len >= input->buffer_size) {
        len = input->buffer_size - 1;
    }

    memcpy(input->buffer, text, len);
    input->buffer[len] = '\0';
    input->length = len;
    input->cursor_pos = len;

    if (input->on_change) {
        input->on_change(input->buffer);
    }
}

void _cr_text_input_insert(CR_TextInputState *input, const char *text) {
    if (!input || !text) return;

    size_t insert_len = strlen(text);
    if (input->length + insert_len >= input->buffer_size) {
        insert_len = input->buffer_size - input->length - 1;
    }
    if (insert_len == 0) return;

    // Move text after cursor
    memmove(input->buffer + input->cursor_pos + insert_len,
            input->buffer + input->cursor_pos,
            input->length - input->cursor_pos + 1);

    // Insert new text
    memcpy(input->buffer + input->cursor_pos, text, insert_len);
    input->length += insert_len;
    input->cursor_pos += insert_len;

    if (input->on_change) {
        input->on_change(input->buffer);
    }
}

void _cr_text_input_backspace(CR_TextInputState *input) {
    if (!input || input->cursor_pos == 0) return;

    memmove(input->buffer + input->cursor_pos - 1,
            input->buffer + input->cursor_pos,
            input->length - input->cursor_pos + 1);

    input->length--;
    input->cursor_pos--;

    if (input->on_change) {
        input->on_change(input->buffer);
    }
}

void _cr_text_input_delete(CR_TextInputState *input) {
    if (!input || input->cursor_pos >= input->length) return;

    memmove(input->buffer + input->cursor_pos,
            input->buffer + input->cursor_pos + 1,
            input->length - input->cursor_pos);

    input->length--;

    if (input->on_change) {
        input->on_change(input->buffer);
    }
}

void _cr_text_input_move_cursor(CR_TextInputState *input, int delta) {
    if (!input) return;

    int new_pos = (int)input->cursor_pos + delta;
    if (new_pos < 0) new_pos = 0;
    if (new_pos > (int)input->length) new_pos = (int)input->length;
    input->cursor_pos = (size_t)new_pos;
}

void _cr_focus_input(CR_TextInputState *input, uint32_t element_id) {
    // Unfocus previous
    if (_cr_focused_input && _cr_focused_input != input) {
        _cr_focused_input->focused = false;
        _cr_focused_input->editing = false;
    }

    _cr_focused_input = input;
    if (input) {
        input->focused = true;
        input->editing = true;
        input->element_id = element_id;
        input->cursor_pos = input->length; // Move cursor to end
    }
}

void _cr_unfocus_input(void) {
    if (_cr_focused_input) {
        _cr_focused_input->focused = false;
        _cr_focused_input->editing = false;
    }
    _cr_focused_input = NULL;
}

void _cr_handle_text_event(const char *text) {
    if (_cr_focused_input && text) {
        _cr_text_input_insert(_cr_focused_input, text);
    }
}

void _cr_handle_key_event(int keycode, bool is_press) {
    if (!_cr_focused_input || !is_press) return;

    // SDL keycodes
    switch (keycode) {
        case 8:   // Backspace
            _cr_text_input_backspace(_cr_focused_input);
            break;
        case 127: // Delete
            _cr_text_input_delete(_cr_focused_input);
            break;
        case 1073741904: // Left arrow (SDL_SCANCODE_LEFT)
            _cr_text_input_move_cursor(_cr_focused_input, -1);
            break;
        case 1073741903: // Right arrow (SDL_SCANCODE_RIGHT)
            _cr_text_input_move_cursor(_cr_focused_input, 1);
            break;
        case 27:  // Escape
            _cr_unfocus_input();
            break;
        case 13:  // Enter - unfocus but keep text
            _cr_unfocus_input();
            break;
    }
}

// ============================================================================
// DEBUG
// ============================================================================

void cr_debug_enable(bool enabled) {
    (void)enabled;
}

void cr_debug_log_tree(void) {
    fprintf(stderr, "[Clay React] Debug: frame=%llu, handlers=%zu\n",
            cr_runtime ? (unsigned long long)cr_runtime->frame : 0,
            cr_runtime ? cr_runtime->click_handler_count : 0);
}
