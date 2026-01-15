/**
 * Clay React tests - runtime and hook behavior
 */
#define CLAY_IMPLEMENTATION
#include <clay.h>
#include "clay_react/clay_react.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef void (*TestFn)(void);

typedef struct TestCase {
    const char *name;
    TestFn fn;
    struct TestCase *next;
} TestCase;

static TestCase *g_test_head = NULL;
static TestCase *g_test_tail = NULL;
static void (*g_suite_setup)(void) = NULL;
static void (*g_suite_teardown)(void) = NULL;
static int g_current_failures = 0;
static int g_current_assertions = 0;
static int g_total_failures = 0;
static int g_total_tests = 0;
static int g_total_assertions = 0;
static bool g_abort_enabled = false;
static jmp_buf g_abort_jmp;

static void test_register(TestCase *test) {
    if (!test) return;
    test->next = NULL;
    if (!g_test_head) {
        g_test_head = test;
        g_test_tail = test;
    } else {
        g_test_tail->next = test;
        g_test_tail = test;
    }
}

static void test_record_assertion(void) {
    g_current_assertions++;
}

static void test_record_failure(const char *file, int line, const char *kind, const char *expr) {
    g_current_failures++;
    fprintf(stderr, "  %s:%d: %s\n    %s\n", file, line, kind, expr);
}

static void test_abort(void) {
    if (g_abort_enabled) {
        longjmp(g_abort_jmp, 1);
    }
    abort();
}

#ifndef TEST_FLOAT_EPS
#define TEST_FLOAT_EPS 1e-5
#endif

static bool test_eq_str(const char *a, const char *b) {
    if (a == NULL || b == NULL) {
        return a == b;
    }
    return strcmp(a, b) == 0;
}

static bool test_eq_signed(long long a, long long b) {
    return a == b;
}

static bool test_eq_unsigned(unsigned long long a, unsigned long long b) {
    return a == b;
}

static bool test_eq_fp(long double a, long double b) {
    long double diff = a - b;
    if (diff < 0) {
        diff = -diff;
    }
    return diff <= (long double)TEST_FLOAT_EPS;
}

static bool test_eq_ptr(const void *a, const void *b) {
    return a == b;
}

static void test_print_str(FILE *out, const char *value) {
    if (value == NULL) {
        fputs("(null)", out);
        return;
    }
    fprintf(out, "\"%s\"", value);
}

static void test_print_int(FILE *out, long long value) {
    fprintf(out, "%lld", value);
}

static void test_print_uint(FILE *out, unsigned long long value) {
    fprintf(out, "%llu", value);
}

static void test_print_double(FILE *out, long double value) {
    fprintf(out, "%Lg", value);
}

static void test_print_bool(FILE *out, bool value) {
    fputs(value ? "true" : "false", out);
}

static void test_print_ptr(FILE *out, const void *value) {
    fprintf(out, "%p", value);
}

#define TEST_CONCAT_INNER(a, b) a##b
#define TEST_CONCAT(a, b) TEST_CONCAT_INNER(a, b)

#define TEST_EQ(a, b) _Generic((a), \
    char: test_eq_signed, \
    signed char: test_eq_signed, \
    short: test_eq_signed, \
    int: test_eq_signed, \
    long: test_eq_signed, \
    long long: test_eq_signed, \
    unsigned char: test_eq_unsigned, \
    unsigned short: test_eq_unsigned, \
    unsigned int: test_eq_unsigned, \
    unsigned long: test_eq_unsigned, \
    unsigned long long: test_eq_unsigned, \
    float: test_eq_fp, \
    double: test_eq_fp, \
    long double: test_eq_fp, \
    _Bool: test_eq_signed, \
    char *: test_eq_str, \
    const char *: test_eq_str, \
    default: test_eq_ptr \
)(a, b)

#define TEST_PRINT_VALUE(out, value) _Generic((value), \
    char: test_print_int, \
    signed char: test_print_int, \
    short: test_print_int, \
    int: test_print_int, \
    long: test_print_int, \
    long long: test_print_int, \
    unsigned char: test_print_uint, \
    unsigned short: test_print_uint, \
    unsigned int: test_print_uint, \
    unsigned long: test_print_uint, \
    unsigned long long: test_print_uint, \
    float: test_print_double, \
    double: test_print_double, \
    long double: test_print_double, \
    _Bool: test_print_bool, \
    char *: test_print_str, \
    const char *: test_print_str, \
    default: test_print_ptr \
)(out, value)

#define TEST_LOG_VALUE(label, value) do { \
    fprintf(stderr, "    %s: ", (label)); \
    TEST_PRINT_VALUE(stderr, (value)); \
    fputc('\n', stderr); \
} while (0)

#define TEST_CASE(case_fn) \
    static void case_fn(void); \
    static TestCase TEST_CONCAT(case_fn, _case) = { \
        .name = #case_fn, \
        .fn = case_fn, \
        .next = NULL, \
    }; \
    static void TEST_CONCAT(case_fn, _register)(void) __attribute__((constructor)); \
    static void TEST_CONCAT(case_fn, _register)(void) { test_register(&TEST_CONCAT(case_fn, _case)); } \
    static void case_fn(void)

#define TEST_SUITE_SETUP(name) \
    static void name(void); \
    static void TEST_CONCAT(name, _suite_setup_register)(void) __attribute__((constructor)); \
    static void TEST_CONCAT(name, _suite_setup_register)(void) { g_suite_setup = name; } \
    static void name(void)

#define TEST_SUITE_TEARDOWN(name) \
    static void name(void); \
    static void TEST_CONCAT(name, _suite_teardown_register)(void) __attribute__((constructor)); \
    static void TEST_CONCAT(name, _suite_teardown_register)(void) { g_suite_teardown = name; } \
    static void name(void)

#define EXPECT_TRUE(expr) do { \
    test_record_assertion(); \
    if (!(expr)) { \
        test_record_failure(__FILE__, __LINE__, "EXPECT_TRUE", #expr); \
    } \
} while (0)

#define EXPECT_FALSE(expr) do { \
    test_record_assertion(); \
    if ((expr)) { \
        test_record_failure(__FILE__, __LINE__, "EXPECT_FALSE", #expr); \
    } \
} while (0)

#define EXPECT_EQ(a, b) do { \
    __auto_type _a = (a); \
    __auto_type _b = (b); \
    test_record_assertion(); \
    if (!TEST_EQ(_a, _b)) { \
        test_record_failure(__FILE__, __LINE__, "EXPECT_EQ", #a " == " #b); \
        TEST_LOG_VALUE("left", _a); \
        TEST_LOG_VALUE("right", _b); \
    } \
} while (0)

#define EXPECT_NE(a, b) do { \
    __auto_type _a = (a); \
    __auto_type _b = (b); \
    test_record_assertion(); \
    if (TEST_EQ(_a, _b)) { \
        test_record_failure(__FILE__, __LINE__, "EXPECT_NE", #a " != " #b); \
        TEST_LOG_VALUE("left", _a); \
        TEST_LOG_VALUE("right", _b); \
    } \
} while (0)

#define EXPECT_STREQ(a, b) do { \
    const char *_a = (a); \
    const char *_b = (b); \
    test_record_assertion(); \
    if (!test_eq_str(_a, _b)) { \
        test_record_failure(__FILE__, __LINE__, "EXPECT_STREQ", #a " == " #b); \
        TEST_LOG_VALUE("left", _a); \
        TEST_LOG_VALUE("right", _b); \
    } \
} while (0)

#define ASSERT_TRUE(expr) do { \
    test_record_assertion(); \
    if (!(expr)) { \
        test_record_failure(__FILE__, __LINE__, "ASSERT_TRUE", #expr); \
        test_abort(); \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    __auto_type _a = (a); \
    __auto_type _b = (b); \
    test_record_assertion(); \
    if (!TEST_EQ(_a, _b)) { \
        test_record_failure(__FILE__, __LINE__, "ASSERT_EQ", #a " == " #b); \
        TEST_LOG_VALUE("left", _a); \
        TEST_LOG_VALUE("right", _b); \
        test_abort(); \
    } \
} while (0)

#define ASSERT_NOT_NULL(ptr) do { \
    test_record_assertion(); \
    if ((ptr) == NULL) { \
        test_record_failure(__FILE__, __LINE__, "ASSERT_NOT_NULL", #ptr); \
        test_abort(); \
    } \
} while (0)

static TestCase *find_test(const char *name) {
    if (!name) return NULL;
    for (TestCase *test = g_test_head; test != NULL; test = test->next) {
        if (strcmp(test->name, name) == 0) {
            return test;
        }
    }
    return NULL;
}

static void list_tests(void) {
    for (TestCase *test = g_test_head; test != NULL; test = test->next) {
        printf("%s\n", test->name);
    }
}

static void run_test_case(TestCase *test) {
    g_current_failures = 0;
    g_current_assertions = 0;
    if (setjmp(g_abort_jmp) == 0) {
        g_abort_enabled = true;
        test->fn();
    }
    g_abort_enabled = false;
    g_total_assertions += g_current_assertions;
    if (g_current_failures > 0) {
        g_total_failures++;
    }
    g_total_tests++;
}

static int run_all_tests(const char *filter) {
    g_total_failures = 0;
    g_total_tests = 0;
    g_total_assertions = 0;

    if (filter) {
        TestCase *test = find_test(filter);
        if (!test) {
            fprintf(stderr, "Unknown test: %s\n", filter);
            return 1;
        }
        if (g_suite_setup) {
            g_suite_setup();
        }
        run_test_case(test);
        if (g_suite_teardown) {
            g_suite_teardown();
        }
    } else {
        if (g_suite_setup) {
            g_suite_setup();
        }
        for (TestCase *test = g_test_head; test != NULL; test = test->next) {
            run_test_case(test);
        }
        if (g_suite_teardown) {
            g_suite_teardown();
        }
    }

    if (g_total_failures > 0) {
        fprintf(stderr, "Failed %d/%d test(s) with %d assertion(s)\n",
            g_total_failures, g_total_tests, g_total_assertions);
    }
    return g_total_failures > 0 ? 1 : 0;
}

#define TEST_MAIN() \
    int main(int argc, char **argv) { \
        if (argc > 1 && strcmp(argv[1], "--list") == 0) { \
            list_tests(); \
            return 0; \
        } \
        return run_all_tests(argc > 1 ? argv[1] : NULL); \
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

TEST_CASE(test_state_persistence) {
    g_state_render_count = 0;
    g_state_last_value = -1;
    g_state_get = NULL;
    g_state_set = NULL;

    cr_begin_frame();
    StateTestComponent();
    cr_end_frame();

    EXPECT_EQ(g_state_render_count, 1);
    EXPECT_EQ(g_state_last_value, 0);
    ASSERT_NOT_NULL(g_state_get);
    ASSERT_NOT_NULL(g_state_set);

    g_state_set(5);
    EXPECT_TRUE(cr_should_render());

    cr_begin_frame();
    StateTestComponent();
    cr_end_frame();

    EXPECT_EQ(g_state_render_count, 2);
    EXPECT_EQ(g_state_last_value, 5);
    EXPECT_EQ(g_state_get(), 5);
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

TEST_CASE(test_effects) {
    g_effect_runs = 0;
    g_effect_cleanups = 0;
    g_effect_seen = -1;
    g_effect_set = NULL;

    cr_begin_frame();
    EffectTestComponent();
    cr_end_frame();

    EXPECT_EQ(g_effect_runs, 1);
    EXPECT_EQ(g_effect_cleanups, 0);
    EXPECT_EQ(g_effect_seen, 0);

    ASSERT_NOT_NULL(g_effect_set);
    g_effect_set(1);
    EXPECT_TRUE(cr_should_render());

    cr_begin_frame();
    EffectTestComponent();
    cr_end_frame();

    EXPECT_EQ(g_effect_runs, 2);
    EXPECT_EQ(g_effect_cleanups, 1);
    EXPECT_EQ(g_effect_seen, 1);
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

TEST_CASE(test_effect_queue_realloc) {
    g_realloc_effect_runs = 0;

    cr_begin_frame();
    EffectReallocComponent();
    cr_end_frame();

    EXPECT_EQ(g_realloc_effect_runs, 1);
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

TEST_CASE(test_memo) {
    g_memo_runs = 0;
    g_memo_value = 0;
    g_memo_set = NULL;

    cr_begin_frame();
    MemoTestComponent();
    cr_end_frame();

    EXPECT_EQ(g_memo_runs, 1);
    EXPECT_EQ(g_memo_value, 2);

    cr_begin_frame();
    MemoTestComponent();
    cr_end_frame();

    EXPECT_EQ(g_memo_runs, 1);
    EXPECT_EQ(g_memo_value, 2);

    ASSERT_NOT_NULL(g_memo_set);
    g_memo_set(3);

    cr_begin_frame();
    MemoTestComponent();
    cr_end_frame();

    EXPECT_EQ(g_memo_runs, 2);
    EXPECT_EQ(g_memo_value, 6);
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

TEST_CASE(test_callback) {
    g_cb_render = 0;
    g_cb_seen = 0;
    g_cb_stable = NULL;
    g_cb_stable_current = NULL;
    g_cb_versioned = NULL;

    cr_begin_frame();
    CallbackTestComponent();
    cr_end_frame();

    ASSERT_NOT_NULL(g_cb_stable);
    ASSERT_NOT_NULL(g_cb_versioned);
    g_cb_versioned();
    EXPECT_EQ(g_cb_seen, 1);

    cr_begin_frame();
    CallbackTestComponent();
    cr_end_frame();

    EXPECT_TRUE(g_cb_stable_current == g_cb_stable);
    g_cb_versioned();
    EXPECT_EQ(g_cb_seen, 2);

    g_cb_stable();
    EXPECT_EQ(g_cb_seen, -1);
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

TEST_CASE(test_ref) {
    g_ref_ptr = NULL;
    g_ref_value = 0;

    cr_begin_frame();
    RefTestComponent();
    cr_end_frame();

    ASSERT_NOT_NULL(g_ref_ptr);
    EXPECT_EQ(g_ref_value, 10);

    cr_begin_frame();
    RefTestComponent();
    cr_end_frame();

    EXPECT_EQ(g_ref_value, 42);
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

TEST_CASE(test_use_id) {
    g_id_a = (CR_Id){0};
    g_id_b = (CR_Id){0};

    cr_begin_frame();
    IdRoot();
    cr_end_frame();

    CR_Id first_a = g_id_a;
    CR_Id first_b = g_id_b;

    ASSERT_NOT_NULL(first_a.name);
    ASSERT_NOT_NULL(first_b.name);
    EXPECT_FALSE(id_equal(first_a, first_b));

    cr_begin_frame();
    IdRoot();
    cr_end_frame();

    EXPECT_TRUE(id_equal(first_a, g_id_a));
    EXPECT_TRUE(id_equal(first_b, g_id_b));
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

TEST_CASE(test_text_input) {
    g_input = NULL;
    g_input_len = 0;
    memset(g_input_buf, 0, sizeof(g_input_buf));

    cr_begin_frame();
    TextInputTestComponent();
    cr_end_frame();

    ASSERT_NOT_NULL(g_input);
    EXPECT_EQ(g_input_len, (size_t)2);
    EXPECT_STREQ(g_input_buf, "hi");

    cr_begin_frame();
    TextInputTestComponent();
    cr_end_frame();

    EXPECT_EQ(g_input_len, (size_t)2);
    EXPECT_STREQ(g_input_buf, "hi");
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

TEST_CASE(test_click_handler_props) {
    g_capture_value = 0;

    cr_begin_frame();
    CaptureComponent((CaptureProps){ .value = 42 });
    cr_end_frame();

    ASSERT_NOT_NULL(cr_runtime);
    ASSERT_TRUE(cr_runtime->click_handler_count > 0);
    cr_runtime->click_handlers[0].handler();
    EXPECT_EQ(g_capture_value, 42);
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

TEST_CASE(test_keyed_components) {
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

    EXPECT_EQ(g_key_values[0], 11);
    EXPECT_EQ(g_key_values[1], 12);
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

TEST_CASE(test_context) {
    Theme default_theme = { .value = 13 };
    if (!g_theme_ctx) {
        g_theme_ctx = $create_context(Theme, &default_theme);
    }

    cr_begin_frame();
    ContextChild();
    cr_end_frame();

    EXPECT_EQ(g_context_seen, 13);

    cr_begin_frame();
    ContextParent();
    cr_end_frame();

    EXPECT_EQ(g_context_seen, 77);
}

// ============================================================================
// SIGNAL TESTS
// ============================================================================

static int g_signal_value = 0;
static int g_signal_notify_count = 0;

TEST_CASE(test_signal) {
    g_signal_value = 0;
    g_signal_notify_count = 0;

    auto sig = $signal(int, 1);
    ASSERT_NOT_NULL(sig);
    sig->subscribe(^(int *value) {
        g_signal_value = *value;
        g_signal_notify_count++;
    });

    sig->set(5);

    EXPECT_EQ(sig->get(), 5);
    EXPECT_EQ(g_signal_notify_count, 1);
    EXPECT_EQ(g_signal_value, 5);
}

TEST_SUITE_SETUP(clay_react_suite_setup) {
    init_clay_once();
    cr_init();
}

TEST_SUITE_TEARDOWN(clay_react_suite_teardown) {
    cr_shutdown();
    if (g_arena.memory) {
        free(g_arena.memory);
        g_arena.memory = NULL;
        g_arena.capacity = 0;
    }
}

TEST_MAIN();
