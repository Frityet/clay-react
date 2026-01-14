#define _GNU_SOURCE

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "reflect.h"

static bool check_eq_str(const char *a, const char *b)
{
    if (a == NULL || b == NULL) return a == b;
    return strcmp(a, b) == 0;
}

static bool check_eq_ptr(void *a, void *b)
{
    return a == b;
}

static bool check_eq_long(long a, long b)
{
    return a == b;
}

static bool check_eq_int(int a, int b)
{
    return a == b;
}

static bool check_eq_size(size_t a, size_t b)
{
    return a == b;
}

static bool check_eq_double(double a, double b)
{
    return a - b < 0.00001;
}

#define check_eq(a, b) _Generic((a), \
    char *: check_eq_str, \
    const char *: check_eq_str, \
    int: check_eq_int, \
    unsigned int: check_eq_int, \
    long: check_eq_long, \
    unsigned long: check_eq_size, \
    double: check_eq_double, \
    float: check_eq_double, \
    _Bool: check_eq_int, \
    default: check_eq_ptr \
)(a, b)

#define $assert_eq(a, b) ({\
    bool res = check_eq(a, b); \
    if (not res) { \
        fprintf(stderr, "Assertion failed: %s == %s (at %s:%d)\n", #a, #b, __FILE__, __LINE__); \
    } \
    res; \
})

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
    $field(_Bool, ok);
} Simple;

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

    assert(stream != NULL);
    $assert_eq(value_to_json(stream, value), 0);
    fclose(stream);

    return buffer;
}

static void test_parse_struct(void)
{
    struct Type type = {0};
    const char *enc = @encode(Point);

    $assert_eq(parse_type(&type, &enc), 0);
    // if (type.type != TypeType_STRUCT) return;
    $assert_eq(type.type, TypeType_STRUCT);
    $assert_eq(type.structure.field_count, 2ul);
    $assert_eq(type.structure.field_count, 2ul);
    $assert_eq((const char *)type.structure.fields[0].name->data, "x");
    $assert_eq((const char *)type.structure.fields[1].name->data, "y");
    $assert_eq(type_name(type), "Point");
    $assert_eq(type_size(type), sizeof(Point));

    free_type(&type);
}

static void test_get_value(void)
{
    Point point = {.x = 3, .y = 4.5f};
    struct Value value = $valueof(&point);

    struct Value x_value = get_value(value, "x");
    assert(x_value.data != nullptr);
    $assert_eq(*(int *)x_value.data, 3);

    struct Value y_value = get_value(value, "y");
    assert(y_value.data != nullptr);
    $assert_eq(*(float *)y_value.data, 4.5f);

    struct Value missing = get_value(value, "z");
    $assert_eq(missing.data, nullptr);

    free_type(&value.type);
}

static void test_type_size_array(void)
{
    int items[3] = {0};
    struct Type type = {0};
    const char *enc = @encode(typeof(items));

    $assert_eq(parse_type(&type, &enc), 0);
    $assert_eq(type.type, TypeType_ARRAY);
    $assert_eq(type.array.length, 3ul);
    $assert_eq(type_size(type), sizeof(items));

    free_type(&type);
}

static void test_parse_invalid(void)
{
    struct Type type = {0};
    const char *enc = "{Invalid";
    int result = parse_type(&type, &enc);
    (void)enc; // enc is modified by parse_type
    (void)type; // type may be partially initialized but we only care about the error
    (void)result; // used in assert below
    assert(result != 0);
}

static void test_nested_struct_access(void)
{
    Container container = {.pair = {.a = 5, .b = 7}, .count = 2};
    struct Value value = $valueof(&container);

    struct Value pair_value = get_value(value, "pair");
    assert(pair_value.data != nullptr);

    struct Value a_value = get_value(pair_value, "a");
    assert(a_value.data != nullptr);
    $assert_eq(*(int *)a_value.data, 5);

    $assert_eq($access(int, value, "count"), 2);

    free_type(&value.type);
}

static void test_value_to_json_struct(void)
{
    Simple simple = {.x = 42, .ok = true};
    struct Value value = $valueof(&simple);
    char *buffer = value_to_json_string(value);

    $assert_eq(buffer, "{\"x\": 42, \"ok\": true}");

    free(buffer);
    free_type(&value.type);
}

static void test_value_to_json_nested_struct(void)
{
    Container container = {.pair = {.a = 1, .b = 2}, .count = 3};
    struct Value value = $valueof(&container);
    char *buffer = value_to_json_string(value);

    $assert_eq(buffer, "{\"pair\": {\"a\": 1, \"b\": 2}, \"count\": 3}");

    free(buffer);
    free_type(&value.type);
}

static void test_value_to_json_array(void)
{
    int items[3] = {1, 2, 3};
    struct Value value = $valueof(&items);
    char *buffer = value_to_json_string(value);

    $assert_eq(buffer, "[1, 2, 3]");

    free(buffer);
    free_type(&value.type);
}

static void test_value_to_json_pointer(void)
{
    int number = 7;
    int *ptr = &number;
    struct Value value = $valueof(&ptr);
    char *buffer = value_to_json_string(value);

    $assert_eq(buffer, "7");

    free(buffer);
    free_type(&value.type);

    int *null_ptr = NULL;
    struct Value null_value = $valueof(&null_ptr);
    buffer = value_to_json_string(null_value);

    $assert_eq(buffer, "null");

    free(buffer);
    free_type(&null_value.type);
}

static void test_value_to_json_cstring(void)
{
    const char *label = "hello";
    struct Value value = $valueof(&label);
    char *buffer = value_to_json_string(value);

    $assert_eq(buffer, "\"hello\"");

    free(buffer);
    free_type(&value.type);
}

static void test_pointer_type(void)
{
    int number = 0;
    int *ptr = &number;
    struct Type type = {0};
    const char *enc = @encode(typeof(ptr));

    $assert_eq(parse_type(&type, &enc), 0);
    $assert_eq(type.type, TypeType_POINTER);
    assert(type.pointer.type != NULL);
    $assert_eq(type.pointer.type->type, TypeType_PRIMITIVE);

    free_type(&type);
}

static void test_const_qualifier(void)
{
    const int *number = NULL;
    struct Type type = {0};
    const char *enc = @encode(typeof(number));

    $assert_eq(parse_type(&type, &enc), 0);
    $assert_eq(type.type, TypeType_POINTER);

    // GCC encodes `const int *` as `^ri` (Pointer to Const Int).
    // Clang encodes `const int *` as `r^i` (Const Pointer to Int??).
    // We check for either possibility.
    bool const_on_pointer = (type.modifiers & TypeModifiers_CONST);
    bool const_on_pointee = (type.pointer.type->modifiers & TypeModifiers_CONST);

    if (!(const_on_pointer || const_on_pointee)) {
        fprintf(stderr, "Expected const qualifier on pointer or pointee, got neither.\n");
        exit(1);
    }

    assert(type.pointer.type != NULL);
    $assert_eq(type.pointer.type->type, TypeType_PRIMITIVE);

    free_type(&type);
}

static void test_type_hash_equal(void)
{
    struct Type first = $reflect(Point);
    struct Type second = $reflect(Point);

    $assert_eq(first.hash, second.hash);

    free_type(&first);
    free_type(&second);
}

static void test_cast(void)
{
    int number = 123;
    struct Value value = $valueof(&number);

    int *as_int = $cast(value, int);
    float *as_float = $cast(value, float);

    assert(as_int != nullptr);
    $assert_eq(*as_int, 123);
    $assert_eq(as_float, nullptr);

    free_type(&value.type);
}

static void test_renderdata()
{
    struct Type type = $reflect(AppState);

    $assert_eq(type.type, TypeType_STRUCT);
    $assert_eq(type.structure.field_count, 2ul);
    $assert_eq((const char *)type.structure.fields[0].name->data, "window");
    $assert_eq((const char *)type.structure.fields[1].name->data, "rendererData");

    free_type(&type);
}

typedef struct SizedIntPtrTest {
    $field(size_t, len, no_serialise);
    $field(int *, nums, $sized_by(len));
} SizedIntPtrTest;

static void test_sized_ptr(void)
{
    int nums[] = {1, 2, 3};
    SizedIntPtrTest val = { .len = 3, .nums = nums };
    struct Value v = $valueof(&val);

    char *json = value_to_json_string(v);
    const char *expected = "{\"nums\": [1, 2, 3]}";

    if (strcmp(json, expected) != 0) {
        fprintf(stderr, "Sized ptr test failed.\nExpected: %s\nActual: %s\n", expected, json);
        free(json);
        free_type(&v.type);
        exit(1);
    }

    free(json);
    free_type(&v.type);
}

typedef struct SizedFieldTest {
    $field(size_t, len);
    $field(int *, nums, $sized_by(len));
} SizedFieldTest;

static void test_sized_field(void)
{
    int nums[] = {10, 20};
    SizedFieldTest val = { .len = 2, .nums = nums };
    struct Value v = $valueof(&val);

    char *json = value_to_json_string(v);
    const char *expected = "{\"len\": 2, \"nums\": [10, 20]}";

     if (strcmp(json, expected) != 0) {
        fprintf(stderr, "Sized field test failed.\nExpected: %s\nActual: %s\n", expected, json);
        free(json);
        free_type(&v.type);
        exit(1);
    }

    struct Type t = v.type;
    assert(t.type == TypeType_STRUCT);
    struct Field *f = &t.structure.fields[1];
    $assert_eq(f->modifier_count, 1ul);
    $assert_eq((const char *)f->modifiers[0]->data, "sized_by_len");

    free(json);
    free_type(&v.type);
}

typedef struct SerialiseAsTest {
    $field(int, my_val, $serialise_as(MyVal));
} SerialiseAsTest;

static void test_serialise_as(void)
{
    SerialiseAsTest val = { .my_val = 42 };
    struct Value v = $valueof(&val);

    char *json = value_to_json_string(v);
    // Expect "MyVal": 42 instead of "my_val": 42
    const char *expected = "{\"MyVal\": 42}";

    if (strcmp(json, expected) != 0) {
        fprintf(stderr, "Mismatched JSON output in test_serialise_as.\nExpected: %s\nActual:   %s\n", expected, json);
        free(json);
        free_type(&v.type);
        exit(1);
    }

    free(json);
    free_type(&v.type);
}


typedef struct CachedNode {
    $field(int, val);
    $field(struct CachedNode *, next);
} CachedNode;

typedef struct CacheContainer {
    $field(struct CachedNode *, first);
} CacheContainer;

static void test_type_cache(void)
{
    // 1. Reflect on CachedNode first. This should populate the cache.
    // Explicitly using a struct with fields to ensure it's not opaque.
    // Note: 'next' is a pointer to CachedNode.
    struct Type t_node = {0};
    const char *enc_node = @encode(struct CachedNode);
    $assert_eq(parse_type(&t_node, &enc_node), 0);
    $assert_eq(t_node.type, TypeType_STRUCT);
    $assert_eq(t_node.structure.field_count, 2ul); // val, next

    // 2. Reflect on CacheContainer.
    // @encode(struct CacheContainer) -> "{CacheContainer=^{CachedNode}}".
    // If cache works, parsing '^{CachedNode}' -> Pointer -> Struct CachedNode (opaque in encoding).
    // lookup_cached_type should find CachedNode and fill it.
    struct Type t_cont = {0};
    const char *enc_cont = @encode(struct CacheContainer);

    $assert_eq(parse_type(&t_cont, &enc_cont), 0);
    $assert_eq(t_cont.type, TypeType_STRUCT);
    struct Field *f_first = &t_cont.structure.fields[0];
    $assert_eq(f_first->type.type, TypeType_POINTER);

    // Check if the pointee is resolved
    struct Type *pointee = f_first->type.pointer.type;
    $assert_eq(pointee->type, TypeType_STRUCT);
    // If not resolved, field_count would be 0
    $assert_eq(pointee->structure.field_count, 2ul);
    $assert_eq((const char*)pointee->structure.fields[0].name->data, "val");

    free_type(&t_node);
    free_type(&t_cont);
}

int main(void)
{
    void (*tests[])(void) = {
        test_parse_struct,
        test_get_value,
        test_type_size_array,
        test_parse_invalid,
        test_nested_struct_access,
        test_value_to_json_struct,
        test_value_to_json_nested_struct,
        test_value_to_json_array,
        test_value_to_json_pointer,
        test_value_to_json_cstring,
        test_pointer_type,
        test_const_qualifier,
        test_type_hash_equal,
        test_cast,
        test_renderdata,
        test_sized_ptr,
        test_sized_field,
        test_serialise_as,
        test_type_cache,
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        tests[i]();
        printf("Test %zu/%zu passed\n", i + 1, sizeof(tests) / sizeof(tests[0]));
    }

    printf("reflect tests passed\n");
    return 0;
}
