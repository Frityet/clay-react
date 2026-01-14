/**
 * Todo App Example - backend-agnostic Clay React UI
 */

#include "demo/todo_app.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

// ============================================================================
// THEME
// ============================================================================

static const TodoTheme k_theme = {
    .background = { 244, 246, 249, 255 },
    .surface = { 255, 255, 255, 255 },
    .surface_alt = { 247, 249, 252, 255 },
    .text = { 22, 27, 36, 255 },
    .text_muted = { 98, 110, 125, 255 },
    .accent = { 59, 130, 246, 255 },
    .accent_soft = { 219, 234, 254, 255 },
    .danger = { 239, 68, 68, 255 },
    .success = { 34, 197, 94, 255 },
    .warning = { 234, 179, 8, 255 },
};

const TodoTheme *TodoAppTheme(void) {
    return &k_theme;
}

// ============================================================================
// DATA
// ============================================================================

#define MAX_TODOS 48
#define TITLE_MAX 96

typedef enum {
    TODO_FILTER_ALL = 0,
    TODO_FILTER_ACTIVE,
    TODO_FILTER_DONE,
} TodoFilter;

typedef enum {
    TODO_SORT_CREATED = 0,
    TODO_SORT_PRIORITY,
    TODO_SORT_ALPHA,
} TodoSort;

typedef enum {
    TODO_PRIORITY_LOW = 0,
    TODO_PRIORITY_MEDIUM,
    TODO_PRIORITY_HIGH,
} TodoPriority;

typedef struct TodoItem {
    int id;
    char title[TITLE_MAX];
    bool done;
    bool pinned;
    TodoPriority priority;
    uint8_t tag;
    CR_TextInputState *input;
} TodoItem;

typedef struct TodoState {
    TodoItem items[MAX_TODOS];
    int count;
    int next_id;
    int editing_id;
    TodoFilter filter;
    TodoSort sort;
    TodoPriority draft_priority;
    uint8_t draft_tag;
    bool show_done;
    bool seeded;
    uint64_t version;
} TodoState;

typedef struct TodoStats {
    int total;
    int done;
    int active;
    int pinned;
} TodoStats;

typedef void (^TodoStateSetter)(TodoState value);

static const char *k_priority_labels[] = { "Low", "Medium", "High" };
static const char *k_filter_labels[] = { "All", "Active", "Done" };
static const char *k_sort_labels[] = { "Created", "Priority", "Alpha" };
static const char *k_tags[] = { "Work", "Home", "Study", "Errand", "Health" };
static const Clay_Color k_tag_colors[] = {
    { 219, 234, 254, 255 },
    { 220, 252, 231, 255 },
    { 254, 243, 199, 255 },
    { 254, 226, 226, 255 },
    { 207, 250, 254, 255 },
};

static int todo_tag_count(void) {
    return (int)(sizeof(k_tags) / sizeof(k_tags[0]));
}

// ============================================================================
// TEXT STYLES
// ============================================================================

static TextConfig text_style_title(void) {
    return (TextConfig){
        .font_id = 0,
        .font_size = 32,
        .line_height = 40,
        .color = k_theme.text,
    };
}

static TextConfig text_style_body(void) {
    return (TextConfig){
        .font_id = 0,
        .font_size = 16,
        .line_height = 24,
        .color = k_theme.text,
    };
}

static TextConfig text_style_muted(void) {
    return (TextConfig){
        .font_id = 0,
        .font_size = 14,
        .line_height = 20,
        .color = k_theme.text_muted,
    };
}

static TextConfig text_style_chip(Clay_Color color) {
    return (TextConfig){
        .font_id = 0,
        .font_size = 12,
        .line_height = 16,
        .color = color,
    };
}

// ============================================================================
// HELPERS
// ============================================================================

static Clay_Color todo_priority_color(TodoPriority priority) {
    switch (priority) {
        case TODO_PRIORITY_LOW:
            return k_theme.success;
        case TODO_PRIORITY_MEDIUM:
            return k_theme.warning;
        case TODO_PRIORITY_HIGH:
            return k_theme.danger;
    }
    return k_theme.text_muted;
}

static const char *todo_priority_label(TodoPriority priority) {
    switch (priority) {
        case TODO_PRIORITY_LOW:
            return k_priority_labels[0];
        case TODO_PRIORITY_MEDIUM:
            return k_priority_labels[1];
        case TODO_PRIORITY_HIGH:
            return k_priority_labels[2];
    }
    return "";
}

static bool todo_contains_case_insensitive(const char *text, const char *query) {
    if (!query || !*query) return true;
    if (!text || !*text) return false;

    size_t qlen = strlen(query);
    for (size_t i = 0; text[i] != '\0'; i++) {
        size_t j = 0;
        while (j < qlen && text[i + j] != '\0') {
            char a = (char)tolower((unsigned char)text[i + j]);
            char b = (char)tolower((unsigned char)query[j]);
            if (a != b) break;
            j++;
        }
        if (j == qlen) return true;
    }
    return false;
}

static int todo_compare(const TodoItem *a, const TodoItem *b, TodoSort sort) {
    if (a->pinned != b->pinned) {
        return a->pinned ? -1 : 1;
    }
    if (a->done != b->done) {
        return a->done ? 1 : -1;
    }

    switch (sort) {
        case TODO_SORT_PRIORITY:
            return (int)b->priority - (int)a->priority;
        case TODO_SORT_ALPHA:
            return strcmp(a->title, b->title);
        case TODO_SORT_CREATED:
        default:
            return a->id - b->id;
    }
}

static void todo_sort_indices(int *indices, int count, const TodoItem *items, TodoSort sort) {
    for (int i = 1; i < count; i++) {
        int key = indices[i];
        int j = i - 1;
        while (j >= 0 && todo_compare(&items[indices[j]], &items[key], sort) > 0) {
            indices[j + 1] = indices[j];
            j--;
        }
        indices[j + 1] = key;
    }
}

static TodoStats todo_compute_stats(const TodoState *state) {
    TodoStats stats = {0};
    if (!state) return stats;

    stats.total = state->count;
    for (int i = 0; i < state->count; i++) {
        const TodoItem *item = &state->items[i];
        if (item->done) {
            stats.done++;
        } else {
            stats.active++;
        }
        if (item->pinned) {
            stats.pinned++;
        }
    }
    return stats;
}

static void todo_add_seed_item(TodoState *state, const char *title,
        TodoPriority priority, uint8_t tag, bool pinned) {
    if (!state || state->count >= MAX_TODOS) return;

    TodoItem *item = &state->items[state->count++];
    item->id = state->next_id++;
    item->done = false;
    item->pinned = pinned;
    item->priority = priority;
    item->tag = tag;
    strncpy(item->title, title, TITLE_MAX - 1);
    item->title[TITLE_MAX - 1] = '\0';
    item->input = _cr_alloc_text_input(TITLE_MAX);
    _cr_text_input_set_text(item->input, item->title);
}

static void todo_seed_defaults(TodoState *state, TodoStateSetter set_state) {
    if (!state || !set_state || state->seeded) return;

    TodoState next = *state;
    next.seeded = true;

    todo_add_seed_item(&next, "Plan the day and pick a top 3", TODO_PRIORITY_HIGH, 0, true);
    todo_add_seed_item(&next, "Review inbox and clear quick replies", TODO_PRIORITY_MEDIUM, 1, false);
    todo_add_seed_item(&next, "Read 20 pages of a book", TODO_PRIORITY_LOW, 2, false);
    todo_add_seed_item(&next, "Order groceries for the week", TODO_PRIORITY_MEDIUM, 3, false);

    next.version++;
    set_state(next);
}

static void todo_add_item(TodoState *state, TodoStateSetter set_state, CR_TextInputState *input) {
    if (!state || !set_state || !input) return;
    if (input->length == 0 || state->count >= MAX_TODOS) return;

    TodoState next = *state;
    TodoItem *item = &next.items[next.count++];
    item->id = next.next_id++;
    item->done = false;
    item->pinned = false;
    item->priority = next.draft_priority;
    item->tag = next.draft_tag;
    strncpy(item->title, input->buffer, TITLE_MAX - 1);
    item->title[TITLE_MAX - 1] = '\0';
    item->input = _cr_alloc_text_input(TITLE_MAX);
    _cr_text_input_set_text(item->input, item->title);

    next.version++;
    set_state(next);

    _cr_text_input_set_text(input, "");
    _cr_unfocus_input();
}

static void todo_toggle_done(TodoState *state, TodoStateSetter set_state, int index) {
    if (!state || !set_state || index < 0 || index >= state->count) return;

    TodoState next = *state;
    next.items[index].done = !next.items[index].done;
    next.version++;
    set_state(next);
}

static void todo_toggle_pin(TodoState *state, TodoStateSetter set_state, int index) {
    if (!state || !set_state || index < 0 || index >= state->count) return;

    TodoState next = *state;
    next.items[index].pinned = !next.items[index].pinned;
    next.version++;
    set_state(next);
}

static void todo_delete_item(TodoState *state, TodoStateSetter set_state, int index) {
    if (!state || !set_state || index < 0 || index >= state->count) return;

    TodoState next = *state;
    TodoItem *item = &next.items[index];
    int deleted_id = item->id;

    if (_cr_focused_input == item->input) {
        _cr_unfocus_input();
    }
    if (item->input) {
        free(item->input->buffer);
        free(item->input);
    }

    for (int i = index; i < next.count - 1; i++) {
        next.items[i] = next.items[i + 1];
    }
    next.count--;

    if (next.editing_id == deleted_id) {
        next.editing_id = -1;
    }

    next.version++;
    set_state(next);
}

static void todo_clear_completed(TodoState *state, TodoStateSetter set_state) {
    if (!state || !set_state) return;

    TodoState next = *state;
    int write = 0;
    bool editing_found = false;

    for (int i = 0; i < next.count; i++) {
        TodoItem *item = &next.items[i];
        if (item->done) {
            if (_cr_focused_input == item->input) {
                _cr_unfocus_input();
            }
            if (item->input) {
                free(item->input->buffer);
                free(item->input);
            }
            continue;
        }

        if (write != i) {
            next.items[write] = next.items[i];
        }
        if (next.editing_id == next.items[write].id) {
            editing_found = true;
        }
        write++;
    }

    next.count = write;
    if (next.editing_id >= 0 && !editing_found) {
        next.editing_id = -1;
    }

    next.version++;
    set_state(next);
}

static void todo_toggle_all(TodoState *state, TodoStateSetter set_state) {
    if (!state || !set_state) return;

    TodoState next = *state;
    bool all_done = true;
    for (int i = 0; i < next.count; i++) {
        if (!next.items[i].done) {
            all_done = false;
            break;
        }
    }

    for (int i = 0; i < next.count; i++) {
        next.items[i].done = !all_done;
    }

    next.version++;
    set_state(next);
}

static void todo_start_edit(TodoState *state, TodoStateSetter set_state, TodoItem *item) {
    if (!state || !set_state || !item) return;

    TodoState next = *state;
    next.editing_id = item->id;
    next.version++;
    set_state(next);

    _cr_text_input_set_text(item->input, item->title);
    _cr_focus_input(item->input, cr_element_id(cr_idi("TodoEdit", (uint32_t)item->id)).id);
}

static void todo_save_edit(TodoState *state, TodoStateSetter set_state, TodoItem *item, int index) {
    if (!state || !set_state || !item) return;

    TodoState next = *state;
    if (item->input && item->input->length > 0) {
        strncpy(next.items[index].title, item->input->buffer, TITLE_MAX - 1);
        next.items[index].title[TITLE_MAX - 1] = '\0';
    }
    next.editing_id = -1;
    next.version++;
    set_state(next);

    _cr_unfocus_input();
}

static void todo_cancel_edit(TodoState *state, TodoStateSetter set_state) {
    if (!state || !set_state) return;

    TodoState next = *state;
    next.editing_id = -1;
    next.version++;
    set_state(next);

    _cr_unfocus_input();
}

static void TodoChip(const char *label, Clay_Color bg, Clay_Color text) {
    if (!label || !*label) return;
    Box((BoxParams){
        .style = {
            .layout = { .padding = $pad_lrtb(8, 8, 3, 3) },
            .background = bg,
            .corner_radius = $radius(999),
        },
    }, ^{
        Text((TextParams){
            .text = label,
            .style = text_style_chip(text),
        });
    });
}

// ============================================================================
// TODO ROW COMPONENT
// ============================================================================

typedef struct {
    TodoItem *item;
    int index;
    bool is_editing;
    TodoState *state;
    TodoStateSetter set_state;
    const TodoTheme *theme;
} TodoRowProps;

$component(TodoRow, TodoRowProps) {
    TodoItem *item = props->item;
    const TodoTheme *theme = props->theme;
    bool done = item->done;

    auto confirm = $use_state((bool)false);
    bool confirm_delete = confirm ? confirm->get() : false;

    Clay_Color row_bg = done ? $alpha(theme->surface_alt, 200) : theme->surface_alt;
    Clay_Color pr_color = todo_priority_color(item->priority);
    Clay_Color pr_bg = $alpha(pr_color, 48);
    Clay_Color tag_bg = k_tag_colors[item->tag % todo_tag_count()];

    Row((BoxParams){
        .style = {
            .layout = {
                .sizing = { .width = $grow(0) },
                .padding = $pad_lrtb(14, 14, 12, 12),
                .childGap = 12,
            },
            .background = row_bg,
            .background_hover = $alpha(theme->accent, 20),
            .corner_radius = $radius(10),
        },
    }, ^{
        Checkbox((CheckboxParams){
            .id = cr_idi("TodoCheck", (uint32_t)item->id),
            .checked = done,
            .on_toggle = ^{
                todo_toggle_done(props->state, props->set_state, props->index);
                if (confirm) confirm->set(false);
            },
            .checked_color = theme->accent,
            .unchecked_color = theme->surface,
            .border_color = theme->text_muted,
            .size = 22,
        });

        Column((BoxParams){
            .style = {
                .layout = {
                    .sizing = { .width = $grow(0) },
                    .childGap = 6,
                },
            },
        }, ^{
            if (props->is_editing) {
                TextInput((TextInputParams){
                    .id = cr_idi("TodoEdit", (uint32_t)item->id),
                    .state = item->input,
                    .style = {
                        .layout = {
                            .sizing = { .width = $grow(0) },
                            .padding = $pad_lrtb(10, 10, 8, 8),
                        },
                        .background = theme->surface,
                        .corner_radius = $radius(6),
                        .border = (Clay_BorderElementConfig){ .width = $border_outside(1), .color = $gray(220) },
                        .has_border = true,
                    },
                    .focus_border = (Clay_BorderElementConfig){
                        .width = $border_outside(2),
                        .color = theme->accent,
                    },
                    .has_focus_border = true,
                    .text = text_style_body(),
                });
            } else {
                Clickable((ClickableParams){
                    .id = cr_idi("TodoTitle", (uint32_t)item->id),
                    .on_click = ^{
                        todo_start_edit(props->state, props->set_state, item);
                        if (confirm) confirm->set(false);
                    },
                    .style = {
                        .layout = { .padding = $pad_lrtb(6, 6, 4, 4) },
                        .corner_radius = $radius(6),
                    },
                }, ^{
                    Text((TextParams){
                        .text = item->title,
                        .style = done ? text_style_muted() : text_style_body(),
                    });
                });
            }

            Row((BoxParams){
                .style = {
                    .layout = { .childGap = 8 },
                },
            }, ^{
                TodoChip(k_tags[item->tag % todo_tag_count()], tag_bg, theme->text);
                TodoChip(todo_priority_label(item->priority), pr_bg, pr_color);
                if (item->pinned) {
                    TodoChip("Pinned", theme->accent_soft, theme->accent);
                }
            });
        });

        Row((BoxParams){
            .style = {
                .layout = { .childGap = 8 },
            },
        }, ^{
            if (props->is_editing) {
                Button((ButtonParams){
                    .id = cr_idi("TodoSave", (uint32_t)item->id),
                    .label = "Save",
                    .on_click = ^{
                        todo_save_edit(props->state, props->set_state, item, props->index);
                        if (confirm) confirm->set(false);
                    },
                    .style = {
                        .layout = {
                            .padding = $pad_lrtb(10, 10, 6, 6),
                            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                        },
                        .background = theme->accent,
                        .background_hover = $alpha(theme->accent, 220),
                        .corner_radius = $radius(6),
                    },
                    .text = (TextConfig){ .font_size = 13, .color = $WHITE },
                }, NULL);

                IconButton((IconButtonParams){
                    .id = cr_idi("TodoCancel", (uint32_t)item->id),
                    .icon = "x",
                    .on_click = ^{
                        todo_cancel_edit(props->state, props->set_state);
                        if (confirm) confirm->set(false);
                    },
                    .style = {
                        .background = $alpha(theme->danger, 60),
                        .background_hover = theme->danger,
                    },
                    .text = (TextConfig){ .font_size = 12, .color = $WHITE },
                }, NULL);
            } else {
                IconButton((IconButtonParams){
                    .id = cr_idi("TodoPin", (uint32_t)item->id),
                    .icon = item->pinned ? "unpin" : "pin",
                    .on_click = ^{
                        todo_toggle_pin(props->state, props->set_state, props->index);
                        if (confirm) confirm->set(false);
                    },
                    .style = {
                        .layout = {
                            .sizing = { .width = $fixed(46), .height = $fixed(28) },
                            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                        },
                        .background = $alpha(theme->accent, 50),
                        .background_hover = theme->accent,
                    },
                    .text = (TextConfig){ .font_size = 11, .color = $WHITE },
                }, NULL);

                if (confirm_delete) {
                    Button((ButtonParams){
                        .id = cr_idi("TodoConfirm", (uint32_t)item->id),
                        .label = "Sure",
                        .on_click = ^{
                            todo_delete_item(props->state, props->set_state, props->index);
                        },
                        .style = {
                            .layout = {
                                .padding = $pad_lrtb(10, 10, 6, 6),
                                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                            },
                            .background = theme->danger,
                            .background_hover = $alpha(theme->danger, 220),
                            .corner_radius = $radius(6),
                        },
                        .text = (TextConfig){ .font_size = 13, .color = $WHITE },
                    }, NULL);

                    IconButton((IconButtonParams){
                        .id = cr_idi("TodoCancelDelete", (uint32_t)item->id),
                        .icon = "x",
                        .on_click = ^{
                            if (confirm) confirm->set(false);
                        },
                        .style = {
                            .background = $alpha(theme->danger, 60),
                            .background_hover = theme->danger,
                        },
                        .text = (TextConfig){ .font_size = 12, .color = $WHITE },
                    }, NULL);
                } else {
                    IconButton((IconButtonParams){
                        .id = cr_idi("TodoDelete", (uint32_t)item->id),
                        .icon = "del",
                        .on_click = ^{
                            if (confirm) confirm->set(true);
                        },
                        .style = {
                            .background = $alpha(theme->danger, 60),
                            .background_hover = theme->danger,
                        },
                        .text = (TextConfig){ .font_size = 12, .color = $WHITE },
                    }, NULL);
                }
            }
        });
    });
}

// ============================================================================
// ROOT COMPONENT
// ============================================================================

$component(TodoAppView) {
    auto state = $use_state((TodoState){
        .count = 0,
        .next_id = 1,
        .editing_id = -1,
        .filter = TODO_FILTER_ALL,
        .sort = TODO_SORT_CREATED,
        .draft_priority = TODO_PRIORITY_MEDIUM,
        .draft_tag = 0,
        .show_done = true,
        .seeded = false,
        .version = 0
    });

    auto new_input = $use_text_input(TITLE_MAX);
    auto search_input = $use_text_input(64);
    CR_Id new_input_id = $use_id("NewTodoInput");
    CR_Id search_input_id = $use_id("TodoSearchInput");
    TodoState *state_ptr = state ? state->ptr : NULL;
    TodoStateSetter set_state = state ? state->set : NULL;

    $use_effect(^{
        todo_seed_defaults(state_ptr, set_state);
        return (CleanupBlock)NULL;
    }, $deps_once());

    TodoStats stats = $use_memo(TodoStats, ^{
        return todo_compute_stats(state->ptr);
    }, $deps(state->ptr->version));

    auto celebrate = $use_state((bool)false);
    $use_effect(^{
        bool done = (stats.total > 0) && (stats.active == 0);
        if (celebrate && celebrate->get() != done) {
            celebrate->set(done);
        }
        return (CleanupBlock)NULL;
    }, $deps(stats.total, stats.active));

    $use_effect(^{
        if (state->ptr->editing_id < 0) return (CleanupBlock)NULL;
        bool found = false;
        for (int i = 0; i < state->ptr->count; i++) {
            if (state->ptr->items[i].id == state->ptr->editing_id) {
                found = true;
                break;
            }
        }
        if (!found) {
            TodoState next = *state->ptr;
            next.editing_id = -1;
            next.version++;
            state->set(next);
        }
        return (CleanupBlock)NULL;
    }, $deps(state->ptr->count, state->ptr->editing_id, state->ptr->version));

    VoidBlock add_todo = $use_callback(^{
        todo_add_item(state_ptr, set_state, new_input);
    }, $deps_once());

    VoidBlock clear_done = $use_callback(^{
        todo_clear_completed(state_ptr, set_state);
    }, $deps_once());

    VoidBlock toggle_all = $use_callback(^{
        todo_toggle_all(state_ptr, set_state);
    }, $deps_once());

    int visible[MAX_TODOS];
    int visible_count = 0;
    int *visible_ptr = visible;
    for (int i = 0; i < state->ptr->count; i++) {
        TodoItem *item = &state->ptr->items[i];
        if (state->ptr->filter == TODO_FILTER_ACTIVE && item->done) continue;
        if (state->ptr->filter == TODO_FILTER_DONE && !item->done) continue;
        if (!state->ptr->show_done && item->done) continue;
        if (search_input && !todo_contains_case_insensitive(item->title, search_input->buffer)) continue;
        visible[visible_count++] = i;
    }
    todo_sort_indices(visible, visible_count, state->ptr->items, state->ptr->sort);

    float progress = stats.total > 0 ? (float)stats.done / (float)stats.total : 0.0f;

    Column((BoxParams){
        .style = {
            .layout = {
                .sizing = { .width = $fixed(760), .height = $grow(0) },
                .padding = $pad_lrtb(32, 32, 28, 28),
                .childGap = 20,
            },
            .background = k_theme.surface,
            .corner_radius = $radius(18),
            .border = (Clay_BorderElementConfig){ .width = $border_outside(1), .color = $gray(230) },
            .has_border = true,
        },
    }, ^{
        Row((BoxParams){
            .style = {
                .layout = { .childGap = 16 },
            },
        }, ^{
            Column((BoxParams){
                .style = {
                    .layout = { .childGap = 4 },
                },
            }, ^{
                Text((TextParams){
                    .text = "Todo Atlas",
                    .style = text_style_title(),
                });
                Text((TextParams){
                    .text = celebrate && celebrate->get() ?
                        "All clear. Enjoy the free time." :
                        "Focus on one small win at a time.",
                    .style = text_style_muted(),
                });
            });

            Spacer();

            Column((BoxParams){
                .style = {
                    .layout = { .childGap = 4 },
                },
            }, ^{
                Textf((TextParams){ .style = text_style_body() },
                    "Active %d | Done %d",
                    stats.active,
                    stats.done);
                Textf((TextParams){ .style = text_style_muted() },
                    "Pinned %d | Total %d",
                    stats.pinned,
                    stats.total);
            });
        });

        Box((BoxParams){
            .style = {
                .layout = {
                    .sizing = { .width = $grow(0), .height = $fixed(10) },
                },
                .background = k_theme.accent_soft,
                .corner_radius = $radius(999),
            },
        }, ^{
            Box((BoxParams){
                .style = {
                    .layout = {
                        .sizing = { .width = $percent(progress), .height = $fixed(10) },
                    },
                    .background = k_theme.accent,
                    .corner_radius = $radius(999),
                },
            }, NULL);
        });

        Row((BoxParams){
            .style = {
                .layout = { .childGap = 12 },
            },
        }, ^{
            TextInput((TextInputParams){
                .id = search_input_id,
                .state = search_input,
                .placeholder = "Search tasks",
                .text = text_style_body(),
                .placeholder_text = text_style_muted(),
                .style = {
                    .layout = {
                        .sizing = { .width = $grow(0), .height = $fit(.min = 40) },
                        .padding = $pad_lrtb(12, 12, 10, 10),
                    },
                    .background = k_theme.surface_alt,
                    .corner_radius = $radius(8),
                    .border = (Clay_BorderElementConfig){ .width = $border_outside(1), .color = $gray(220) },
                    .has_border = true,
                },
            });

            Row((BoxParams){
                .style = {
                    .layout = { .childGap = 8 },
                },
            }, ^{
                for (int i = 0; i < 3; i++) {
                    bool active = state->ptr->filter == (TodoFilter)i;
                    Button((ButtonParams){
                        .id = cr_idi("TodoFilter", (uint32_t)i),
                        .label = k_filter_labels[i],
                        .on_click = ^{
                            TodoState next = *state->ptr;
                            next.filter = (TodoFilter)i;
                            next.version++;
                            state->set(next);
                        },
                        .style = {
                            .layout = {
                                .padding = $pad_lrtb(12, 12, 6, 6),
                                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                            },
                            .background = active ? k_theme.accent_soft : k_theme.surface_alt,
                            .background_hover = active ? k_theme.accent : $alpha(k_theme.accent_soft, 200),
                            .corner_radius = $radius(999),
                        },
                        .text = (TextConfig){
                            .font_size = 13,
                            .line_height = 16,
                            .color = active ? k_theme.accent : k_theme.text_muted,
                        },
                    }, NULL);
                }

                Button((ButtonParams){
                    .id = cr_id("TodoSort"),
                    .label = k_sort_labels[state->ptr->sort],
                    .on_click = ^{
                        TodoState next = *state->ptr;
                        next.sort = (TodoSort)((next.sort + 1) % 3);
                        next.version++;
                        state->set(next);
                    },
                    .style = {
                        .layout = {
                            .padding = $pad_lrtb(12, 12, 6, 6),
                            .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                        },
                        .background = k_theme.surface_alt,
                        .background_hover = k_theme.accent_soft,
                        .corner_radius = $radius(999),
                    },
                    .text = (TextConfig){ .font_size = 13, .color = k_theme.text_muted },
                }, NULL);
            });
        });

        Row((BoxParams){
            .style = {
                .layout = { .childGap = 12 },
            },
        }, ^{
            TextInput((TextInputParams){
                .id = new_input_id,
                .state = new_input,
                .placeholder = "Add a task",
                .text = text_style_body(),
                .placeholder_text = text_style_muted(),
                .style = {
                    .layout = {
                        .sizing = { .width = $grow(0), .height = $fit(.min = 44) },
                        .padding = $pad_lrtb(12, 12, 10, 10),
                    },
                    .background = k_theme.surface_alt,
                    .corner_radius = $radius(8),
                    .border = (Clay_BorderElementConfig){ .width = $border_outside(1), .color = $gray(220) },
                    .has_border = true,
                },
            });

            Button((ButtonParams){
                .id = cr_id("TodoAdd"),
                .label = "Add",
                .on_click = add_todo,
                .style = {
                    .layout = {
                        .padding = $pad_lrtb(16, 16, 10, 10),
                        .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                    },
                    .background = k_theme.accent,
                    .background_hover = $alpha(k_theme.accent, 220),
                    .corner_radius = $radius(8),
                },
                .text = (TextConfig){ .font_size = 15, .color = $WHITE },
            }, NULL);
        });

        Column((BoxParams){
            .style = {
                .layout = { .childGap = 8 },
            },
        }, ^{
            Row((BoxParams){
                .style = { .layout = { .childGap = 8 } },
            }, ^{
                Text((TextParams){ .text = "Priority", .style = text_style_muted() });
                for (int i = 0; i < 3; i++) {
                    bool active = state->ptr->draft_priority == (TodoPriority)i;
                    Clay_Color pr = todo_priority_color((TodoPriority)i);
                    Button((ButtonParams){
                        .id = cr_idi("TodoPriority", (uint32_t)i),
                        .label = k_priority_labels[i],
                        .on_click = ^{
                            TodoState next = *state->ptr;
                            next.draft_priority = (TodoPriority)i;
                            next.version++;
                            state->set(next);
                        },
                        .style = {
                            .layout = {
                                .padding = $pad_lrtb(10, 10, 4, 4),
                                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                            },
                            .background = active ? pr : k_theme.surface_alt,
                            .background_hover = $alpha(pr, 200),
                            .corner_radius = $radius(999),
                        },
                        .text = (TextConfig){
                            .font_size = 12,
                            .line_height = 16,
                            .color = active ? $WHITE : k_theme.text_muted,
                        },
                    }, NULL);
                }
            });

            Row((BoxParams){
                .style = { .layout = { .childGap = 8 } },
            }, ^{
                Text((TextParams){ .text = "Tag", .style = text_style_muted() });
                for (int i = 0; i < todo_tag_count(); i++) {
                    bool active = state->ptr->draft_tag == (uint8_t)i;
                    Clay_Color tag = k_tag_colors[i];
                    Button((ButtonParams){
                        .id = cr_idi("TodoTag", (uint32_t)i),
                        .label = k_tags[i],
                        .on_click = ^{
                            TodoState next = *state->ptr;
                            next.draft_tag = (uint8_t)i;
                            next.version++;
                            state->set(next);
                        },
                        .style = {
                            .layout = {
                                .padding = $pad_lrtb(10, 10, 4, 4),
                                .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                            },
                            .background = active ? tag : k_theme.surface_alt,
                            .background_hover = $alpha(tag, 220),
                            .corner_radius = $radius(999),
                        },
                        .text = (TextConfig){
                            .font_size = 12,
                            .line_height = 16,
                            .color = active ? k_theme.text : k_theme.text_muted,
                        },
                    }, NULL);
                }
            });
        });

        Column((BoxParams){
            .style = {
                .layout = { .childGap = 10 },
            },
        }, ^{
            for (int i = 0; i < visible_count; i++) {
                int idx = visible_ptr[i];
                TodoItem *item = &state->ptr->items[idx];
                bool is_editing = state->ptr->editing_id == item->id;

                $keyi("TodoRow", (uint32_t)item->id);
                TodoRow((TodoRowProps){
                    .item = item,
                    .index = idx,
                    .is_editing = is_editing,
                    .state = state_ptr,
                    .set_state = set_state,
                    .theme = &k_theme,
                });
            }

            if (visible_count == 0) {
                Center((BoxParams){
                    .style = {
                        .layout = { .padding = $pad_lrtb(24, 24, 18, 18) },
                    },
                }, ^{
                    Text((TextParams){
                        .text = "No tasks here. Add one above.",
                        .style = text_style_muted(),
                    });
                });
            }
        });

        Row((BoxParams){
            .style = {
                .layout = { .childGap = 14 },
            },
        }, ^{
            Row((BoxParams){
                .style = { .layout = { .childGap = 8 } },
            }, ^{
                Checkbox((CheckboxParams){
                    .id = cr_id("TodoShowDone"),
                    .checked = state->ptr->show_done,
                    .on_toggle = ^{
                        TodoState next = *state->ptr;
                        next.show_done = !next.show_done;
                        next.version++;
                        state->set(next);
                    },
                    .checked_color = k_theme.accent,
                    .unchecked_color = k_theme.surface,
                    .border_color = k_theme.text_muted,
                    .size = 18,
                });
                Text((TextParams){ .text = "Show done", .style = text_style_muted() });
            });

            Spacer();

            Button((ButtonParams){
                .id = cr_id("TodoClearDone"),
                .label = "Clear done",
                .on_click = clear_done,
                .style = {
                    .layout = {
                        .padding = $pad_lrtb(12, 12, 6, 6),
                        .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                    },
                    .background = $alpha(k_theme.danger, 80),
                    .background_hover = k_theme.danger,
                    .corner_radius = $radius(8),
                },
                .text = (TextConfig){ .font_size = 13, .color = $WHITE },
            }, NULL);

            Button((ButtonParams){
                .id = cr_id("TodoToggleAll"),
                .label = "Toggle all",
                .on_click = toggle_all,
                .style = {
                    .layout = {
                        .padding = $pad_lrtb(12, 12, 6, 6),
                        .childAlignment = { .x = CLAY_ALIGN_X_CENTER, .y = CLAY_ALIGN_Y_CENTER },
                    },
                    .background = k_theme.surface_alt,
                    .background_hover = k_theme.accent_soft,
                    .corner_radius = $radius(8),
                    .border = (Clay_BorderElementConfig){ .width = $border_outside(1), .color = $gray(220) },
                    .has_border = true,
                },
                .text = (TextConfig){ .font_size = 13, .color = k_theme.text_muted },
            }, NULL);
        });
    });
}

void TodoApp(void) {
    TodoAppView();
}
