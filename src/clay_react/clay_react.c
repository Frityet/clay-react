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
static uint64_t _cr_next_component_id = 1;

// ============================================================================
// COMPONENTS & HOOKS
// ============================================================================

struct CR_Component {
    const char *name;
    uint64_t id;
    CR_Component *parent;
    CR_Component **children;
    size_t child_count;
    size_t child_capacity;
    size_t child_cursor;
    CR_Id key;
    bool keyed;
    CR_Hook *hooks;
    size_t hook_count;
    size_t hook_capacity;
    size_t hook_cursor;
    uint64_t last_render_frame;
    void *props_copy;
    size_t props_size;
};

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

static void _cr_clear_temp_strings(void) {
    if (!cr_runtime || !cr_runtime->temp_strings) {
        return;
    }

    for (size_t i = 0; i < cr_runtime->temp_string_count; i++) {
        _cr_free(cr_runtime->temp_strings[i].ptr, cr_runtime->temp_strings[i].size);
    }
    cr_runtime->temp_string_count = 0;
}

char *_cr_temp_string_alloc(size_t size) {
    if (!cr_runtime) {
        cr_init();
    }
    if (!cr_runtime || size == 0) {
        return NULL;
    }

    if (cr_runtime->temp_string_count >= cr_runtime->temp_string_capacity) {
        size_t new_cap = cr_runtime->temp_string_capacity == 0 ? 64 : cr_runtime->temp_string_capacity * 2;
        CR_TempString *new_list = realloc(cr_runtime->temp_strings, new_cap * sizeof(CR_TempString));
        if (!new_list) {
            return NULL;
        }
        cr_runtime->temp_strings = new_list;
        cr_runtime->temp_string_capacity = new_cap;
    }

    char *buffer = _cr_alloc(size);
    if (!buffer) {
        return NULL;
    }

    cr_runtime->temp_strings[cr_runtime->temp_string_count++] = (CR_TempString){
        .ptr = buffer,
        .size = size,
    };
    return buffer;
}

// ============================================================================
// HOOK & COMPONENT HELPERS
// ============================================================================

typedef struct {
    void *get;
    void *set;
} CR_StateHandleBlocks;

static bool _cr_id_equal(CR_Id a, CR_Id b) {
    if (!a.name || !b.name) return false;
    if (strcmp(a.name, b.name) != 0) return false;
    if (a.indexed != b.indexed) return false;
    if (a.indexed && a.index != b.index) return false;
    return true;
}

static bool _cr_ensure_capacity(void **buffer, size_t *capacity, size_t needed, size_t item_size) {
    if (*capacity >= needed) {
        return true;
    }
    size_t new_cap = *capacity == 0 ? 8 : *capacity;
    while (new_cap < needed) {
        new_cap *= 2;
    }
    void *new_buffer = realloc(*buffer, new_cap * item_size);
    if (!new_buffer) {
        return false;
    }
    *buffer = new_buffer;
    *capacity = new_cap;
    return true;
}

static void _cr_register_component(CR_Component *component) {
    if (!cr_runtime || !component) return;
    if (!_cr_ensure_capacity((void **)&cr_runtime->components,
            &cr_runtime->component_capacity,
            cr_runtime->component_count + 1,
            sizeof(*cr_runtime->components))) {
        return;
    }
    cr_runtime->components[cr_runtime->component_count++] = component;
}

static void _cr_unregister_component(CR_Component *component) {
    if (!cr_runtime || !component) return;
    for (size_t i = 0; i < cr_runtime->component_count; i++) {
        if (cr_runtime->components[i] == component) {
            cr_runtime->components[i] = cr_runtime->components[cr_runtime->component_count - 1];
            cr_runtime->component_count--;
            return;
        }
    }
}

static void _cr_component_add_child(CR_Component *parent, CR_Component *child, size_t index) {
    if (!parent || !child) return;
    if (!_cr_ensure_capacity((void **)&parent->children,
            &parent->child_capacity,
            parent->child_count + 1,
            sizeof(*parent->children))) {
        return;
    }
    if (index > parent->child_count) {
        index = parent->child_count;
    }
    if (index < parent->child_count) {
        memmove(&parent->children[index + 1],
                &parent->children[index],
                (parent->child_count - index) * sizeof(*parent->children));
    }
    parent->children[index] = child;
    parent->child_count++;
}

static void _cr_component_remove_child(CR_Component *parent, CR_Component *child) {
    if (!parent || !child || parent->child_count == 0) return;
    for (size_t i = 0; i < parent->child_count; i++) {
        if (parent->children[i] == child) {
            if (i + 1 < parent->child_count) {
                memmove(&parent->children[i],
                        &parent->children[i + 1],
                        (parent->child_count - i - 1) * sizeof(*parent->children));
            }
            parent->child_count--;
            return;
        }
    }
}

static void _cr_clear_deps(CR_Hook *hook) {
    if (!hook) return;
    if (hook->deps) {
        for (size_t i = 0; i < hook->dep_count; i++) {
            if (hook->deps[i].data) {
                _cr_free(hook->deps[i].data, hook->deps[i].size);
            }
        }
        free(hook->deps);
    }
    hook->deps = NULL;
    hook->dep_count = 0;
    hook->deps_initialized = false;
}

static void _cr_cleanup_hook(CR_Hook *hook) {
    if (!hook) return;

    _cr_clear_deps(hook);

    switch (hook->type) {
        case CR_HOOK_STATE: {
            if (hook->state.handle) {
                CR_StateHandleBlocks *blocks = (CR_StateHandleBlocks *)hook->state.handle;
                if (blocks->get) Block_release(blocks->get);
                if (blocks->set) Block_release(blocks->set);
                _cr_free(hook->state.handle, hook->state.handle_size);
            }
            if (hook->state.state) {
                _cr_free(hook->state.state->value, hook->state.state->size);
                _cr_free(hook->state.state, sizeof(*hook->state.state));
            }
            break;
        }
        case CR_HOOK_REF:
            if (hook->ref.ptr) {
                _cr_free(hook->ref.ptr, hook->ref.size);
            }
            break;
        case CR_HOOK_TEXT_INPUT:
            if (hook->text_input.state) {
                free(hook->text_input.state->buffer);
                free(hook->text_input.state);
            }
            break;
        case CR_HOOK_MEMO:
            if (hook->memo.value) {
                _cr_free(hook->memo.value, hook->memo.size);
            }
            break;
        case CR_HOOK_CALLBACK:
            if (hook->callback.block) {
                Block_release(hook->callback.block);
            }
            break;
        case CR_HOOK_EFFECT:
            if (hook->effect.effect.cleanup) {
                hook->effect.effect.cleanup();
                Block_release(hook->effect.effect.cleanup);
            }
            if (hook->effect.effect.effect) {
                Block_release(hook->effect.effect.effect);
            }
            break;
        case CR_HOOK_ID:
        case CR_HOOK_SIGNAL:
        case CR_HOOK_NONE:
        default:
            break;
    }

    *hook = (CR_Hook){0};
}

static void _cr_destroy_component(CR_Component *component) {
    if (!component) return;

    while (component->child_count > 0) {
        CR_Component *child = component->children[component->child_count - 1];
        _cr_destroy_component(child);
    }

    if (component->parent) {
        _cr_component_remove_child(component->parent, component);
    }

    for (size_t i = 0; i < component->hook_count; i++) {
        _cr_cleanup_hook(&component->hooks[i]);
    }

    if (component->hooks) {
        free(component->hooks);
    }
    if (component->children) {
        free(component->children);
    }
    if (component->props_copy) {
        _cr_free(component->props_copy, component->props_size);
    }

    if (cr_runtime && cr_runtime->root == component) {
        cr_runtime->root = NULL;
    }

    _cr_unregister_component(component);
    _cr_free(component, sizeof(*component));
}

static CR_Component *_cr_create_component(const char *name, CR_Component *parent, bool keyed, CR_Id key) {
    CR_Component *component = _cr_alloc(sizeof(CR_Component));
    if (!component) return NULL;
    component->name = name;
    component->id = _cr_next_component_id++;
    component->parent = parent;
    component->keyed = keyed;
    component->key = key;
    component->children = NULL;
    component->child_count = 0;
    component->child_capacity = 0;
    component->child_cursor = 0;
    component->hooks = NULL;
    component->hook_count = 0;
    component->hook_capacity = 0;
    component->hook_cursor = 0;
    component->last_render_frame = 0;
    component->props_copy = NULL;
    component->props_size = 0;

    _cr_register_component(component);
    return component;
}

static void _cr_move_child(CR_Component *parent, size_t from, size_t to) {
    if (!parent || from == to || parent->child_count == 0) return;
    if (to >= parent->child_count) {
        to = parent->child_count - 1;
    }
    CR_Component *child = parent->children[from];
    if (from < to) {
        memmove(&parent->children[from],
                &parent->children[from + 1],
                (to - from) * sizeof(*parent->children));
    } else {
        memmove(&parent->children[to + 1],
                &parent->children[to],
                (from - to) * sizeof(*parent->children));
    }
    parent->children[to] = child;
}

static CR_Component *_cr_find_child_by_key(CR_Component *parent, CR_Id key, const char *name, size_t index) {
    if (!parent || !name) return NULL;
    size_t target = index;
    if (target >= parent->child_count && parent->child_count > 0) {
        target = parent->child_count - 1;
    }
    for (size_t i = 0; i < parent->child_count; i++) {
        CR_Component *child = parent->children[i];
        if (child->keyed && _cr_id_equal(child->key, key)) {
            if (strcmp(child->name, name) != 0) {
                _cr_destroy_component(child);
                return NULL;
            }
            if (i != target) {
                _cr_move_child(parent, i, target);
            }
            return parent->children[target];
        }
    }
    return NULL;
}

static CR_Component *_cr_get_child_by_index(CR_Component *parent, const char *name, size_t index) {
    if (!parent || !name) return NULL;
    if (index >= parent->child_count) {
        return NULL;
    }
    CR_Component *child = parent->children[index];
    if (!child || child->keyed || strcmp(child->name, name) != 0) {
        if (child) {
            _cr_destroy_component(child);
        }
        return NULL;
    }
    return child;
}

static void _cr_queue_effect(CR_Hook *hook, bool is_layout) {
    if (!cr_runtime || !hook || !cr_runtime->current_component) return;
    CR_Component *component = cr_runtime->current_component;
    if (!component || !component->hooks) return;

    size_t hook_index = (size_t)(hook - component->hooks);
    CR_EffectRef ref = {
        .component = component,
        .hook_index = hook_index,
        .component_id = component->id,
    };

    CR_EffectRef **queue = is_layout ? &cr_runtime->pending_layout_effects : &cr_runtime->pending_effects;
    size_t *count = is_layout ? &cr_runtime->pending_layout_effect_count : &cr_runtime->pending_effect_count;
    size_t *capacity = is_layout ? &cr_runtime->pending_layout_effect_capacity : &cr_runtime->pending_effect_capacity;
    if (!_cr_ensure_capacity((void **)queue, capacity, *count + 1, sizeof(CR_EffectRef))) {
        return;
    }
    (*queue)[(*count)++] = ref;
}

static bool _cr_component_is_alive(CR_Component *component, uint64_t id) {
    if (!cr_runtime || !component) return false;
    for (size_t i = 0; i < cr_runtime->component_count; i++) {
        if (cr_runtime->components[i] == component) {
            return component->id == id;
        }
    }
    return false;
}

static void _cr_flush_effect_queue(CR_EffectRef *queue, size_t *count) {
    if (!queue || !count) return;
    for (size_t i = 0; i < *count; i++) {
        CR_EffectRef *ref = &queue[i];
        if (!_cr_component_is_alive(ref->component, ref->component_id)) {
            continue;
        }
        CR_Component *component = ref->component;
        if (!component || ref->hook_index >= component->hook_count) {
            continue;
        }
        CR_Hook *hook = &component->hooks[ref->hook_index];
        if (hook->type == CR_HOOK_EFFECT) {
            _cr_run_effect(&hook->effect.effect);
        }
    }
    *count = 0;
}

static void _cr_collect_garbage(void) {
    if (!cr_runtime) return;
    for (size_t i = 0; i < cr_runtime->component_count; ) {
        CR_Component *component = cr_runtime->components[i];
        if (component->last_render_frame != cr_runtime->frame) {
            _cr_destroy_component(component);
            continue;
        }
        i++;
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
    cr_runtime->temp_strings = NULL;
    cr_runtime->temp_string_count = 0;
    cr_runtime->temp_string_capacity = 0;
    cr_runtime->components = NULL;
    cr_runtime->component_count = 0;
    cr_runtime->component_capacity = 0;
    cr_runtime->component_stack = NULL;
    cr_runtime->component_stack_count = 0;
    cr_runtime->component_stack_capacity = 0;
    cr_runtime->pending_effects = NULL;
    cr_runtime->pending_effect_count = 0;
    cr_runtime->pending_effect_capacity = 0;
    cr_runtime->pending_layout_effects = NULL;
    cr_runtime->pending_layout_effect_count = 0;
    cr_runtime->pending_layout_effect_capacity = 0;
    cr_runtime->needs_render = true;
    cr_runtime->next_uid = 1;
    cr_runtime->has_next_key = false;
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

    // Free temp strings
    _cr_clear_temp_strings();
    if (cr_runtime->temp_strings) {
        free(cr_runtime->temp_strings);
    }

    // Free context stack
    while (cr_runtime->context_stack) {
        CR_ContextProvider *next = cr_runtime->context_stack->parent;
        free(cr_runtime->context_stack);
        cr_runtime->context_stack = next;
    }

    // Destroy component tree
    if (cr_runtime->root) {
        _cr_destroy_component(cr_runtime->root);
    }

    if (cr_runtime->components) {
        free(cr_runtime->components);
    }
    if (cr_runtime->component_stack) {
        free(cr_runtime->component_stack);
    }
    if (cr_runtime->pending_effects) {
        free(cr_runtime->pending_effects);
    }
    if (cr_runtime->pending_layout_effects) {
        free(cr_runtime->pending_layout_effects);
    }

    free(cr_runtime);
    cr_runtime = NULL;
}

void cr_begin_frame(void) {
    if (!cr_runtime) {
        cr_init();
    }

    _cr_clear_temp_strings();

    cr_runtime->is_rendering = true;
    cr_runtime->frame++;
    cr_runtime->needs_render = false;
    cr_runtime->current_component = NULL;
    cr_runtime->component_stack_count = 0;
    cr_runtime->has_next_key = false;

    // Clear handlers from previous frame
    _cr_clear_handlers();

    Clay_BeginLayout();
}

Clay_RenderCommandArray cr_end_frame(void) {
    Clay_RenderCommandArray commands = Clay_EndLayout();
    _cr_flush_effect_queue(cr_runtime->pending_layout_effects, &cr_runtime->pending_layout_effect_count);
    _cr_flush_effect_queue(cr_runtime->pending_effects, &cr_runtime->pending_effect_count);
    _cr_collect_garbage();
    cr_runtime->is_rendering = false;
    return commands;
}

bool cr_should_render(void) {
    if (!cr_runtime) {
        return true;
    }
    return cr_runtime->needs_render;
}

void cr_request_render(void) {
    _cr_schedule_render();
}

// ============================================================================
// HOOKS & COMPONENT LIFECYCLE
// ============================================================================

void _cr_schedule_render(void) {
    if (!cr_runtime) {
        cr_init();
    }
    if (cr_runtime) {
        cr_runtime->needs_render = true;
    }
}

uint32_t _cr_next_uid(void) {
    if (!cr_runtime) {
        cr_init();
    }
    if (!cr_runtime) {
        return 0;
    }
    return cr_runtime->next_uid++;
}

CR_Hook *_cr_use_hook(int type) {
    static bool warned = false;

    if (!cr_runtime) {
        cr_init();
    }
    if (!cr_runtime || !cr_runtime->current_component) {
        if (!warned) {
            fprintf(stderr, "Clay React: hooks can only be used inside components\n");
            warned = true;
        }
        return NULL;
    }

    CR_Component *component = cr_runtime->current_component;
    if (component->hook_cursor >= component->hook_count) {
        if (!_cr_ensure_capacity((void **)&component->hooks,
                &component->hook_capacity,
                component->hook_count + 1,
                sizeof(*component->hooks))) {
            return NULL;
        }
        component->hooks[component->hook_count] = (CR_Hook){ .type = type };
        component->hook_count++;
    }

    CR_Hook *hook = &component->hooks[component->hook_cursor++];
    if (hook->type != type) {
        _cr_cleanup_hook(hook);
        *hook = (CR_Hook){ .type = type };
    }

    return hook;
}

bool _cr_deps_should_run(CR_Hook *hook, CR_DepList deps) {
    if (!hook) return false;

    switch (deps.mode) {
        case CR_DEPS_NONE:
            return true;
        case CR_DEPS_ONCE:
            return !hook->deps_initialized;
        case CR_DEPS_LIST:
            if (deps.count == 0) {
                return !hook->deps_initialized;
            }
            if (!hook->deps_initialized || hook->dep_count != deps.count) {
                return true;
            }
            for (size_t i = 0; i < deps.count; i++) {
                const CR_Dep *dep = &deps.items[i];
                const CR_DepSnapshot *snapshot = &hook->deps[i];
                if (snapshot->size != dep->size) {
                    return true;
                }
                if (memcmp(snapshot->data, dep->ptr, dep->size) != 0) {
                    return true;
                }
            }
            return false;
    }

    return true;
}

void _cr_deps_store(CR_Hook *hook, CR_DepList deps) {
    if (!hook) return;

    if (deps.mode != CR_DEPS_LIST || deps.count == 0) {
        _cr_clear_deps(hook);
        hook->deps_initialized = true;
        return;
    }

    if (hook->dep_count != deps.count) {
        _cr_clear_deps(hook);
        hook->deps = calloc(deps.count, sizeof(CR_DepSnapshot));
        hook->dep_count = deps.count;
    }

    for (size_t i = 0; i < deps.count; i++) {
        const CR_Dep *dep = &deps.items[i];
        CR_DepSnapshot *snapshot = &hook->deps[i];
        if (snapshot->size != dep->size || !snapshot->data) {
            if (snapshot->data) {
                _cr_free(snapshot->data, snapshot->size);
            }
            snapshot->data = _cr_alloc(dep->size);
            snapshot->size = dep->size;
        }
        if (snapshot->data) {
            memcpy(snapshot->data, dep->ptr, dep->size);
        }
    }

    hook->deps_initialized = true;
}

void _cr_use_effect_impl(EffectBlock effect, CR_DepList deps, bool is_layout) {
    if (!effect) return;
    CR_Hook *hook = _cr_use_hook(CR_HOOK_EFFECT);
    if (!hook) return;

    bool should_run = _cr_deps_should_run(hook, deps);
    if (!should_run) {
        return;
    }

    if (hook->effect.effect.effect) {
        Block_release(hook->effect.effect.effect);
    }
    hook->effect.effect.effect = Block_copy(effect);
    hook->effect.is_layout = is_layout;
    _cr_deps_store(hook, deps);
    _cr_queue_effect(hook, is_layout);
}

void cr_key(CR_Id key) {
    if (!cr_runtime) {
        cr_init();
    }
    if (!cr_runtime) return;
    cr_runtime->next_key = key;
    cr_runtime->has_next_key = true;
}

void _cr_component_begin(const char *name, const void *props, size_t props_size) {
    if (!cr_runtime) {
        cr_init();
    }
    if (!cr_runtime || !name) return;

    CR_Component *parent = cr_runtime->current_component;
    CR_Component *component = NULL;
    bool has_key = cr_runtime->has_next_key;
    CR_Id key = cr_runtime->next_key;
    cr_runtime->has_next_key = false;
    if (has_key && !key.name) {
        has_key = false;
    }

    if (parent) {
        size_t index = parent->child_cursor;
        if (has_key) {
            component = _cr_find_child_by_key(parent, key, name, index);
            if (!component) {
                component = _cr_create_component(name, parent, true, key);
                _cr_component_add_child(parent, component, index);
            }
        } else {
            component = _cr_get_child_by_index(parent, name, index);
            if (!component) {
                component = _cr_create_component(name, parent, false, (CR_Id){0});
                _cr_component_add_child(parent, component, index);
            }
        }
        parent->child_cursor++;
    } else {
        if (cr_runtime->root && strcmp(cr_runtime->root->name, name) == 0) {
            component = cr_runtime->root;
        } else {
            if (cr_runtime->root) {
                _cr_destroy_component(cr_runtime->root);
            }
            component = _cr_create_component(name, NULL, false, (CR_Id){0});
            cr_runtime->root = component;
        }
    }

    if (!component) return;

    component->last_render_frame = cr_runtime->frame;
    component->hook_cursor = 0;
    component->child_cursor = 0;

    if (props && props_size > 0) {
        if (component->props_size != props_size) {
            if (component->props_copy) {
                _cr_free(component->props_copy, component->props_size);
            }
            component->props_copy = _cr_alloc(props_size);
            component->props_size = props_size;
        }
        if (component->props_copy) {
            memcpy(component->props_copy, props, props_size);
        }
    }

    if (!_cr_ensure_capacity((void **)&cr_runtime->component_stack,
            &cr_runtime->component_stack_capacity,
            cr_runtime->component_stack_count + 1,
            sizeof(*cr_runtime->component_stack))) {
        return;
    }
    cr_runtime->component_stack[cr_runtime->component_stack_count++] = cr_runtime->current_component;
    cr_runtime->current_component = component;
}

void _cr_component_end(void) {
    if (!cr_runtime || cr_runtime->component_stack_count == 0) {
        if (cr_runtime) {
            cr_runtime->current_component = NULL;
        }
        return;
    }
    cr_runtime->current_component =
        cr_runtime->component_stack[--cr_runtime->component_stack_count];
}

void *_cr_current_props(void) {
    if (!cr_runtime || !cr_runtime->current_component) {
        return NULL;
    }
    return cr_runtime->current_component->props_copy;
}

// ============================================================================
// STATE IMPLEMENTATION
// ============================================================================

CR_StateInternal *_cr_alloc_state(size_t size, void *initial) {
    CR_StateInternal *state = _cr_alloc(sizeof(CR_StateInternal));
    if (!state) return NULL;

    state->value = _cr_alloc(size);
    if (!state->value) {
        _cr_free(state, sizeof(CR_StateInternal));
        return NULL;
    }

    memcpy(state->value, initial, size);
    state->size = size;
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
    CR_SignalInternal *signal = _cr_alloc(sizeof(CR_SignalInternal));
    if (!signal) return NULL;

    signal->value = _cr_alloc(size);
    if (!signal->value) {
        _cr_free(signal, sizeof(CR_SignalInternal));
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

    _cr_schedule_render();
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

    _cr_schedule_render();
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

    _cr_schedule_render();
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

    _cr_schedule_render();
}

void _cr_text_input_move_cursor(CR_TextInputState *input, int delta) {
    if (!input) return;

    int new_pos = (int)input->cursor_pos + delta;
    if (new_pos < 0) new_pos = 0;
    if (new_pos > (int)input->length) new_pos = (int)input->length;
    input->cursor_pos = (size_t)new_pos;

    _cr_schedule_render();
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

    _cr_schedule_render();
}

void _cr_unfocus_input(void) {
    if (_cr_focused_input) {
        _cr_focused_input->focused = false;
        _cr_focused_input->editing = false;
    }
    _cr_focused_input = NULL;

    _cr_schedule_render();
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
