#pragma once

#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <iso646.h>
#include <stddef.h>

#define $_CONCAT(x, y) x##y
#define $concat(...) $_CONCAT(__VA_ARGS__)

#define $ARG_N(_1, _2, _3, _4, _5, _6, _7, _8, N, ...) N
#define $RSEQ_N() 8, 7, 6, 5, 4, 3, 2, 1, 0
#define $NARG_(...) $ARG_N(__VA_ARGS__)
#define $NARG(...) $NARG_(__VA_ARGS__, $RSEQ_N())

#define $_TAG_PASTE_1(name, line, d) $##name##$##line
#define $_TAG_PASTE_2(name, line, d, x) $##name##$##line##$##x
#define $_TAG_PASTE_3(name, line, d, x, y) $##name##$##line##$##x##$##y
#define $_TAG_PASTE_4(name, line, d, x, y, z) $##name##$##line##$##x##$##y##$##z
#define $_TAG_PASTE_5(name, line, d, x, y, z, w) $##name##$##line##$##x##$##y##$##z##$##w
#define $_TAG_PASTE_6(name, line, d, x, y, z, w, v) $##name##$##line##$##x##$##y##$##z##$##w##$##v
#define $_TAG_PASTE_7(name, line, d, x, y, z, w, v, u) $##name##$##line##$##x##$##y##$##z##$##w##$##v##$##u
#define $_TAG_PASTE_8(name, line, d, x, y, z, w, v, u, t) $##name##$##line##$##x##$##y##$##z##$##w##$##v##$##u##$##t
#define $_TAG_PASTE_9(name, line, d, x, y, z, w, v, u, t, s) $##name##$##line##$##x##$##y##$##z##$##w##$##v##$##u##$##t##$##s

#define $_TAG_PASTE_N(N, ...) $_CONCAT($_TAG_PASTE_, N)(__VA_ARGS__)
#define $_TAG_PASTE(name, line, ...) $_TAG_PASTE_N($NARG(dummy, ##__VA_ARGS__), name, line, dummy, ##__VA_ARGS__)

#define $sized_by(field) sized_by_##field
#define $serialise_as(name) serialise_as_##name

#define $field(ty, name, ...) \
    union { \
        typeof(ty) name; \
        struct $_TAG_PASTE(name, __LINE__, ##__VA_ARGS__) { } $##name##$; \
    }

#if defined(__clang__)
#   define $if_clang(...) __VA_ARGS__
#   define $if_gcc(...)
#   define $nullable _Nullable
#   define $nonnull _Nonnull
#   define nullptr ((void *$nullable)NULL)
#   define $nonnull_cast(...) ((typeof(typeof(*(__VA_ARGS__)) *$nonnull))__VA_ARGS__)
#elif defined(__GNUC__) and not defined(__clang__)
#   define $if_clang(...)
#   define $if_gcc(...) __VA_ARGS__
#   define nullptr NULL
#   define $nullable
#   define $nonnull
#   define $nonnull_cast(...) (__VA_ARGS__)
#endif

$if_clang (
    _Pragma("clang assume_nonnull begin")
)

struct Buffer {
    size_t size;
    uint8_t data[];
};


enum
$if_clang (
    [[clang::enum_extensibility(closed)]]
)
TypeEncoding $if_clang(: char) {
    TypeEncoding_SIGNED_CHAR = 'c', TypeEncoding_UNSIGNED_CHAR = 'C',
    TypeEncoding_SIGNED_SHORT = 's', TypeEncoding_UNSIGNED_SHORT = 'S',
    TypeEncoding_SIGNED_INT = 'i', TypeEncoding_UNSIGNED_INT = 'I',
    TypeEncoding_SIGNED_LONG = 'l', TypeEncoding_UNSIGNED_LONG = 'L',
    TypeEncoding_SIGNED_LONG_LONG = 'q', TypeEncoding_UNSIGNED_LONG_LONG = 'Q',
    TypeEncoding_FLOAT = 'f', TypeEncoding_DOUBLE = 'd', TypeEncoding_LONG_DOUBLE = 'D',
    TypeEncoding_CHAR_POINTER = '*', TypeEncoding_POINTER = '^',
    TypeEncoding_BOOL = 'B',
    TypeEncoding_VOID = 'v',
    TypeEncoding_STRUCT = '{',  //TypeEncoding_STRUCT_END = '}',
    TypeEncoding_ARRAY = '[',   //TypeEncoding_ARRAY_END = ']',
    TypeEncoding_UNION = '(',
};

enum
$if_clang (
    [[clang::enum_extensibility(closed)]]
)
TypeModifierEncodings $if_clang(: char) {
    TypeModifierEncoding_CONST = 'r',
};


struct Type {
    enum TypeType {
        TypeType_PRIMITIVE,
        TypeType_STRUCT,
        TypeType_ARRAY,
        TypeType_POINTER,
    } type;
    enum TypeModifiers {
        TypeModifiers_NONE = 0 << 0,
        TypeModifiers_CONST = 1 << 0,
    } modifiers;
    uint64_t hash;
    union {
        struct {
            char type;
        } primitive;

        struct {
            struct Buffer *name;

            size_t field_count;
            struct Field *$nullable fields;
        } structure;

        struct {
            size_t length;
            struct Type *type;
        } array;

        struct {
            struct Type *type;
        } pointer;
    };
};

struct Field {
    struct Type type;
    struct Buffer *name;
    struct Buffer *$nullable length_field_name;
    size_t modifier_count;
    struct Buffer *$nullable *$nullable modifiers;
    size_t offset;
};


static inline size_t primitive_type_size(enum TypeEncoding encoding)
{
    switch (encoding) {
        case TypeEncoding_SIGNED_CHAR:
        case TypeEncoding_UNSIGNED_CHAR:
            return sizeof(char);

        case TypeEncoding_SIGNED_SHORT:
        case TypeEncoding_UNSIGNED_SHORT:
            return sizeof(short);

        case TypeEncoding_SIGNED_INT:
        case TypeEncoding_UNSIGNED_INT:
            return sizeof(int);

        case TypeEncoding_SIGNED_LONG:
        case TypeEncoding_UNSIGNED_LONG:
            return sizeof(long);

        case TypeEncoding_SIGNED_LONG_LONG:
        case TypeEncoding_UNSIGNED_LONG_LONG:
            return sizeof(long long);

        case TypeEncoding_FLOAT:
            return sizeof(float);

        case TypeEncoding_DOUBLE:
            return sizeof(double);

        case TypeEncoding_LONG_DOUBLE:
            return sizeof(long double);

        case TypeEncoding_CHAR_POINTER:
            return sizeof(char *);

        case TypeEncoding_POINTER:
            return sizeof(void *);

        case TypeEncoding_BOOL:
            return sizeof(_Bool);

        case TypeEncoding_VOID:
            return 0;

        default:
            return -1;
    }

    return 0;
}


static inline size_t type_size(struct Type type)
{
    switch (type.type) {
        case TypeType_PRIMITIVE:
            return primitive_type_size(type.primitive.type);

        case TypeType_ARRAY:
            return type.array.length * type_size(*type.array.type);

        case TypeType_POINTER:
            return sizeof(void *);

        case TypeType_STRUCT: {
            size_t size = 0;
            for (size_t i = 0; i < type.structure.field_count; i++) {
                size += type_size(type.structure.fields[i].type);
            }
            return size;
        }
    }

    return 0;
}

int parse_type(struct Type *type, const char *$nonnull *$nonnull str);
void free_type(struct Type *type);


struct Value {
    struct Type type;
    void *data;
};
#define $valueof(...) ((struct Value) {\
    .type = ({\
        struct Type type = {0};\
        assert(parse_type(&type, &(const char *){ @encode(typeof(*(__VA_ARGS__))) }) == 0);\
        type;\
    }),\
    .data = (__VA_ARGS__)\
})

#define $reflect(...) ({\
    struct Type type = {0};\
    assert(parse_type(&type, &(const char *){ @encode(typeof(__VA_ARGS__)) }) == 0);\
    type;\
})

#define $access(T, val, field) ({\
    assert(val.type.type == TypeType_STRUCT);\
    struct Value $val = get_value(val, field);\
    assert($val.data != nullptr);\
    assert($reflect(T).hash == $val.type.hash);\
    *(T *)$val.data;\
})

#define $cast(val, T) ({\
    void *ptr; \
    if (val.type.hash != $reflect(T).hash) {\
        ptr = nullptr;\
    } else {\
        ptr = val.data;\
    }\
    (T *)ptr;\
})

struct Value get_value(struct Value value, const char *field);
int value_to_json(FILE *buf, struct Value val);
void print_type(FILE *to, struct Type type, int indent);
const char *type_name(struct Type type);
bool field_has_modifier(struct Field field, const char *modifier);

$if_clang (
    _Pragma("clang assume_nonnull end")
)
