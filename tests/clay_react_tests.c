/**
 * Clay React tests - runtime and hook behavior
 */
#define CLAY_IMPLEMENTATION
#include <clay.h>
#include "clay_react/clay_react.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ============================================================================
// TEST HARNESS
// ============================================================================

#define CHECK(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return false; \
    } \
} while (0)

typedef bool (*TestFn)(void);

static bool run_test(const char *name, TestFn fn) {
    fprintf(stdout, "[TEST] %s\n", name);
    if (!fn()) {
        fprintf(stdout, "[FAIL] %s\n", name);
        return false;
    }
    fprintf(stdout, "[PASS] %s\n", name);
    return true;
}

static Clay_Arena g_arena = {0};
static bool g_clay_initialized = false;

static Clay_Dimensions test_measure_text(Clay_StringSlice text,
        Clay_TextElementConfig *config, void *userData) {
    (void)text;
    (void)config;
    (void)userData;
    return (Clay_Dimensions){0};
}

static void test_error_handler(Clay_ErrorData error) {
    fprintf(stderr, "Clay error: %.*s\n",
        (int)error.errorText.length, error.errorText.chars);
}

static void init_clay_once(void) {
    if (g_clay_initialized) return;
    size_t mem = Clay_MinMemorySize();
    g_arena.memory = calloc(1, mem);
    g_arena.capacity = mem;
    Clay_Initialize(g_arena, (Clay_Dimensions){ 800.0f, 600.0f },
        (Clay_ErrorHandler){ .errorHandlerFunction = test_error_handler });
    Clay_SetMeasureTextFunction(test_measure_text, NULL);
    g_clay_initialized = true;
}

static bool id_equal(CR_Id a, CR_Id b) {
    if (!a.name || !b.name) return false;
    if (strcmp(a.name, b.name) != 0) return false;
    if (a.indexed != b.indexed) return false;
    if (a.indexed && a.index != b.index) return false;
    return true;
}

// ============================================================================
// STATE TESTS
// ============================================================================

static int g_state_render_count = 0;
static int g_state_last_value = 0;
static int (^g_state_get)(void) = NULL;
static void (^g_state_set)(int) = NULL;

$component(StateTestComponent) {
    auto state = $use_state(0);
    if (!g_state_get) {
        g_state_get = state->get;
        g_state_set = state->set;
    }
    g_state_last_value = state->get();
    g_state_render_count++;
}

static bool test_state_persistence(void) {
    g_state_render_count = 0;
    g_state_last_value = -1;
    g_state_get = NULL;
    g_state_set = NULL;

    cr_begin_frame();
    StateTestComponent();
    cr_end_frame();

    CHECK(g_state_render_count == 1);
    CHECK(g_state_last_value == 0);
    CHECK(g_state_get != NULL);
    CHECK(g_state_set != NULL);

    g_state_set(5);
    CHECK(cr_should_render());

    cr_begin_frame();
    StateTestComponent();
    cr_end_frame();

    CHECK(g_state_render_count == 2);
    CHECK(g_state_last_value == 5);
    CHECK(g_state_get() == 5);

    return true;
}

// ============================================================================
// EFFECT TESTS
// ============================================================================

static int g_effect_runs = 0;
static int g_effect_cleanups = 0;
static int g_effect_seen = 0;
static void (^g_effect_set)(int) = NULL;

$component(EffectTestComponent) {
    auto count = $use_state(0);
    if (!g_effect_set) g_effect_set = count->set;

    $use_effect(^{
        g_effect_runs++;
        g_effect_seen = count->get();
        return (CleanupBlock)^{ g_effect_cleanups++; };
    }, $deps(count->get()));
}

static bool test_effects(void) {
    g_effect_runs = 0;
    g_effect_cleanups = 0;
    g_effect_seen = -1;
    g_effect_set = NULL;

    cr_begin_frame();
    EffectTestComponent();
    cr_end_frame();

    CHECK(g_effect_runs == 1);
    CHECK(g_effect_cleanups == 0);
    CHECK(g_effect_seen == 0);

    g_effect_set(1);
    CHECK(cr_should_render());

    cr_begin_frame();
    EffectTestComponent();
    cr_end_frame();

    CHECK(g_effect_runs == 2);
    CHECK(g_effect_cleanups == 1);
    CHECK(g_effect_seen == 1);

    return true;
}

static int g_realloc_effect_runs = 0;

$component(EffectReallocComponent) {
    $use_effect(^{
        g_realloc_effect_runs++;
        return (CleanupBlock)NULL;
    }, $deps_once());

    for (int i = 0; i < 24; i++) {
        auto ref = $use_ref(int, i);
        (void)ref;
    }
}

static bool test_effect_queue_realloc(void) {
    g_realloc_effect_runs = 0;

    cr_begin_frame();
    EffectReallocComponent();
    cr_end_frame();

    CHECK(g_realloc_effect_runs == 1);
    return true;
}

// ============================================================================
// MEMO TESTS
// ============================================================================

static int g_memo_runs = 0;
static int g_memo_value = 0;
static void (^g_memo_set)(int) = NULL;

$component(MemoTestComponent) {
    auto count = $use_state(1);
    if (!g_memo_set) g_memo_set = count->set;

    int memo = $use_memo(int, ^{
        g_memo_runs++;
        return count->get() * 2;
    }, $deps(count->get()));

    g_memo_value = memo;
}

static bool test_memo(void) {
    g_memo_runs = 0;
    g_memo_value = 0;
    g_memo_set = NULL;

    cr_begin_frame();
    MemoTestComponent();
    cr_end_frame();

    CHECK(g_memo_runs == 1);
    CHECK(g_memo_value == 2);

    cr_begin_frame();
    MemoTestComponent();
    cr_end_frame();

    CHECK(g_memo_runs == 1);
    CHECK(g_memo_value == 2);

    g_memo_set(3);

    cr_begin_frame();
    MemoTestComponent();
    cr_end_frame();

    CHECK(g_memo_runs == 2);
    CHECK(g_memo_value == 6);

    return true;
}

// ============================================================================
// CALLBACK TESTS
// ============================================================================

static int g_cb_render = 0;
static int g_cb_seen = 0;
static VoidBlock g_cb_stable = NULL;
static VoidBlock g_cb_stable_current = NULL;
static VoidBlock g_cb_versioned = NULL;

$component(CallbackTestComponent) {
    g_cb_render++;
    int version = g_cb_render;

    VoidBlock stable = $use_callback(^{ g_cb_seen = -1; }, $deps_once());
    VoidBlock versioned = $use_callback(^{ g_cb_seen = version; }, $deps(version));

    g_cb_stable_current = stable;
    if (!g_cb_stable) g_cb_stable = stable;
    g_cb_versioned = versioned;
}

static bool test_callback(void) {
    g_cb_render = 0;
    g_cb_seen = 0;
    g_cb_stable = NULL;
    g_cb_stable_current = NULL;
    g_cb_versioned = NULL;

    cr_begin_frame();
    CallbackTestComponent();
    cr_end_frame();

    CHECK(g_cb_stable != NULL);
    CHECK(g_cb_versioned != NULL);
    g_cb_versioned();
    CHECK(g_cb_seen == 1);

    cr_begin_frame();
    CallbackTestComponent();
    cr_end_frame();

    CHECK(g_cb_stable_current == g_cb_stable);
    g_cb_versioned();
    CHECK(g_cb_seen == 2);

    g_cb_stable();
    CHECK(g_cb_seen == -1);

    return true;
}

// ============================================================================
// REF TESTS
// ============================================================================

static int *g_ref_ptr = NULL;
static int g_ref_value = 0;

$component(RefTestComponent) {
    int *ref = $use_ref(int, 10);
    if (!g_ref_ptr) g_ref_ptr = ref;
    g_ref_value = *ref;
    if (*ref == 10) {
        *ref = 42;
    }
}

static bool test_ref(void) {
    g_ref_ptr = NULL;
    g_ref_value = 0;

    cr_begin_frame();
    RefTestComponent();
    cr_end_frame();

    CHECK(g_ref_ptr != NULL);
    CHECK(g_ref_value == 10);

    cr_begin_frame();
    RefTestComponent();
    cr_end_frame();

    CHECK(g_ref_value == 42);

    return true;
}

// ============================================================================
// ID TESTS
// ============================================================================

static CR_Id g_id_a = {0};
static CR_Id g_id_b = {0};

$component(IdComponentA) { g_id_a = $use_id("TestId"); }
$component(IdComponentB) { g_id_b = $use_id("TestId"); }

$component(IdRoot) {
    IdComponentA();
    IdComponentB();
}

static bool test_use_id(void) {
    g_id_a = (CR_Id){0};
    g_id_b = (CR_Id){0};

    cr_begin_frame();
    IdRoot();
    cr_end_frame();

    CR_Id first_a = g_id_a;
    CR_Id first_b = g_id_b;

    CHECK(first_a.name != NULL);
    CHECK(first_b.name != NULL);
    CHECK(!id_equal(first_a, first_b));

    cr_begin_frame();
    IdRoot();
    cr_end_frame();

    CHECK(id_equal(first_a, g_id_a));
    CHECK(id_equal(first_b, g_id_b));

    return true;
}

// ============================================================================
// TEXT INPUT TESTS
// ============================================================================

static CR_TextInputState *g_input = NULL;
static size_t g_input_len = 0;
static char g_input_buf[16] = {0};

$component(TextInputTestComponent) {
    auto input = $use_text_input(16);
    if (!g_input && input) {
        g_input = input;
        _cr_text_input_set_text(input, "hi");
    }
    if (input) {
        g_input_len = input->length;
        if (input->buffer) {
            snprintf(g_input_buf, sizeof(g_input_buf), "%s", input->buffer);
        }
    }
}

static bool test_text_input(void) {
    g_input = NULL;
    g_input_len = 0;
    memset(g_input_buf, 0, sizeof(g_input_buf));

    cr_begin_frame();
    TextInputTestComponent();
    cr_end_frame();

    CHECK(g_input != NULL);
    CHECK(g_input_len == 2);
    CHECK(strcmp(g_input_buf, "hi") == 0);

    cr_begin_frame();
    TextInputTestComponent();
    cr_end_frame();

    CHECK(g_input_len == 2);
    CHECK(strcmp(g_input_buf, "hi") == 0);

    return true;
}

// ============================================================================
// CLICK HANDLER PROPS TEST
// ============================================================================

typedef struct {
    int value;
} CaptureProps;

static int g_capture_value = 0;

$component(CaptureComponent, CaptureProps) {
    Button((ButtonParams){
        .id = cr_id("CaptureButton"),
        .label = "Capture",
        .on_click = ^{ g_capture_value = props->value; },
    }, NULL);
}

static bool test_click_handler_props(void) {
    g_capture_value = 0;

    cr_begin_frame();
    CaptureComponent((CaptureProps){ .value = 42 });
    cr_end_frame();

    CHECK(cr_runtime != NULL);
    CHECK(cr_runtime->click_handler_count > 0);
    cr_runtime->click_handlers[0].handler();
    CHECK(g_capture_value == 42);

    return true;
}

// ============================================================================
// KEYED COMPONENT TESTS
// ============================================================================

typedef struct {
    int id;
    int init;
} KeyedProps;

static int g_key_values[2] = {0, 0};
static int g_key_set_once[2] = {0, 0};
static bool g_key_swap = false;

$component(KeyedChild, KeyedProps) {
    auto st = $use_state(props->init);
    if (!g_key_set_once[props->id]) {
        st->set(props->init + 10);
        g_key_set_once[props->id] = 1;
    }
    g_key_values[props->id] = st->get();
}

$component(KeyedParent) {
    if (!g_key_swap) {
        $keyi("KeyedChild", 0);
        KeyedChild((KeyedProps){ .id = 0, .init = 1 });
        $keyi("KeyedChild", 1);
        KeyedChild((KeyedProps){ .id = 1, .init = 2 });
    } else {
        $keyi("KeyedChild", 1);
        KeyedChild((KeyedProps){ .id = 1, .init = 2 });
        $keyi("KeyedChild", 0);
        KeyedChild((KeyedProps){ .id = 0, .init = 1 });
    }
}

static bool test_keyed_components(void) {
    g_key_values[0] = g_key_values[1] = 0;
    g_key_set_once[0] = g_key_set_once[1] = 0;
    g_key_swap = false;

    cr_begin_frame();
    KeyedParent();
    cr_end_frame();

    g_key_swap = true;
    cr_begin_frame();
    KeyedParent();
    cr_end_frame();

    CHECK(g_key_values[0] == 11);
    CHECK(g_key_values[1] == 12);

    return true;
}

// ============================================================================
// CONTEXT TESTS
// ============================================================================

typedef struct {
    $field(int, value);
} Theme;

static CR_Context *g_theme_ctx = NULL;
static int g_context_seen = -1;

$component(ContextChild) {
    Theme *theme = $use_context(g_theme_ctx);
    g_context_seen = theme ? theme->value : -1;
}

$component(ContextParent) {
    Theme local = { .value = 77 };
    $provide(g_theme_ctx, &local) {
        ContextChild();
    }
}

static bool test_context(void) {
    Theme default_theme = { .value = 13 };
    if (!g_theme_ctx) {
        g_theme_ctx = $create_context(Theme, &default_theme);
    }

    cr_begin_frame();
    ContextChild();
    cr_end_frame();

    CHECK(g_context_seen == 13);

    cr_begin_frame();
    ContextParent();
    cr_end_frame();

    CHECK(g_context_seen == 77);

    return true;
}

// ============================================================================
// SIGNAL TESTS
// ============================================================================

static int g_signal_value = 0;
static int g_signal_notify_count = 0;

static bool test_signal(void) {
    g_signal_value = 0;
    g_signal_notify_count = 0;

    auto sig = $signal(int, 1);
    sig->subscribe(^(int *value) {
        g_signal_value = *value;
        g_signal_notify_count++;
    });

    sig->set(5);

    CHECK(sig->get() == 5);
    CHECK(g_signal_notify_count == 1);
    CHECK(g_signal_value == 5);

    return true;
}

// ============================================================================
// MAIN
// ============================================================================

int main(void) {
    init_clay_once();
    cr_init();

    int failed = 0;
    failed += !run_test("state_persistence", test_state_persistence);
    failed += !run_test("effects", test_effects);
    failed += !run_test("effect_queue_realloc", test_effect_queue_realloc);
    failed += !run_test("memo", test_memo);
    failed += !run_test("callback", test_callback);
    failed += !run_test("ref", test_ref);
    failed += !run_test("use_id", test_use_id);
    failed += !run_test("text_input", test_text_input);
    failed += !run_test("click_handler_props", test_click_handler_props);
    failed += !run_test("keyed_components", test_keyed_components);
    failed += !run_test("context", test_context);
    failed += !run_test("signal", test_signal);

    cr_shutdown();
    if (g_arena.memory) {
        free(g_arena.memory);
        g_arena.memory = NULL;
        g_arena.capacity = 0;
    }

    if (failed == 0) {
        fprintf(stdout, "All Clay React tests passed.\n");
    } else {
        fprintf(stdout, "%d Clay React tests failed.\n", failed);
    }

    return failed ? 1 : 0;
}
