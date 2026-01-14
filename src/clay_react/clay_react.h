/**
 * Clay React - A React-like Framework for ClayUI
 *
 * A modern, idiomatic C framework for building reactive UIs with Clay.
 * Requires: Clang with blocks support (-fblocks -lBlocksRuntime)
 *
 * Features:
 * - Type-safe reactive state: $use_state(int, 0) -> state->get() / state->set(5)
 * - Declarative elements: $div({ .bg = RED }) { ... }
 * - Semantic layout: $row { } $col { } $center { }
 * - Event handling: $on_click(^{ ... })
 * - Signals for fine-grained reactivity
 * - Context API for dependency injection
 *
 * Example:
 *   void Counter(void) {
 *       auto count = $use_state(int, 0);
 *
 *       $col({ .gap = 16, .padding = $pad(20) }) {
 *           $text("Count: %d", count->get());
 *           $button("Increment") { $on_click(^{ count->set(count->get() + 1); }); }
 *       }
 *   }
 */

#pragma once

#ifndef __clang__
#   error "Clay React requires Clang with blocks support (-fblocks)"
#endif

#include <clay.h>
#include <Block.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <iso646.h>
#include "../reflect/reflect.h"

// ============================================================================
// COMPILER ATTRIBUTES
// ============================================================================

#define $nullable _Nullable
#define $nonnull _Nonnull
#define $unused __attribute__((unused))
#define $always_inline __attribute__((always_inline)) inline
#define $cleanup(fn) __attribute__((cleanup(fn)))
#define $overload __attribute__((overloadable))

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

typedef struct CR_Component CR_Component;
typedef struct CR_Hook CR_Hook;
typedef struct CR_Context CR_Context;
typedef struct CR_ContextProvider CR_ContextProvider;
typedef struct CR_Signal CR_Signal;
typedef struct CR_Runtime CR_Runtime;

// ============================================================================
// BLOCK TYPES
// ============================================================================

typedef void (^VoidBlock)(void);
typedef void (^CleanupBlock)(void);
typedef CleanupBlock $nullable (^EffectBlock)(void);
typedef void * $nullable (^ComputeBlock)(void);
typedef void (^RenderBlock)(void * $nullable props);
typedef void (^SubscriberBlock)(void * $nullable value);

// ============================================================================
// STATE HANDLE - Type-safe reactive state access
// ============================================================================

/**
 * Generic state handle with get/set methods.
 * Created via $use_state(Type, initial_value)
 *
 * Usage:
 *   auto counter = $use_state(int, 0);
 *   int value = counter->get();
 *   counter->set(42);
 */
#define CR_STATE_HANDLE(T) \
    struct { \
        T (*get)(void); \
        void (*set)(T value); \
        T *ptr; \
        size_t _size; \
        void *_state; \
    }

// Generic state handle for internal use
typedef struct CR_StateHandle {
    void *(*get_ptr)(void *state);
    void (*set_ptr)(void *state, void *value);
    void *ptr;
    size_t size;
    void *state;
} CR_StateHandle;

// ============================================================================
// SIGNAL HANDLE - Fine-grained reactivity
// ============================================================================

/**
 * Signal handle with reactive get/set and subscriptions.
 *
 * Usage:
 *   auto signal = $signal(int, 0);
 *   signal->get();
 *   signal->set(10);
 *   signal->subscribe(^(int *val) { printf("changed: %d\n", *val); });
 */
#define CR_SIGNAL_HANDLE(T) \
    struct { \
        T (*get)(void); \
        void (*set)(T value); \
        void (*subscribe)(void (^handler)(T *value)); \
        void *_signal; \
    }

// ============================================================================
// EVENT TYPES
// ============================================================================

typedef struct {
    float x, y;
    int button;
    bool is_press;
} ClickEvent;

typedef struct {
    float x, y;
    float dx, dy;
    bool entered;
    bool exited;
} HoverEvent;

typedef struct {
    int keycode;
    int scancode;
    int modifiers;
    bool is_press;
    bool is_repeat;
} KeyEvent;

typedef struct {
    float dx, dy;
} ScrollEvent;

typedef void (^OnClickBlock)(ClickEvent *event);
typedef void (^OnHoverBlock)(HoverEvent *event);
typedef void (^OnKeyBlock)(KeyEvent *event);
typedef void (^OnScrollBlock)(ScrollEvent *event);
typedef void (^OnTextChangeBlock)(const char *text);

// ============================================================================
// TEXT INPUT STATE
// ============================================================================

/**
 * Text input field state - used for editable text fields
 */
typedef struct CR_TextInputState {
    char *buffer;           // Text buffer
    size_t buffer_size;     // Allocated size
    size_t length;          // Current text length
    size_t cursor_pos;      // Cursor position
    size_t selection_start; // Selection start (if any)
    size_t selection_end;   // Selection end (if any)
    bool focused;           // Is this input focused?
    bool editing;           // Is text being edited?
    uint32_t element_id;    // Associated element ID
    OnTextChangeBlock on_change; // Callback when text changes
} CR_TextInputState;

// Current focused input (only one at a time)
extern CR_TextInputState * $nullable _cr_focused_input;

// Text input management
CR_TextInputState *_cr_alloc_text_input(size_t buffer_size);
void _cr_text_input_set_text(CR_TextInputState *input, const char *text);
void _cr_text_input_insert(CR_TextInputState *input, const char *text);
void _cr_text_input_backspace(CR_TextInputState *input);
void _cr_text_input_delete(CR_TextInputState *input);
void _cr_text_input_move_cursor(CR_TextInputState *input, int delta);
void _cr_focus_input(CR_TextInputState *input, uint32_t element_id);
void _cr_unfocus_input(void);
void _cr_handle_text_event(const char *text);
void _cr_handle_key_event(int keycode, bool is_press);

// ============================================================================
// ELEMENT CONFIGURATION
// ============================================================================

/**
 * Simplified element configuration for $div, $row, $col, etc.
 * Includes event handlers directly in the config.
 *
 * Usage:
 *   $box(
 *       .id = "MyButton",
 *       .bg = $BLUE,
 *       .on_click = ^{ printf("clicked!"); },
 *   ) { ... }
 */
typedef struct {
    // Identity (string literal for auto CLAY_ID)
    const char * $nullable id;

    // Layout
    Clay_LayoutDirection direction;
    Clay_Sizing sizing;
    Clay_Padding padding;
    uint16_t gap;
    Clay_ChildAlignment align;

    // Appearance
    Clay_Color bg;
    Clay_Color bg_hover;  // If set, auto-applies hover effect
    Clay_Color border_color;
    Clay_BorderWidth border_width;
    Clay_CornerRadius corner_radius;

    // Text (for text-containing elements)
    const char * $nullable text;
    Clay_TextElementConfig * $nullable text_config;

    // Scroll
    bool scroll_x;
    bool scroll_y;

    // Events - handlers called automatically
    VoidBlock $nullable on_click;
    VoidBlock $nullable on_hover_start;
    VoidBlock $nullable on_hover_end;
} ElementConfig;

/**
 * Text configuration
 */
typedef struct {
    uint16_t font_id;
    uint16_t font_size;
    uint16_t line_height;
    uint16_t letter_spacing;
    uint16_t wrap_mode;
    Clay_Color color;
} TextConfig;

// ============================================================================
// CORE RUNTIME
// ============================================================================

struct CR_Runtime {
    CR_Component * $nullable current_component;
    CR_Component * $nullable root;
    CR_ContextProvider * $nullable context_stack;

    bool is_rendering;
    uint64_t frame;

    // Event handler registry
    struct {
        uint32_t element_id;
        VoidBlock handler;
    } *click_handlers;
    size_t click_handler_count;
    size_t click_handler_capacity;

    // Memory tracking
    size_t allocated;
    size_t peak_allocated;
};

extern CR_Runtime * $nullable cr_runtime;

// ============================================================================
// INITIALIZATION
// ============================================================================

void cr_init(void);
void cr_shutdown(void);
void cr_begin_frame(void);
Clay_RenderCommandArray cr_end_frame(void);

// ============================================================================
// STATE IMPLEMENTATION
// ============================================================================

typedef struct CR_StateInternal {
    void *value;
    size_t size;
    struct Type type;
    uint64_t version;
} CR_StateInternal;

CR_StateInternal *_cr_alloc_state(size_t size, void *initial, struct Type type);
void *_cr_state_get(CR_StateInternal *state);
void _cr_state_set(CR_StateInternal *state, void *value);

/**
 * $use_state - Create reactive state with type-safe handle
 *
 * Usage:
 *   // For primitives:
 *   auto counter = $use_state(int, 0);
 *   printf("%d\n", counter->get());
 *   counter->set(counter->get() + 1);
 *
 *   // For structs:
 *   auto person = $use_state(Person, { .name = "John", .age = 30 });
 *   person->ptr->age = 31;  // Direct access via ptr
 */
#define $use_state(T, ...) \
    ({ \
        static T *_value = NULL; \
        static uint64_t _version $unused = 0; \
        if (!_value) { \
            _value = calloc(1, sizeof(T)); \
            *_value = (T)__VA_ARGS__; \
        } \
        /* Create typed handle with block types */ \
        static struct { \
            void (^set)(T value); \
            T *value; \
        } _handle; \
        _handle.value = _value; \
        _handle.set = ^(T v){ \
            *_value = v; \
            _version++; \
        }; \
        &_handle; \
    })

// ============================================================================
// REF IMPLEMENTATION
// ============================================================================

/**
 * $use_ref - Mutable reference that persists across renders
 *
 * Usage:
 *   auto ref = $use_ref(int, 0);
 *   *ref = 42;  // Doesn't trigger re-render
 */
#define $use_ref(T, initial_value) \
    ({ \
        static T *_ref = NULL; \
        if (!_ref) { \
            _ref = malloc(sizeof(T)); \
            *_ref = (T){initial_value}; \
        } \
        _ref; \
    })

// ============================================================================
// EFFECT IMPLEMENTATION
// ============================================================================

typedef struct CR_EffectInternal {
    EffectBlock effect;
    CleanupBlock $nullable cleanup;
    void **deps;
    size_t dep_count;
    bool ran;
} CR_EffectInternal;

void _cr_run_effect(CR_EffectInternal *effect);

/**
 * $use_effect - Run side effects
 *
 * Usage:
 *   $use_effect(^{
 *       printf("mounted!\n");
 *       return ^{ printf("cleanup!\n"); };
 *   });
 */
#define $use_effect(effect_block) \
    do { \
        static CR_EffectInternal _effect = {0}; \
        if (!_effect.ran) { \
            _effect.effect = Block_copy(effect_block); \
            _effect.ran = true; \
            _cr_run_effect(&_effect); \
        } \
    } while(0)

// ============================================================================
// MEMO IMPLEMENTATION
// ============================================================================

/**
 * $use_memo - Memoize expensive computations
 *
 * Usage:
 *   auto result = $use_memo(int, ^{ return expensive_calc(); }, dep1, dep2);
 */
#define $use_memo(T, compute_block, ...) \
    ({ \
        static T _cached; \
        static bool _computed = false; \
        static void *_deps[] = {__VA_ARGS__}; \
        if (!_computed) { \
            T *result = (T *)compute_block(); \
            if (result) _cached = *result; \
            _computed = true; \
        } \
        _cached; \
    })

// ============================================================================
// SIGNAL IMPLEMENTATION
// ============================================================================

typedef struct CR_SignalInternal {
    void *value;
    size_t size;
    struct Type type;
    uint64_t version;
    struct {
        SubscriberBlock handler;
    } *subscribers;
    size_t subscriber_count;
} CR_SignalInternal;

CR_SignalInternal *_cr_alloc_signal(size_t size, void *initial, struct Type type);
void _cr_signal_notify(CR_SignalInternal *signal);

/**
 * $signal - Create a reactive signal
 *
 * Usage:
 *   auto sig = $signal(int, 0);
 *   sig->get();
 *   sig->set(10);
 *   sig->subscribe(^(int *val) { ... });
 */
#define $signal(T, initial_value) \
    ({ \
        static CR_SignalInternal *_sig = NULL; \
        if (!_sig) { \
            _sig = _cr_alloc_signal(sizeof(T), &(T){initial_value}, $reflect(T)); \
        } \
        static struct { \
            T (*get)(void); \
            void (*set)(T value); \
            void (*subscribe)(void (^handler)(T *value)); \
        } _handle; \
        _handle.get = ^T{ return *(T *)_sig->value; }; \
        _handle.set = ^(T v){ \
            *(T *)_sig->value = v; \
            _sig->version++; \
            _cr_signal_notify(_sig); \
        }; \
        _handle.subscribe = ^(void (^h)(T *value)){ \
            /* Add subscriber */ \
            _sig->subscriber_count++; \
            _sig->subscribers = realloc(_sig->subscribers, \
                _sig->subscriber_count * sizeof(_sig->subscribers[0])); \
            _sig->subscribers[_sig->subscriber_count - 1].handler = Block_copy((SubscriberBlock)h); \
        }; \
        &_handle; \
    })

// ============================================================================
// EVENT HANDLING
// ============================================================================

void _cr_register_click(uint32_t element_id, VoidBlock handler);
void _cr_dispatch_clicks(void);
void _cr_clear_handlers(void);

/**
 * $on_click - Register click handler for an element
 *
 * Usage:
 *   Clay_ElementId my_id = $id("MyButton");
 *   CLAY({ .id = my_id, ... }) {
 *       $on_click(my_id, ^{ printf("clicked!\n"); });
 *   }
 */
#define $on_click(element_id, handler_block) \
    _cr_register_click((element_id).id, Block_copy(handler_block))

/**
 * $on_hover - Check hover state (use in conditionals)
 */
#define $hovered() Clay_Hovered()

/**
 * $hover_style - Conditional style based on hover
 */
#define $hover_style(normal, hovered) \
    ($hovered() ? (hovered) : (normal))

// ============================================================================
// ELEMENT MACROS - Declarative UI building
// ============================================================================

// Internal: Convert ElementConfig to Clay_ElementDeclaration
static $always_inline Clay_ElementDeclaration _cr_to_clay_decl(ElementConfig cfg) {
    return (Clay_ElementDeclaration){
        // Note: id should be set separately using CLAY_ID() macro
        .layout = {
            .layoutDirection = cfg.direction,
            .sizing = cfg.sizing,
            .padding = cfg.padding,
            .childGap = cfg.gap,
            .childAlignment = cfg.align,
        },
        .backgroundColor = cfg.bg,
        .cornerRadius = cfg.corner_radius,
        .border = {
            .width = cfg.border_width,
            .color = cfg.border_color,
        },
    };
}

/**
 * $div - Generic container element
 *
 * Usage:
 *   $div({ .bg = COLOR_RED, .padding = $pad(16) }) {
 *       $text("Hello");
 *   }
 */
#define $div(...) \
    for (uint8_t _l = (Clay__OpenElement(), \
         Clay__ConfigureOpenElement(_cr_to_clay_decl((ElementConfig)__VA_ARGS__)), 0); \
         _l < 1; _l++, Clay__CloseElement())

/**
 * $box - Alias for $div
 */
#define $box(...) $div(__VA_ARGS__)

/**
 * $row - Horizontal layout container
 */
#define $row(...) \
    for (uint8_t _l = (Clay__OpenElement(), \
         Clay__ConfigureOpenElement(_cr_to_clay_decl((ElementConfig){ \
             .direction = CLAY_LEFT_TO_RIGHT, \
             __VA_ARGS__ \
         })), 0); \
         _l < 1; _l++, Clay__CloseElement())

/**
 * $col - Vertical layout container
 */
#define $col(...) \
    for (uint8_t _l = (Clay__OpenElement(), \
         Clay__ConfigureOpenElement(_cr_to_clay_decl((ElementConfig){ \
             .direction = CLAY_TOP_TO_BOTTOM, \
             __VA_ARGS__ \
         })), 0); \
         _l < 1; _l++, Clay__CloseElement())

/**
 * $center - Centered content container
 */
#define $center(...) \
    for (uint8_t _l = (Clay__OpenElement(), \
         Clay__ConfigureOpenElement(_cr_to_clay_decl((ElementConfig){ \
             .align = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }, \
             __VA_ARGS__ \
         })), 0); \
         _l < 1; _l++, Clay__CloseElement())

/**
 * $spacer - Flexible space
 */
#define $spacer() \
    CLAY({ .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } } }) {}

/**
 * $hspacer - Horizontal spacer
 */
#define $hspacer() \
    CLAY({ .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) } } }) {}

/**
 * $vspacer - Vertical spacer
 */
#define $vspacer() \
    CLAY({ .layout = { .sizing = { .height = CLAY_SIZING_GROW(0) } } }) {}

// ============================================================================
// TEXT MACROS
// ============================================================================

// Default text config - can be overridden
#ifndef $TEXT_DEFAULT_FONT_ID
#define $TEXT_DEFAULT_FONT_ID 0
#endif

#ifndef $TEXT_DEFAULT_SIZE
#define $TEXT_DEFAULT_SIZE 16
#endif

#ifndef $TEXT_DEFAULT_COLOR
#define $TEXT_DEFAULT_COLOR ((Clay_Color){30, 35, 45, 255})
#endif

/**
 * $text - Render text with printf-style formatting
 *
 * Usage:
 *   $text("Hello %s, you have %d messages", name, count);
 */
#define $text(fmt, ...) \
    do { \
        static char _buf[256]; \
        snprintf(_buf, sizeof(_buf), fmt, ##__VA_ARGS__); \
        Clay__OpenTextElement( \
            (Clay_String){ .length = strlen(_buf), .chars = _buf }, \
            CLAY_TEXT_CONFIG({ \
                .fontId = $TEXT_DEFAULT_FONT_ID, \
                .fontSize = $TEXT_DEFAULT_SIZE, \
                .textColor = $TEXT_DEFAULT_COLOR \
            }) \
        ); \
    } while(0)

/**
 * $text_ex - Text with custom styling
 *
 * Usage:
 *   $text_ex((.fontSize = 24, .textColor = $RED), "Big red text");
 * Note: Config must be wrapped in extra parentheses due to macro comma handling
 */
#define $text_ex(config, fmt, ...) \
    do { \
        static char _buf[256]; \
        snprintf(_buf, sizeof(_buf), fmt, ##__VA_ARGS__); \
        Clay__OpenTextElement( \
            (Clay_String){ .length = strlen(_buf), .chars = _buf }, \
            CLAY_TEXT_CONFIG({ $CR_UNWRAP config }) \
        ); \
    } while(0)

// Helper to unwrap the extra parentheses
#define $CR_UNWRAP(...) __VA_ARGS__

/**
 * $label - Simple text label (no formatting, compile-time string)
 */
#define $label(str) \
    Clay__OpenTextElement(CLAY_STRING(str), CLAY_TEXT_CONFIG({ \
        .fontId = $TEXT_DEFAULT_FONT_ID, \
        .fontSize = $TEXT_DEFAULT_SIZE, \
        .textColor = $TEXT_DEFAULT_COLOR \
    }))

/**
 * $label_ex - Label with custom config
 * Note: Config must be wrapped in extra parentheses due to macro comma handling
 * Usage: $label_ex((.fontSize = 24, .textColor = $RED), "text")
 */
#define $label_ex(config, str) \
    Clay__OpenTextElement(CLAY_STRING(str), CLAY_TEXT_CONFIG({ $CR_UNWRAP config }))

// ============================================================================
// CLICKABLE COMPONENT MACROS - With built-in event handling
// ============================================================================

/**
 * $clickable - Generic clickable container with on_click handler built in
 *
 * Usage:
 *   $clickable("MyBtn", ^{ handle_click(); },
 *       .bg = $BLUE,
 *       .corner_radius = $radius(8),
 *   ) {
 *       $label("Click me");
 *   }
 */
#define $clickable(id_str, on_click_handler, ...) \
    for (Clay_ElementId _eid = CLAY_ID(id_str), \
         *_setup = (_cr_register_click(_eid.id, Block_copy(on_click_handler)), (Clay_ElementId*)1); \
         _setup; \
         _setup = NULL) \
    for (uint8_t _l = (Clay__OpenElement(), \
         Clay__ConfigureOpenElement((Clay_ElementDeclaration){ \
             .id = _eid, \
             __VA_ARGS__ \
         }), 0); \
         _l < 1; _l++, Clay__CloseElement())

/**
 * $clickable_i - Clickable with indexed ID (for use in loops)
 */
#define $clickable_i(id_str, index, on_click_handler, ...) \
    for (Clay_ElementId _eid = CLAY_IDI(id_str, index), \
         *_setup = (_cr_register_click(_eid.id, Block_copy(on_click_handler)), (Clay_ElementId*)1); \
         _setup; \
         _setup = NULL) \
    for (uint8_t _l = (Clay__OpenElement(), \
         Clay__ConfigureOpenElement((Clay_ElementDeclaration){ \
             .id = _eid, \
             __VA_ARGS__ \
         }), 0); \
         _l < 1; _l++, Clay__CloseElement())

/**
 * $button - Button with label and click handler
 *
 * Usage:
 *   $button("AddBtn", "Add Item", ^{ add_item(); },
 *       .bg = $BLUE,
 *   ) {}
 */
#define $button(id_str, label_text, on_click_handler, ...) \
    for (Clay_ElementId _eid = CLAY_ID(id_str), \
         *_setup = (_cr_register_click(_eid.id, Block_copy(on_click_handler)), (Clay_ElementId*)1); \
         _setup; \
         _setup = NULL) \
    for (uint8_t _l = (Clay__OpenElement(), \
         Clay__ConfigureOpenElement((Clay_ElementDeclaration){ \
             .id = _eid, \
             .layout = { \
                 .padding = { 16, 16, 10, 10 }, \
                 .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }, \
             }, \
             .cornerRadius = CLAY_CORNER_RADIUS(6), \
             __VA_ARGS__ \
         }), \
         Clay__OpenTextElement(CLAY_STRING(label_text), \
             CLAY_TEXT_CONFIG({ .fontSize = 16, .textColor = {255,255,255,255} })), 0); \
         _l < 1; _l++, Clay__CloseElement())

/**
 * $icon_button - Small icon button (e.g., for close/delete buttons)
 *
 * Usage:
 *   $icon_button("DelBtn", 0, "×", ^{ delete_item(); },
 *       .bg = $RED,
 *   ) {}
 */
#define $icon_button(id_str, index, icon_char, on_click_handler, ...) \
    for (Clay_ElementId _eid = CLAY_IDI(id_str, index), \
         *_setup = (_cr_register_click(_eid.id, Block_copy(on_click_handler)), (Clay_ElementId*)1); \
         _setup; \
         _setup = NULL) \
    for (uint8_t _l = (Clay__OpenElement(), \
         Clay__ConfigureOpenElement((Clay_ElementDeclaration){ \
             .id = _eid, \
             .layout = { \
                 .sizing = { .width = CLAY_SIZING_FIXED(28), .height = CLAY_SIZING_FIXED(28) }, \
                 .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }, \
             }, \
             .cornerRadius = CLAY_CORNER_RADIUS(4), \
             __VA_ARGS__ \
         }), \
         Clay__OpenTextElement(CLAY_STRING(icon_char), \
             CLAY_TEXT_CONFIG({ .fontSize = 16, .textColor = {255,255,255,255} })), 0); \
         _l < 1; _l++, Clay__CloseElement())

/**
 * $checkbox - Checkbox with toggle callback
 *
 * Usage:
 *   $checkbox("Check", index, is_checked, ^{ toggle(); },
 *       .checked_color = $BLUE,
 *       .unchecked_color = $WHITE,
 *   ) {}
 */
#define $checkbox(id_str, index, checked, on_toggle_handler, ...) \
    for (Clay_ElementId _eid = CLAY_IDI(id_str, index), \
         *_setup = (_cr_register_click(_eid.id, Block_copy(on_toggle_handler)), (Clay_ElementId*)1); \
         _setup; \
         _setup = NULL) \
    for (uint8_t _l = (Clay__OpenElement(), \
         Clay__ConfigureOpenElement((Clay_ElementDeclaration){ \
             .id = _eid, \
             .layout = { \
                 .sizing = { .width = CLAY_SIZING_FIXED(24), .height = CLAY_SIZING_FIXED(24) }, \
                 .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER }, \
             }, \
             .backgroundColor = (checked) ? $BLUE : $WHITE, \
             .cornerRadius = CLAY_CORNER_RADIUS(4), \
             .border = (checked) ? (Clay_BorderElementConfig){0} : \
                 (Clay_BorderElementConfig){ .width = CLAY_BORDER_OUTSIDE(2), .color = $gray(150) }, \
             __VA_ARGS__ \
         }), 0); \
         _l < 1; \
         _l++, Clay__CloseElement()) \
    if (checked) Clay__OpenTextElement(CLAY_STRING("✓"), \
        CLAY_TEXT_CONFIG({ .fontSize = 16, .textColor = $WHITE })); \
    else (void)0; \
    if (0)

/**
 * $flex - Flexible container that just takes space
 */
#define $flex(...) \
    for (uint8_t _l = (Clay__OpenElement(), \
         Clay__ConfigureOpenElement((Clay_ElementDeclaration){ \
             .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) } }, \
             __VA_ARGS__ \
         }), 0); \
         _l < 1; _l++, Clay__CloseElement())

/**
 * $card - Styled card container
 */
#define $card(...) \
    for (uint8_t _l = (Clay__OpenElement(), \
         Clay__ConfigureOpenElement(_cr_to_clay_decl((ElementConfig){ \
             .padding = $pad(16), \
             .corner_radius = $radius(12), \
             .bg = $WHITE, \
             __VA_ARGS__ \
         })), 0); \
         _l < 1; _l++, Clay__CloseElement())

// ============================================================================
// TEXT INPUT COMPONENT
// ============================================================================

/**
 * $use_text_input - Create a persistent text input state
 *
 * Usage:
 *   auto input = $use_text_input(64);  // 64 byte buffer
 *   $text_input("MyInput", input, .placeholder = "Enter text...");
 */
#define $use_text_input(buffer_size) \
    ({ \
        static CR_TextInputState *_input = NULL; \
        if (!_input) { \
            _input = _cr_alloc_text_input(buffer_size); \
        } \
        _input; \
    })

/**
 * $text_input - Editable text input field
 *
 * Usage:
 *   auto input = $use_text_input(64);
 *   $text_input("InputField", input,
 *       .bg = $WHITE,
 *       .placeholder_color = $gray(150),
 *   ) {}
 */
#define $text_input(id_str, input_state, ...) \
    for (Clay_ElementId _eid = CLAY_ID(id_str), \
         *_setup = (_cr_register_click(_eid.id, Block_copy(^{ _cr_focus_input(input_state, _eid.id); })), (Clay_ElementId*)1); \
         _setup; \
         _setup = NULL) \
    for (uint8_t _l = (Clay__OpenElement(), \
         Clay__ConfigureOpenElement((Clay_ElementDeclaration){ \
             .id = _eid, \
             .layout = { \
                 .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIT(.min = 40) }, \
                 .padding = { 12, 12, 10, 10 }, \
                 .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }, \
             }, \
             .backgroundColor = $WHITE, \
             .cornerRadius = CLAY_CORNER_RADIUS(6), \
             .border = (input_state->focused) ? \
                 (Clay_BorderElementConfig){ .width = CLAY_BORDER_OUTSIDE(2), .color = $BLUE } : \
                 (Clay_BorderElementConfig){ .width = CLAY_BORDER_OUTSIDE(1), .color = $gray(200) }, \
             __VA_ARGS__ \
         }), \
         Clay__OpenTextElement( \
             (input_state->length > 0) ? \
                 (Clay_String){ .length = input_state->length, .chars = input_state->buffer } : \
                 CLAY_STRING(""), \
             CLAY_TEXT_CONFIG({ .fontSize = 16, .textColor = $gray(30) }) \
         ), 0); \
         _l < 1; _l++, Clay__CloseElement())

/**
 * $text_input_i - Text input with indexed ID (for use in loops)
 */
#define $text_input_i(id_str, index, input_state, ...) \
    for (Clay_ElementId _eid = CLAY_IDI(id_str, index), \
         *_setup = (_cr_register_click(_eid.id, Block_copy(^{ _cr_focus_input(input_state, _eid.id); })), (Clay_ElementId*)1); \
         _setup; \
         _setup = NULL) \
    for (uint8_t _l = (Clay__OpenElement(), \
         Clay__ConfigureOpenElement((Clay_ElementDeclaration){ \
             .id = _eid, \
             .layout = { \
                 .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIT(.min = 32) }, \
                 .padding = { 8, 8, 6, 6 }, \
                 .childAlignment = { .y = CLAY_ALIGN_Y_CENTER }, \
             }, \
             .backgroundColor = $TRANSPARENT, \
             .cornerRadius = CLAY_CORNER_RADIUS(4), \
             .border = (input_state->focused && input_state->element_id == _eid.id) ? \
                 (Clay_BorderElementConfig){ .width = CLAY_BORDER_OUTSIDE(2), .color = $BLUE } : \
                 (Clay_BorderElementConfig){ 0 }, \
             __VA_ARGS__ \
         }), \
         Clay__OpenTextElement( \
             (input_state->length > 0) ? \
                 (Clay_String){ .length = input_state->length, .chars = input_state->buffer } : \
                 CLAY_STRING(""), \
             CLAY_TEXT_CONFIG({ .fontSize = 16, .textColor = $gray(30) }) \
         ), 0); \
         _l < 1; _l++, Clay__CloseElement())


// ============================================================================
// CONTEXT API
// ============================================================================

struct CR_Context {
    uint64_t id;
    const char *name;
    void *default_value;
    size_t value_size;
    struct Type type;
};

struct CR_ContextProvider {
    CR_Context *context;
    void *value;
    CR_ContextProvider * $nullable parent;
};

/**
 * $create_context - Create a new context
 *
 * Usage:
 *   static CR_Context *ThemeContext = NULL;
 *   if (!ThemeContext) ThemeContext = $create_context(Theme, default_theme);
 */
#define $create_context(T, default_val) \
    _cr_create_context_impl(#T, sizeof(T), &(T){default_val}, $reflect(T))

CR_Context *_cr_create_context_impl(const char *name, size_t size, void *default_value, struct Type type);

/**
 * $provide - Provide context value to children
 *
 * Usage:
 *   $provide(ThemeContext, &dark_theme) {
 *       ChildComponent();
 *   }
 */
#define $provide(context, value_ptr) \
    for (CR_ContextProvider *_prov = _cr_push_context(context, value_ptr), *_done = NULL; \
         _done == NULL; \
         _cr_pop_context(_prov), _done = (void *)1)

CR_ContextProvider *_cr_push_context(CR_Context *context, void *value);
void _cr_pop_context(CR_ContextProvider *provider);

/**
 * $use_context - Access context value
 *
 * Usage:
 *   Theme *theme = $use_context(ThemeContext);
 */
#define $use_context(context) \
    _cr_use_context_impl(context)

void *_cr_use_context_impl(CR_Context *context);

// ============================================================================
// COMPONENT DEFINITION
// ============================================================================

/**
 * $component - Define a component with typed props
 *
 * Usage:
 *   typedef struct { const char *title; int count; } CardProps;
 *
 *   $component(Card, CardProps) {
 *       $col({ .padding = $pad(16), .bg = $WHITE }) {
 *           $text("%s: %d", props->title, props->count);
 *       }
 *   }
 */
#define $component(name, props_type) \
    void name##_render(props_type *props); \
    static inline void name(props_type p) { name##_render(&p); } \
    void name##_render(props_type *props)

/**
 * $component_void - Component without props
 */
#define $component_void(name) \
    void name(void)

// ============================================================================
// SIZING HELPERS
// ============================================================================

#define $grow(...)     CLAY_SIZING_GROW(__VA_ARGS__)
#define $fit(...)      CLAY_SIZING_FIT(__VA_ARGS__)
#define $fixed(px)     CLAY_SIZING_FIXED(px)
#define $percent(p)    CLAY_SIZING_PERCENT(p)
#define $fill()        (Clay_Sizing){ .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) }

// ============================================================================
// PADDING HELPERS
// ============================================================================

#define $pad(all)           CLAY_PADDING_ALL(all)
#define $pad_xy(x, y)       ((Clay_Padding){ .left = x, .right = x, .top = y, .bottom = y })
#define $pad_x(x)           ((Clay_Padding){ .left = x, .right = x })
#define $pad_y(y)           ((Clay_Padding){ .top = y, .bottom = y })
#define $pad_lrtb(l,r,t,b)  ((Clay_Padding){ .left = l, .right = r, .top = t, .bottom = b })

// ============================================================================
// CORNER RADIUS HELPERS
// ============================================================================

#define $radius(r)      CLAY_CORNER_RADIUS(r)
#define $rounded(r)     CLAY_CORNER_RADIUS(r)
#define $circle()       CLAY_CORNER_RADIUS(9999)

// ============================================================================
// COLOR HELPERS
// ============================================================================

#define $rgb(r, g, b)       ((Clay_Color){ r, g, b, 255 })
#define $rgba(r, g, b, a)   ((Clay_Color){ r, g, b, a })
#define $gray(v)            ((Clay_Color){ v, v, v, 255 })
#define $alpha(color, a)    ((Clay_Color){ (color).r, (color).g, (color).b, a })

// Common colors
#define $WHITE      $rgb(255, 255, 255)
#define $BLACK      $rgb(0, 0, 0)
#define $RED        $rgb(239, 68, 68)
#define $GREEN      $rgb(34, 197, 94)
#define $BLUE       $rgb(59, 130, 246)
#define $YELLOW     $rgb(250, 204, 21)
#define $PURPLE     $rgb(168, 85, 247)
#define $PINK       $rgb(236, 72, 153)
#define $ORANGE     $rgb(249, 115, 22)
#define $CYAN       $rgb(6, 182, 212)
#define $TRANSPARENT ((Clay_Color){0, 0, 0, 0})

// ============================================================================
// BORDER HELPERS
// ============================================================================

#define $border(w)          CLAY_BORDER_ALL(w)
#define $border_outside(w)  CLAY_BORDER_OUTSIDE(w)

// ============================================================================
// STRING HELPERS
// ============================================================================

#define $str(s)         CLAY_STRING(s)
#define $cstr(s)        ((Clay_String){ .length = strlen(s), .chars = s })

// ============================================================================
// ID HELPERS
// ============================================================================

#define $id(name)       CLAY_ID(name)
#define $idi(name, i)   CLAY_IDI(name, i)

// ============================================================================
// ANIMATION HELPERS
// ============================================================================

static $always_inline float $lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

static $always_inline Clay_Color $lerp_color(Clay_Color a, Clay_Color b, float t) {
    return (Clay_Color){
        .r = $lerp(a.r, b.r, t),
        .g = $lerp(a.g, b.g, t),
        .b = $lerp(a.b, b.b, t),
        .a = $lerp(a.a, b.a, t),
    };
}

// ============================================================================
// INTERNAL IMPLEMENTATION
// ============================================================================

// Memory allocation with tracking
void *_cr_alloc(size_t size);
void _cr_free(void *ptr, size_t size);

// Component management
CR_Component *cr_create_component(const char *name, RenderBlock render);
void cr_destroy_component(CR_Component *component);
void cr_render_component(CR_Component *component);

// Hook management
CR_Hook *_cr_alloc_hook(int type);

// Debug
void cr_debug_enable(bool enabled);
void cr_debug_log_tree(void);

#ifdef __cplusplus
}
#endif

#pragma clang diagnostic pop
