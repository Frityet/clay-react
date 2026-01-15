#define _GNU_SOURCE

#include <setjmp.h>
#include <stdbool.h>
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

#include "reflect.h"

typedef struct Point {
    $field(int, x);
    $field(float, y);
} Point;

typedef struct Pair {
    $field(int, a);
    $field(int, b);
} Pair;

typedef struct Container {
    $field(Pair, pair);
    $field(int, count);
} Container;

typedef struct Simple {
    $field(int, x);
    $field(bool, ok);
} Simple;

typedef struct PointerFields {
    $field(char *, name);
    $field(int *, value);
} PointerFields;

typedef struct OptionalFields {
    $field(int, required);
    $field(int, maybe, $modifiers(optional));
    $field(int, skip, $modifiers(no_deserialise));
} OptionalFields;

typedef struct SizedIntPtrTest {
    $field(size_t, len, $modifiers(no_serialise));
    $field(int *, nums, $modifiers($sized_by(len)));
} SizedIntPtrTest;

typedef struct SizedFieldTest {
    $field(size_t, len);
    $field(int *, nums, $modifiers($sized_by(len)));
} SizedFieldTest;

typedef struct SerialiseAsTest {
    $field(int, my_val, $modifiers($serialise_as(MyVal)));
} SerialiseAsTest;

typedef struct SizedStringTest {
    $field(size_t, len);
    $field(char *, text, $modifiers($sized_by(len)));
} SizedStringTest;

typedef struct PaddedStruct {
    $field(char, a);
    $field(int, b);
    $field(char, c);
} PaddedStruct;


typedef struct TaggedNumber {
    $field(int, tag);
    $field(union NumberUnion {
        $field(int, i, $modifiers($tag_value(0)));
        $field(float, f, $modifiers($tag_value(1)));
    }, value, $modifiers($tagged_by(tag)));
} TaggedNumber;

typedef struct Plain {
    int x;
    float y;
} Plain;

typedef struct Clay_SDL3RendererData { } Clay_SDL3RendererData;

typedef struct app_state {
    $field(struct SDL_Window *, window);
    $field(Clay_SDL3RendererData, rendererData);
} AppState;

static char *value_to_json_string(struct Value value)
{
    char *buffer = NULL;
    size_t size = 0;
    FILE *stream = open_memstream(&buffer, &size);

    ASSERT_NOT_NULL(stream);
    EXPECT_EQ(value_to_json(stream, value), 0);
    fclose(stream);

    return buffer;
}

static int json_to_value_string(const char *json, struct Value *value)
{
    return json_to_value(json, strlen(json), 256, value);
}

TEST_CASE(test_parse_struct)
{
    struct Type type = {0};
    const char *enc = @encode(Point);

    EXPECT_EQ(parse_type(&type, &enc), 0);
    // if (type.type != TypeType_STRUCT) return;
    EXPECT_EQ(type.type, TypeType_STRUCT);
    EXPECT_EQ(type.structure.field_count, 2ul);
    EXPECT_EQ(type.structure.field_count, 2ul);
    EXPECT_EQ((const char *)type.structure.fields[0].name->data, "x");
    EXPECT_EQ((const char *)type.structure.fields[1].name->data, "y");
    EXPECT_EQ(type_name(type), "Point");
    EXPECT_EQ(type_size(type), sizeof(Point));
}

TEST_CASE(test_parse_struct_no_field)
{
    struct Type type = {0};
    const char *enc = @encode(Plain);

    EXPECT_EQ(parse_type(&type, &enc), 0);
    EXPECT_EQ(type.type, TypeType_STRUCT);
    EXPECT_EQ(type.structure.field_count, 2ul);
    EXPECT_EQ(type.structure.fields[0].name, nullptr);
    EXPECT_EQ(type.structure.fields[1].name, nullptr);
    EXPECT_EQ(type_name(type), "Plain");
    EXPECT_EQ(type_size(type), sizeof(Plain));
}

TEST_CASE(test_get_value)
{
    Point point = {.x = 3, .y = 4.5f};
    struct Value value = $valueof(&point);

    struct Value x_value = get_value(value, "x");
    ASSERT_NOT_NULL(x_value.data);
    EXPECT_EQ(*(int *)x_value.data, 3);

    struct Value y_value = get_value(value, "y");
    ASSERT_NOT_NULL(y_value.data);
    EXPECT_EQ(*(float *)y_value.data, 4.5f);

    struct Value missing = get_value(value, "z");
    EXPECT_EQ(missing.data, nullptr);
}

TEST_CASE(test_type_size_array)
{
    int items[3] = {0};
    struct Type type = {0};
    const char *enc = @encode(typeof(items));

    EXPECT_EQ(parse_type(&type, &enc), 0);
    EXPECT_EQ(type.type, TypeType_ARRAY);
    EXPECT_EQ(type.array.length, 3ul);
    EXPECT_EQ(type_size(type), sizeof(items));
}

TEST_CASE(test_parse_union)
{
    struct Type type = {0};
    const char *enc = @encode(union NumberUnion);

    EXPECT_EQ(parse_type(&type, &enc), 0);
    EXPECT_EQ(type.type, TypeType_UNION);
    EXPECT_EQ(type.union_.field_count, 2ul);
    EXPECT_EQ((const char *)type.union_.fields[0].name->data, "i");
    EXPECT_EQ((const char *)type.union_.fields[1].name->data, "f");
    EXPECT_EQ(type.union_.fields[0].offset, 0ul);
    EXPECT_EQ(type.union_.fields[1].offset, 0ul);
    EXPECT_EQ(type_size(type), sizeof(union NumberUnion));
}

TEST_CASE(test_padded_struct_layout)
{
    PaddedStruct padded = {.a = 1, .b = 0x12345678, .c = 2};
    struct Value value = $valueof(&padded);

    EXPECT_EQ(type_size(value.type), sizeof(PaddedStruct));
    EXPECT_EQ(value.type.structure.field_count, 3ul);
    EXPECT_EQ(value.type.structure.fields[0].offset, offsetof(PaddedStruct, a));
    EXPECT_EQ(value.type.structure.fields[1].offset, offsetof(PaddedStruct, b));
    EXPECT_EQ(value.type.structure.fields[2].offset, offsetof(PaddedStruct, c));

    struct Value a_val = get_value(value, "a");
    struct Value b_val = get_value(value, "b");
    struct Value c_val = get_value(value, "c");

    ASSERT_NOT_NULL(a_val.data);
    ASSERT_NOT_NULL(b_val.data);
    ASSERT_NOT_NULL(c_val.data);

    EXPECT_EQ((int)*(char *)a_val.data, (int)padded.a);
    EXPECT_EQ(*(int *)b_val.data, padded.b);
    EXPECT_EQ((int)*(char *)c_val.data, (int)padded.c);
}

TEST_CASE(test_parse_invalid)
{
    struct Type type = {0};
    const char *enc = "{Invalid";
    int result = parse_type(&type, &enc);
    (void)enc; // enc is modified by parse_type
    (void)type; // type may be partially initialized but we only care about the error
    EXPECT_NE(result, 0);
}

TEST_CASE(test_nested_struct_access)
{
    Container container = {.pair = {.a = 5, .b = 7}, .count = 2};
    struct Value value = $valueof(&container);

    struct Value pair_value = get_value(value, "pair");
    ASSERT_NOT_NULL(pair_value.data);

    struct Value a_value = get_value(pair_value, "a");
    ASSERT_NOT_NULL(a_value.data);
    EXPECT_EQ(*(int *)a_value.data, 5);

    EXPECT_EQ($access(int, value, "count"), 2);
}

TEST_CASE(test_value_to_json_struct)
{
    Simple simple = {.x = 42, .ok = true};
    struct Value value = $valueof(&simple);
    char *buffer = value_to_json_string(value);

    EXPECT_EQ(buffer, "{\"x\": 42, \"ok\": true}");

    free(buffer);
}

TEST_CASE(test_value_to_json_nested_struct)
{
    Container container = {.pair = {.a = 1, .b = 2}, .count = 3};
    struct Value value = $valueof(&container);
    char *buffer = value_to_json_string(value);

    EXPECT_EQ(buffer, "{\"pair\": {\"a\": 1, \"b\": 2}, \"count\": 3}");

    free(buffer);
}

TEST_CASE(test_value_to_json_array)
{
    int items[3] = {1, 2, 3};
    struct Value value = $valueof(&items);
    char *buffer = value_to_json_string(value);

    EXPECT_EQ(buffer, "[1, 2, 3]");

    free(buffer);
}

TEST_CASE(test_value_to_json_pointer)
{
    int number = 7;
    int *ptr = &number;
    struct Value value = $valueof(&ptr);
    char *buffer = value_to_json_string(value);

    EXPECT_EQ(buffer, "7");

    free(buffer);

    int *null_ptr = NULL;
    struct Value null_value = $valueof(&null_ptr);
    buffer = value_to_json_string(null_value);

    EXPECT_EQ(buffer, "null");

    free(buffer);
}

TEST_CASE(test_value_to_json_cstring)
{
    const char *label = "hello";
    struct Value value = $valueof(&label);
    char *buffer = value_to_json_string(value);

    EXPECT_EQ(buffer, "\"hello\"");

    free(buffer);
}

TEST_CASE(test_json_to_value_struct_simple)
{
    Simple simple = {.x = 0, .ok = false};
    struct Value value = $valueof(&simple);
    const char *json = "{\"x\": 42, \"ok\": true}";

    EXPECT_EQ(json_to_value_string(json, &value), 0);
    EXPECT_EQ(simple.x, 42);
    EXPECT_EQ(simple.ok, true);
}

TEST_CASE(test_json_to_value_nested_struct)
{
    Container container = {0};
    struct Value value = $valueof(&container);
    const char *json = "{\"pair\": {\"a\": 1, \"b\": 2}, \"count\": 3}";

    EXPECT_EQ(json_to_value_string(json, &value), 0);
    EXPECT_EQ(container.pair.a, 1);
    EXPECT_EQ(container.pair.b, 2);
    EXPECT_EQ(container.count, 3);
}

TEST_CASE(test_json_to_value_array)
{
    int items[3] = {0};
    struct Value value = $valueof(&items);
    const char *json = "[1, 2, 3]";

    EXPECT_EQ(json_to_value_string(json, &value), 0);
    EXPECT_EQ(items[0], 1);
    EXPECT_EQ(items[1], 2);
    EXPECT_EQ(items[2], 3);
}

TEST_CASE(test_json_to_value_pointer_fields)
{
    PointerFields fields = {0};
    struct Value value = $valueof(&fields);
    const char *json = "{\"name\": \"hello\", \"value\": 7}";

    EXPECT_EQ(json_to_value_string(json, &value), 0);
    EXPECT_EQ(fields.name, "hello");
    EXPECT_EQ(*fields.value, 7);

    free(fields.name);
    free(fields.value);
}

TEST_CASE(test_json_to_value_cstring)
{
    char *label = NULL;
    struct Value value = $valueof(&label);
    const char *json = "\"hello\\nworld\"";

    EXPECT_EQ(json_to_value_string(json, &value), 0);
    EXPECT_EQ(label, "hello\nworld");

    free(label);
}

TEST_CASE(test_json_to_value_sized_ptr)
{
    SizedFieldTest val = {0};
    struct Value v = $valueof(&val);
    const char *json = "{\"nums\": [5, 6, 7]}";

    EXPECT_EQ(json_to_value_string(json, &v), 0);
    EXPECT_EQ(val.len, 3ul);
    EXPECT_EQ(val.nums[0], 5);
    EXPECT_EQ(val.nums[1], 6);
    EXPECT_EQ(val.nums[2], 7);

    free(val.nums);
}

TEST_CASE(test_json_to_value_sized_ptr_with_length)
{
    SizedFieldTest val = {0};
    struct Value v = $valueof(&val);
    const char *json = "{\"len\": 2, \"nums\": [10, 20]}";

    EXPECT_EQ(json_to_value_string(json, &v), 0);
    EXPECT_EQ(val.len, 2ul);
    EXPECT_EQ(val.nums[0], 10);
    EXPECT_EQ(val.nums[1], 20);

    free(val.nums);
}

TEST_CASE(test_json_to_value_sized_ptr_mismatch)
{
    SizedFieldTest val = {0};
    struct Value v = $valueof(&val);
    const char *json = "{\"len\": 1, \"nums\": [10, 20]}";

    EXPECT_EQ(json_to_value_string(json, &v) != 0, true);
    if (val.nums != NULL) {
        free(val.nums);
    }
}

TEST_CASE(test_json_to_value_sized_string)
{
    SizedStringTest val = {0};
    struct Value v = $valueof(&val);
    const char *json = "{\"text\": \"hello\"}";

    EXPECT_EQ(json_to_value_string(json, &v), 0);
    EXPECT_EQ(val.len, 5ul);
    EXPECT_EQ(val.text, "hello");

    free(val.text);
}

TEST_CASE(test_json_to_value_serialise_as)
{
    SerialiseAsTest val = {0};
    struct Value v = $valueof(&val);
    const char *json = "{\"MyVal\": 42}";

    EXPECT_EQ(json_to_value_string(json, &v), 0);
    EXPECT_EQ(val.my_val, 42);
}

TEST_CASE(test_value_to_json_tagged_union)
{
    TaggedNumber val = {.tag = 0};
    val.value.i = 7;
    struct Value v = $valueof(&val);
    char *json = value_to_json_string(v);
    const char *expected = "{\"tag\": 0, \"value\": 7}";

    EXPECT_STREQ(json, expected);

    free(json);
}

TEST_CASE(test_json_to_value_tagged_union_ordered)
{
    TaggedNumber val = {0};
    struct Value v = $valueof(&val);
    const char *json = "{\"tag\": 1, \"value\": 2.5}";

    EXPECT_EQ(json_to_value_string(json, &v), 0);
    EXPECT_EQ(val.tag, 1);
    EXPECT_EQ(val.value.f, 2.5f);
}

TEST_CASE(test_json_to_value_tagged_union_reordered)
{
    TaggedNumber val = {0};
    struct Value v = $valueof(&val);
    const char *json = "{\"value\": 11, \"tag\": 0}";

    EXPECT_EQ(json_to_value_string(json, &v), 0);
    EXPECT_EQ(val.tag, 0);
    EXPECT_EQ(val.value.i, 11);
}

TEST_CASE(test_json_optional_no_deserialise)
{
    OptionalFields val = {.required = 0, .maybe = 111, .skip = 222};
    struct Value v = $valueof(&val);
    const char *json = "{\"required\": 5, \"skip\": 9}";

    EXPECT_EQ(json_to_value_string(json, &v), 0);
    EXPECT_EQ(val.required, 5);
    EXPECT_EQ(val.maybe, 111);
    EXPECT_EQ(val.skip, 222);
}

TEST_CASE(test_json_missing_required)
{
    OptionalFields val = {0};
    struct Value v = $valueof(&val);
    const char *json = "{\"maybe\": 3}";

    EXPECT_EQ(json_to_value_string(json, &v) != 0, true);
}

TEST_CASE(test_pointer_type)
{
    int number = 0;
    int *ptr = &number;
    struct Type type = {0};
    const char *enc = @encode(typeof(ptr));

    EXPECT_EQ(parse_type(&type, &enc), 0);
    EXPECT_EQ(type.type, TypeType_POINTER);
    ASSERT_NOT_NULL(type.pointer.type);
    EXPECT_EQ(type.pointer.type->type, TypeType_PRIMITIVE);
}

TEST_CASE(test_const_qualifier)
{
    const int *number = NULL;
    struct Type type = {0};
    const char *enc = @encode(typeof(number));

    EXPECT_EQ(parse_type(&type, &enc), 0);
    EXPECT_EQ(type.type, TypeType_POINTER);

    // GCC encodes `const int *` as `^ri` (Pointer to Const Int).
    // Clang encodes `const int *` as `r^i` (Const Pointer to Int??).
    // We check for either possibility.
    ASSERT_NOT_NULL(type.pointer.type);
    bool const_on_pointer = (type.modifiers & TypeModifiers_CONST);
    bool const_on_pointee = (type.pointer.type->modifiers & TypeModifiers_CONST);
    EXPECT_TRUE(const_on_pointer || const_on_pointee);
    EXPECT_EQ(type.pointer.type->type, TypeType_PRIMITIVE);
}

TEST_CASE(test_type_hash_equal)
{
    struct Type first = $reflect(Point);
    struct Type second = $reflect(Point);

    EXPECT_EQ(first.hash, second.hash);
}

TEST_CASE(test_cast)
{
    int number = 123;
    struct Value value = $valueof(&number);

    int *as_int = $cast(value, int);
    float *as_float = $cast(value, float);

    ASSERT_NOT_NULL(as_int);
    EXPECT_EQ(*as_int, 123);
    EXPECT_EQ(as_float, nullptr);
}

TEST_CASE(test_renderdata)
{
    struct Type type = $reflect(AppState);

    EXPECT_EQ(type.type, TypeType_STRUCT);
    EXPECT_EQ(type.structure.field_count, 2ul);
    EXPECT_EQ((const char *)type.structure.fields[0].name->data, "window");
    EXPECT_EQ((const char *)type.structure.fields[1].name->data, "rendererData");
}

TEST_CASE(test_sized_ptr)
{
    int nums[] = {1, 2, 3};
    SizedIntPtrTest val = { .len = 3, .nums = nums };
    struct Value v = $valueof(&val);

    char *json = value_to_json_string(v);
    const char *expected = "{\"nums\": [1, 2, 3]}";

    EXPECT_STREQ(json, expected);

    free(json);
}

TEST_CASE(test_sized_field)
{
    int nums[] = {10, 20};
    SizedFieldTest val = { .len = 2, .nums = nums };
    struct Value v = $valueof(&val);

    char *json = value_to_json_string(v);
    const char *expected = "{\"len\": 2, \"nums\": [10, 20]}";

    EXPECT_STREQ(json, expected);

    struct Type t = v.type;
    ASSERT_EQ(t.type, TypeType_STRUCT);
    struct Field *f = &t.structure.fields[1];
    EXPECT_EQ(f->modifier_count, 1ul);
    EXPECT_EQ((const char *)f->modifiers[0]->data, "sized_by_len");

    free(json);
}

TEST_CASE(test_serialise_as)
{
    SerialiseAsTest val = { .my_val = 42 };
    struct Value v = $valueof(&val);

    char *json = value_to_json_string(v);
    // Expect "MyVal": 42 instead of "my_val": 42
    const char *expected = "{\"MyVal\": 42}";

    EXPECT_STREQ(json, expected);

    free(json);
}


typedef struct CachedNode {
    $field(int, val);
    $field(struct CachedNode *, next);
} CachedNode;

typedef struct CacheContainer {
    $field(struct CachedNode *, first);
} CacheContainer;

TEST_CASE(test_type_cache)
{
    // 1. Reflect on CachedNode first. This should populate the cache.
    // Explicitly using a struct with fields to ensure it's not opaque.
    // Note: 'next' is a pointer to CachedNode.
    struct Type t_node = {0};
    const char *enc_node = @encode(struct CachedNode);
    EXPECT_EQ(parse_type(&t_node, &enc_node), 0);
    EXPECT_EQ(t_node.type, TypeType_STRUCT);
    EXPECT_EQ(t_node.structure.field_count, 2ul); // val, next

    // 2. Reflect on CacheContainer.
    // @encode(struct CacheContainer) -> "{CacheContainer=^{CachedNode}}".
    // If cache works, parsing '^{CachedNode}' -> Pointer -> Struct CachedNode (opaque in encoding).
    // lookup_cached_type should find CachedNode and fill it.
    struct Type t_cont = {0};
    const char *enc_cont = @encode(struct CacheContainer);

    EXPECT_EQ(parse_type(&t_cont, &enc_cont), 0);
    EXPECT_EQ(t_cont.type, TypeType_STRUCT);
    struct Field *f_first = &t_cont.structure.fields[0];
    EXPECT_EQ(f_first->type.type, TypeType_POINTER);

    // Check if the pointee is resolved
    struct Type *pointee = f_first->type.pointer.type;
    EXPECT_EQ(pointee->type, TypeType_STRUCT);
    // If not resolved, field_count would be 0
    EXPECT_EQ(pointee->structure.field_count, 2ul);
    EXPECT_EQ((const char *)pointee->structure.fields[0].name->data, "val");
}

TEST_MAIN();
