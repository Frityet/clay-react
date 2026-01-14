/**
 * Clay React - A React-like Framework for ClayUI
 *
 * A modern, idiomatic C framework for building reactive UIs with Clay.
 * Requires: Clang with blocks support (-fblocks -lBlocksRuntime)
 *
 * Features:
 * - Type-safe reactive state: $use_state(0) -> state->get() / state->set(5)
 * - Function-based components with struct params
 * - Semantic layout: Row / Column / Center
 * - Click handling via component params
 * - Signals for fine-grained reactivity
 * - Context API for dependency injection
 *
 * Example:
 *   void Counter(void) {
 *       auto count = $use_state(0);
 *
 *       Column((BoxParams){
 *           .style = {
 *               .layout = { .childGap = 16, .padding = $pad(20) },
 *           },
 *       }, ^{
 *           Textf((TextParams){0}, "Count: %d", count->get());
 *           Button((ButtonParams){
 *               .id = cr_id("Increment"),
 *               .label = "Increment",
 *               .on_click = ^{ count->set(count->get() + 1); },
 *           }, NULL);
 *       });
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
#include <stdarg.h>
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
typedef struct CR_StateInternal CR_StateInternal;
typedef struct CR_SignalInternal CR_SignalInternal;
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
 * Created via $use_state(initial_value)
 *
 * Usage:
 *   auto counter = $use_state(0);
 *   int value = counter->get();
 *   counter->set(42);
 */
#define CR_STATE_HANDLE(T) \
    struct { \
        T (^get)(void); \
        void (^set)(T value); \
        T *ptr; \
        size_t _size; \
        void *_state; \
    }

// Generic state handle for internal use
typedef struct CR_StateHandle {
    void *ptr;
    size_t size;
    void *state;
    void *get;
    void *set;
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
        T (^get)(void); \
        void (^set)(T value); \
        void (^subscribe)(void (^handler)(T *value)); \
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

// Frame-scoped string allocation (used by formatted text helpers)
char *_cr_temp_string_alloc(size_t size);

// ============================================================================
// CORE COMPONENT TYPES
// ============================================================================

/**
 * CR_Id - Component identity (string + optional index)
 */
typedef struct {
    const char * $nullable name;
    uint32_t index;
    bool indexed;
} CR_Id;

static $always_inline CR_Id cr_id(const char *name) {
    return (CR_Id){ .name = name, .index = 0, .indexed = false };
}

static $always_inline CR_Id cr_idi(const char *name, uint32_t index) {
    return (CR_Id){ .name = name, .index = index, .indexed = true };
}

/**
 * View style configuration for layout containers and components.
 */
typedef struct {
    Clay_LayoutConfig layout;
    Clay_Color background;
    Clay_Color background_hover;
    Clay_BorderElementConfig border;
    Clay_CornerRadius corner_radius;
    bool has_background;
    bool has_background_hover;
    bool has_border;
    bool has_corner_radius;
} ViewStyle;

/**
 * BoxParams - Base container params used by layout components.
 */
typedef struct {
    CR_Id id;
    ViewStyle style;
    bool scroll_x;
    bool scroll_y;
    VoidBlock $nullable on_click;
} BoxParams;

/**
 * Text configuration for core text components.
 */
typedef struct {
    uint16_t font_id;
    uint16_t font_size;
    uint16_t line_height;
    uint16_t letter_spacing;
    Clay_TextElementConfigWrapMode wrap_mode;
    Clay_TextAlignment text_alignment;
    Clay_Color color;
} TextConfig;

/**
 * TextParams - Text content + style.
 */
typedef struct {
    const char * $nullable text;
    TextConfig style;
} TextParams;

// ============================================================================
// CORE RUNTIME
// ============================================================================

typedef struct {
    char *ptr;
    size_t size;
} CR_TempString;

typedef struct {
    CR_Component *component;
    size_t hook_index;
    uint64_t component_id;
} CR_EffectRef;

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

    // Frame-scoped text buffers
    CR_TempString *temp_strings;
    size_t temp_string_count;
    size_t temp_string_capacity;

    // Component registry
    CR_Component **components;
    size_t component_count;
    size_t component_capacity;

    // Component stack (for nested renders)
    CR_Component **component_stack;
    size_t component_stack_count;
    size_t component_stack_capacity;

    // Effect queues
    CR_EffectRef *pending_effects;
    size_t pending_effect_count;
    size_t pending_effect_capacity;
    CR_EffectRef *pending_layout_effects;
    size_t pending_layout_effect_count;
    size_t pending_layout_effect_capacity;

    // Keyed component support
    CR_Id next_key;
    bool has_next_key;

    // Render scheduling
    bool needs_render;

    // Unique IDs for $use_id
    uint32_t next_uid;
};

extern CR_Runtime * $nullable cr_runtime;

// ============================================================================
// INITIALIZATION
// ============================================================================

void cr_init(void);
void cr_shutdown(void);
void cr_begin_frame(void);
Clay_RenderCommandArray cr_end_frame(void);
bool cr_should_render(void);
void cr_request_render(void);

// ============================================================================
// STATE IMPLEMENTATION
// ============================================================================

struct CR_StateInternal {
    void *value;
    size_t size;
    uint64_t version;
};

typedef enum {
    CR_HOOK_NONE = 0,
    CR_HOOK_STATE,
    CR_HOOK_REF,
    CR_HOOK_TEXT_INPUT,
    CR_HOOK_EFFECT,
    CR_HOOK_MEMO,
    CR_HOOK_CALLBACK,
    CR_HOOK_ID,
    CR_HOOK_SIGNAL,
} CR_HookType;

typedef struct {
    void *data;
    size_t size;
} CR_DepSnapshot;

typedef struct {
    const void *ptr;
    size_t size;
} CR_Dep;

typedef enum {
    CR_DEPS_NONE,
    CR_DEPS_ONCE,
    CR_DEPS_LIST,
} CR_DepMode;

typedef struct {
    CR_DepMode mode;
    const CR_Dep *items;
    size_t count;
} CR_DepList;

typedef struct CR_EffectInternal {
    EffectBlock effect;
    CleanupBlock $nullable cleanup;
} CR_EffectInternal;

struct CR_Hook {
    int type;
    bool deps_initialized;
    CR_DepSnapshot *deps;
    size_t dep_count;
    union {
        struct {
            CR_StateInternal *state;
            void *handle;
            size_t handle_size;
        } state;
        struct {
            void *ptr;
            size_t size;
        } ref;
        struct {
            CR_TextInputState *state;
        } text_input;
        struct {
            void *value;
            size_t size;
            bool initialized;
        } memo;
        struct {
            void *block;
        } callback;
        struct {
            CR_EffectInternal effect;
            bool is_layout;
        } effect;
        struct {
            uint32_t id;
            bool initialized;
        } uid;
        struct {
            CR_SignalInternal *signal;
            void *handle;
            size_t handle_size;
        } signal;
    };
};

CR_StateInternal *_cr_alloc_state(size_t size, void *initial);
void *_cr_state_get(CR_StateInternal *state);
void _cr_state_set(CR_StateInternal *state, void *value);
CR_Hook *_cr_use_hook(int type);
bool _cr_deps_should_run(CR_Hook *hook, CR_DepList deps);
void _cr_deps_store(CR_Hook *hook, CR_DepList deps);
void _cr_schedule_render(void);
uint32_t _cr_next_uid(void);
void _cr_use_effect_impl(EffectBlock effect, CR_DepList deps, bool is_layout);

#define $dep(value) \
    ((CR_Dep){ .ptr = &(typeof(value)){ (value) }, .size = sizeof(value) })

#define _CR_DEP_1(a) $dep(a)
#define _CR_DEP_2(a, b) $dep(a), $dep(b)
#define _CR_DEP_3(a, b, c) $dep(a), $dep(b), $dep(c)
#define _CR_DEP_4(a, b, c, d) $dep(a), $dep(b), $dep(c), $dep(d)
#define _CR_DEP_5(a, b, c, d, e) $dep(a), $dep(b), $dep(c), $dep(d), $dep(e)
#define _CR_DEP_6(a, b, c, d, e, f) $dep(a), $dep(b), $dep(c), $dep(d), $dep(e), $dep(f)
#define _CR_DEP_7(a, b, c, d, e, f, g) $dep(a), $dep(b), $dep(c), $dep(d), $dep(e), $dep(f), $dep(g)
#define _CR_DEP_8(a, b, c, d, e, f, g, h) $dep(a), $dep(b), $dep(c), $dep(d), $dep(e), $dep(f), $dep(g), $dep(h)
#define _CR_DEP_N(N, ...) _CR_DEP_N_(N, __VA_ARGS__)
#define _CR_DEP_N_(N, ...) _CR_DEP_##N(__VA_ARGS__)
#define _CR_DEP_LIST(...) _CR_DEP_N($NARG(__VA_ARGS__), __VA_ARGS__)

#define _CR_GET_MACRO(_1, _2, NAME, ...) NAME
#define _CR_GET_MACRO_3(_1, _2, _3, NAME, ...) NAME

#define $deps(...) \
    ((CR_DepList){ \
        .mode = CR_DEPS_LIST, \
        .items = (CR_Dep[]){ _CR_DEP_LIST(__VA_ARGS__) }, \
        .count = sizeof((CR_Dep[]){ _CR_DEP_LIST(__VA_ARGS__) }) / sizeof(CR_Dep), \
    })

#define $deps_none() ((CR_DepList){ .mode = CR_DEPS_NONE })
#define $deps_once() ((CR_DepList){ .mode = CR_DEPS_ONCE })

/**
 * $use_state - Create reactive state with type-safe handle
 *
 * Usage:
 *   // For primitives:
 *   auto counter = $use_state(0);
 *   printf("%d\n", counter->get());
 *   counter->set(counter->get() + 1);
 *
 *   // For structs:
 *   auto person = $use_state(((Person){ .name = "John", .age = 30 }));
 *   person->ptr->age = 31;  // Direct access via ptr
 */
#define $use_state(...) \
    ({ \
        typedef typeof(__VA_ARGS__) T; \
        typedef CR_STATE_HANDLE(T) CR_StateHandle_T; \
        CR_StateHandle_T *_result = NULL; \
        CR_Hook *_hook = _cr_use_hook(CR_HOOK_STATE); \
        if (_hook) { \
            if (!_hook->state.state) { \
                T _init = (__VA_ARGS__); \
                _hook->state.state = _cr_alloc_state(sizeof(T), &_init); \
            } \
            if (!_hook->state.handle) { \
                _hook->state.handle_size = sizeof(CR_StateHandle_T); \
                _hook->state.handle = _cr_alloc(_hook->state.handle_size); \
                CR_StateHandle_T *_handle = _hook->state.handle; \
                CR_StateInternal *_state = _hook->state.state; \
                _handle->ptr = (T *)_state->value; \
                _handle->_size = sizeof(T); \
                _handle->_state = _state; \
                _handle->get = Block_copy(^T{ return *(T *)_state->value; }); \
                _handle->set = Block_copy(^(T value){ \
                    _cr_state_set(_state, &value); \
                    _cr_schedule_render(); \
                }); \
            } \
            _result = _hook->state.handle; \
        } \
        _result; \
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
#define $use_ref(T, ...) \
    ({ \
        T *_result = NULL; \
        CR_Hook *_hook = _cr_use_hook(CR_HOOK_REF); \
        if (_hook) { \
            if (!_hook->ref.ptr) { \
                _hook->ref.size = sizeof(T); \
                _hook->ref.ptr = _cr_alloc(sizeof(T)); \
                *(T *)_hook->ref.ptr = (T){ __VA_ARGS__ }; \
            } \
            _result = (T *)_hook->ref.ptr; \
        } \
        _result; \
    })

// ============================================================================
// EFFECT IMPLEMENTATION
// ============================================================================

void _cr_run_effect(CR_EffectInternal *effect);

/**
 * $use_effect - Run side effects
 *
 * Usage:
 *   $use_effect(^{
 *       printf("mounted!\n");
 *       return ^{ printf("cleanup!\n"); };
 *   }, $deps_once());
 */
#define _CR_USE_EFFECT_1(effect_block) \
    _cr_use_effect_impl((EffectBlock)effect_block, $deps_none(), false)
#define _CR_USE_EFFECT_2(effect_block, deps) \
    _cr_use_effect_impl((EffectBlock)effect_block, deps, false)
#define $use_effect(...) \
    _CR_GET_MACRO(__VA_ARGS__, _CR_USE_EFFECT_2, _CR_USE_EFFECT_1)(__VA_ARGS__)

#define _CR_USE_LAYOUT_EFFECT_1(effect_block) \
    _cr_use_effect_impl((EffectBlock)effect_block, $deps_none(), true)
#define _CR_USE_LAYOUT_EFFECT_2(effect_block, deps) \
    _cr_use_effect_impl((EffectBlock)effect_block, deps, true)
#define $use_layout_effect(...) \
    _CR_GET_MACRO(__VA_ARGS__, _CR_USE_LAYOUT_EFFECT_2, _CR_USE_LAYOUT_EFFECT_1)(__VA_ARGS__)

// ============================================================================
// MEMO IMPLEMENTATION
// ============================================================================

/**
 * $use_memo - Memoize expensive computations
 *
 * Usage:
 *   auto result = $use_memo(int, ^{ return expensive_calc(); }, $deps(dep1, dep2));
 */
#define _CR_USE_MEMO_2(T, compute_block) \
    _CR_USE_MEMO_3(T, compute_block, $deps_none())
#define _CR_USE_MEMO_3(T, compute_block, deps) \
    ({ \
        T _result = (T){0}; \
        CR_Hook *_hook = _cr_use_hook(CR_HOOK_MEMO); \
        if (_hook) { \
            if (!_hook->memo.value) { \
                _hook->memo.size = sizeof(T); \
                _hook->memo.value = _cr_alloc(sizeof(T)); \
            } \
            bool _should = _cr_deps_should_run(_hook, deps) || !_hook->memo.initialized; \
            if (_should) { \
                T (^_compute)(void) = (T (^)(void))compute_block; \
                T _val = _compute(); \
                memcpy(_hook->memo.value, &_val, sizeof(T)); \
                _hook->memo.initialized = true; \
                _cr_deps_store(_hook, deps); \
            } \
            _result = *(T *)_hook->memo.value; \
        } \
        _result; \
    })
#define $use_memo(...) \
    _CR_GET_MACRO_3(__VA_ARGS__, _CR_USE_MEMO_3, _CR_USE_MEMO_2)(__VA_ARGS__)

// ============================================================================
// CALLBACK IMPLEMENTATION
// ============================================================================

/**
 * $use_callback - Memoize a callback block
 *
 * Usage:
 *   auto onClick = $use_callback(^{ printf("clicked\n"); }, $deps(count));
 */
#define _CR_USE_CALLBACK_1(callback_block) \
    _CR_USE_CALLBACK_2(callback_block, $deps_none())
#define _CR_USE_CALLBACK_2(callback_block, deps) \
    ({ \
        typeof(callback_block) _result = NULL; \
        CR_Hook *_hook = _cr_use_hook(CR_HOOK_CALLBACK); \
        if (_hook) { \
            bool _should = _cr_deps_should_run(_hook, deps) || !_hook->callback.block; \
            if (_should) { \
                if (_hook->callback.block) { \
                    Block_release(_hook->callback.block); \
                } \
                _hook->callback.block = Block_copy(callback_block); \
                _cr_deps_store(_hook, deps); \
            } \
            _result = (typeof(callback_block))_hook->callback.block; \
        } \
        _result; \
    })
#define $use_callback(...) \
    _CR_GET_MACRO(__VA_ARGS__, _CR_USE_CALLBACK_2, _CR_USE_CALLBACK_1)(__VA_ARGS__)

// ============================================================================
// ID IMPLEMENTATION
// ============================================================================

/**
 * $use_id - Stable, component-scoped ID helper
 *
 * Usage:
 *   CR_Id input_id = $use_id("TodoInput");
 */
#define $use_id(prefix) \
    ({ \
        CR_Id _result = cr_id(prefix); \
        CR_Hook *_hook = _cr_use_hook(CR_HOOK_ID); \
        if (_hook) { \
            if (!_hook->uid.initialized) { \
                _hook->uid.id = _cr_next_uid(); \
                _hook->uid.initialized = true; \
            } \
            _result = cr_idi(prefix, _hook->uid.id); \
        } \
        _result; \
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
        static CR_SIGNAL_HANDLE(T) _handle = {0}; \
        static bool _init = false; \
        if (!_init) { \
            _handle.get = Block_copy(^T{ return *(T *)_sig->value; }); \
            _handle.set = Block_copy(^(T v){ \
                *(T *)_sig->value = v; \
                _sig->version++; \
                _cr_signal_notify(_sig); \
                _cr_schedule_render(); \
            }); \
            _handle.subscribe = Block_copy(^(void (^h)(T *value)){ \
                _sig->subscriber_count++; \
                _sig->subscribers = realloc(_sig->subscribers, \
                    _sig->subscriber_count * sizeof(_sig->subscribers[0])); \
                _sig->subscribers[_sig->subscriber_count - 1].handler = Block_copy((SubscriberBlock)h); \
            }); \
            _init = true; \
        } \
        _handle._signal = _sig; \
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
// TEXT DEFAULTS
// ============================================================================

#ifndef $TEXT_DEFAULT_FONT_ID
#define $TEXT_DEFAULT_FONT_ID 0
#endif

#ifndef $TEXT_DEFAULT_SIZE
#define $TEXT_DEFAULT_SIZE 16
#endif

#ifndef $TEXT_DEFAULT_COLOR
#define $TEXT_DEFAULT_COLOR ((Clay_Color){30, 35, 45, 255})
#endif

// ============================================================================
// COLOR HELPERS (must be before components that use them)
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
// PADDING & RADIUS HELPERS (must be before components that use them)
// ============================================================================

#define $pad(all)           CLAY_PADDING_ALL(all)
#define $pad_xy(x, y)       ((Clay_Padding){ .left = x, .right = x, .top = y, .bottom = y })
#define $pad_x(x)           ((Clay_Padding){ .left = x, .right = x })
#define $pad_y(y)           ((Clay_Padding){ .top = y, .bottom = y })
#define $pad_lrtb(l,r,t,b)  ((Clay_Padding){ .left = l, .right = r, .top = t, .bottom = b })

#define $radius(r)      CLAY_CORNER_RADIUS(r)
#define $rounded(r)     CLAY_CORNER_RADIUS(r)
#define $circle()       CLAY_CORNER_RADIUS(9999)

// ============================================================================
// COMPONENT PARAM TYPES
// ============================================================================

typedef BoxParams ClickableParams;

typedef struct {
    CR_Id id;
    const char * $nullable label;
    VoidBlock $nullable on_click;
    ViewStyle style;
    TextConfig text;
} ButtonParams;

typedef struct {
    CR_Id id;
    const char * $nullable icon;
    VoidBlock $nullable on_click;
    ViewStyle style;
    TextConfig text;
} IconButtonParams;

typedef struct {
    CR_Id id;
    bool checked;
    VoidBlock $nullable on_toggle;
    uint16_t size;
    Clay_Color checked_color;
    Clay_Color unchecked_color;
    Clay_Color border_color;
    Clay_CornerRadius corner_radius;
    bool corner_radius_set;
    uint16_t border_width;
    bool border_width_set;
    const char * $nullable checkmark;
    TextConfig checkmark_text;
} CheckboxParams;

typedef struct {
    CR_Id id;
    CR_TextInputState * $nullable state;
    ViewStyle style;
    Clay_BorderElementConfig focus_border;
    bool has_focus_border;
    const char * $nullable placeholder;
    TextConfig text;
    TextConfig placeholder_text;
} TextInputParams;

// ============================================================================
// CORE COMPONENT FUNCTIONS
// ============================================================================

static $always_inline Clay_String cr_string(const char *text) {
    if (!text) return CLAY_STRING("");
    return (Clay_String){
        .isStaticallyAllocated = false,
        .length = (int32_t)strlen(text),
        .chars = text,
    };
}

static $always_inline Clay_ElementId cr_element_id(CR_Id id) {
    if (!id.name) return (Clay_ElementId){0};
    Clay_String name = cr_string(id.name);
    return id.indexed ? CLAY_SIDI(name, id.index) : CLAY_SID(name);
}

static $always_inline bool _cr_border_has_value(Clay_BorderElementConfig border) {
    return border.width.left || border.width.right || border.width.top ||
           border.width.bottom || border.width.betweenChildren || border.color.a;
}

static $always_inline bool _cr_corner_has_value(Clay_CornerRadius radius) {
    return radius.topLeft || radius.topRight || radius.bottomLeft || radius.bottomRight;
}

static $always_inline bool _cr_layout_is_zero(Clay_LayoutConfig layout) {
    const Clay_LayoutConfig empty = {0};
    return memcmp(&layout, &empty, sizeof(layout)) == 0;
}

static $always_inline bool _cr_style_has_background(ViewStyle style) {
    return style.has_background || style.background.a > 0;
}

static $always_inline bool _cr_style_has_background_hover(ViewStyle style) {
    return style.has_background_hover || style.background_hover.a > 0;
}

static $always_inline bool _cr_style_has_border(ViewStyle style) {
    return style.has_border || _cr_border_has_value(style.border);
}

static $always_inline bool _cr_style_has_corner(ViewStyle style) {
    return style.has_corner_radius || _cr_corner_has_value(style.corner_radius);
}

static $always_inline Clay_TextElementConfig *_cr_text_config(TextConfig cfg, Clay_Color default_color, uint16_t default_size) {
    Clay_TextElementConfig config = {
        .fontId = cfg.font_id ? cfg.font_id : $TEXT_DEFAULT_FONT_ID,
        .fontSize = cfg.font_size ? cfg.font_size : default_size,
        .lineHeight = cfg.line_height,
        .letterSpacing = cfg.letter_spacing,
        .wrapMode = cfg.wrap_mode ? cfg.wrap_mode : CLAY_TEXT_WRAP_WORDS,
        .textAlignment = cfg.text_alignment ? cfg.text_alignment : CLAY_TEXT_ALIGN_LEFT,
        .textColor = cfg.color.a ? cfg.color : default_color,
    };
    return CLAY_TEXT_CONFIG(config);
}

static $always_inline void _cr_apply_view_style(Clay_ElementDeclaration *decl, ViewStyle style, bool hovered) {
    decl->layout = style.layout;
    if (_cr_style_has_background(style) || _cr_style_has_background_hover(style)) {
        Clay_Color bg = _cr_style_has_background(style) ? style.background : (Clay_Color){0};
        if (_cr_style_has_background_hover(style) && hovered) {
            bg = style.background_hover;
        }
        decl->backgroundColor = bg;
    }
    if (_cr_style_has_corner(style)) {
        decl->cornerRadius = style.corner_radius;
    }
    if (_cr_style_has_border(style)) {
        decl->border = style.border;
    }
}

/**
 * Box - Generic container
 */
static $always_inline void Box(BoxParams params, VoidBlock children) {
    Clay_ElementId eid = cr_element_id(params.id);
    if (params.on_click && eid.id != 0) {
        _cr_register_click(eid.id, Block_copy(params.on_click));
    }

    bool wants_hover = _cr_style_has_background_hover(params.style);
    bool hovered = false;
    bool opened = false;
    if (wants_hover && eid.id == 0) {
        Clay__OpenElement();
        opened = true;
        hovered = Clay_Hovered();
    } else if (wants_hover && eid.id != 0) {
        hovered = Clay_PointerOver(eid);
    }

    Clay_ElementDeclaration decl = {0};
    decl.id = eid;
    _cr_apply_view_style(&decl, params.style, hovered);
    if (params.scroll_x || params.scroll_y) {
        decl.clip = (Clay_ClipElementConfig){
            .horizontal = params.scroll_x,
            .vertical = params.scroll_y,
            .childOffset = Clay_GetScrollOffset(),
        };
    }

    if (!opened) {
        Clay__OpenElement();
    }
    Clay__ConfigureOpenElement(decl);
    if (children) children();
    Clay__CloseElement();
}

/**
 * Row - Horizontal layout container
 */
static $always_inline void Row(BoxParams params, VoidBlock children) {
    params.style.layout.layoutDirection = CLAY_LEFT_TO_RIGHT;
    Box(params, children);
}

/**
 * Column - Vertical layout container
 */
static $always_inline void Column(BoxParams params, VoidBlock children) {
    params.style.layout.layoutDirection = CLAY_TOP_TO_BOTTOM;
    Box(params, children);
}

/**
 * Center - Centered content container
 */
static $always_inline void Center(BoxParams params, VoidBlock children) {
    params.style.layout.childAlignment = (Clay_ChildAlignment){
        .x = CLAY_ALIGN_X_CENTER,
        .y = CLAY_ALIGN_Y_CENTER,
    };
    Box(params, children);
}

/**
 * Spacer - Flexible space
 */
static $always_inline void Spacer(void) {
    Box((BoxParams){
        .style = {
            .layout = { .sizing = { CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0) } },
        },
    }, NULL);
}

/**
 * HSpacer - Horizontal spacer
 */
static $always_inline void HSpacer(void) {
    Box((BoxParams){
        .style = {
            .layout = { .sizing = { .width = CLAY_SIZING_GROW(0) } },
        },
    }, NULL);
}

/**
 * VSpacer - Vertical spacer
 */
static $always_inline void VSpacer(void) {
    Box((BoxParams){
        .style = {
            .layout = { .sizing = { .height = CLAY_SIZING_GROW(0) } },
        },
    }, NULL);
}

/**
 * Flex - Flexible container that takes available space
 */
static $always_inline void Flex(BoxParams params, VoidBlock children) {
    params.style.layout.sizing.width = CLAY_SIZING_GROW(0);
    Box(params, children);
}

/**
 * Card - Styled card container
 */
static $always_inline void Card(BoxParams params, VoidBlock children) {
    if (_cr_layout_is_zero(params.style.layout)) {
        params.style.layout.padding = $pad(16);
    }
    if (!_cr_style_has_corner(params.style)) {
        params.style.corner_radius = $radius(12);
        params.style.has_corner_radius = true;
    }
    if (!_cr_style_has_background(params.style)) {
        params.style.background = $WHITE;
        params.style.has_background = true;
    }
    Box(params, children);
}

/**
 * Text - Render text
 */
static $always_inline void Text(TextParams params) {
    Clay_String text = cr_string(params.text);
    Clay__OpenTextElement(text, _cr_text_config(params.style, $TEXT_DEFAULT_COLOR, $TEXT_DEFAULT_SIZE));
}

/**
 * Textf - Render formatted text
 */
static $always_inline void Textf(TextParams params, const char *fmt, ...) {
    if (!fmt) {
        Text(params);
        return;
    }

    va_list args;
    va_start(args, fmt);
    int needed = vsnprintf(NULL, 0, fmt, args);
    va_end(args);
    if (needed < 0) return;

    size_t size = (size_t)needed + 1;
    char *buffer = _cr_temp_string_alloc(size);
    if (!buffer) return;

    va_start(args, fmt);
    vsnprintf(buffer, size, fmt, args);
    va_end(args);

    params.text = buffer;
    Text(params);
}

/**
 * Clickable - Generic clickable container
 */
static $always_inline void Clickable(ClickableParams params, VoidBlock children) {
    Box(params, children);
}

/**
 * Button - Button with label and click handler
 */
static $always_inline void Button(ButtonParams params, VoidBlock children) {
    ViewStyle style = params.style;
    if (_cr_layout_is_zero(style.layout)) {
        style.layout = (Clay_LayoutConfig){
            .padding = { 16, 16, 10, 10 },
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
        };
    }
    if (!_cr_style_has_corner(style)) {
        style.corner_radius = CLAY_CORNER_RADIUS(6);
        style.has_corner_radius = true;
    }
    if (!_cr_style_has_background(style)) {
        style.background = $BLUE;
        style.has_background = true;
    }

    Clay_ElementId eid = cr_element_id(params.id);
    if (params.on_click && eid.id != 0) {
        _cr_register_click(eid.id, Block_copy(params.on_click));
    }

    Clay_ElementDeclaration decl = {0};
    decl.id = eid;
    bool hovered = _cr_style_has_background_hover(style) && eid.id != 0 && Clay_PointerOver(eid);
    _cr_apply_view_style(&decl, style, hovered);

    Clay__OpenElement();
    Clay__ConfigureOpenElement(decl);

    if (children) {
        children();
    } else if (params.label) {
        TextConfig text = params.text;
        if (!text.color.a) {
            text.color = (Clay_Color){255, 255, 255, 255};
        }
        Clay__OpenTextElement(cr_string(params.label), _cr_text_config(text, (Clay_Color){255, 255, 255, 255}, 16));
    }

    Clay__CloseElement();
}

/**
 * IconButton - Compact icon button
 */
static $always_inline void IconButton(IconButtonParams params, VoidBlock children) {
    ViewStyle style = params.style;
    if (_cr_layout_is_zero(style.layout)) {
        style.layout = (Clay_LayoutConfig){
            .sizing = { .width = CLAY_SIZING_FIXED(28), .height = CLAY_SIZING_FIXED(28) },
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
        };
    }
    if (!_cr_style_has_corner(style)) {
        style.corner_radius = CLAY_CORNER_RADIUS(4);
        style.has_corner_radius = true;
    }
    if (!_cr_style_has_background(style)) {
        style.background = $BLUE;
        style.has_background = true;
    }

    Clay_ElementId eid = cr_element_id(params.id);
    if (params.on_click && eid.id != 0) {
        _cr_register_click(eid.id, Block_copy(params.on_click));
    }

    Clay_ElementDeclaration decl = {0};
    decl.id = eid;
    bool hovered = _cr_style_has_background_hover(style) && eid.id != 0 && Clay_PointerOver(eid);
    _cr_apply_view_style(&decl, style, hovered);

    Clay__OpenElement();
    Clay__ConfigureOpenElement(decl);

    if (children) {
        children();
    } else if (params.icon) {
        TextConfig text = params.text;
        if (!text.color.a) {
            text.color = (Clay_Color){255, 255, 255, 255};
        }
        Clay__OpenTextElement(cr_string(params.icon), _cr_text_config(text, (Clay_Color){255, 255, 255, 255}, 16));
    }

    Clay__CloseElement();
}

/**
 * Checkbox - Checkbox with toggle callback
 */
static $always_inline void Checkbox(CheckboxParams params) {
    Clay_ElementId eid = cr_element_id(params.id);
    if (params.on_toggle && eid.id != 0) {
        _cr_register_click(eid.id, Block_copy(params.on_toggle));
    }

    uint16_t size = params.size ? params.size : 24;
    uint16_t border_width = params.border_width_set ? params.border_width : 2;
    Clay_Color checked_color = params.checked_color.a ? params.checked_color : $BLUE;
    Clay_Color unchecked_color = params.unchecked_color.a ? params.unchecked_color : $WHITE;
    Clay_Color border_color = params.border_color.a ? params.border_color : $gray(150);
    Clay_CornerRadius corner = params.corner_radius_set ? params.corner_radius : CLAY_CORNER_RADIUS(4);

    Clay__OpenElement();
    Clay__ConfigureOpenElement((Clay_ElementDeclaration){
        .id = eid,
        .layout = {
            .sizing = { .width = CLAY_SIZING_FIXED(size), .height = CLAY_SIZING_FIXED(size) },
            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
        },
        .backgroundColor = params.checked ? checked_color : unchecked_color,
        .cornerRadius = corner,
        .border = params.checked ? (Clay_BorderElementConfig){0} :
            (Clay_BorderElementConfig){ .width = CLAY_BORDER_OUTSIDE(border_width), .color = border_color },
    });

    if (params.checked) {
        const char *mark = params.checkmark ? params.checkmark : "*";
        TextConfig text = params.checkmark_text;
        if (!text.color.a) {
            text.color = $WHITE;
        }
        Clay__OpenTextElement(cr_string(mark), _cr_text_config(text, $WHITE, 16));
    }

    Clay__CloseElement();
}

/**
 * TextInput - Editable text input field
 */
static $always_inline void TextInput(TextInputParams params) {
    if (!params.state) return;

    Clay_ElementId eid = cr_element_id(params.id);
    if (eid.id != 0) {
        _cr_register_click(eid.id, Block_copy(^{ _cr_focus_input(params.state, eid.id); }));
    }

    ViewStyle style = params.style;
    if (_cr_layout_is_zero(style.layout)) {
        style.layout = (Clay_LayoutConfig){
            .sizing = { .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_FIT(.min = 40) },
            .padding = { 12, 12, 10, 10 },
            .childAlignment = { .y = CLAY_ALIGN_Y_CENTER },
        };
    }
    if (!_cr_style_has_background(style)) {
        style.background = $WHITE;
        style.has_background = true;
    }
    if (!_cr_style_has_corner(style)) {
        style.corner_radius = CLAY_CORNER_RADIUS(6);
        style.has_corner_radius = true;
    }

    Clay_BorderElementConfig normal_border = _cr_style_has_border(style) ?
        style.border :
        (Clay_BorderElementConfig){ .width = CLAY_BORDER_OUTSIDE(1), .color = $gray(200) };
    Clay_BorderElementConfig focus_border = params.has_focus_border || _cr_border_has_value(params.focus_border) ?
        params.focus_border :
        (Clay_BorderElementConfig){ .width = CLAY_BORDER_OUTSIDE(2), .color = $BLUE };

    Clay_ElementDeclaration decl = {0};
    decl.id = eid;
    bool hovered = _cr_style_has_background_hover(style) && eid.id != 0 && Clay_PointerOver(eid);
    _cr_apply_view_style(&decl, style, hovered);
    bool is_focused = params.state->focused &&
        (params.state->element_id == 0 || params.state->element_id == eid.id);
    decl.border = is_focused ? focus_border : normal_border;

    Clay__OpenElement();
    Clay__ConfigureOpenElement(decl);

    const char *text = params.state->length > 0 ? params.state->buffer : params.placeholder;
    TextConfig text_style = params.state->length > 0 ? params.text : params.placeholder_text;
    Clay_Color default_color = params.state->length > 0 ? $gray(30) : $gray(150);
    uint16_t default_size = params.text.font_size ? params.text.font_size : 16;

    Clay__OpenTextElement(cr_string(text ? text : ""), _cr_text_config(text_style, default_color, default_size));

    Clay__CloseElement();
}

// ============================================================================
// TEXT INPUT STATE
// ============================================================================

/**
 * $use_text_input - Create a persistent text input state
 *
 * Usage:
 *   auto input = $use_text_input(64);  // 64 byte buffer
 */
#define $use_text_input(buffer_size) \
    ({ \
        CR_TextInputState *_input = NULL; \
        CR_Hook *_hook = _cr_use_hook(CR_HOOK_TEXT_INPUT); \
        if (_hook) { \
            if (!_hook->text_input.state) { \
                _hook->text_input.state = _cr_alloc_text_input(buffer_size); \
            } \
            _input = _hook->text_input.state; \
        } \
        _input; \
    })


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
#define $create_context(T, ...) \
    _cr_create_context_impl(#T, sizeof(T), __VA_ARGS__, $reflect(T))

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
 * $component - Define a component (with or without props)
 *
 * Usage:
 *   $component(Spinner) {
 *       Text((TextParams){ .text = "Loading..." });
 *   }
 *
 *   typedef struct { const char *title; int count; } CardProps;
 *   $component(Card, CardProps) {
 *       Column((BoxParams){
 *           .style = {
 *               .layout = { .padding = $pad(16) },
 *               .background = $WHITE,
 *           },
 *       }, ^{
 *           Textf((TextParams){0}, "%s: %d", props->title, props->count);
 *       });
 *   }
 */
#define _CR_COMPONENT_1(name) \
    void name##_render(void); \
    static inline void name(void) { \
        _cr_component_begin(#name, NULL, 0); \
        name##_render(); \
        _cr_component_end(); \
    } \
    void name##_render(void)

#define _CR_COMPONENT_2(name, props_type) \
    void name##_render(props_type *props); \
    static inline void name(props_type p) { \
        props_type _props = p; \
        _cr_component_begin(#name, &_props, sizeof(_props)); \
        props_type *_props_ptr = (props_type *)_cr_current_props(); \
        name##_render(_props_ptr ? _props_ptr : &_props); \
        _cr_component_end(); \
    } \
    void name##_render(props_type *props)

#define $component(...) \
    _CR_GET_MACRO(__VA_ARGS__, _CR_COMPONENT_2, _CR_COMPONENT_1)(__VA_ARGS__)

// ============================================================================
// SIZING HELPERS
// ============================================================================

#define $grow(...)     CLAY_SIZING_GROW(__VA_ARGS__)
#define $fit(...)      CLAY_SIZING_FIT(__VA_ARGS__)
#define $fixed(px)     CLAY_SIZING_FIXED(px)
#define $percent(p)    CLAY_SIZING_PERCENT(p)
#define $fill()        (Clay_Sizing){ .width = CLAY_SIZING_GROW(0), .height = CLAY_SIZING_GROW(0) }

// NOTE: Padding, radius, and color helpers are defined earlier in the file
//       See: $pad, $radius, $rgb, $WHITE, etc.

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
#define $key(name)      cr_key(cr_id(name))
#define $keyi(name, i)  cr_key(cr_idi(name, i))

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

// Component lifecycle (used by $component wrappers)
void _cr_component_begin(const char *name, const void *props, size_t props_size);
void _cr_component_end(void);
void *_cr_current_props(void);
void cr_key(CR_Id key);

// Debug
void cr_debug_enable(bool enabled);
void cr_debug_log_tree(void);

#ifdef __cplusplus
}
#endif

#pragma clang diagnostic pop
