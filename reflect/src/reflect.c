#include "reflect.h"
#include <assert.h>
#include <errno.h>
#include <iso646.h>
#include <limits.h>
#include <memory.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jsmn.h>

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define REFLECT_HAS_LSAN 1
#endif
#endif

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_LEAK__)
#define REFLECT_HAS_LSAN 1
#endif

#if defined(REFLECT_HAS_LSAN)
#include <sanitizer/lsan_interface.h>
#endif

$if_clang(
    _Pragma("clang assume_nonnull begin"))

static void lsan_ignore_allocation(void *ptr)
{
#if defined(REFLECT_HAS_LSAN)
    if (ptr != nullptr)
        __lsan_ignore_object(ptr);
#else
    (void)ptr;
#endif
}

static void *$nullable type_malloc(size_t size)
{
    void *ptr = malloc(size);
    lsan_ignore_allocation(ptr);
    return ptr;
}

static void *$nullable type_calloc(size_t count, size_t size)
{
    void *ptr = calloc(count, size);
    lsan_ignore_allocation(ptr);
    return ptr;
}

static void *$nullable type_realloc(void *$nullable ptr, size_t size)
{
    void *newptr = realloc(ptr, size);
    lsan_ignore_allocation(newptr);
    return newptr;
}

    static uint64_t hash_data(size_t size, uint8_t data[static size])
{
    uint64_t hash = 0;
    for (size_t i = 0; i < size; i++) {
        hash = data[i] + (hash << 6) + (hash << 16) - hash;
    }
    return hash;
}

static struct Buffer *$nullable buffer_create(size_t size)
{
    struct Buffer *buffer = type_calloc(1, sizeof(struct Buffer) + size);
    if (buffer == nullptr)
        return nullptr;

    buffer->size = size;
    return buffer;
}

static struct Buffer *$nullable buffer_append(struct Buffer *buffer, size_t size, uint8_t data[static size])
{
    void *newbuf = type_realloc(buffer, buffer->size + size);
    if (newbuf == nullptr)
        return nullptr;
    buffer = newbuf;
    memcpy(buffer->data + buffer->size, data, size);
    buffer->size += size;
    return buffer;
}

static _Atomic(struct {
    char *name;
    struct Type type;
} *) type_cache = nullptr;
static _Atomic(size_t) type_cache_count = 0;

static int clone_type(struct Type *dest, const struct Type *src)
{
    memset(dest, 0, sizeof(struct Type));
    dest->type = src->type;
    dest->modifiers = src->modifiers;
    dest->hash = src->hash;

    switch (src->type) {
    case TypeType_PRIMITIVE:
        dest->primitive = src->primitive;
        break;

    case TypeType_POINTER: {
        void *$nullable p = type_malloc(sizeof(struct Type));
        if (p == nullptr)
            return 1;
        dest->pointer.type = $nonnull_cast(p);
        if (clone_type(dest->pointer.type, src->pointer.type) != 0)
            return 1;
        break;
    }

    case TypeType_ARRAY: {
        dest->array.length = src->array.length;
        void *$nullable p = type_malloc(sizeof(struct Type));
        if (p == nullptr)
            return 1;
        dest->array.type = $nonnull_cast(p);
        if (clone_type(dest->array.type, src->array.type) != 0)
            return 1;
        break;
    }

    case TypeType_STRUCT: {
        void *p = buffer_create(src->structure.name->size + 1);
        if (p == nullptr)
            return 1;
        dest->structure.name = p;
        dest->structure.name->size = src->structure.name->size;
        memcpy(dest->structure.name->data, src->structure.name->data, src->structure.name->size);

        if (src->structure.field_count > 0) {
            dest->structure.field_count = src->structure.field_count;
            dest->structure.fields = type_calloc(src->structure.field_count, sizeof(struct Field));
            if (dest->structure.fields == nullptr)
                return 1;

            for (size_t i = 0; i < src->structure.field_count; i++) {
                struct Field *df = &dest->structure.fields[i];
                const struct Field *sf = &src->structure.fields[i];

                if (sf->name != nullptr) {
                    df->name = buffer_create(sf->name->size + 1);
                    if (df->name == nullptr)
                        return 1;
                    df->name->size = sf->name->size;
                    memcpy(df->name->data, sf->name->data, sf->name->size);
                }

                df->offset = sf->offset;

                if (sf->length_field_name) {
                    df->length_field_name = buffer_create(sf->length_field_name->size + 1);
                    if (df->length_field_name == nullptr)
                        return 1;
                    df->length_field_name->size = sf->length_field_name->size;
                    memcpy(df->length_field_name->data, sf->length_field_name->data, sf->length_field_name->size);
                }

                if (sf->modifier_count > 0) {
                    df->modifier_count = sf->modifier_count;
                    df->modifiers = type_calloc(sf->modifier_count, sizeof(struct Buffer *));
                    if (df->modifiers == nullptr)
                        return 1;
                    for (size_t m = 0; m < sf->modifier_count; m++) {
                        df->modifiers[m] = buffer_create(sf->modifiers[m]->size + 1);
                        if (df->modifiers[m] == nullptr)
                            return 1;
                        df->modifiers[m]->size = sf->modifiers[m]->size;
                        memcpy(df->modifiers[m]->data, sf->modifiers[m]->data, sf->modifiers[m]->size);
                    }
                }

                if (clone_type(&df->type, &sf->type) != 0)
                    return 1;
            }
        }
        break;
    }

    case TypeType_UNION: {
        void *p = buffer_create(src->union_.name->size + 1);
        if (p == nullptr)
            return 1;
        dest->union_.name = p;
        dest->union_.name->size = src->union_.name->size;
        memcpy(dest->union_.name->data, src->union_.name->data, src->union_.name->size);

        if (src->union_.field_count > 0) {
            dest->union_.field_count = src->union_.field_count;
            dest->union_.fields = type_calloc(src->union_.field_count, sizeof(struct Field));
            if (dest->union_.fields == nullptr)
                return 1;

            for (size_t i = 0; i < src->union_.field_count; i++) {
                struct Field *df = &dest->union_.fields[i];
                const struct Field *sf = &src->union_.fields[i];

                if (sf->name != nullptr) {
                    df->name = buffer_create(sf->name->size + 1);
                    if (df->name == nullptr)
                        return 1;
                    df->name->size = sf->name->size;
                    memcpy(df->name->data, sf->name->data, sf->name->size);
                }

                df->offset = sf->offset;

                if (sf->length_field_name) {
                    df->length_field_name = buffer_create(sf->length_field_name->size + 1);
                    if (df->length_field_name == nullptr)
                        return 1;
                    df->length_field_name->size = sf->length_field_name->size;
                    memcpy(df->length_field_name->data, sf->length_field_name->data,
                           sf->length_field_name->size);
                }

                if (sf->modifier_count > 0) {
                    df->modifier_count = sf->modifier_count;
                    df->modifiers = type_calloc(sf->modifier_count, sizeof(struct Buffer *));
                    if (df->modifiers == nullptr)
                        return 1;
                    for (size_t m = 0; m < sf->modifier_count; m++) {
                        df->modifiers[m] = buffer_create(sf->modifiers[m]->size + 1);
                        if (df->modifiers[m] == nullptr)
                            return 1;
                        df->modifiers[m]->size = sf->modifiers[m]->size;
                        memcpy(df->modifiers[m]->data, sf->modifiers[m]->data, sf->modifiers[m]->size);
                    }
                }

                if (clone_type(&df->type, &sf->type) != 0)
                    return 1;
            }
        }
        break;
    }
    }
    return 0;
}

static void cache_type_definition(struct Type *type)
{
    if ((type->type != TypeType_STRUCT and type->type != TypeType_UNION))
        return;
    if ((type->type == TypeType_STRUCT and type->structure.field_count == 0) or
        (type->type == TypeType_UNION and type->union_.field_count == 0))
        return;

    // Check if exists
    // The name in the buffer is not null terminated, but buffer_create adds + size?
    // Wait, Buffer struct: `uint8_t data[]`. memory.h says calloc(1, size + ...).
    // so data is raw bytes.
    // We need to match by string.

    struct Buffer *name_buf = type->type == TypeType_STRUCT ? type->structure.name : type->union_.name;
    char *name = malloc(name_buf->size + 1);
    if (not name)
        return;
    memcpy(name, name_buf->data, name_buf->size);
    name[name_buf->size] = 0;

    for (size_t i = 0; i < type_cache_count; i++) {
        if (strcmp(type_cache[i].name, name) == 0) {
            if (type_cache[i].type.type == type->type) {
                free(name);
                return; // Already cached
            }
        }
    }

    // Add to cache
    type_cache = realloc(type_cache, (type_cache_count + 1) * sizeof(*type_cache));
    if (not type_cache) {
        free(name);
        return;
    }

    type_cache[type_cache_count].name = name;
    if (clone_type(&type_cache[type_cache_count].type, type) != 0) {
        free(name);
        return;
    }

    type_cache_count++;
}

static bool lookup_cached_type(struct Type *type, const char *name_str, size_t name_len)
{
    char *name = malloc(name_len + 1);
    if (not name)
        return false;
    memcpy(name, name_str, name_len);
    name[name_len] = 0;

    for (size_t i = 0; i < type_cache_count; i++) {
        if (strcmp(type_cache[i].name, name) == 0) {
            if (type_cache[i].type.type != type->type) {
                free(name);
                return false;
            }
            // Found! Clone it
            struct Type *cached = &type_cache[i].type;
            uint32_t mods = type->modifiers;
            if (type->type == TypeType_STRUCT) {
                free(type->structure.name);
            } else if (type->type == TypeType_UNION) {
                free(type->union_.name);
            }

            if (clone_type(type, cached) != 0) {
                free(name);
                return false;
            }

            type->modifiers |= mods;

            free(name);
            return true;
        }
    }
    free(name);
    return false;
}

static uint64_t hash_type(struct Type type)
{
    uint64_t hash = 0;
    switch (type.type) {
    case TypeType_PRIMITIVE:
        hash = type.primitive.type;
        break;

    case TypeType_STRUCT:
        hash = hash_data(type.structure.name->size, type.structure.name->data);
        for (size_t i = 0; i < type.structure.field_count; i++) {
            hash += hash_type(type.structure.fields[i].type);
        }
        break;

    case TypeType_UNION:
        hash = hash_data(type.union_.name->size, type.union_.name->data);
        for (size_t i = 0; i < type.union_.field_count; i++) {
            hash += hash_type(type.union_.fields[i].type);
        }
        hash += TypeEncoding_UNION;
        break;

    case TypeType_ARRAY:
        hash = type.array.length * hash_type(*type.array.type);
        break;

    case TypeType_POINTER:
        hash = hash_type(*type.pointer.type);
        hash *= 31;
        hash += TypeEncoding_POINTER;
        break;
    }

    hash += type.modifiers;

    return hash;
}

static int parse_field_tag(const char **str, struct Field *field)
{
    if (**str != '{')
        return 2;
    (*str)++;

    if (**str != '$')
        return 2;
    (*str)++;

    char *endptr = strchr(*str, '$');
    if (endptr == nullptr)
        return 1;

    struct Buffer *$nullable field_name = buffer_create(endptr - *str + 1);
    if (field_name == nullptr)
        return 1;
    memcpy(field_name->data, *str, endptr - *str);
    field_name->size = endptr - *str;
    *str = endptr + 1;

    // next is the line number, which we don't care about (used to make a unique tag)
    strtol(*str, &endptr, 10);
    *str = endptr;

    // Parse modifiers (if any after line number)
    field->modifier_count = 0;
    field->modifiers = nullptr;
    field->length_field_name = nullptr;

    while (**str == '$') {
        (*str)++;
        char *sep = strpbrk(*str, "$}=");
        if (sep == nullptr) {
            break;
        }

        size_t mod_len = sep - *str;
        struct Buffer *mod = buffer_create(mod_len + 1);
        if (mod == nullptr)
            return 1;
        memcpy(mod->data, *str, mod_len);
        mod->size = mod_len;

        // Check if this is a sized_by modifier
        if (strncmp(*str, "sized_by_", 9) == 0) {
            size_t len_offset = 9;
            size_t len_name_len = sep - (*str + len_offset);
            struct Buffer *$nullable len_name = buffer_create(len_name_len + 1);
            if (len_name == nullptr)
                return 1;
            memcpy(len_name->data, *str + len_offset, len_name_len);
            len_name->size = len_name_len;
            field->length_field_name = $nonnull_cast(len_name);
        }

        // Add to modifiers array regardless
        field->modifier_count++;
        void *$nullable newmods =
            type_realloc(field->modifiers, field->modifier_count * sizeof(struct Buffer *));
        if (newmods == nullptr)
            return 1;
        field->modifiers = $nonnull_cast(newmods);
        field->modifiers[field->modifier_count - 1] = mod;

        *str = sep;
    }

    // Skip `=` and a single closing `}` for this tag.
    if (**str == '=')
        (*str)++;
    if (**str == '}')
        (*str)++;

    field->name = $nonnull_cast(field_name);
    return 0;
}

// {Point=(?={$x$=}i)(?={$y$=}i)}
int parse_type(struct Type *type, const char **str)
{
    while (**str == TypeModifierEncoding_CONST) { // 'r'
        type->modifiers |= TypeModifiers_CONST;
        (*str)++;
    }

    char encoding = **str;
    switch (encoding) {
    // already handled, we have to do it before because GCC and Clang differ with const placement
    case TypeModifierEncoding_CONST:
        break;

    case TypeEncoding_POINTER: {
        type->type = TypeType_POINTER;
        (*str)++;

        // GCC encodes `const int *` as `^ri` (Pointer to Const Int).
        // Clang encodes `const int *` as `r^i` (Const Pointer to Int).
        // Yes, this is incredibly stupid.

        void *$nullable p = type_malloc(sizeof(struct Type));
        if (p == nullptr)
            return 1;
        type->pointer.type = $nonnull_cast(p);
        if (parse_type(type->pointer.type, str) != 0)
            return 1;
        break;
    }
    case TypeEncoding_SIGNED_CHAR:
    case TypeEncoding_UNSIGNED_CHAR:
    case TypeEncoding_SIGNED_SHORT:
    case TypeEncoding_UNSIGNED_SHORT:
    case TypeEncoding_SIGNED_INT:
    case TypeEncoding_UNSIGNED_INT:
    case TypeEncoding_SIGNED_LONG:
    case TypeEncoding_UNSIGNED_LONG:
    case TypeEncoding_SIGNED_LONG_LONG:
    case TypeEncoding_UNSIGNED_LONG_LONG:
    case TypeEncoding_FLOAT:
    case TypeEncoding_DOUBLE:
    case TypeEncoding_LONG_DOUBLE:
    case TypeEncoding_CHAR_POINTER:
    case TypeEncoding_VOID:
    case TypeEncoding_BOOL: {
        type->type = TypeType_PRIMITIVE;
        type->primitive.type = encoding;
        (*str)++;
        break;
    }

    case TypeEncoding_STRUCT: {
        type->type = TypeType_STRUCT;
        (*str)++;

        char *$nullable endptr = strpbrk(*str, "=}");
        if (endptr == nullptr) {
            return 1;
        }
        if (*endptr == '}') {
            // likley there is just a name here
            struct Buffer *$nullable buf = buffer_create(endptr - *str + 1);
            if (buf == nullptr)
                return 1;
            memcpy(buf->data, *str, endptr - *str);
            buf->size = endptr - *str;

            type->structure.name = $nonnull_cast(buf);
            type->structure.field_count = 0;
            type->structure.fields = nullptr;

            // Try to resolve from cache
            if (lookup_cached_type(type, (char *)buf->data, buf->size)) {
                *str = $nonnull_cast(endptr) + 1;
                break;
            }

            *str = $nonnull_cast(endptr) + 1;
            break;
        }
        struct Buffer *$nullable buf = buffer_create(endptr - *str + 1);
        if (buf == nullptr)
            return 1;
        memcpy(buf->data, *str, endptr - *str);
        buf->size = endptr - *str;

        type->structure.name = $nonnull_cast(buf);
        *str = $nonnull_cast(endptr) + 1;

        // (?=i{$x$6=})(?=f{$y$7=})

        type->structure.field_count = 0;
        size_t offset = 0;
        while (**str != '}') {
            type->structure.field_count++;
            void *$nullable newfields =
                type_realloc(type->structure.fields, type->structure.field_count * sizeof(struct Field));
            if (newfields == nullptr)
                return 1;
            type->structure.fields = $nonnull_cast(newfields);

            struct Field *field = &type->structure.fields[type->structure.field_count - 1];
            *field = (struct Field){0};

            bool tagged_wrapper = false;
            if (**str == '(' and(*str)[1] == '?' and(*str)[2] == '=') { // legacy struct field decl
                tagged_wrapper = true;
                *str = *str + 3;
            }

            const char *prev = *str;
            if (parse_type(&field->type, str) != 0)
                return 1;
            if (*str == prev)
                return 2;

            field->name = nullptr;
            field->modifier_count = 0;
            field->modifiers = nullptr;
            field->length_field_name = nullptr;

            if (**str == '{' and (*str)[1] == '$') {
                if (parse_field_tag(str, field) != 0)
                    return 1;
            }

            size_t align = type_alignment(field->type);
            offset = align_up_size(offset, align);
            field->offset = offset;
            offset += type_size(field->type);

            if (tagged_wrapper) {
                if (**str != ')') {
                    return 2;
                }
                (*str)++;
            }
        }
        (*str)++;

        cache_type_definition(type);
        break;
    }

    case TypeEncoding_UNION: {
        type->type = TypeType_UNION;
        (*str)++;

        char *$nullable endptr = strpbrk(*str, "=)");
        if (endptr == nullptr) {
            return 1;
        }
        if (*endptr == ')') {
            struct Buffer *$nullable buf = buffer_create(endptr - *str + 1);
            if (buf == nullptr)
                return 1;
            memcpy(buf->data, *str, endptr - *str);
            buf->size = endptr - *str;

            type->union_.name = $nonnull_cast(buf);
            type->union_.field_count = 0;
            type->union_.fields = nullptr;

            if (lookup_cached_type(type, (char *)buf->data, buf->size)) {
                *str = $nonnull_cast(endptr) + 1;
                break;
            }

            *str = $nonnull_cast(endptr) + 1;
            break;
        }

        struct Buffer *$nullable buf = buffer_create(endptr - *str + 1);
        if (buf == nullptr)
            return 1;
        memcpy(buf->data, *str, endptr - *str);
        buf->size = endptr - *str;

        type->union_.name = $nonnull_cast(buf);
        *str = $nonnull_cast(endptr) + 1;

        type->union_.field_count = 0;
        while (**str != ')') {
            type->union_.field_count++;
            void *$nullable newfields =
                type_realloc(type->union_.fields, type->union_.field_count * sizeof(struct Field));
            if (newfields == nullptr)
                return 1;
            type->union_.fields = $nonnull_cast(newfields);

            struct Field *field = &type->union_.fields[type->union_.field_count - 1];
            *field = (struct Field){0};

            bool tagged_wrapper = false;
            if (**str == '(' and(*str)[1] == '?' and(*str)[2] == '=') {
                tagged_wrapper = true;
                *str = *str + 3;
            }

            const char *prev = *str;
            if (parse_type(&field->type, str) != 0)
                return 1;
            if (*str == prev)
                return 2;

            field->name = nullptr;
            field->modifier_count = 0;
            field->modifiers = nullptr;
            field->length_field_name = nullptr;

            if (**str == '{' and (*str)[1] == '$') {
                if (parse_field_tag(str, field) != 0)
                    return 1;
            }

            field->offset = 0;

            if (tagged_wrapper) {
                if (**str != ')') {
                    return 2;
                }
                (*str)++;
            }
        }
        (*str)++;

        cache_type_definition(type);
        break;
    }

    case TypeEncoding_ARRAY: {
        type->type = TypeType_ARRAY;
        (*str)++;
        char *endptr;
        size_t len = strtol(*str, &endptr, 10);
        if (endptr == *str)
            return 1;
        type->array.length = len;
        *str = endptr;

        void *$nullable p = type_malloc(sizeof(struct Type));
        if (p == nullptr)
            return 1;
        type->array.type = $nonnull_cast(p);
        if (parse_type(type->array.type, str) != 0)
            return 1;

        // skip the `]`
        if (**str != ']') {
            return 2;
        }
        (*str)++;

        break;
    }
    }

    type->hash = hash_type(*type);

    return 0;
}

struct Value get_value(struct Value value, const char *field)
{
    assert(value.type.type == TypeType_STRUCT or value.type.type == TypeType_UNION);

    size_t field_count = value.type.type == TypeType_STRUCT ? value.type.structure.field_count
                                                            : value.type.union_.field_count;
    struct Field *fields = value.type.type == TypeType_STRUCT ? value.type.structure.fields
                                                              : value.type.union_.fields;

    for (size_t i = 0; i < field_count; i++) {
        if (fields[i].name == nullptr) {
            continue;
        }
        if (strcmp((const char *)fields[i].name->data, field) == 0) {
            return (struct Value){
                .type = fields[i].type,
                .data = value.data + fields[i].offset};
        }
    }

    return (struct Value){0};
}

static const char *$nullable json_field_tagged_by_name(const struct Field *field);
static int json_select_union_field(const struct Type *union_type,
                                   struct Value tag_val,
                                   struct Field **out_field);

int value_to_json(FILE *buf, struct Value val)
{
    // fprintf(buf, "{");
    {
        switch (val.type.type) {
        case TypeType_PRIMITIVE: {
            switch (val.type.primitive.type) {
            case TypeEncoding_SIGNED_CHAR:
            case TypeEncoding_UNSIGNED_CHAR:
                fprintf(buf, "%d", (int)*(char *)val.data);
                break;

            case TypeEncoding_SIGNED_SHORT:
            case TypeEncoding_UNSIGNED_SHORT:
                fprintf(buf, "%d", (int)*(short *)val.data);
                break;

            case TypeEncoding_SIGNED_INT:
            case TypeEncoding_UNSIGNED_INT:
                fprintf(buf, "%d", *(int *)val.data);
                break;

            case TypeEncoding_SIGNED_LONG:
            case TypeEncoding_UNSIGNED_LONG:
                fprintf(buf, "%ld", *(long *)val.data);
                break;

            case TypeEncoding_SIGNED_LONG_LONG:
            case TypeEncoding_UNSIGNED_LONG_LONG:
                fprintf(buf, "%lld", *(long long *)val.data);
                break;

            case TypeEncoding_FLOAT:
                fprintf(buf, "%f", *(float *)val.data);
                break;

            case TypeEncoding_DOUBLE:
                fprintf(buf, "%f", *(double *)val.data);
                break;

            case TypeEncoding_LONG_DOUBLE:
                fprintf(buf, "%Lf", *(long double *)val.data);
                break;

            case TypeEncoding_CHAR_POINTER:
                fprintf(buf, "\"%s\"", *(char **)val.data);
                break;

            case TypeEncoding_POINTER:
                fprintf(buf, "%p", *(void **)val.data);
                break;

            case TypeEncoding_BOOL:
                fprintf(buf, "%s", *(bool *)val.data ? "true" : "false");
                break;
            }
            break;
        }

        case TypeType_STRUCT: {
            fprintf(buf, "{");
            bool first = true;
            for (size_t i = 0; i < val.type.structure.field_count; i++) {
                struct Field *field = &val.type.structure.fields[i];
                if (field_has_modifier(*field, "no_serialise")) {
                    continue;
                }
                if (field->name == nullptr) {
                    continue;
                }
                if (not first) {
                    fprintf(buf, ", ");
                }
                first = false;
                const char *json_key = (const char *)field->name->data;
                for (size_t k = 0; k < field->modifier_count; k++) {
                    const char *mod = (const char *)field->modifiers[k]->data;
                    if (strncmp(mod, "serialise_as_", 13) == 0) {
                        json_key = mod + 13;
                        break;
                    }
                }

                fprintf(buf, "\"%s\": ", json_key);

                bool handled = false;

                const char *target_length_field = nullptr;
                if (field->length_field_name != nullptr) {
                    target_length_field = (const char *)field->length_field_name->data;
                } else {
                    for (size_t k = 0; k < field->modifier_count; k++) {
                        const char *mod = (const char *)field->modifiers[k]->data;
                        if (strncmp(mod, "sized_by_", 9) == 0) {
                            target_length_field = mod + 9;
                            break;
                        }
                        if (strcmp(mod, "sizedby") == 0) {
                            if (k + 1 < field->modifier_count) {
                                target_length_field = (const char *)field->modifiers[k + 1]->data;
                            }
                            break;
                        }
                    }
                }

                if (target_length_field != nullptr) {
                    struct Value len_val = get_value(val, target_length_field);
                    if (len_val.data != nullptr and len_val.type.type == TypeType_PRIMITIVE) {
                        size_t len = 0;
                        bool ok = true;
                        switch (len_val.type.primitive.type) {
                        case TypeEncoding_SIGNED_CHAR:
                            len = *(signed char *)len_val.data;
                            break;
                        case TypeEncoding_UNSIGNED_CHAR:
                            len = *(unsigned char *)len_val.data;
                            break;
                        case TypeEncoding_SIGNED_SHORT:
                            len = *(short *)len_val.data;
                            break;
                        case TypeEncoding_UNSIGNED_SHORT:
                            len = *(unsigned short *)len_val.data;
                            break;
                        case TypeEncoding_SIGNED_INT:
                            len = *(int *)len_val.data;
                            break;
                        case TypeEncoding_UNSIGNED_INT:
                            len = *(unsigned int *)len_val.data;
                            break;
                        case TypeEncoding_SIGNED_LONG:
                            len = *(long *)len_val.data;
                            break;
                        case TypeEncoding_UNSIGNED_LONG:
                            len = *(unsigned long *)len_val.data;
                            break;
                        case TypeEncoding_SIGNED_LONG_LONG:
                            len = *(long long *)len_val.data;
                            break;
                        case TypeEncoding_UNSIGNED_LONG_LONG:
                            len = *(unsigned long long *)len_val.data;
                            break;
                        default:
                            ok = false;
                            break;
                        }

                        if (ok) {
                            if (field->type.type == TypeType_PRIMITIVE and field->type.primitive.type == TypeEncoding_CHAR_POINTER) {
                                char *ptr = *(char **)(val.data + field->offset);
                                if (ptr == nullptr) {
                                    fprintf(buf, "null");
                                } else {
                                    fprintf(buf, "\"");
                                    for (size_t k = 0; k < len; k++) {
                                        unsigned char c = ptr[k];
                                        if (c == '"' or c == '\\')
                                            fprintf(buf, "\\%c", c);
                                        else if (c == '\n')
                                            fprintf(buf, "\\n");
                                        else if (c == '\r')
                                            fprintf(buf, "\\r");
                                        else if (c == '\t')
                                            fprintf(buf, "\\t");
                                        else if (c < 32 or c > 126)
                                            fprintf(buf, "\\u%04x", c);
                                        else
                                            fputc(c, buf);
                                    }
                                    fprintf(buf, "\"");
                                }
                                handled = true;
                            } else if (field->type.type == TypeType_POINTER) {
                                void *ptr = *(void **)(val.data + field->offset);
                                if (ptr == nullptr) {
                                    fprintf(buf, "null");
                                } else {
                                    fprintf(buf, "[");
                                    struct Type elem_type = *field->type.pointer.type;
                                    size_t elem_size = type_size(elem_type);
                                    for (size_t k = 0; k < len; k++) {
                                        if (value_to_json(buf, (struct Value){
                                                                   .type = elem_type,
                                                                   .data = (char *)ptr + (k * elem_size)}) != 0)
                                            return 1;
                                        if (k != len - 1)
                                            fprintf(buf, ", ");
                                    }
                                    fprintf(buf, "]");
                                }
                                handled = true;
                            }
                        }
                    }
                }

                if (not handled and field->type.type == TypeType_UNION) {
                    const char *tag_name = json_field_tagged_by_name(field);
                    if (tag_name != nullptr) {
                        struct Value tag_val = get_value(val, tag_name);
                        if (tag_val.data == nullptr)
                            return 1;
                        struct Field *active = nullptr;
                        if (json_select_union_field(&field->type, tag_val, &active) != 0)
                            return 1;
                        if (value_to_json(buf, (struct Value){
                                                   .type = active->type,
                                                   .data = val.data + field->offset + active->offset}) != 0)
                            return 1;
                        handled = true;
                    }
                }

                if (not handled) {
                    if (value_to_json(buf, (struct Value){
                                               .type = field->type,
                                               .data = val.data + field->offset}) != 0)
                        return 1;
                }
            }
            fprintf(buf, "}");
            break;
        }

        case TypeType_ARRAY: {
            fprintf(buf, "[");
            for (size_t i = 0; i < val.type.array.length; i++) {
                if (value_to_json(buf, (struct Value){
                                           .type = *val.type.array.type,
                                           .data = val.data + (i * type_size(*val.type.array.type))}) != 0)
                    return 1;
                if (i != val.type.array.length - 1)
                    fprintf(buf, ", ");
            }
            fprintf(buf, "]");
            break;
        }

        case TypeType_POINTER: {
            if (*(void **)val.data == nullptr) {
                fprintf(buf, "null");
            } else {
                if (value_to_json(buf, (struct Value){
                                           .type = *val.type.pointer.type,
                                           .data = *(void **)val.data}) != 0)
                    return 1;
            }
            break;
        }

        case TypeType_UNION:
            return 1;
        }
    }
    // fprintf(buf, "}");

    return 0;
}

void print_type(FILE *to, struct Type type, int indent)
{
    size_t tysiz = type_size(type);
    if (type.modifiers & TypeModifiers_CONST) {
        fprintf(to, "const ");
    }
    switch (type.type) {
    case TypeType_PRIMITIVE:
        fprintf(to, "%c\n", type.primitive.type);
        break;

    case TypeType_STRUCT:
        fprintf(to, "%s (size: %zu) ", type.structure.name->data, tysiz);
        if (type.structure.field_count > 0) {
            fprintf(to, " {\n");
            for (size_t i = 0; i < type.structure.field_count; i++) {
                struct Field *field = &type.structure.fields[i];
                const char *field_name = field->name != nullptr
                                             ? (const char *)field->name->data
                                             : "<unnamed>";
                fprintf(to, "%*s(+%zu) %s: ", indent + 4, "", field->offset, field_name);
                print_type(to, field->type, indent + 4);
            }
            fprintf(to, "%*s}", indent, "");
        }
        fprintf(to, "\n");
        break;

    case TypeType_UNION:
        fprintf(to, "union %s (size: %zu) ", type.union_.name->data, tysiz);
        if (type.union_.field_count > 0) {
            fprintf(to, " {\n");
            for (size_t i = 0; i < type.union_.field_count; i++) {
                struct Field *field = &type.union_.fields[i];
                const char *field_name = field->name != nullptr
                                             ? (const char *)field->name->data
                                             : "<unnamed>";
                fprintf(to, "%*s(+%zu) %s: ", indent + 4, "", field->offset, field_name);
                print_type(to, field->type, indent + 4);
            }
            fprintf(to, "%*s}", indent, "");
        }
        fprintf(to, "\n");
        break;

    case TypeType_ARRAY:
        fprintf(to, "[%zu]", type.array.length);
        print_type(to, *type.array.type, indent);
        break;

    case TypeType_POINTER:
        fprintf(to, "*");
        print_type(to, *type.pointer.type, indent + 4);
        break;
    }
}

const char *type_name(struct Type type)
{
    switch (type.type) {
    case TypeType_PRIMITIVE: {
        switch (type.primitive.type) {
        case TypeEncoding_SIGNED_CHAR:
            return "signed char";

        case TypeEncoding_UNSIGNED_CHAR:
            return "unsigned char";

        case TypeEncoding_SIGNED_SHORT:
            return "signed short";

        case TypeEncoding_UNSIGNED_SHORT:
            return "unsigned short";

        case TypeEncoding_SIGNED_INT:
            return "signed int";

        case TypeEncoding_UNSIGNED_INT:
            return "unsigned int";

        case TypeEncoding_SIGNED_LONG:
            return "signed long";

        case TypeEncoding_UNSIGNED_LONG:
            return "unsigned long";

        case TypeEncoding_SIGNED_LONG_LONG:
            return "signed long long";

        case TypeEncoding_UNSIGNED_LONG_LONG:
            return "unsigned long long";

        case TypeEncoding_FLOAT:
            return "float";

        case TypeEncoding_DOUBLE:
            return "double";

        case TypeEncoding_LONG_DOUBLE:
            return "long double";

        case TypeEncoding_CHAR_POINTER:
            return "char *";

        case TypeEncoding_VOID:
            return "void";

        case TypeEncoding_BOOL:
            return "_Bool";

        default:
            return "unknown";
        }
        break;
    }

    case TypeType_STRUCT:
        return (const char *)type.structure.name->data;

    case TypeType_UNION:
        return (const char *)type.union_.name->data;

    case TypeType_ARRAY:
        return "array";

    case TypeType_POINTER:
        return "pointer";

    default:
        return "unknown";
    }
}

bool field_has_modifier(struct Field field, const char *modifier)
{
    for (size_t i = 0; i < field.modifier_count; i++) {
        if (strcmp((const char *)field.modifiers[i]->data, modifier) == 0) {
            return true;
        }
    }
    return false;
}

static size_t json_token_len(const jsmntok_t *tok)
{
    return (size_t)(tok->end - tok->start);
}

static bool json_token_eq(const char *json, const jsmntok_t *tok, const char *str)
{
    size_t len = json_token_len(tok);
    return strlen(str) == len and strncmp(json + tok->start, str, len) == 0;
}

static char *$nullable json_token_to_cstr(const char *json, const jsmntok_t *tok)
{
    size_t len = json_token_len(tok);
    char *buf = malloc(len + 1);
    if (buf == nullptr)
        return nullptr;
    memcpy(buf, json + tok->start, len);
    buf[len] = '\0';
    return buf;
}

static int json_parse_signed_token(const char *json, const jsmntok_t *tok, long long *out)
{
    if (tok->type != JSMN_PRIMITIVE)
        return 1;
    if (json_token_eq(json, tok, "true") or json_token_eq(json, tok, "false") or json_token_eq(json, tok, "null"))
        return 1;
    char *buf = json_token_to_cstr(json, tok);
    if (buf == nullptr)
        return 1;
    errno = 0;
    char *endptr = nullptr;
    long long val = strtoll(buf, &endptr, 10);
    if (errno == ERANGE or endptr == buf or *endptr != '\0') {
        free(buf);
        return 1;
    }
    *out = val;
    free(buf);
    return 0;
}

static int json_parse_unsigned_token(const char *json, const jsmntok_t *tok, unsigned long long *out)
{
    if (tok->type != JSMN_PRIMITIVE)
        return 1;
    if (json_token_eq(json, tok, "true") or json_token_eq(json, tok, "false") or json_token_eq(json, tok, "null"))
        return 1;
    if (json[tok->start] == '-')
        return 1;
    char *buf = json_token_to_cstr(json, tok);
    if (buf == nullptr)
        return 1;
    errno = 0;
    char *endptr = nullptr;
    unsigned long long val = strtoull(buf, &endptr, 10);
    if (errno == ERANGE or endptr == buf or *endptr != '\0') {
        free(buf);
        return 1;
    }
    *out = val;
    free(buf);
    return 0;
}

static int json_parse_float_token(const char *json, const jsmntok_t *tok, long double *out)
{
    if (tok->type != JSMN_PRIMITIVE)
        return 1;
    if (json_token_eq(json, tok, "true") or json_token_eq(json, tok, "false") or json_token_eq(json, tok, "null"))
        return 1;
    char *buf = json_token_to_cstr(json, tok);
    if (buf == nullptr)
        return 1;
    errno = 0;
    char *endptr = nullptr;
    long double val = strtold(buf, &endptr);
    if (errno == ERANGE or endptr == buf or *endptr != '\0') {
        free(buf);
        return 1;
    }
    *out = val;
    free(buf);
    return 0;
}

static int json_parse_bool_token(const char *json, const jsmntok_t *tok, bool *out)
{
    if (tok->type != JSMN_PRIMITIVE)
        return 1;
    if (json_token_eq(json, tok, "true")) {
        *out = true;
        return 0;
    }
    if (json_token_eq(json, tok, "false")) {
        *out = false;
        return 0;
    }
    if (json_token_eq(json, tok, "0")) {
        *out = false;
        return 0;
    }
    if (json_token_eq(json, tok, "1")) {
        *out = true;
        return 0;
    }
    return 1;
}

static int json_hex_value(char c)
{
    if (c >= '0' and c <= '9')
        return c - '0';
    if (c >= 'a' and c <= 'f')
        return 10 + (c - 'a');
    if (c >= 'A' and c <= 'F')
        return 10 + (c - 'A');
    return -1;
}

static int json_utf8_encode(uint32_t cp, char out[static 4], size_t *written)
{
    if (cp <= 0x7F) {
        out[0] = (char)cp;
        *written = 1;
        return 0;
    }
    if (cp <= 0x7FF) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        *written = 2;
        return 0;
    }
    if (cp <= 0xFFFF) {
        if (cp >= 0xD800 and cp <= 0xDFFF)
            return 1;
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        *written = 3;
        return 0;
    }
    if (cp <= 0x10FFFF) {
        out[0] = (char)(0xF0 | (cp >> 18));
        out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[3] = (char)(0x80 | (cp & 0x3F));
        *written = 4;
        return 0;
    }
    return 1;
}

static int json_decode_string(const char *json, const jsmntok_t *tok, char **out, size_t *$nullable out_len)
{
    if (tok->type != JSMN_STRING)
        return 1;
    size_t in_len = json_token_len(tok);
    const char *src = json + tok->start;
    char *dst = malloc(in_len + 1);
    if (dst == nullptr)
        return 1;

    size_t di = 0;
    for (size_t si = 0; si < in_len; si++) {
        char c = src[si];
        if (c != '\\') {
            dst[di++] = c;
            continue;
        }
        if (si + 1 >= in_len) {
            free(dst);
            return 1;
        }
        char esc = src[++si];
        switch (esc) {
        case '"':
        case '\\':
        case '/':
            dst[di++] = esc;
            break;
        case 'b':
            dst[di++] = '\b';
            break;
        case 'f':
            dst[di++] = '\f';
            break;
        case 'n':
            dst[di++] = '\n';
            break;
        case 'r':
            dst[di++] = '\r';
            break;
        case 't':
            dst[di++] = '\t';
            break;
        case 'u': {
            if (si + 4 >= in_len) {
                free(dst);
                return 1;
            }
            int h1 = json_hex_value(src[si + 1]);
            int h2 = json_hex_value(src[si + 2]);
            int h3 = json_hex_value(src[si + 3]);
            int h4 = json_hex_value(src[si + 4]);
            if (h1 < 0 or h2 < 0 or h3 < 0 or h4 < 0) {
                free(dst);
                return 1;
            }
            uint32_t cp = (uint32_t)((h1 << 12) | (h2 << 8) | (h3 << 4) | h4);
            si += 4;
            char utf8[4];
            size_t written = 0;
            if (json_utf8_encode(cp, utf8, &written) != 0) {
                free(dst);
                return 1;
            }
            for (size_t k = 0; k < written; k++) {
                dst[di++] = utf8[k];
            }
            break;
        }
        default:
            free(dst);
            return 1;
        }
    }
    dst[di] = '\0';
    *out = dst;
    if (out_len != nullptr)
        *out_len = di;
    return 0;
}

static int json_skip_token(const jsmntok_t *tokens, int index, int token_count)
{
    if (index < 0 or index >= token_count)
        return -1;
    jsmntok_t tok = tokens[index];
    switch (tok.type) {
    case JSMN_PRIMITIVE:
    case JSMN_STRING:
        return index + 1;
    case JSMN_ARRAY: {
        int i = index + 1;
        for (int n = 0; n < tok.size; n++) {
            i = json_skip_token(tokens, i, token_count);
            if (i < 0)
                return -1;
        }
        return i;
    }
    case JSMN_OBJECT: {
        int i = index + 1;
        for (int n = 0; n < tok.size; n++) {
            i = json_skip_token(tokens, i, token_count);
            if (i < 0)
                return -1;
            i = json_skip_token(tokens, i, token_count);
            if (i < 0)
                return -1;
        }
        return i;
    }
    default:
        return -1;
    }
}

static const char *$nullable json_field_json_key(const struct Field *field)
{
    if (field->name == nullptr)
        return nullptr;
    const char *json_key = (const char *)field->name->data;
    for (size_t k = 0; k < field->modifier_count; k++) {
        const char *mod = (const char *)field->modifiers[k]->data;
        if (strncmp(mod, "serialise_as_", 13) == 0) {
            json_key = mod + 13;
            break;
        }
    }
    return json_key;
}

static const char *$nullable json_field_length_name(const struct Field *field)
{
    if (field->length_field_name != nullptr)
        return (const char *)field->length_field_name->data;
    for (size_t k = 0; k < field->modifier_count; k++) {
        const char *mod = (const char *)field->modifiers[k]->data;
        if (strncmp(mod, "sized_by_", 9) == 0) {
            return mod + 9;
        }
        if (strcmp(mod, "sizedby") == 0) {
            if (k + 1 < field->modifier_count) {
                return (const char *)field->modifiers[k + 1]->data;
            }
        }
    }
    return nullptr;
}

static const char *$nullable json_field_tagged_by_name(const struct Field *field)
{
    for (size_t k = 0; k < field->modifier_count; k++) {
        const char *mod = (const char *)field->modifiers[k]->data;
        if (strncmp(mod, "tagged_by_", 10) == 0) {
            return mod + 10;
        }
    }
    return nullptr;
}

static const char *$nullable json_union_field_tag_value(const struct Field *field)
{
    for (size_t k = 0; k < field->modifier_count; k++) {
        const char *mod = (const char *)field->modifiers[k]->data;
        if (strncmp(mod, "tag_value_", 10) == 0) {
            return mod + 10;
        }
    }
    return nullptr;
}

static bool json_primitive_is_unsigned(enum TypeEncoding encoding)
{
    switch (encoding) {
    case TypeEncoding_UNSIGNED_CHAR:
    case TypeEncoding_UNSIGNED_SHORT:
    case TypeEncoding_UNSIGNED_INT:
    case TypeEncoding_UNSIGNED_LONG:
    case TypeEncoding_UNSIGNED_LONG_LONG:
        return true;
    default:
        return false;
    }
}

static int json_read_signed_value(struct Value val, long long *out)
{
    if (val.type.type != TypeType_PRIMITIVE or val.data == nullptr)
        return 1;
    switch (val.type.primitive.type) {
    case TypeEncoding_SIGNED_CHAR:
        *out = *(signed char *)val.data;
        return 0;
    case TypeEncoding_SIGNED_SHORT:
        *out = *(short *)val.data;
        return 0;
    case TypeEncoding_SIGNED_INT:
        *out = *(int *)val.data;
        return 0;
    case TypeEncoding_SIGNED_LONG:
        *out = *(long *)val.data;
        return 0;
    case TypeEncoding_SIGNED_LONG_LONG:
        *out = *(long long *)val.data;
        return 0;
    case TypeEncoding_BOOL:
        *out = *(bool *)val.data ? 1 : 0;
        return 0;
    default:
        return 1;
    }
}

static int json_read_unsigned_value(struct Value val, unsigned long long *out)
{
    if (val.type.type != TypeType_PRIMITIVE or val.data == nullptr)
        return 1;
    switch (val.type.primitive.type) {
    case TypeEncoding_UNSIGNED_CHAR:
        *out = *(unsigned char *)val.data;
        return 0;
    case TypeEncoding_UNSIGNED_SHORT:
        *out = *(unsigned short *)val.data;
        return 0;
    case TypeEncoding_UNSIGNED_INT:
        *out = *(unsigned int *)val.data;
        return 0;
    case TypeEncoding_UNSIGNED_LONG:
        *out = *(unsigned long *)val.data;
        return 0;
    case TypeEncoding_UNSIGNED_LONG_LONG:
        *out = *(unsigned long long *)val.data;
        return 0;
    default:
        return 1;
    }
}

static int json_select_union_field(const struct Type *union_type,
                                   struct Value tag_val,
                                   struct Field **out_field)
{
    if (union_type->type != TypeType_UNION)
        return 1;
    if (tag_val.type.type != TypeType_PRIMITIVE or tag_val.data == nullptr)
        return 1;

    if (tag_val.type.primitive.type == TypeEncoding_CHAR_POINTER) {
        const char *tag = *(char **)tag_val.data;
        if (tag == nullptr)
            return 1;
        for (size_t i = 0; i < union_type->union_.field_count; i++) {
            struct Field *field = &union_type->union_.fields[i];
            const char *match = json_union_field_tag_value(field);
            if (match == nullptr) {
                match = json_field_json_key(field);
            }
            if (match != nullptr and strcmp(match, tag) == 0) {
                *out_field = field;
                return 0;
            }
        }
        return 1;
    }

    if (json_primitive_is_unsigned(tag_val.type.primitive.type)) {
        unsigned long long tag = 0;
        if (json_read_unsigned_value(tag_val, &tag) != 0)
            return 1;
        for (size_t i = 0; i < union_type->union_.field_count; i++) {
            struct Field *field = &union_type->union_.fields[i];
            const char *tag_str = json_union_field_tag_value(field);
            if (tag_str == nullptr)
                continue;
            char *endptr = nullptr;
            errno = 0;
            unsigned long long val = strtoull(tag_str, &endptr, 0);
            if (errno != 0 or endptr == tag_str or *endptr != '\0')
                continue;
            if (val == tag) {
                *out_field = field;
                return 0;
            }
        }
        return 1;
    }

    long long tag = 0;
    if (json_read_signed_value(tag_val, &tag) != 0)
        return 1;
    for (size_t i = 0; i < union_type->union_.field_count; i++) {
        struct Field *field = &union_type->union_.fields[i];
        const char *tag_str = json_union_field_tag_value(field);
        if (tag_str == nullptr)
            continue;
        char *endptr = nullptr;
        errno = 0;
        long long val = strtoll(tag_str, &endptr, 0);
        if (errno != 0 or endptr == tag_str or *endptr != '\0')
            continue;
        if (val == tag) {
            *out_field = field;
            return 0;
        }
    }
    return 1;
}

static int json_find_field_index(const struct Type *type, const char *name)
{
    if (type->type != TypeType_STRUCT)
        return -1;
    for (size_t i = 0; i < type->structure.field_count; i++) {
        struct Field *field = &type->structure.fields[i];
        if (field->name == nullptr)
            continue;
        if (strcmp((const char *)field->name->data, name) == 0)
            return (int)i;
    }
    return -1;
}

static int json_find_field_by_key(const char *json, const struct Type *type, const jsmntok_t *key_tok)
{
    if (type->type != TypeType_STRUCT)
        return -1;
    for (size_t i = 0; i < type->structure.field_count; i++) {
        struct Field *field = &type->structure.fields[i];
        const char *json_key = json_field_json_key(field);
        if (json_key == nullptr)
            continue;
        if (json_token_eq(json, key_tok, json_key))
            return (int)i;
    }
    return -1;
}

static int json_read_size_value(struct Value val, size_t *out)
{
    if (val.type.type != TypeType_PRIMITIVE or val.data == nullptr)
        return 1;
    switch (val.type.primitive.type) {
    case TypeEncoding_SIGNED_CHAR: {
        signed char v = *(signed char *)val.data;
        if (v < 0)
            return 1;
        *out = (size_t)v;
        return 0;
    }
    case TypeEncoding_UNSIGNED_CHAR:
        *out = (size_t)*(unsigned char *)val.data;
        return 0;
    case TypeEncoding_SIGNED_SHORT: {
        short v = *(short *)val.data;
        if (v < 0)
            return 1;
        *out = (size_t)v;
        return 0;
    }
    case TypeEncoding_UNSIGNED_SHORT:
        *out = (size_t)*(unsigned short *)val.data;
        return 0;
    case TypeEncoding_SIGNED_INT: {
        int v = *(int *)val.data;
        if (v < 0)
            return 1;
        *out = (size_t)v;
        return 0;
    }
    case TypeEncoding_UNSIGNED_INT:
        *out = (size_t)*(unsigned int *)val.data;
        return 0;
    case TypeEncoding_SIGNED_LONG: {
        long v = *(long *)val.data;
        if (v < 0)
            return 1;
        *out = (size_t)v;
        return 0;
    }
    case TypeEncoding_UNSIGNED_LONG:
        *out = (size_t)*(unsigned long *)val.data;
        return 0;
    case TypeEncoding_SIGNED_LONG_LONG: {
        long long v = *(long long *)val.data;
        if (v < 0)
            return 1;
        *out = (size_t)v;
        return 0;
    }
    case TypeEncoding_UNSIGNED_LONG_LONG:
        *out = (size_t)*(unsigned long long *)val.data;
        return 0;
    default:
        return 1;
    }
}

static int json_write_size_value(struct Value val, size_t size)
{
    if (val.type.type != TypeType_PRIMITIVE or val.data == nullptr)
        return 1;
    switch (val.type.primitive.type) {
    case TypeEncoding_SIGNED_CHAR:
        if (size > (size_t)SCHAR_MAX)
            return 1;
        *(signed char *)val.data = (signed char)size;
        return 0;
    case TypeEncoding_UNSIGNED_CHAR:
        if (size > (size_t)UCHAR_MAX)
            return 1;
        *(unsigned char *)val.data = (unsigned char)size;
        return 0;
    case TypeEncoding_SIGNED_SHORT:
        if (size > (size_t)SHRT_MAX)
            return 1;
        *(short *)val.data = (short)size;
        return 0;
    case TypeEncoding_UNSIGNED_SHORT:
        if (size > (size_t)USHRT_MAX)
            return 1;
        *(unsigned short *)val.data = (unsigned short)size;
        return 0;
    case TypeEncoding_SIGNED_INT:
        if (size > (size_t)INT_MAX)
            return 1;
        *(int *)val.data = (int)size;
        return 0;
    case TypeEncoding_UNSIGNED_INT:
        if (size > (size_t)UINT_MAX)
            return 1;
        *(unsigned int *)val.data = (unsigned int)size;
        return 0;
    case TypeEncoding_SIGNED_LONG:
        if (size > (size_t)LONG_MAX)
            return 1;
        *(long *)val.data = (long)size;
        return 0;
    case TypeEncoding_UNSIGNED_LONG:
        if (size > (size_t)ULONG_MAX)
            return 1;
        *(unsigned long *)val.data = (unsigned long)size;
        return 0;
    case TypeEncoding_SIGNED_LONG_LONG:
        if (size > (size_t)LLONG_MAX)
            return 1;
        *(long long *)val.data = (long long)size;
        return 0;
    case TypeEncoding_UNSIGNED_LONG_LONG:
        if (size > (size_t)ULLONG_MAX)
            return 1;
        *(unsigned long long *)val.data = (unsigned long long)size;
        return 0;
    default:
        return 1;
    }
}

static int json_parse_size_token(const char *json, const jsmntok_t *tok, struct Type type, size_t *out)
{
    if (type.type != TypeType_PRIMITIVE)
        return 1;
    switch (type.primitive.type) {
    case TypeEncoding_SIGNED_CHAR: {
        long long val = 0;
        if (json_parse_signed_token(json, tok, &val) != 0)
            return 1;
        if (val < 0 or val > SCHAR_MAX)
            return 1;
        if ((unsigned long long)val > (unsigned long long)SIZE_MAX)
            return 1;
        *out = (size_t)val;
        return 0;
    }
    case TypeEncoding_SIGNED_SHORT: {
        long long val = 0;
        if (json_parse_signed_token(json, tok, &val) != 0)
            return 1;
        if (val < 0 or val > SHRT_MAX)
            return 1;
        if ((unsigned long long)val > (unsigned long long)SIZE_MAX)
            return 1;
        *out = (size_t)val;
        return 0;
    }
    case TypeEncoding_SIGNED_INT: {
        long long val = 0;
        if (json_parse_signed_token(json, tok, &val) != 0)
            return 1;
        if (val < 0 or val > INT_MAX)
            return 1;
        if ((unsigned long long)val > (unsigned long long)SIZE_MAX)
            return 1;
        *out = (size_t)val;
        return 0;
    }
    case TypeEncoding_SIGNED_LONG: {
        long long val = 0;
        if (json_parse_signed_token(json, tok, &val) != 0)
            return 1;
        if (val < 0 or val > LONG_MAX)
            return 1;
        if ((unsigned long long)val > (unsigned long long)SIZE_MAX)
            return 1;
        *out = (size_t)val;
        return 0;
    }
    case TypeEncoding_SIGNED_LONG_LONG: {
        long long val = 0;
        if (json_parse_signed_token(json, tok, &val) != 0)
            return 1;
        if (val < 0 or val > LLONG_MAX)
            return 1;
        if ((unsigned long long)val > (unsigned long long)SIZE_MAX)
            return 1;
        *out = (size_t)val;
        return 0;
    }
    case TypeEncoding_UNSIGNED_CHAR: {
        unsigned long long val = 0;
        if (json_parse_unsigned_token(json, tok, &val) != 0)
            return 1;
        if (val > UCHAR_MAX)
            return 1;
        if (val > (unsigned long long)SIZE_MAX)
            return 1;
        *out = (size_t)val;
        return 0;
    }
    case TypeEncoding_UNSIGNED_SHORT: {
        unsigned long long val = 0;
        if (json_parse_unsigned_token(json, tok, &val) != 0)
            return 1;
        if (val > USHRT_MAX)
            return 1;
        if (val > (unsigned long long)SIZE_MAX)
            return 1;
        *out = (size_t)val;
        return 0;
    }
    case TypeEncoding_UNSIGNED_INT: {
        unsigned long long val = 0;
        if (json_parse_unsigned_token(json, tok, &val) != 0)
            return 1;
        if (val > UINT_MAX)
            return 1;
        if (val > (unsigned long long)SIZE_MAX)
            return 1;
        *out = (size_t)val;
        return 0;
    }
    case TypeEncoding_UNSIGNED_LONG: {
        unsigned long long val = 0;
        if (json_parse_unsigned_token(json, tok, &val) != 0)
            return 1;
        if (val > ULONG_MAX)
            return 1;
        if (val > (unsigned long long)SIZE_MAX)
            return 1;
        *out = (size_t)val;
        return 0;
    }
    case TypeEncoding_UNSIGNED_LONG_LONG: {
        unsigned long long val = 0;
        if (json_parse_unsigned_token(json, tok, &val) != 0)
            return 1;
        if (val > (unsigned long long)SIZE_MAX)
            return 1;
        *out = (size_t)val;
        return 0;
    }
    default:
        return 1;
    }
}

static int json_parse_value(const char *json,
                            const jsmntok_t *tokens,
                            int token_count,
                            int index,
                            struct Value *out);

static int json_parse_primitive(const char *json, const jsmntok_t *tok, struct Value *out)
{
    switch (out->type.primitive.type) {
    case TypeEncoding_SIGNED_CHAR: {
        long long val = 0;
        if (json_parse_signed_token(json, tok, &val) != 0)
            return 1;
        if (val < SCHAR_MIN or val > SCHAR_MAX)
            return 1;
        *(signed char *)out->data = (signed char)val;
        return 0;
    }
    case TypeEncoding_UNSIGNED_CHAR: {
        unsigned long long val = 0;
        if (json_parse_unsigned_token(json, tok, &val) != 0)
            return 1;
        if (val > UCHAR_MAX)
            return 1;
        *(unsigned char *)out->data = (unsigned char)val;
        return 0;
    }
    case TypeEncoding_SIGNED_SHORT: {
        long long val = 0;
        if (json_parse_signed_token(json, tok, &val) != 0)
            return 1;
        if (val < SHRT_MIN or val > SHRT_MAX)
            return 1;
        *(short *)out->data = (short)val;
        return 0;
    }
    case TypeEncoding_UNSIGNED_SHORT: {
        unsigned long long val = 0;
        if (json_parse_unsigned_token(json, tok, &val) != 0)
            return 1;
        if (val > USHRT_MAX)
            return 1;
        *(unsigned short *)out->data = (unsigned short)val;
        return 0;
    }
    case TypeEncoding_SIGNED_INT: {
        long long val = 0;
        if (json_parse_signed_token(json, tok, &val) != 0)
            return 1;
        if (val < INT_MIN or val > INT_MAX)
            return 1;
        *(int *)out->data = (int)val;
        return 0;
    }
    case TypeEncoding_UNSIGNED_INT: {
        unsigned long long val = 0;
        if (json_parse_unsigned_token(json, tok, &val) != 0)
            return 1;
        if (val > UINT_MAX)
            return 1;
        *(unsigned int *)out->data = (unsigned int)val;
        return 0;
    }
    case TypeEncoding_SIGNED_LONG: {
        long long val = 0;
        if (json_parse_signed_token(json, tok, &val) != 0)
            return 1;
        if (val < LONG_MIN or val > LONG_MAX)
            return 1;
        *(long *)out->data = (long)val;
        return 0;
    }
    case TypeEncoding_UNSIGNED_LONG: {
        unsigned long long val = 0;
        if (json_parse_unsigned_token(json, tok, &val) != 0)
            return 1;
        if (val > ULONG_MAX)
            return 1;
        *(unsigned long *)out->data = (unsigned long)val;
        return 0;
    }
    case TypeEncoding_SIGNED_LONG_LONG: {
        long long val = 0;
        if (json_parse_signed_token(json, tok, &val) != 0)
            return 1;
        if (val < LLONG_MIN or val > LLONG_MAX)
            return 1;
        *(long long *)out->data = (long long)val;
        return 0;
    }
    case TypeEncoding_UNSIGNED_LONG_LONG: {
        unsigned long long val = 0;
        if (json_parse_unsigned_token(json, tok, &val) != 0)
            return 1;
        *(unsigned long long *)out->data = (unsigned long long)val;
        return 0;
    }
    case TypeEncoding_FLOAT: {
        long double val = 0.0L;
        if (json_parse_float_token(json, tok, &val) != 0)
            return 1;
        *(float *)out->data = (float)val;
        return 0;
    }
    case TypeEncoding_DOUBLE: {
        long double val = 0.0L;
        if (json_parse_float_token(json, tok, &val) != 0)
            return 1;
        *(double *)out->data = (double)val;
        return 0;
    }
    case TypeEncoding_LONG_DOUBLE: {
        long double val = 0.0L;
        if (json_parse_float_token(json, tok, &val) != 0)
            return 1;
        *(long double *)out->data = val;
        return 0;
    }
    case TypeEncoding_BOOL: {
        bool val = false;
        if (json_parse_bool_token(json, tok, &val) != 0)
            return 1;
        *(_Bool *)out->data = val;
        return 0;
    }
    case TypeEncoding_CHAR_POINTER: {
        if (tok->type == JSMN_PRIMITIVE and json_token_eq(json, tok, "null")) {
            *(char **)out->data = nullptr;
            return 0;
        }
        char *decoded = nullptr;
        if (json_decode_string(json, tok, &decoded, nullptr) != 0)
            return 1;
        *(char **)out->data = decoded;
        return 0;
    }
    case TypeEncoding_VOID:
        return 1;
    default:
        return 1;
    }
}

static int json_parse_array(const char *json, const jsmntok_t *tokens, int token_count, int index, struct Value *out)
{
    if (index < 0 or index >= token_count)
        return 1;
    jsmntok_t tok = tokens[index];
    if (tok.type != JSMN_ARRAY)
        return 1;
    if ((size_t)tok.size != out->type.array.length)
        return 1;

    size_t elem_size = type_size(*out->type.array.type);
    int idx = index + 1;
    for (int i = 0; i < tok.size; i++) {
        struct Value elem = {
            .type = *out->type.array.type,
            .data = (char *)out->data + (i * elem_size),
        };
        if (json_parse_value(json, tokens, token_count, idx, &elem) != 0)
            return 1;
        idx = json_skip_token(tokens, idx, token_count);
        if (idx < 0)
            return 1;
    }
    return 0;
}

static int json_parse_pointer(const char *json, const jsmntok_t *tokens, int token_count, int index, struct Value *out)
{
    if (index < 0 or index >= token_count)
        return 1;
    jsmntok_t tok = tokens[index];
    if (tok.type == JSMN_PRIMITIVE and json_token_eq(json, &tok, "null")) {
        *(void **)out->data = nullptr;
        return 0;
    }
    size_t elem_size = type_size(*out->type.pointer.type);
    if (elem_size == 0)
        return 1;
    void *mem = calloc(1, elem_size);
    if (mem == nullptr)
        return 1;

    struct Value elem = {
        .type = *out->type.pointer.type,
        .data = mem,
    };
    if (json_parse_value(json, tokens, token_count, index, &elem) != 0) {
        free(mem);
        return 1;
    }
    *(void **)out->data = mem;
    return 0;
}

static int json_parse_sized_field(const char *json,
                                  const jsmntok_t *tokens,
                                  int token_count,
                                  int value_index,
                                  struct Value *struct_val,
                                  struct Field *field,
                                  bool *field_seen,
                                  bool *length_from_array)
{
    const char *len_name = json_field_length_name(field);
    if (len_name == nullptr)
        return 1;
    int len_index = json_find_field_index(&struct_val->type, len_name);
    if (len_index < 0)
        return 1;
    struct Field *len_field = &struct_val->type.structure.fields[len_index];
    if (len_field->type.type != TypeType_PRIMITIVE)
        return 1;

    struct Value len_val = {
        .type = len_field->type,
        .data = (char *)struct_val->data + len_field->offset,
    };
    bool write_length = not field_has_modifier(*len_field, "no_deserialise");
    bool have_length = false;
    size_t existing_length = 0;
    if (field_seen[len_index]) {
        if (json_read_size_value(len_val, &existing_length) != 0)
            return 1;
        have_length = true;
    }

    jsmntok_t tok = tokens[value_index];
    void *field_ptr = (char *)struct_val->data + field->offset;
    if (tok.type == JSMN_PRIMITIVE and json_token_eq(json, &tok, "null")) {
        if (field->type.type == TypeType_POINTER) {
            *(void **)field_ptr = nullptr;
        } else {
            *(char **)field_ptr = nullptr;
        }
        if (have_length) {
            if (existing_length != 0)
                return 1;
        } else {
            if (write_length) {
                if (json_write_size_value(len_val, 0) != 0)
                    return 1;
                field_seen[len_index] = true;
                length_from_array[len_index] = true;
            } else {
                size_t current = 0;
                if (json_read_size_value(len_val, &current) != 0)
                    return 1;
                if (current != 0)
                    return 1;
                field_seen[len_index] = true;
            }
        }
        return 0;
    }

    if (field->type.type == TypeType_POINTER) {
        if (tok.type != JSMN_ARRAY)
            return 1;
        size_t json_len = (size_t)tok.size;
        if (have_length) {
            if (existing_length != json_len)
                return 1;
        } else if (write_length) {
            if (json_write_size_value(len_val, json_len) != 0)
                return 1;
            field_seen[len_index] = true;
            length_from_array[len_index] = true;
        } else {
            size_t current = 0;
            if (json_read_size_value(len_val, &current) != 0)
                return 1;
            if (current != json_len)
                return 1;
            field_seen[len_index] = true;
        }

        if (json_len == 0) {
            *(void **)field_ptr = nullptr;
            return 0;
        }

        size_t elem_size = type_size(*field->type.pointer.type);
        if (elem_size == 0)
            return 1;
        void *mem = calloc(json_len, elem_size);
        if (mem == nullptr)
            return 1;
        int idx = value_index + 1;
        for (size_t i = 0; i < json_len; i++) {
            struct Value elem = {
                .type = *field->type.pointer.type,
                .data = (char *)mem + (i * elem_size),
            };
            if (json_parse_value(json, tokens, token_count, idx, &elem) != 0) {
                free(mem);
                return 1;
            }
            idx = json_skip_token(tokens, idx, token_count);
            if (idx < 0) {
                free(mem);
                return 1;
            }
        }
        *(void **)field_ptr = mem;
        return 0;
    }

    if (field->type.type == TypeType_PRIMITIVE and field->type.primitive.type == TypeEncoding_CHAR_POINTER) {
        if (tok.type != JSMN_STRING)
            return 1;
        char *decoded = nullptr;
        size_t decoded_len = 0;
        if (json_decode_string(json, &tok, &decoded, &decoded_len) != 0)
            return 1;
        if (have_length) {
            if (existing_length != decoded_len) {
                free(decoded);
                return 1;
            }
        } else if (write_length) {
            if (json_write_size_value(len_val, decoded_len) != 0) {
                free(decoded);
                return 1;
            }
            field_seen[len_index] = true;
            length_from_array[len_index] = true;
        } else {
            size_t current = 0;
            if (json_read_size_value(len_val, &current) != 0) {
                free(decoded);
                return 1;
            }
            if (current != decoded_len) {
                free(decoded);
                return 1;
            }
            field_seen[len_index] = true;
        }
        *(char **)field_ptr = decoded;
        return 0;
    }

    return 1;
}

static int json_parse_tagged_union_field(const char *json,
                                         const jsmntok_t *tokens,
                                         int token_count,
                                         int value_index,
                                         struct Value *struct_val,
                                         struct Field *union_field,
                                         struct Field *tag_field)
{
    if (union_field->type.type != TypeType_UNION)
        return 1;

    struct Value tag_val = {
        .type = tag_field->type,
        .data = (char *)struct_val->data + tag_field->offset,
    };
    struct Field *active = nullptr;
    if (json_select_union_field(&union_field->type, tag_val, &active) != 0)
        return 1;

    struct Value active_val = {
        .type = active->type,
        .data = (char *)struct_val->data + union_field->offset + active->offset,
    };
    if (json_parse_value(json, tokens, token_count, value_index, &active_val) != 0)
        return 1;

    return 0;
}

static int json_parse_struct(const char *json, const jsmntok_t *tokens, int token_count, int index, struct Value *out)
{
    if (index < 0 or index >= token_count)
        return 1;
    jsmntok_t tok = tokens[index];
    if (tok.type != JSMN_OBJECT)
        return 1;

    size_t field_count = out->type.structure.field_count;
    bool *field_seen = calloc(field_count, sizeof(bool));
    bool *length_from_array = calloc(field_count, sizeof(bool));
    int *pending_union_index = calloc(field_count, sizeof(int));
    if (field_seen == nullptr or length_from_array == nullptr or pending_union_index == nullptr) {
        free(field_seen);
        free(length_from_array);
        free(pending_union_index);
        return 1;
    }
    for (size_t i = 0; i < field_count; i++) {
        pending_union_index[i] = -1;
    }

    int idx = index + 1;
    for (int i = 0; i < tok.size; i++) {
        if (idx + 1 >= token_count) {
            free(field_seen);
            free(length_from_array);
            free(pending_union_index);
            return 1;
        }
        int key_index = idx;
        int value_index = idx + 1;
        idx = json_skip_token(tokens, value_index, token_count);
        if (idx < 0) {
            free(field_seen);
            free(length_from_array);
            free(pending_union_index);
            return 1;
        }

        int field_index = json_find_field_by_key(json, &out->type, &tokens[key_index]);
        if (field_index < 0)
            continue;

        struct Field *field = &out->type.structure.fields[field_index];
        if (field_has_modifier(*field, "no_deserialise")) {
            field_seen[field_index] = true;
            continue;
        }

        if (field_seen[field_index]) {
            if (length_from_array[field_index]) {
                size_t parsed_len = 0;
                size_t existing_len = 0;
                struct Value len_val = {
                    .type = field->type,
                    .data = (char *)out->data + field->offset,
                };
                if (json_parse_size_token(json, &tokens[value_index], field->type, &parsed_len) != 0 or
                    json_read_size_value(len_val, &existing_len) != 0 or parsed_len != existing_len) {
                    free(field_seen);
                    free(length_from_array);
                    free(pending_union_index);
                    return 1;
                }
            }
            continue;
        }

        int handled = 0;
        const char *len_name = json_field_length_name(field);
        if (len_name != nullptr) {
            if (field->type.type == TypeType_POINTER or
                (field->type.type == TypeType_PRIMITIVE and field->type.primitive.type == TypeEncoding_CHAR_POINTER)) {
                if (json_parse_sized_field(json, tokens, token_count, value_index, out, field,
                                           field_seen, length_from_array) != 0) {
                    free(field_seen);
                    free(length_from_array);
                    free(pending_union_index);
                    return 1;
                }
                handled = 1;
            }
        }

        if (!handled && field->type.type == TypeType_UNION) {
            const char *tag_name = json_field_tagged_by_name(field);
            if (tag_name == nullptr) {
                free(field_seen);
                free(length_from_array);
                free(pending_union_index);
                return 1;
            }
            int tag_index = json_find_field_index(&out->type, tag_name);
            if (tag_index < 0) {
                free(field_seen);
                free(length_from_array);
                free(pending_union_index);
                return 1;
            }
            struct Field *tag_field = &out->type.structure.fields[tag_index];
            bool tag_available = field_seen[tag_index] or field_has_modifier(*tag_field, "no_deserialise");
            if (!tag_available) {
                pending_union_index[field_index] = value_index;
                handled = 1;
            } else {
                if (json_parse_tagged_union_field(json, tokens, token_count, value_index, out,
                                                  field, tag_field) != 0) {
                    free(field_seen);
                    free(length_from_array);
                    free(pending_union_index);
                    return 1;
                }
                handled = 1;
            }
        }

        if (!handled) {
            struct Value field_val = {
                .type = field->type,
                .data = (char *)out->data + field->offset,
            };
            if (json_parse_value(json, tokens, token_count, value_index, &field_val) != 0) {
                free(field_seen);
                free(length_from_array);
                free(pending_union_index);
                return 1;
            }
        }
        field_seen[field_index] = true;
    }

    for (size_t i = 0; i < field_count; i++) {
        if (pending_union_index[i] < 0)
            continue;
        struct Field *field = &out->type.structure.fields[i];
        const char *tag_name = json_field_tagged_by_name(field);
        if (tag_name == nullptr) {
            free(field_seen);
            free(length_from_array);
            free(pending_union_index);
            return 1;
        }
        int tag_index = json_find_field_index(&out->type, tag_name);
        if (tag_index < 0) {
            free(field_seen);
            free(length_from_array);
            free(pending_union_index);
            return 1;
        }
        struct Field *tag_field = &out->type.structure.fields[tag_index];
        if (!field_seen[tag_index] and !field_has_modifier(*tag_field, "no_deserialise")) {
            free(field_seen);
            free(length_from_array);
            free(pending_union_index);
            return 1;
        }
        if (json_parse_tagged_union_field(json, tokens, token_count, pending_union_index[i], out,
                                          field, tag_field) != 0) {
            free(field_seen);
            free(length_from_array);
            free(pending_union_index);
            return 1;
        }
        field_seen[i] = true;
    }

    for (size_t i = 0; i < field_count; i++) {
        struct Field *field = &out->type.structure.fields[i];
        if (field->name == nullptr)
            continue;
        if (field_seen[i])
            continue;
        if (field_has_modifier(*field, "no_deserialise"))
            continue;
        if (field_has_modifier(*field, "optional"))
            continue;
        free(field_seen);
        free(length_from_array);
        free(pending_union_index);
        return 1;
    }

    free(field_seen);
    free(length_from_array);
    free(pending_union_index);
    return 0;
}

static int json_parse_value(const char *json,
                            const jsmntok_t *tokens,
                            int token_count,
                            int index,
                            struct Value *out)
{
    if (index < 0 or index >= token_count or out->data == nullptr)
        return 1;

    switch (out->type.type) {
    case TypeType_PRIMITIVE:
        return json_parse_primitive(json, &tokens[index], out);
    case TypeType_STRUCT:
        return json_parse_struct(json, tokens, token_count, index, out);
    case TypeType_ARRAY:
        return json_parse_array(json, tokens, token_count, index, out);
    case TypeType_POINTER:
        return json_parse_pointer(json, tokens, token_count, index, out);
    case TypeType_UNION:
        return 1;
    }
    return 1;
}

int json_to_value(const char *json, size_t length, size_t expected_tokens, struct Value *$nullable out)
{
    jsmntok_t *tokens = calloc(expected_tokens, sizeof(jsmntok_t));
    if (tokens == nullptr)
        return -1;
    jsmn_parser parser;
    jsmn_init(&parser);
    int ret = jsmn_parse(&parser, json, length, tokens, expected_tokens);
    if (ret < 0) {
        free(tokens);
        return ret;
    }

    if (ret == 0) {
        free(tokens);
        return 1;
    }
    if (out == nullptr or out->data == nullptr) {
        free(tokens);
        return 1;
    }

    int parse_ret = json_parse_value(json, tokens, ret, 0, $nonnull_cast(out));
    free(tokens);
    return parse_ret;
}

$if_clang(
    _Pragma("clang assume_nonnull end"))
